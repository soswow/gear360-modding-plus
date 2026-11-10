import struct
import unittest

from decode_logs import decode_frames, decode_objects


def frame_line(body=b"\x01\x02\x03\x04", labelled=True, extra=b""):
    data = struct.pack("<HHHH", 0x0300, 0x81, 0x18c, len(body)) + body + b"\0\0"
    return (f"T 3 {'ring ' if labelled else ''}A W 18 R 18 D 18 "
            f"L {len(data)} B{(data + extra).hex()}")


class FrameTests(unittest.TestCase):
    def test_both_log_formats_and_extra_printed_bytes(self):
        for labelled in (True, False):
            record, = decode_frames(frame_line(labelled=labelled, extra=b"\xff" * 8))
            self.assertEqual(record["ring"], "A")
            self.assertEqual(record["attribute"], 0x18c)
            self.assertEqual(record["body_words_le"], [0x04030201])
            self.assertEqual(record["body_hex"], "01020304")
            self.assertEqual(record["extra_printed_bytes"], 8)

    def test_no_partial_words_or_field_names_invented(self):
        record, = decode_frames(frame_line(body=b"\x03"))
        self.assertEqual(record["body_words_le"], [])
        self.assertEqual(record["body_hex"], "03")

    def test_truncation_and_bad_hex_are_refused(self):
        valid = frame_line()
        for text in (valid[:-2], valid[:-1] + "z", valid.replace("L 14", "L 15")):
            with self.assertRaises(ValueError):
                list(decode_frames(text))

    def test_status_records_are_not_frames(self):
        self.assertEqual(list(decode_frames("T 0 INIT A W 0 R 0\nSUMMARY frames=0")), [])


class ObjectTests(unittest.TestCase):
    def test_decimal_addresses_and_neutral_words(self):
        address = 0x81000010
        record, = decode_objects(f"OBJECT {address} len 5\n01000000ff")
        self.assertEqual(record["address"], address)
        self.assertEqual(record["words_le"], [1])
        self.assertEqual(record["tail_hex"], "ff")
        self.assertNotIn("width", record)

    def test_follow_source_index_is_separate_from_address(self):
        follow, obj = decode_objects(
            "FOLLOW 14 -> 2164260880\nOBJECT 0x81000010 len 4\n02000000")
        self.assertEqual(follow["source_word"], 14)
        self.assertEqual(follow["address"], obj["address"])

    def test_truncated_overlong_and_odd_records_are_refused(self):
        for text in ("OBJECT 1 len 8\n01000000", "OBJECT 1 len 1\n0102",
                     "OBJECT 1 len 1\nf", "OBJECT 1 len 4097"):
            with self.assertRaises(ValueError):
                list(decode_objects(text))

    def test_out_of_u32_address_is_refused(self):
        with self.assertRaises(ValueError):
            list(decode_objects("OBJECT 4294967296 len 4\n00000000"))


if __name__ == "__main__":
    unittest.main()
