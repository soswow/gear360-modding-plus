#!/usr/bin/env python3
"""Durable sequence, torn-state recovery and no-overwrite behavior."""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import tempfile

HERE=Path(__file__).resolve().parents[1]
RE=HERE.parent/'lib'
class Storage(C.Structure):
    _fields_=[('photos',C.c_char*448),('state',C.c_char*448),('lock_fd',C.c_int),
              ('counter_fd',C.c_int*2),('counter',C.c_uint32),('slot',C.c_uint),
              ('opened',C.c_int),('latched',C.c_int)]

def record(value):
    data=b'G360CTR1'+struct.pack('<II',value,(~value)&0xffffffff)
    checksum=2166136261
    for byte in data: checksum=((checksum^byte)*16777619)&0xffffffff
    return data+struct.pack('<I',checksum)

def main():
    with tempfile.TemporaryDirectory(prefix='g360-storage-') as td:
        work=Path(td); library=work/'storage.so'
        flags=[os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
               '-I'+str(HERE),'-I'+str(RE/'capture-session')]
        subprocess.run(flags+['-O2','-shared','-fPIC',str(HERE/'storage.c'),'-o',str(library)],check=True)
        lib=C.CDLL(str(library))
        lib.g360_storage_open.argtypes=[C.POINTER(Storage),C.c_char_p]
        lib.g360_storage_close.argtypes=[C.POINTER(Storage)]
        lib.g360_storage_reserve_at.argtypes=[C.POINTER(Storage),C.c_char_p,*([C.c_char_p]*4)]
        lib.g360_storage_reserve.argtypes=[C.POINTER(Storage),*([C.c_char_p]*4)]
        root=work/'G360'; s=Storage()
        def open_(s_=s): return lib.g360_storage_open(C.byref(s_),os.fsencode(root))
        def close(s_=s): lib.g360_storage_close(C.byref(s_))
        def reserve(stamp=b'20261105-123456',s_=s):
            outputs=[C.create_string_buffer(n) for n in (64,512,512,20)]
            status=lib.g360_storage_reserve_at(C.byref(s_),stamp,*outputs)
            return status,*[out.value.decode() for out in outputs]
        assert open_()==0
        r=reserve(); assert r[0]==0 and r[1]=='20261105-123456-00001',r
        assert r[4]=='2026:11:05 12:34:56',r
        first_jpg,first_dng=Path(r[2]),Path(r[3])
        assert first_jpg.read_bytes()==first_dng.read_bytes()==b''
        first_jpg.write_bytes(b'jpeg-original');first_dng.write_bytes(b'dng-original')
        other=Storage();assert open_(other)==3
        # Every reservation is already durable when it is returned.
        assert max(struct.unpack_from('<I',(root/'state'/f'counter.{i}').read_bytes(),8)[0] for i in (0,1))==1
        close();assert open_()==0
        r=reserve(b'20170101-000000');assert r[0]==0 and r[1].endswith('-00002'),r
        r=reserve(b'20170101-000000');assert r[0]==0 and r[1].endswith('-00003'),r
        assert first_jpg.read_bytes()==b'jpeg-original' and first_dng.read_bytes()==b'dng-original'
        close()
        # Torn newest slot recovers from the alternate and scanned photos.
        values=[struct.unpack_from('<I',(root/'state'/f'counter.{i}').read_bytes(),8)[0] for i in (0,1)]
        latest=values.index(max(values));(root/'state'/f'counter.{latest}').write_bytes(b'G360C')
        assert open_()==0 and s.counter==3
        r=reserve();assert r[0]==0 and r[1].endswith('-00004'),r
        close()
        # Stale state is advanced once on open, even after a wall-clock jump.
        for i in (0,1):(root/'state'/f'counter.{i}').write_bytes(record(1))
        assert open_()==0 and s.counter==4
        # A foreign photo copied in while open must never be overwritten.
        foreign=root/'photos'/'20261105-123456-00005.jpg';foreign.write_bytes(b'do-not-touch')
        r=reserve();assert r[0]==0 and r[1].endswith('-00006'),r
        assert foreign.read_bytes()==b'do-not-touch'
        foreign_dng=root/'photos'/'20261105-123456-00007.dng';foreign_dng.write_bytes(b'also-keep')
        r=reserve();assert r[0]==0 and r[1].endswith('-00008'),r
        assert foreign_dng.read_bytes()==b'also-keep'
        # Our unused JPEG reservation is retained, rather than deleting files.
        assert (root/'photos'/'20261105-123456-00007.jpg').read_bytes()==b''
        before=s.counter
        for invalid in (b'',b'20261105_123456',b'20261305-123456',b'20261105-246000',b'x'*15):
            assert reserve(invalid)[0]==1
        assert s.counter==before
        close()
        # Missing both state slots may be reconstructed from names, no reset.
        for i in (0,1):(root/'state'/f'counter.{i}').unlink()
        assert open_()==0 and s.counter==8
        close()
        # Two existing damaged slots are not treated as a fresh installation.
        for i in (0,1):(root/'state'/f'counter.{i}').write_bytes(b'bad')
        assert open_()==4
        assert first_jpg.read_bytes()==b'jpeg-original'
        for i in (0,1):(root/'state'/f'counter.{i}').write_bytes(record(99999))
        assert open_()==0 and reserve()[0]==5
        close()
        # One IO failure latches the active session and exposes no paths.
        for i in (0,1):(root/'state'/f'counter.{i}').write_bytes(record(8))
        assert open_()==0
        os.close(s.counter_fd[s.slot^1])
        r=reserve();assert r==(2,'','','',''),r
        assert reserve()[0]==7
        close()
        # Reopen recovers; normal local-wall-clock entrypoint formats a name.
        assert open_()==0
        outputs=[C.create_string_buffer(n) for n in (64,512,512,20)]
        assert lib.g360_storage_reserve(C.byref(s),*outputs)==0
        assert len(outputs[0].value)==21 and outputs[0].value.endswith(b'-00009')
        # A sweep frame reserves only its DNG, on the same sequence.
        lib.g360_storage_reserve_dng_at.argtypes=[C.POINTER(Storage),C.c_char_p,C.c_char_p,C.c_char_p]
        stem,dng=C.create_string_buffer(64),C.create_string_buffer(512)
        assert lib.g360_storage_reserve_dng_at(C.byref(s),b'20261105-123456',stem,dng)==0
        assert stem.value==b'20261105-123456-00010' and dng.value.endswith(b'/photos/20261105-123456-00010.dng')
        assert Path(dng.value.decode()).read_bytes()==b''
        assert not (root/'photos'/'20261105-123456-00010.jpg').exists()
        assert lib.g360_storage_reserve_dng_at(C.byref(s),b'2026',stem,dng)==1 and stem.value==dng.value==b''
        # An existing DNG with the next number is skipped, never touched.
        keep=root/'photos'/'20261105-123456-00011.dng';keep.write_bytes(b'keep')
        assert lib.g360_storage_reserve_dng_at(C.byref(s),b'20261105-123456',stem,dng)==0
        assert stem.value.endswith(b'-00012') and keep.read_bytes()==b'keep'
        # RAW off: a NULL DNG reserves only the JPEG, on the same sequence; an
        # existing JPEG with the next number is skipped, never touched.
        keep_jpg=root/'photos'/'20261105-123456-00013.jpg';keep_jpg.write_bytes(b'keep')
        stem,jpg,date=C.create_string_buffer(64),C.create_string_buffer(512),C.create_string_buffer(20)
        assert lib.g360_storage_reserve_at(C.byref(s),b'20261105-123456',stem,jpg,None,date)==0
        assert stem.value==b'20261105-123456-00014' and jpg.value.endswith(b'/photos/20261105-123456-00014.jpg')
        assert date.value==b'2026:11:05 12:34:56' and Path(jpg.value.decode()).read_bytes()==b''
        assert not (root/'photos'/'20261105-123456-00014.dng').exists() and keep_jpg.read_bytes()==b'keep'
        assert lib.g360_storage_reserve(C.byref(s),stem,jpg,None,date)==0 and stem.value.endswith(b'-00015')
        assert not list((root/'photos').glob('*-00015.dng')) and len(list((root/'photos').glob('*-00015.jpg')))==1
        # At least one output is needed.
        assert lib.g360_storage_reserve_at(C.byref(s),b'20261105-123456',stem,None,None,date)==1
        # DNG-only uses the same durable counter and date API without making
        # an empty JPEG. Both the supplied-clock and wall-clock APIs accept it.
        dng=C.create_string_buffer(512)
        assert lib.g360_storage_reserve_at(C.byref(s),b'20261105-123456',stem,None,dng,date)==0
        assert stem.value.endswith(b'-00016') and date.value==b'2026:11:05 12:34:56'
        assert Path(dng.value.decode()).read_bytes()==b''
        assert not (root/'photos'/'20261105-123456-00016.jpg').exists()
        assert lib.g360_storage_reserve(C.byref(s),stem,None,dng,date)==0 and stem.value.endswith(b'-00017')
        assert not list((root/'photos').glob('*-00017.jpg'))
        assert lib.g360_storage_reserve(C.byref(s),stem,None,None,date)==1
        close()
        assert open_()==0 and s.counter==17
        close()
        # A symlinked state slot must never redirect counter writes elsewhere.
        target=work/'user-file';target.write_bytes(b'unchanged')
        (root/'state'/'counter.0').unlink();(root/'state'/'counter.0').symlink_to(target)
        assert open_()==2 and target.read_bytes()==b'unchanged'
        # Exercise the complete active-session IO in standalone sanitizers.
        harness=work/'sanitized.c'
        harness.write_text(r'''
#include "storage.h"
#include "capture_libc.h"
int main(int argc,char **argv) {
    g360_storage s;
    char stem[64],jpg[512],dng[512],date[20];
    int k;
    if(argc!=2 || g360_storage_open(&s,argv[1])) return 1;
    for(k=0;k<20;++k) if(g360_storage_reserve_at(&s,"20261105-123456",stem,jpg,dng,date)) return 2;
    g360_storage_close(&s);
    if(g360_storage_open(&s,argv[1]) || s.counter!=20) return 3;
    g360_storage_close(&s);return 0;
}
''')
        exe=work/'sanitized'
        subprocess.run(flags+['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all',
                              str(HERE/'storage.c'),str(harness),'-o',str(exe)],check=True)
        subprocess.run([str(exe),str(work/'sanitized-root')],check=True)
        print('storage restart/clock reversal/torn-state/no-overwrite/locking/exhaustion/IO/sanitizer checks passed')
if __name__=='__main__':main()
