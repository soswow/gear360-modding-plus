#!/usr/bin/env python3
"""The sweep queue on the built-in flash: frames, exposures and the move to the card."""
import ctypes as C
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parents[1]
RE = HERE.parent / 'lib'
FRAME = 1000  # G360_QUEUE_FRAME_BYTES for this build
OLD_FRAME = 700  # G360_QUEUE_OLD_FRAME_BYTES: frames queued by older builds
STAMP = b'20261105-123456'


class Storage(C.Structure):
    _fields_ = [('photos', C.c_char * 448), ('state', C.c_char * 448), ('lock_fd', C.c_int),
                ('counter_fd', C.c_int * 2), ('counter', C.c_uint32), ('slot', C.c_uint),
                ('opened', C.c_int), ('latched', C.c_int)]


class Queue(C.Structure):
    _fields_ = [('dir', C.c_char * 400), ('opened', C.c_int)]


class Info(C.Structure):
    _fields_ = [('frame', C.c_uint32), ('tv', C.c_int32), ('iso', C.c_uint32),
                ('exposure_numerator', C.c_uint32), ('exposure_denominator', C.c_uint32),
                ('actual_iso', C.c_uint32)]


BEFORE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint, C.c_uint, C.c_uint64)
AFTER = C.CFUNCTYPE(None, C.c_void_p, C.c_char_p, C.c_uint64)


class Hooks(C.Structure):
    _fields_ = [('context', C.c_void_p), ('before', BEFORE), ('after', AFTER)]


def main():
    with tempfile.TemporaryDirectory(prefix='g360-queue-') as td:
        library = Path(td) / 'queue.so'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-O1', '-g', f'-DG360_QUEUE_FRAME_BYTES=UINT64_C({FRAME})',
                        f'-DG360_QUEUE_OLD_FRAME_BYTES=UINT64_C({OLD_FRAME})',
                        '-I' + str(HERE), '-I' + str(RE / 'capture-session'), '-shared', '-fPIC',
                        str(HERE / 'queue.c'), str(HERE / 'storage.c'), '-o', str(library)], check=True)
        run(library)


def run(library):
    lib = C.CDLL(str(library))
    lib.g360_storage_open.argtypes = [C.POINTER(Storage), C.c_char_p]
    lib.g360_storage_close.argtypes = [C.POINTER(Storage)]
    lib.g360_storage_reserve_dng_at.argtypes = [C.POINTER(Storage), C.c_char_p, C.c_char_p, C.c_char_p]
    lib.g360_storage_reserve_at.argtypes = [C.POINTER(Storage), C.c_char_p, *([C.c_char_p] * 4)]
    lib.g360_storage_timestamp.argtypes = [C.c_char_p]
    lib.g360_queue_open.argtypes = [C.POINTER(Queue), C.c_char_p]
    lib.g360_queue_create_frame.argtypes = [C.POINTER(Queue), C.c_char_p, C.c_uint, C.c_char_p]
    lib.g360_queue_create_jpeg.argtypes = lib.g360_queue_create_frame.argtypes
    lib.g360_queue_append_info.argtypes = [C.POINTER(Queue), C.c_char_p, C.POINTER(Info)]
    lib.g360_queue_oldest.argtypes = [C.POINTER(Queue), C.c_char_p]
    lib.g360_queue_move.argtypes = [C.POINTER(Queue), C.c_char_p, C.POINTER(Storage), C.POINTER(Hooks),
                                    C.POINTER(C.c_uint64), C.POINTER(C.c_uint), C.POINTER(C.c_uint)]
    with tempfile.TemporaryDirectory(prefix='g360-queue-run-') as td:
        work = Path(td)
        root, qdir = work / 'G360', work / 'flash' / 'queue'
        (work / 'flash').mkdir()
        storage, queue = Storage(), Queue()
        assert lib.g360_storage_open(C.byref(storage), os.fsencode(root)) == 0
        assert lib.g360_queue_open(C.byref(queue), os.fsencode(qdir)) == 0 and qdir.is_dir()
        assert lib.g360_queue_open(C.byref(Queue()), os.fsencode(work / 'missing' / 'q')) == 2

        stamp = C.create_string_buffer(16)
        assert lib.g360_storage_timestamp(stamp) == 0 and len(stamp.value) == 15 and stamp.value[8:9] == b'-'

        def oldest():
            out = C.create_string_buffer(16)
            found = lib.g360_queue_oldest(C.byref(queue), out)
            return found, out.value

        def create(frame, data, stamp_=STAMP, jpeg=False):
            path = C.create_string_buffer(512)
            create_fn = lib.g360_queue_create_jpeg if jpeg else lib.g360_queue_create_frame
            status = create_fn(C.byref(queue), stamp_, frame, path)
            if status == 0:
                assert Path(path.value.decode()).read_bytes() == b''
                Path(path.value.decode()).write_bytes(data)
            return status, path.value.decode()

        def info(rows, stamp_=STAMP):
            # One line per captured frame, as the sweep appends them.
            for row in rows:
                status = lib.g360_queue_append_info(C.byref(queue), stamp_, C.byref(Info(*row)))
                if status != 0:
                    return status
            return 0

        def move(stamp_=STAMP, stop_at=None):
            seen = []
            before = BEFORE(lambda _c, done, total, size: seen.append((done, total)) or int(stop_at is not None and done >= stop_at))
            after = AFTER(lambda _c, stem, size: seen.append((stem.decode(), size)))
            hooks = Hooks(None, before, after)
            written, moved, dropped = C.c_uint64(0), C.c_uint(), C.c_uint()
            status = lib.g360_queue_move(C.byref(queue), stamp_, C.byref(storage), C.byref(hooks),
                                         C.byref(written), C.byref(moved), C.byref(dropped))
            return status, written.value, moved.value, dropped.value, seen

        assert oldest() == (0, b'')
        frames = {k: bytes([k]) * FRAME for k in (1, 2, 3)}
        for k, data in frames.items():
            assert create(k, data)[0] == 0
        assert create(2, b'x')[0] == 3, 'an existing frame is never reused'
        assert frames[2] == (qdir / '20261105-123456-2.dng').read_bytes()
        assert create(4, b'short')[0] == 0                     # an interrupted write
        assert create(0, b'')[0] == 1 and create(17, b'')[0] == 1
        assert lib.g360_queue_create_frame(C.byref(queue), b'2026-1105-1234', 1, C.create_string_buffer(512)) == 1
        # Names that aren't ours are left alone.
        for other in ('notes.txt', '20261105-123456-0.dng', '20261105-123456-17.dng', '20261105-123456-01.dng'):
            (qdir / other).write_bytes(b'keep')
        # Frame 3 has no exposure line; frame 4's never matters.
        assert info([(1, 48, 1600, 1, 8, 1600), (2, 48, 400, 1, 8, 400)]) == 0
        assert info([(4, 80, 100, 1, 32, 100)]) == 0, 'a later frame appends'
        assert (qdir / '20261105-123456.csv').read_text() == (
            'g360-sweep 1\nframe,tv,iso,exposure_numerator,exposure_denominator,actual_iso\n'
            '1,48,1600,1,8,1600\n2,48,400,1,8,400\n4,80,100,1,32,100\n')
        assert info([(0, 48, 1600, 1, 8, 1600)]) == 1 and info([(17, 48, 1600, 1, 8, 1600)]) == 1
        assert lib.g360_queue_append_info(C.byref(queue), b'2026-1105-1234', C.byref(Info(1, 48, 1600, 1, 8, 1600))) == 1
        assert oldest() == (1, STAMP)

        # A second, later sweep stays queued while the first moves.
        later = b'20261105-130000'
        assert create(1, b'\x07' * FRAME, later)[0] == 0
        assert oldest() == (1, STAMP)

        status, written, moved, dropped, seen = move()
        assert (status, moved, dropped) == (0, 3, 1), (status, moved, dropped, seen)
        assert [s for s in seen if isinstance(s[0], int)] == [(0, 4), (1, 4), (2, 4), (3, 4)], seen
        photos = sorted(p.name for p in (root / 'photos').iterdir())
        assert photos == ['20261105-123456-00001.dng', '20261105-123456-00002.dng', '20261105-123456-00003.dng',
                          '20261105-123456-sweep.csv'], photos
        for k in (1, 2, 3):
            assert (root / 'photos' / f'20261105-123456-{k:05d}.dng').read_bytes() == frames[k]
        csv = (root / 'photos' / '20261105-123456-sweep.csv').read_text()
        assert csv == ('file,frame,tv,iso,exposure_time,actual_iso\n'
                       '20261105-123456-00001.dng,1,48,1600,1/8,1600\n'
                       '20261105-123456-00002.dng,2,48,400,1/8,400\n'
                       '20261105-123456-00003.dng,3,,,,\n'), csv
        assert written == 3 * FRAME + len(csv.encode()), written
        assert sorted(p.name for p in qdir.iterdir()) == sorted(
            ['notes.txt', '20261105-123456-0.dng', '20261105-123456-17.dng', '20261105-123456-01.dng',
             '20261105-130000-1.dng']), sorted(p.name for p in qdir.iterdir())
        assert oldest() == (1, later)

        # A stop leaves the rest queued; the next move carries on, the card
        # list gets one header only.
        assert create(2, b'\x08' * FRAME, later)[0] == 0
        assert create(3, b'\x09' * FRAME, later)[0] == 0
        assert info([(1, 112, 100, 1, 128, 100), (2, 144, 100, 0, 0, 0), (3, 176, 100, 1, 2048, 100)], later) == 0
        status, _, moved, _, _ = move(later, stop_at=1)
        assert (status, moved) == (5, 1), (status, moved)
        assert (qdir / '20261105-130000.csv').exists() and not (qdir / '20261105-130000-1.dng').exists()
        status, _, moved, _, _ = move(later)
        assert (status, moved) == (0, 2) and not (qdir / '20261105-130000.csv').exists()
        csv = (root / 'photos' / '20261105-130000-sweep.csv').read_text()
        assert csv == ('file,frame,tv,iso,exposure_time,actual_iso\n'
                       '20261105-130000-00004.dng,1,112,100,1/128,100\n'
                       '20261105-130000-00005.dng,2,144,100,,0\n'
                       '20261105-130000-00006.dng,3,176,100,1/2048,100\n'), csv
        assert oldest() == (0, b'')

        # A damaged exposure line ends the reading; earlier lines still count.
        third = b'20261106-080000'
        for k in (1, 2):
            assert create(k, bytes([k]) * FRAME, third)[0] == 0
        (qdir / '20261106-080000.csv').write_text('g360-sweep 1\nframe,tv,iso,exposure_numerator,'
                                                  'exposure_denominator,actual_iso\n1,48,1600,1,8,1600\n2,4x,1,1,1,1\n')
        assert move(third)[0] == 0
        lines = (root / 'photos' / '20261106-080000-sweep.csv').read_text().splitlines()
        assert lines[1].endswith(',1,48,1600,1/8,1600') and lines[2].endswith(',2,,,,'), lines

        # A crash while frame 3's line was being written: frames 1 and 2 keep
        # their exposures, frame 3 moves without one.
        cut = b'20261106-100000'
        for k in (1, 2, 3):
            assert create(k, bytes([k]) * FRAME, cut)[0] == 0
        assert info([(1, 48, 1600, 1, 8, 1600), (2, 48, 400, 1, 8, 400)], cut) == 0
        with open(qdir / '20261106-100000.csv', 'a') as f:
            f.write('3,64,10')
        status, _, moved, _, _ = move(cut)
        assert (status, moved) == (0, 3), (status, moved)
        lines = (root / 'photos' / '20261106-100000-sweep.csv').read_text().splitlines()
        assert [l.split(',', 1)[1] for l in lines[1:]] == ['1,48,1600,1/8,1600', '2,48,400,1/8,400', '3,,,,'], lines

        # A frame queued by an older build (plain header) still moves.
        older = b'20261106-090000'
        assert create(1, b'\x05' * OLD_FRAME, older)[0] == 0
        status, _, moved, dropped, _ = move(older)
        assert (status, moved, dropped) == (0, 1, 0), (status, moved, dropped)

        # RAW+JPEG pairs reserve a single card stem, keep both exposure rows,
        # and resume at frame boundaries rather than treating files as frames.
        paired = b'20261107-100000'
        jpeg = b'\xff\xd8synthetic JPEG\xff\xd9'
        for k in (1, 2):
            assert create(k, bytes([k]) * FRAME, paired)[0] == 0
            assert create(k, jpeg + b'', paired, jpeg=True)[0] == 0
        assert create(1, b'replace', paired, jpeg=True)[0] == 3
        assert create(0, jpeg, paired, jpeg=True)[0] == 1
        assert info([(1, 48, 1600, 1, 8, 1600), (2, 48, 400, 1, 8, 400)], paired) == 0
        assert oldest() == (1, paired)
        status, written, moved, dropped, seen = move(paired, stop_at=1)
        assert (status, moved, dropped) == (5, 1, 0), (status, moved, dropped)
        assert [s for s in seen if isinstance(s[0], int)] == [(0, 2), (1, 2)], seen
        assert not (qdir / '20261107-100000-1.dng').exists()
        assert not (qdir / '20261107-100000-1.jpg').exists()
        assert (qdir / '20261107-100000-2.dng').exists() and (qdir / '20261107-100000-2.jpg').exists()
        assert move(paired)[0:4:2] == (0, 1)
        rows = (root / 'photos' / '20261107-100000-sweep.csv').read_text().splitlines()[1:]
        assert len(rows) == 4, rows
        for k in (0, 2):
            dng_name, jpg_name = rows[k].split(',')[0], rows[k + 1].split(',')[0]
            assert Path(dng_name).stem == Path(jpg_name).stem
            assert rows[k].split(',', 1)[1] == rows[k + 1].split(',', 1)[1]
            assert (root / 'photos' / jpg_name).read_bytes() == jpeg
        assert oldest() == (0, b'')

        # Recovery preserves the complete partner when either output was cut
        # short, including a JPEG left alone after the DNG source was removed.
        partial = b'20261107-110000'
        assert create(1, b'd' * FRAME, partial)[0] == 0
        assert create(1, b'\xff\xd8no end', partial, jpeg=True)[0] == 0
        assert create(2, b'cut DNG', partial)[0] == 0
        assert create(2, jpeg, partial, jpeg=True)[0] == 0
        assert create(3, jpeg, partial, jpeg=True)[0] == 0
        assert create(4, b'bad JPEG', partial, jpeg=True)[0] == 0
        status, _, moved, dropped, _ = move(partial)
        assert (status, moved, dropped) == (0, 3, 3), (status, moved, dropped)
        rows = (root / 'photos' / '20261107-110000-sweep.csv').read_text().splitlines()[1:]
        assert len(rows) == 3 and '.dng,1,' in rows[0] and '.jpg,2,' in rows[1] and '.jpg,3,' in rows[2], rows
        assert oldest() == (0, b'')
        assert not list(qdir.glob('20261107-110000*'))
        # A complete-looking but oversized JPEG is rejected without a bulk
        # read; malformed filenames remain outside queue ownership.
        oversized = b'20261107-120000'
        status, jpg_path = create(1, jpeg, oversized, jpeg=True)
        assert status == 0
        with open(jpg_path, 'r+b') as f:
            f.seek(100663296 + 65536 + 4096)
            f.write(b'\xff\xd9')
        for other in ('20261107-120000-01.jpg', '20261107-120000-17.jpg', '20261107-120000-1.jpeg'):
            (qdir / other).write_bytes(jpeg)
        assert move(oversized)[0:4] == (0, 0, 0, 1)
        assert all((qdir / other).read_bytes() == jpeg for other in
                   ('20261107-120000-01.jpg', '20261107-120000-17.jpg', '20261107-120000-1.jpeg'))
        assert oldest() == (0, b'')

        # If card reservation fails, both members of a pair remain queued.
        retry = b'20261107-130000'
        assert create(1, b'd' * FRAME, retry)[0] == 0
        assert create(1, jpeg, retry, jpeg=True)[0] == 0
        lib.g360_storage_close(C.byref(storage))
        assert move(retry)[0] == 4
        assert (qdir / '20261107-130000-1.dng').exists() and (qdir / '20261107-130000-1.jpg').exists()
        assert lib.g360_storage_open(C.byref(storage), os.fsencode(root)) == 0
        assert move(retry)[0] == 0

        # Failure after both copies, while writing the exposure list, must
        # still retain both sources. Retrying may duplicate but cannot lose.
        failed_list = b'20261107-140000'
        assert create(1, b'd' * FRAME, failed_list)[0] == 0
        assert create(1, jpeg, failed_list, jpeg=True)[0] == 0
        blocked_list = root / 'photos' / '20261107-140000-sweep.csv'
        blocked_list.mkdir()
        assert move(failed_list)[0] == 2
        assert (qdir / '20261107-140000-1.dng').read_bytes() == b'd' * FRAME
        assert (qdir / '20261107-140000-1.jpg').read_bytes() == jpeg
        copied_dng = list((root / 'photos').glob('20261107-140000-*.dng'))
        copied_jpg = list((root / 'photos').glob('20261107-140000-*.jpg'))
        assert len(copied_dng) == len(copied_jpg) == 1
        assert copied_dng[0].stem == copied_jpg[0].stem
        assert copied_dng[0].read_bytes() == b'd' * FRAME and copied_jpg[0].read_bytes() == jpeg
        blocked_list.rmdir()
        assert move(failed_list)[0] == 0
        assert len(list((root / 'photos').glob('20261107-140000-*.dng'))) == 2
        assert len(list((root / 'photos').glob('20261107-140000-*.jpg'))) == 2
        assert oldest() == (0, b'')

        # A failed card reservation leaves everything queued.
        assert create(1, b'\x01' * FRAME, third)[0] == 0
        lib.g360_storage_close(C.byref(storage))
        assert move(third)[0] == 4 and (qdir / '20261106-080000-1.dng').exists()
        assert lib.g360_storage_open(C.byref(storage), os.fsencode(root)) == 0
        assert move(third)[0] == 0 and oldest() == (0, b'')
        lib.g360_storage_close(C.byref(storage))
    print('queue DNG/JPEG pairs, same-stem exposure lists, resumable moves, orphan/partial recovery and refusals passed')


if __name__ == '__main__':
    main()
