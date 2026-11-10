#!/usr/bin/env python3
"""deploy-over-web.sh and install-app.sh against a fake camera CGI on localhost.

The fake runs each command in a temporary directory: /opt/storage/sdcard and
/tmp are moved there, and the app's process commands are simulated. It can
cut the connection after a number of requests or drop one upload piece.
FTP is either a closed port (the HTTP fallback) or curl's file:// upload into
the fake camera's /tmp.
Never contacts a device.
"""
import hashlib
import http.server
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.parse

TOOLS = Path(__file__).resolve().parents[1] / "tools"
APP_DIR = "/opt/storage/sdcard/mods/g360-app-v1-20261105"


class Camera:
    def __init__(self, root):
        self.root = root
        self.pids = ""          # what pgrep -x g360-app prints
        self.fail_after = None  # requests answered before the link drops
        self.drop_piece = None  # 1-based upload piece answered but not run
        self.piece_delay = 0    # seconds each upload piece takes
        self.requests = []
        (root / "tmp").mkdir(parents=True)
        (root / "sdcard/mods/g360-app-v1-20261105").mkdir(parents=True)

    def path(self, remote):
        return self.root / remote.replace("/opt/storage/sdcard/", "sdcard/").replace("/tmp/", "tmp/")

    def run(self, cmd):
        """Returns the command's output, or None to drop the connection."""
        self.requests.append(cmd)
        if self.fail_after is not None and len(self.requests) > self.fail_after:
            return None
        if cmd.startswith("pgrep -x g360-app"):
            return self.pids
        if "kill -TERM" in cmd:
            self.pids = ""
            return ""
        if cmd.startswith("setsid sh"):
            self.pids = "77\n"
            return ""
        if "openssl base64 -d >>" in cmd:
            time.sleep(self.piece_delay)
            pieces = sum("openssl base64 -d >>" in c for c in self.requests)
            if pieces == self.drop_piece:
                return ""
        local = cmd.replace("/opt/storage/sdcard/", f"{self.root}/sdcard/").replace("/tmp/", f"{self.root}/tmp/")
        done = subprocess.run(["sh", "-c", local], capture_output=True, text=True)
        return done.stdout + done.stderr


def serve(camera):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            query = urllib.parse.urlparse(self.path).query
            cmd = urllib.parse.parse_qs(query)["rawcmd"][0]
            out = camera.run(cmd)
            if out is None:
                self.close_connection = True
                self.connection.shutdown(2)
                return
            body = f"<html>\n<body><pre>\nExecuting:  {cmd}\n{out}</pre></body>\n</html>\n".encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *args):
            pass

    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.cam = Camera(self.root / "camera")
        self.server = serve(self.cam)
        # FTP on a closed port: the uploads take the HTTP fallback.
        self.env = dict(os.environ, GEAR360_CGI_BASE=f"http://127.0.0.1:{self.server.server_port}/cgi-bin",
                        GEAR360_FTP_BASE="ftp://127.0.0.1:9")

    def use_ftp(self):
        self.env["GEAR360_FTP_BASE"] = f"file://{self.cam.root}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.tmp.cleanup()

    def local(self, name, data):
        path = self.root / name
        path.write_bytes(data)
        return path

    def run_tool(self, tool, *args):
        return subprocess.run(["sh", str(TOOLS / tool), *map(str, args)], env=self.env,
                              capture_output=True, text=True, timeout=120)

    def card(self, remote):
        return self.cam.path(remote)


OLD = bytes(range(256)) * 3
NEW = os.urandom(700)


class Deploy(Base):
    target = APP_DIR + "/start.sh"

    def test_new_file_then_replace(self):
        r = self.run_tool("deploy-over-web.sh", self.local("a", OLD), self.target)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.card(self.target).read_bytes(), OLD)
        r = self.run_tool("deploy-over-web.sh", self.local("b", NEW), self.target)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.card(self.target).read_bytes(), NEW)
        self.assertEqual(sorted(p.name for p in self.card(APP_DIR).iterdir()), ["start.sh"])
        self.assertEqual(list((self.cam.root / "tmp").iterdir()), [])
        swap = [c for c in self.cam.requests if " mv " in c]
        self.assertEqual(swap, [f"rm -f {self.target} && mv {self.target}.new {self.target} && sync"] * 2)

    def test_cut_off_upload_leaves_old_file(self):
        self.card(self.target).write_bytes(OLD)
        self.cam.fail_after = 6
        r = self.run_tool("deploy-over-web.sh", self.local("b", NEW), self.target)
        self.assertNotEqual(r.returncode, 0)
        self.assertEqual(self.card(self.target).read_bytes(), OLD)
        # Stopped at the first dropped request; the RAM cleanup is dropped too.
        self.assertEqual(len(self.cam.requests), 8)
        self.assertEqual(self.cam.requests[-1], self.cam.requests[0].replace(": >", "rm -f"))

    def test_lost_piece_is_caught_before_the_card(self):
        self.card(self.target).write_bytes(OLD)
        self.cam.drop_piece = 3
        r = self.run_tool("deploy-over-web.sh", self.local("b", NEW), self.target)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("old file untouched", r.stderr)
        self.assertEqual(self.card(self.target).read_bytes(), OLD)
        self.assertFalse(self.card(self.target + ".new").exists())
        self.assertEqual(list((self.cam.root / "tmp").iterdir()), [])

    def test_interrupted_upload_cleans_ram_and_leaves_old_file(self):
        self.card(self.target).write_bytes(OLD)
        self.cam.piece_delay = 0.2
        proc = subprocess.Popen(["sh", str(TOOLS / "deploy-over-web.sh"), self.local("b", NEW), self.target],
                                env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                start_new_session=True)
        while sum("base64 -d" in c for c in self.cam.requests) < 3:
            time.sleep(0.05)
        os.killpg(proc.pid, signal.SIGTERM)
        proc.communicate(timeout=30)
        self.assertNotEqual(proc.returncode, 0)
        self.assertEqual(self.card(self.target).read_bytes(), OLD)
        self.assertFalse(self.card(self.target + ".new").exists())
        self.assertEqual(list((self.cam.root / "tmp").iterdir()), [])

    def test_ftp_upload_then_the_same_checks(self):
        self.card(self.target).write_bytes(OLD)
        self.use_ftp()
        r = self.run_tool("deploy-over-web.sh", self.local("b", NEW), self.target)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("over FTP", r.stdout)
        self.assertEqual(self.card(self.target).read_bytes(), NEW)
        self.assertFalse(any("base64 -d" in c for c in self.cam.requests))
        self.assertEqual(list((self.cam.root / "tmp").iterdir()), [])
        self.assertLessEqual(len(self.cam.requests), 6)

    def test_ftp_failure_falls_back_to_http(self):
        r = self.run_tool("deploy-over-web.sh", self.local("b", NEW), self.target)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("FTP upload failed", r.stderr)
        self.assertIn("over the HTTP shell", r.stdout)
        self.assertEqual(self.card(self.target).read_bytes(), NEW)

    def test_app_binary_goes_through_install(self):
        r = self.run_tool("deploy-over-web.sh", self.local("b", NEW), APP_DIR + "/g360-app")
        self.assertEqual(r.returncode, 2)
        self.assertIn("install-app.sh", r.stderr)
        self.assertEqual(self.cam.requests, [])


class Install(Base):
    def setUp(self):
        super().setUp()
        self.card(APP_DIR + "/g360-app").write_bytes(OLD)
        self.card(APP_DIR + "/g360-app.sha256").write_text(hashlib.sha256(OLD).hexdigest() + "\n")

    def pair(self):
        return (self.card(APP_DIR + "/g360-app").read_bytes(),
                self.card(APP_DIR + "/g360-app.sha256").read_text())

    def old_pair(self):
        return (OLD, hashlib.sha256(OLD).hexdigest() + "\n")

    def test_swaps_app_and_hash_together_with_backup(self):
        r = self.run_tool("install-app.sh", "--backup", "t1-20261106", self.local("b", NEW))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.pair(), (NEW, hashlib.sha256(NEW).hexdigest() + "\n"))
        self.assertEqual(self.card(APP_DIR + "/g360-app.g360-before-t1-20261106").read_bytes(), OLD)
        self.assertEqual(self.card(APP_DIR + "/g360-app.sha256.g360-before-t1-20261106").read_text(),
                         self.old_pair()[1])
        self.assertFalse(any(p.name.endswith(".new") for p in self.card(APP_DIR).iterdir()))
        self.assertFalse(any("setsid" in c for c in self.cam.requests))

    def test_install_over_ftp(self):
        self.use_ftp()
        r = self.run_tool("install-app.sh", self.local("b", NEW))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("over FTP", r.stdout)
        self.assertEqual(self.pair(), (NEW, hashlib.sha256(NEW).hexdigest() + "\n"))

    def test_cut_off_while_staging_keeps_the_old_pair(self):
        # Requests: 1 app check, 2-20 staging the app (15 pieces), 21 the hash
        # file, 22-23 app checks, 24 the swap.
        for after in (1, 10, 19, 21, 23):
            with self.subTest(after=after):
                self.cam.requests.clear()
                self.cam.fail_after = after
                r = self.run_tool("install-app.sh", self.local("b", NEW))
                self.assertNotEqual(r.returncode, 0)
                self.assertEqual(self.pair(), self.old_pair())

    def test_refuses_a_running_app_without_restart(self):
        self.cam.pids = "123\n"
        r = self.run_tool("install-app.sh", self.local("b", NEW))
        self.assertEqual(r.returncode, 1)
        self.assertIn("--restart", r.stderr)
        self.assertEqual(self.cam.requests, ["pgrep -x g360-app"])
        self.assertEqual(self.pair(), self.old_pair())

    def test_existing_backup_is_not_overwritten(self):
        kept = self.card(APP_DIR + "/g360-app.g360-before-t1")
        kept.write_bytes(b"earlier backup")
        r = self.run_tool("install-app.sh", "--backup", "t1", self.local("b", NEW))
        self.assertEqual(r.returncode, 1)
        self.assertEqual(kept.read_bytes(), b"earlier backup")
        self.assertEqual(self.pair(), self.old_pair())

    def test_restart_stops_swaps_then_launches(self):
        self.cam.pids = "123\n"
        r = self.run_tool("install-app.sh", "--restart", self.local("b", NEW))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("Started g360-app (PID 77)", r.stdout)
        self.assertEqual(self.pair()[0], NEW)
        order = [i for i, c in enumerate(self.cam.requests)
                 if "kill -TERM" in c or "mv g360-app.new" in c or c.startswith("setsid")]
        kinds = [("kill" if "kill" in self.cam.requests[i] else
                  "swap" if "mv" in self.cam.requests[i] else "launch") for i in order]
        self.assertEqual(kinds, ["kill", "swap", "launch"])


if __name__ == "__main__":
    unittest.main()
