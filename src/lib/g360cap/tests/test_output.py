#!/usr/bin/env python3
"""Exercise streaming output, source lifetimes and file IO failures."""
import ctypes as C
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from test_dng import Frame, Image, HERE, maker, fixture, make_dng, stock_iq


class ExifInfo(C.Structure):
    _fields_ = [(name, C.c_uint32) for name in
                ("width", "height", "exposure_numerator", "exposure_denominator",
                 "iso", "exposure_program", "white_balance")] + [("datetime", C.c_char * 20)]


def main():
    with tempfile.TemporaryDirectory(prefix="g360-output-") as td:
        work = Path(td)
        cc = os.environ.get("CC", "cc")
        flags = [cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-O2",
                 "-I" + str(HERE), "-I" + str(HERE.parent / "capture-session")]
        sources = [str(HERE / name) for name in ("g360_output.c", "g360_dng.c", "g360_exif.c")]
        libpath = work / "output.so"
        subprocess.run(flags + ["-shared", "-fPIC", *sources, "-o", str(libpath)], check=True)
        lib = C.CDLL(str(libpath))
        lib.g360_save_dng.argtypes = [C.POINTER(Frame), C.POINTER(Image), C.c_char_p]
        lib.g360_save_jpeg_exif.argtypes = [C.POINTER(Image), C.POINTER(Image), C.POINTER(ExifInfo), C.c_char_p]
        pixels = bytes(range(256)) * (11648 * 2896 // 256)
        raw = C.create_string_buffer(pixels)
        note_bytes = maker()
        note_buf = C.create_string_buffer(note_bytes)
        image = Image(C.cast(raw, C.POINTER(C.c_ubyte)), len(pixels), 5824, 2896, 11648, 16, 0, 0x102)
        frame = Frame(image, 0, 0, 3)
        note = Image(C.cast(note_buf, C.POINTER(C.c_ubyte)), len(note_bytes), 0, 0, 0, 0, 0, 2)
        out = work / "saved.dng"
        assert lib.g360_save_dng(C.byref(frame), C.byref(note), os.fsencode(out)) == 0
        expected = make_dng.build(pixels, {"awb": stock_iq.decode_awb(fixture.awb_record())})
        assert out.read_bytes() == expected
        assert bytes(raw)[:-1] == pixels and bytes(note_buf)[:-1] == note_bytes
        # The full DNG (Exif, MakerNote, description) through the same writer.
        lib.g360_save_dng_full.argtypes = [C.POINTER(Frame), C.POINTER(Image), C.POINTER(ExifInfo), C.c_char_p,
                                           C.c_char_p]
        full_info = ExifInfo(0, 0, 1, 125, 100, 1, 0, b"2026:11:06 08:12:30")
        out_full = work / "saved-full.dng"
        assert lib.g360_save_dng_full(C.byref(frame), C.byref(note), C.byref(full_info), b"HDR SWEEP 4/6",
                                      os.fsencode(out_full)) == 0
        assert out_full.read_bytes() == make_dng.build_full(
            pixels, {"awb": stock_iq.decode_awb(fixture.awb_record())}, note_bytes,
            {"exposure": (1, 125), "iso": 100, "program": 1, "white_balance": 0,
             "datetime": "2026:11:06 08:12:30"}, "HDR SWEEP 4/6")
        keep_full = work / "keep-full.dng"
        keep_full.write_bytes(b"keep")
        assert lib.g360_save_dng_full(C.byref(frame), C.byref(note), None, None, os.fsencode(keep_full)) == 1
        assert keep_full.read_bytes() == b"keep"
        original = b"\xff\xd8\xff\xe0\x00\x04ab\xff\xd9"
        jpg_buf = C.create_string_buffer(original)
        jpg = Image(C.cast(jpg_buf, C.POINTER(C.c_ubyte)), len(original), 5792, 2896, 0, 0, 0, 0x22)
        info = ExifInfo(5792, 2896, 1, 250, 100, 1, 0, b"")
        path = work / "saved.jpg"
        assert lib.g360_save_jpeg_exif(C.byref(jpg), C.byref(note), C.byref(info), os.fsencode(path)) == 0
        result = path.read_bytes()
        length = struct.unpack_from(">H", result, 4)[0]
        assert result[:2] + result[4 + length:] == original
        assert stock_iq.makernote(result)[0xa0] == fixture.awb_record()
        assert bytes(jpg_buf)[:-1] == original
        # Refuse invalid inputs before opening/truncating an existing file.
        path.write_bytes(b"keep")
        note.size = 1
        assert lib.g360_save_jpeg_exif(C.byref(jpg), C.byref(note), C.byref(info), os.fsencode(path)) == 14
        assert lib.g360_save_dng(C.byref(frame), C.byref(note), os.fsencode(path)) == 14
        assert path.read_bytes() == b"keep"
        note.size = len(note_bytes)
        assert lib.g360_save_dng(C.byref(frame), C.byref(note), os.fsencode(work / "missing/out")) == 13
        # Stub only OS IO to exercise partial writes and errors under sanitizers.
        note_path = work / "note.bin"
        note_path.write_bytes(note_bytes)
        seam = work / "io.c"
        seam.write_text(r'''
#include "capture_libc.h"
#include "g360_output.h"
static int mode, opened, closed, writes;
static size_t total;
static int fake_open(const char *path, int flags, ...) {
    (void)path; (void)flags; ++opened; return mode == 1 ? -1 : 42;
}
static ptrdiff_t fake_write(int fd, const void *data, size_t count) {
    size_t n = count > 1048576 ? 1048576 : count;
    (void)data; if (fd != 42) abort(); ++writes;
    if (mode == 2) return -1;
    if (mode == 3) return 0;
    if (mode == 6) return (ptrdiff_t)count + 1;
    if (writes == 1 && n > 7) n = 7;
    total += n; return (ptrdiff_t)n;
}
static int fake_fsync(int fd) { (void)fd; return mode == 4 ? -1 : 0; }
static int fake_close(int fd) { (void)fd; ++closed; return mode == 5 ? -1 : 0; }
#define open fake_open
#define write fake_write
#define fsync fake_fsync
#define close fake_close
#include "g360_output.c"
int main(int argc, char **argv) {
    g360_frame frame; g360_image note; FILE *file; int k;
    g360_image jpeg; g360_exif_info info;
    unsigned char jpeg_data[4] = {0xff,0xd8,0xff,0xd9};
    unsigned char app1[G360_EXIF_APP1_MAX_SIZE]; size_t app1_size;
    if (argc != 2) return 1;
    memset(&frame,0,sizeof frame); memset(&note,0,sizeof note);
    frame.image.data=(unsigned char *)(uintptr_t)1;
    frame.image.size=11648u*2896u; frame.image.width=5824; frame.image.height=2896;
    frame.image.stride_x=11648; frame.image.stride_y=16; frame.image.flags=0x102;
    note.size=0x5314; note.data=malloc(note.size); note.flags=2;
    file=fopen(argv[1],"rb");
    if (!file || !note.data || fread(note.data,1,note.size,file)!=note.size) return 2;
    if (fclose(file)) return 3;
    for(k=0;k<=6;++k) {
        int status;
        mode=k; opened=closed=writes=0; total=0;
        status=g360_save_dng(&frame,&note,"unused");
        if (status != (k==0 ? G360_OK : G360_E_IO)) return 4;
        if (opened!=1 || closed!=(k==1 ? 0 : 1)) return 5;
        if (k==0 && (total!=588u+frame.image.size || writes < 4)) return 6;
    }
    memset(&jpeg,0,sizeof jpeg); memset(&info,0,sizeof info);
    jpeg.data=jpeg_data; jpeg.size=sizeof jpeg_data;
    info.width=8; info.height=8; info.exposure_numerator=1; info.exposure_denominator=250;
    info.iso=100; info.exposure_program=1; info.white_balance=0;
    memcpy(info.datetime,"2026:11:05 09:30:00",20);
    if(g360_exif_app1(&note,&info,app1,sizeof app1,&app1_size)!=G360_OK) return 8;
    for(k=0;k<=6;++k) {
        int status;
        mode=k; opened=closed=writes=0; total=0;
        status=g360_save_jpeg_exif(&jpeg,&note,&info,"unused");
        if(status!=(k==0 ? G360_OK : G360_E_IO)) return 9;
        if(opened!=1 || closed!=(k==1 ? 0 : 1)) return 10;
        if(k==0 && total!=sizeof jpeg_data+app1_size) return 11;
    }
    opened=0; note.size=1;
    if (g360_save_dng(&frame,&note,"unused")!=G360_E_IMAGE || opened) return 7;
    free(note.data); return 0;
}
''')
        exe = work / "io"
        subprocess.run(flags + ["-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                str(seam), *sources[1:], "-o", str(exe)], check=True)
        subprocess.run([str(exe), str(note_path)], check=True)
        print("output byte parity, unchanged inputs, refusal-before-truncate and partial/error IO passed")


if __name__ == "__main__":
    main()
