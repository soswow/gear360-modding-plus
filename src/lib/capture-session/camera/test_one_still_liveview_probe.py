#!/usr/bin/env python3
"""Host-only ABI/control-flow tests for the one-still liveview-first probe.

The synthetic SDK and bridge export the same symbol names the pinned
libraries do, so the probe's resolution path and its ordering are exercised
on the host.  The synthetic destination-alive byte can be toggled to model a
latch that rises after the handshake, a latch that stays clear, a latch that
is already nonzero, and a getter that disagrees with the byte.
"""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
camera = root / "camera"
cc = os.environ.get("CC", "cc")

with tempfile.TemporaryDirectory(prefix="one-still-liveview-probe-") as td:
    work = Path(td)
    bridge = work / "bridge.so"
    sdk_real = work / "libsdk.so"
    source = (camera / "one-still-liveview-probe.c").read_text()

    # The probe must contain exactly one liveview request site, one still
    # request site, and no image or RAW handling.
    assert source.count("0x1b") == 1
    assert source.count('"/usr/lib/libcapture-fw-prod.so"') == 1
    assert source.count('"/usr/lib/libmulticore-bridge.so"') == 1
    assert "mm_camera_capture_still" in source
    assert "free(" not in source
    assert "malloc(" not in source

    source = source.replace('"/usr/lib/libcapture-fw-prod.so"', f'"{sdk_real}"')
    source = source.replace('"/usr/lib/libmulticore-bridge.so"', f'"{bridge}"')
    (work / "probe.c").write_text(source)

    (work / "sdk.c").write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void (*notify_fn)(int,void*,unsigned);
typedef void (*image_fn)(int,void*,void*,unsigned);
typedef void (*set_fn)(int,void*,void*,unsigned);
static notify_fn ncb; static image_fn icb; static set_fn scb;
static void mark(const char *s) { FILE *f=fopen(getenv("TRACE"),"a"); if(f){fputs(s,f);fclose(f);} }
/* The destination-alive byte, reached through the SDK handle exactly as the
 * pinned capture library reaches the bridge instance it depends on. */
unsigned char _ZN7CSender12m_bDestAliveE;
void *_ZN11CMCBAdapter8InstanceEv(void){ mark("instance\n"); return (void*)1; }
int _ZN11CMCBAdapter10InitializeEv(void *p){ mark(p== (void*)1?"initialize\n":"bad_initialize\n"); return 0; }
void *_ZN19CBeautyFaceIf_Linux11GetInstanceEv(void){ mark("beauty\n"); return (void*)2; }
int mm_camera_create(unsigned *h){ mark("create\n"); *h=0x1234; return 0; }
uint32_t RegCallbackClient(char *name,notify_fn n,image_fn i,set_fn s){
  if(strlen(name)>9) return UINT32_MAX; ncb=n; icb=i; scb=s; mark("register\n"); return 7;
}
int RegImageSetCallbackId(uint32_t slot,uint32_t topic){
  mark(slot==7 && topic==0x04010005u?"subscribe-public\n":"bad_subscribe\n");
  if(slot!=7 || topic!=0x04010005u) return -2;
  return 0;
}
int RegNotifyCallbackId(uint32_t slot,uint32_t topic){
  char line[40]; snprintf(line,sizeof line,"notify:%x\n",topic);
  int ok = slot==7 && (topic==0x2010001u||topic==0x2010003u||topic==0x2010004u||
                       topic==0x2010005u||topic==0x2010006u||topic==0x2010007u);
  mark(ok?line:"bad_subscribe\n");
  return ok ? 0 : -2;
}
void *mm_camera_ioctl(uint32_t handle,uint32_t selector,int*p3,int p4,int*p5,int*p6){
  char line[200]; snprintf(line,sizeof line,"ioctl:%lu,%lu\n",(unsigned long)handle,(unsigned long)selector);
  mark(line);
  if(selector==0x1bu && getenv("CALLBACK")){ ncb(8,(void*)1,0); }
  return getenv("IOCTL_FAIL") ? (void*)1 : (void*)0;
}
int mm_camera_capture_still(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,
 uint32_t f,uint32_t g,uint32_t h,uint32_t i,uint32_t j,uint32_t k,uint32_t l,
 uint32_t m,uint32_t n,uint32_t o,uint32_t p,uint32_t q){
  char line[200]; snprintf(line,sizeof line,"capture:%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q); mark(line);
  if(getenv("CALLBACK")){ scb(0x02010005,(void*)1,(void*)3,4); }
  return getenv("STILL_FAIL") ? -7 : 0;
}
''')
    (work / "bridge.c").write_text(r'''
#include <stdio.h>
#include <stdlib.h>
/* The same symbol the synthetic SDK exports, so the bridge raises the very
 * byte the SDK handle exposes, as the pinned bridge and capture library share
 * one instance. */
extern unsigned char _ZN7CSender12m_bDestAliveE;
static void mark(const char *s){FILE*f=fopen(getenv("TRACE"),"a");if(f){fputs(s,f);fclose(f);}}
int Mcb_send(unsigned short c,unsigned short a,unsigned short n,unsigned char*p){
 mark(c==16&&a==0&&n==0&&!p?"send\n":"bad_send\n");
 if(!getenv("LATCH_STAYS_CLEAR")) _ZN7CSender12m_bDestAliveE = 1u;
 return 0;
}
unsigned char _ZN16CMulticoreBridge18IsDestinationAliveEv(void){
 if(getenv("LATCH_GETTER_LIES")) return (unsigned char)(_ZN7CSender12m_bDestAliveE ^ 1u);
 return _ZN7CSender12m_bDestAliveE;
}
''')
    (work / "clock.c").write_text(r'''
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned long ticks;
static int trace_has(const char *needle){char b[8192];FILE*f=fopen(getenv("TRACE"),"r");size_t n;if(!f)return 0;n=fread(b,1,sizeof(b)-1,f);fclose(f);b[n]=0;return strstr(b,needle)!=0;}
int fake_clock(clockid_t id,struct timespec*t){(void)id;if(getenv("CLOCK_FAIL") || (getenv("CLOCK_FAIL_AFTER_CREATE")&&trace_has("create\n")) || (getenv("CLOCK_FAIL_AFTER_CAPTURE")&&trace_has("capture:")))return -1;t->tv_sec=ticks/10;t->tv_nsec=(ticks%10)*100000000L;return 0;}
int fake_sleep(const struct timespec*a,struct timespec*b){(void)a;(void)b;if(getenv("SLEEP_FAIL_AFTER_CAPTURE")&&trace_has("capture:"))return -1;++ticks;return 0;}
''')
    inc = [f"-I{root}"]
    subprocess.run([cc, "-shared", "-fPIC", str(work / "sdk.c"), "-o", str(sdk_real)], check=True)
    # The bridge links against the SDK so that both resolve the one
    # destination-alive byte, as the pinned bridge and capture library share
    # a single loaded instance.
    subprocess.run([cc, "-shared", "-fPIC", str(work / "bridge.c"),
                    str(sdk_real), "-o", str(bridge)], check=True)
    exe = work / "probe"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", *inc,
                    "-Dclock_gettime=fake_clock", "-Dnanosleep=fake_sleep",
                    str(work / "probe.c"), str(work / "clock.c"), "-ldl", "-o", str(exe)], check=True)

    def run(*args, **env):
        trace, logfile = work / "trace", work / "run.log"
        trace.unlink(missing_ok=True); logfile.unlink(missing_ok=True)
        cmd = [str(exe), "--diagnostic-one-still-liveview", "--library", str(sdk_real),
               "--log", str(logfile), "--free-bytes", "4294967297"]
        cmd += list(args)
        e = dict(os.environ, TRACE=str(trace), **env)
        result = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=10)
        return result, trace.read_text().splitlines() if trace.exists() else [], logfile.read_text() if logfile.exists() else ""

    def idx(lines, prefix):
        for position, line in enumerate(lines):
            if line.startswith(prefix):
                return position
        raise AssertionError(f"no trace line starts with {prefix}: {lines}")

    # CLI is rejected before dlopen/bootstrap; only the exact invocation works.
    r, tr, _ = run("--extra")
    assert r.returncode == 2 and not tr
    r = subprocess.run([str(exe), "--diagnostic-one-still-liveview"], capture_output=True, text=True)
    assert r.returncode == 2
    r = subprocess.run([str(exe), "--diagnostic-one-still-liveview", "--library", str(sdk_real),
                        "--log", str(work / "run.log"), "--free-bytes", "12x"],
                       capture_output=True, text=True)
    assert r.returncode == 2

    # Initial clock failure refuses before loading/bootstrapping the SDK.
    r, tr, log = run(CLOCK_FAIL="1")
    assert r.returncode == 1 and not tr
    assert "monotonic=unavailable" in log and "preflight failed" in log

    # The stock ordering: the handshake, then the liveview request, then the
    # liveview stop, then the still request, each exactly once.
    r, tr, log = run(CALLBACK="1")
    assert r.returncode == 0, (r.stderr, log)
    assert tr.count("send") == 1
    assert sum(x.startswith("ioctl:") for x in tr) == 2
    assert sum(x.startswith("capture:") for x in tr) == 1
    assert idx(tr, "send") < idx(tr, "ioctl:4660,27") < idx(tr, "ioctl:4660,28") < idx(tr, "capture:")
    assert "ioctl:4660,27" in tr
    assert "ioctl:4660,28" in tr
    assert "capture:4660,0,1,1,1,0,1,0,1,1,0,0,0,0,0,0,0" in tr
    assert "subscribe-public" in tr and "bad_subscribe" not in tr
    assert sum(x.startswith("notify:") for x in tr) == 6, tr
    assert "notify:2010007" in tr
    # The latch is read and reported before the liveview request.
    assert "destination-alive before liveview request value=1" in log
    assert "destination-alive after liveview request value=1" in log
    assert "destination-alive before still request value=1" in log
    assert "destination-alive after still request value=1" in log
    assert "destination-alive variable and getter disagree" not in log
    # The callback families are counted separately.
    assert "callback.notify first=8 second=0" in log
    assert "callback families observed: notify, image, image_set" in log
    assert "capture-shot: before probe hold\n" in log
    assert "capture-shot: after probe hold\n" in log
    assert "unproven" in log
    assert not list(work.glob("*.jpg"))

    # A latch that stays clear suppresses both requests.
    r, tr, log = run(LATCH_STAYS_CLEAR="1")
    assert r.returncode == 1
    assert tr.count("send") == 1
    assert not any(x.startswith("ioctl:") for x in tr)
    assert not any(x.startswith("capture:") for x in tr)
    assert "still zero after the admission handshake" in log
    assert "requests suppressed" in log

    # A getter that disagrees with the byte is reported, so the reading is
    # never silently presented as authoritative.
    r, tr, log = run(LATCH_GETTER_LIES="1")
    assert r.returncode == 0, (r.stderr, log)
    assert "destination-alive variable and getter disagree" in log

    # A nonzero liveview return does not stop the still; it is recorded and
    # the single still request still happens once.
    r, tr, log = run(IOCTL_FAIL="1")
    assert r.returncode == 1
    assert sum(x.startswith("ioctl:") for x in tr) == 2
    assert sum(x.startswith("capture:") for x in tr) == 1
    assert "liveview start edge returned nonzero" in log
    assert "indeterminate; no retry" in log

    # A nonzero still return is indeterminate and never retried.
    r, tr, log = run(STILL_FAIL="1")
    assert r.returncode == 1 and sum(x.startswith("capture:") for x in tr) == 1
    assert "indeterminate; no retry" in log

    # Failures during the observation window make the outcome nonzero.
    r, tr, log = run(CLOCK_FAIL_AFTER_CAPTURE="1")
    assert r.returncode == 1 and sum(x.startswith("capture:") for x in tr) == 1
    assert "observation incomplete" in log and "no retry" in log
    r, tr, log = run(SLEEP_FAIL_AFTER_CAPTURE="1")
    assert r.returncode == 1 and sum(x.startswith("capture:") for x in tr) == 1
    assert "observation incomplete" in log and "no retry" in log

print("one-still-liveview synthetic SDK/bridge tests passed")
