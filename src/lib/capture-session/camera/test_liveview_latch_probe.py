#!/usr/bin/env python3
"""Host-only ABI/control-flow tests for the liveview latch diagnostic.

The synthetic SDK exports the same destination-alive byte and getter names the
pinned bridge exports, so the probe's resolution path is exercised on the
host.  The synthetic byte can be toggled from the environment to model a
latch that is clear, a latch that is set, and a latch that disagrees between
the variable and the getter.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
camera = root / "camera"
cc = os.environ.get("CC", "cc")

with tempfile.TemporaryDirectory(prefix="liveview-latch-probe-") as td:
    work = Path(td)
    sdk = work / "sdk.so"
    source = (camera / "liveview-latch-probe.c").read_text()

    # The probe must not contain a still call, the exported start entry point,
    # a release of the callback data, or an allocation.
    assert "mm_camera_capture_still" not in source
    assert "mm_camera_start" not in source
    assert "free(" not in source
    assert "malloc(" not in source
    # Exactly one liveview selector literal, so there is one request site.
    assert source.count("0x1b") == 1
    assert source.count('"/usr/lib/libcapture-fw-prod.so"') == 1

    source = source.replace('"/usr/lib/libcapture-fw-prod.so"', f'"{sdk}"')
    (work / "probe.c").write_text(source)

    (work / "sdk.c").write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
typedef void (*notify_fn)(int,void*,unsigned);
static notify_fn ncb;
/* The destination-alive byte, named exactly as the pinned bridge names it. */
unsigned char _ZN7CSender12m_bDestAliveE;
static void mark(const char *s) { FILE *f=fopen(getenv("TRACE"),"a"); if(f){fputs(s,f);fclose(f);} }
unsigned char _ZN16CMulticoreBridge18IsDestinationAliveEv(void){
  unsigned char v = _ZN7CSender12m_bDestAliveE;
  if(getenv("LATCH_GETTER_LIES")) v = (unsigned char)(v ^ 1u);
  return v;
}
void *_ZN11CMCBAdapter8InstanceEv(void){ mark("instance\n"); return (void*)1; }
int _ZN11CMCBAdapter10InitializeEv(void *p){ mark(p== (void*)1?"initialize\n":"bad_initialize\n"); return 0; }
void *_ZN19CBeautyFaceIf_Linux11GetInstanceEv(void){ mark("beauty\n"); return (void*)2; }
int mm_camera_create(unsigned *h){ mark("create\n"); if(!h) return -2; *h=0x1234u; return 0; }
int mm_camera_set_notification_callback(uint32_t handle, uintptr_t cb, uint32_t userdata){
  char line[160];
  (void)userdata;
  snprintf(line,sizeof line,"set-callback:%lu\n",(unsigned long)handle); mark(line);
  ncb = (notify_fn)cb;
  return getenv("CB_FAIL") ? -3 : 0;
}
#ifndef OMIT_SET_ID
int mm_camera_set_notification_id(uint32_t handle, uint32_t id, ...){
  char line[160];
  snprintf(line,sizeof line,"set-id:%lu:%lu\n",(unsigned long)handle,(unsigned long)id); mark(line);
  if(getenv("ID_FAIL")) return -3;
  /* The latch may be raised by an incoming broadcast at subscription time. */
  if(getenv("LATCH_RAISED_AT_ID")) _ZN7CSender12m_bDestAliveE = 1u;
  return 0;
}
#endif
void *mm_camera_ioctl(uint32_t handle, uint32_t selector, int *p3, int p4, int *p5, int *p6){
  char line[200];
  snprintf(line,sizeof line,"ioctl:%lu,%lu,%d,%d,%d,%d\n",(unsigned long)handle,(unsigned long)selector,
           p3?1:0,p4,p5?1:0,p6?1:0);
  mark(line);
  /* Record the latch value at the moment the request is issued. */
  { char l[80]; snprintf(l,sizeof l,"latch-at-ioctl:%u\n",_ZN7CSender12m_bDestAliveE); mark(l); }
  if(getenv("CALLBACK") && ncb){
    /* A PROT_NONE page: a probe that dereferenced the data pointer faults. */
    void *poison = mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(poison==MAP_FAILED) poison=NULL;
    ncb(8,poison,1234u);
  }
  return getenv("IOCTL_FAIL") ? (void*)1 : (void*)0;
}
''')
    (work / "clock.c").write_text(r'''
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long ticks;
static int trace_has(const char *needle){char b[8192];FILE*f=fopen(getenv("TRACE"),"r");size_t n;if(!f)return 0;n=fread(b,1,sizeof(b)-1,f);fclose(f);b[n]=0;return strstr(b,needle)!=0;}
int fake_clock(clockid_t id,struct timespec*t){(void)id;if(getenv("CLOCK_FAIL") || (getenv("CLOCK_FAIL_AFTER_ID")&&trace_has("set-id:")))return -1;t->tv_sec=ticks/10;t->tv_nsec=(ticks%10)*100000000L;return 0;}
int fake_sleep(const struct timespec*a,struct timespec*b){(void)a;(void)b;if(getenv("SLEEP_FAIL")&&trace_has("ioctl:"))return -1;++ticks;return 0;}
''')
    inc = [f"-I{root}"]
    subprocess.run([cc, "-shared", "-fPIC", str(work / "sdk.c"), "-o", str(sdk)], check=True)
    exe = work / "probe"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", *inc,
                    "-Dclock_gettime=fake_clock", "-Dnanosleep=fake_sleep",
                    str(work / "probe.c"), str(work / "clock.c"), "-ldl", "-o", str(exe)], check=True)

    def run(*args, **env):
        trace, logfile = work / "trace", work / "run.log"
        trace.unlink(missing_ok=True); logfile.unlink(missing_ok=True)
        cmd = [str(exe), "--diagnostic-liveview-latch-only", "--library", str(sdk),
               "--log", str(logfile), "--free-bytes", "4294967297"]
        cmd += list(args)
        e = dict(os.environ, TRACE=str(trace), **env)
        result = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=60)
        return result, trace.read_text().splitlines() if trace.exists() else [], logfile.read_text() if logfile.exists() else ""

    # CLI is rejected before dlopen/bootstrap; only the exact invocation is accepted.
    r, tr, _ = run("--extra")
    assert r.returncode == 2 and not tr
    r = subprocess.run([str(exe), "--diagnostic-liveview-latch-only"], capture_output=True, text=True)
    assert r.returncode == 2
    r = subprocess.run([str(exe), "--diagnostic-liveview-latch-only", "--library", str(sdk),
                        "--log", str(work / "run.log"), "--free-bytes", "12x"],
                       capture_output=True, text=True)
    assert r.returncode == 2
    r = subprocess.run([str(exe), "--diagnostic-liveview-latch-only", "--library", str(sdk),
                        "--log", str(work / "run.log")], capture_output=True, text=True)
    assert r.returncode == 2

    # Initial clock failure refuses before loading/bootstrapping the SDK.
    r, tr, log = run(CLOCK_FAIL="1")
    assert r.returncode == 1 and not tr
    assert "monotonic=unavailable" in log and "preflight failed" in log

    def idx(lines, prefix):
        for position, line in enumerate(lines):
            if line.startswith(prefix):
                return position
        raise AssertionError(f"no trace line starts with {prefix}: {lines}")

    # Successful path: create, then the callback registration, then the id
    # subscription, then exactly one liveview ioctl with null data and length.
    r, tr, log = run(CALLBACK="1")
    assert r.returncode == 0, (r.stderr, log)
    assert "create" in tr and "set-callback:4660" in tr and "set-id:4660:8" in tr
    assert idx(tr, "create") < idx(tr, "set-callback") < idx(tr, "set-id:")
    assert idx(tr, "set-id:") < idx(tr, "ioctl:")
    assert sum(x.startswith("ioctl:") for x in tr) == 1
    assert "ioctl:4660,27,0,0,0,0" in tr
    assert not any("still" in x or "capture" in x for x in tr)
    # The handle is passed BY VALUE.
    assert not any(re.search(r"set-(callback|id):(?!4660)\d+", x) for x in tr)
    assert "callback.notify first=8 second=1234" in log
    assert "unproven" in log
    assert "status read not attempted" in log
    assert not list(work.glob("*.jpg"))

    # The latch is observed before and after the request, and the clear case
    # is reported as a clear byte rather than as an absence.
    assert "destination-alive before liveview request value=0" in log
    assert "destination-alive getter before liveview request value=0" in log
    assert "destination-alive after liveview request value=0" in log
    assert "destination-alive getter after liveview request value=0" in log
    assert "destination-alive variable and getter disagree" not in log
    # The observation is armed and the byte address is recorded.
    assert "destination-alive observation armed" in log
    assert "destination-alive variable address pointer=" in log
    # The synthetic SDK records the latch at the moment of the request, so the
    # probe's before-value must agree with what the send path would have read.
    assert "latch-at-ioctl:0" in tr

    # A latch that is already set is reported as set, both before and after.
    r, tr, log = run(LATCH_RAISED_AT_ID="1")
    assert r.returncode == 0, (r.stderr, log)
    assert "destination-alive before liveview request value=1" in log
    assert "destination-alive after liveview request value=1" in log
    assert "latch-at-ioctl:1" in tr

    # A getter that disagrees with the byte is reported, so the observation
    # is never silently presented as authoritative.
    r, tr, log = run(LATCH_GETTER_LIES="1")
    assert r.returncode == 0, (r.stderr, log)
    assert "destination-alive variable and getter disagree before the request" in log

    # A nonzero ioctl return is reported and never retried.
    r, tr, log = run(IOCTL_FAIL="1")
    assert r.returncode == 1 and sum(x.startswith("ioctl:") for x in tr) == 1
    assert "no retry" in log and "nonzero" in log
    # The latch is still observed around the failed request.
    assert "destination-alive before liveview request value=0" in log
    assert "destination-alive after liveview request value=0" in log

    # A nonzero id subscription aborts before any liveview request.
    r, tr, log = run(ID_FAIL="1")
    assert r.returncode == 1 and not any(x.startswith("ioctl:") for x in tr)
    assert "create" in tr and "set-callback:4660" in tr
    assert "subscription failed" in log

    # A clock failure after the subscription leaves the outcome indeterminate.
    r, tr, log = run(CLOCK_FAIL_AFTER_ID="1")
    assert r.returncode == 1 and not any(x.startswith("ioctl:") for x in tr)
    assert "pre-liveview" in log

    # A failed sleep inside the observation window is reported, not retried.
    r, tr, log = run(SLEEP_FAIL="1")
    assert r.returncode == 1 and sum(x.startswith("ioctl:") for x in tr) == 1
    assert "observation incomplete" in log and "no retry" in log

    # A missing required symbol stops before any bootstrap call.
    subprocess.run([cc, "-shared", "-fPIC", "-DOMIT_SET_ID", str(work / "sdk.c"), "-o", str(sdk)], check=True)
    r, tr, log = run()
    assert r.returncode == 1
    assert not any(x in tr for x in ("instance", "initialize", "beauty", "create", "set-callback"))
    assert "bootstrap not entered" in log
    assert "mm_camera_set_notification_id" in log

print("liveview-latch synthetic SDK tests passed")
