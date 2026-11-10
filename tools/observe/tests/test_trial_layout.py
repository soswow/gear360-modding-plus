"""Validate trial options without any camera connection."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_still_trial as trial


class TrialLayoutTests(unittest.TestCase):
    def test_layout_bounds(self):
        for layout in ("0xa6000000,5824,2896,11648,16", "0xad134000,2896,2896,5824,16",
                       "0xa6000000,64,32,132,12", "0xa6000000,064,032,0128,016"):
            self.assertTrue(trial.valid_raw_layout(layout), layout)
        for layout in ("0xa6000000,2147483648,1,128,12",
                       "0xa6000000,1,2147483649,2,12", "0xfffff000,64,33,128,16",
                       "0xa6000000,64,32,127,16", "0xa6000000,64,0,128,16",
                       "0xa6000000,64,32,128,17", "1,2,3", "-1,64,32,128,16"):
            self.assertFalse(trial.valid_raw_layout(layout), layout)

    def test_invalid_layout_never_enters_camera_runner(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(trial, "REPO", Path(tmp)), \
                patch.object(trial, "run") as run, patch.object(trial.camsh, "run") as camera:
            for kind in ("raw", "frame"):
                argv = ["run_still_trial.py", "run", "--g360", "2", "--g360-" + kind,
                        "--g360-" + kind + "-layout", "0xa6000000,2147483648,1,128,12"]
                with patch.object(sys, "argv", argv), contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as exc:
                        trial.main()
                self.assertEqual(exc.exception.code, 2)
            run.assert_not_called()
            camera.assert_not_called()


if __name__ == "__main__":
    unittest.main()
