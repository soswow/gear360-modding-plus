#!/usr/bin/env python3
"""Host-only ABI/control-flow tests with synthetic SDK and bridge libraries."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
camera = root / "camera"
cc = os.environ.get("CC", "cc")

with tempfile.TemporaryDirectory(prefix="one-still-probe-") as td:
    work = Path(td)
    sdk, bridge, devnode = work / "sdk.so", work / "bridge.so", work / "devnode.so"
    userspec, apexlib, mapsfile = work / "userspec.so", work / "apex.so", work / "maps"
    source = (camera / "one-still-probe.c").read_text()
    assert source.count('"/usr/lib/libcapture-fw-prod.so"') == 1
    assert source.count('"/usr/lib/libmulticore-bridge.so"') == 1
    source = source.replace('"/usr/lib/libcapture-fw-prod.so"', f'"{sdk}"')
    source = source.replace('"/usr/lib/libmulticore-bridge.so"', f'"{bridge}"')
    assert source.count('"/usr/lib/libdevice-node.so.0.1"') == 1
    source = source.replace('"/usr/lib/libdevice-node.so.0.1"', f'"{devnode}"')
    for real, fake in (('"/usr/lib/libuser-spec.so"', userspec), ('"/usr/lib/libapex.so"', apexlib),
                       ('"/proc/self/maps"', mapsfile)):
        assert source.count(real) == 1, real
        source = source.replace(real, f'"{fake}"')
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
void *_ZN11CMCBAdapter8InstanceEv(void){ mark("instance\n"); return (void*)1; }
int _ZN11CMCBAdapter10InitializeEv(void *p){ mark(p== (void*)1?"initialize\n":"bad_initialize\n"); return 0; }
void *_ZN19CBeautyFaceIf_Linux11GetInstanceEv(void){ mark("beauty\n"); return (void*)2; }
int mm_camera_create(unsigned *h){ mark("create\n"); *h=0x1234; return 0; }
void *mm_camera_ioctl(uint32_t h,uint32_t s,int *p3,int p4,int *p5,int *p6){
  (void)p3;(void)p4;(void)p5;(void)p6;
  char line[40]; snprintf(line,sizeof line,"ioctl:%x\n",s);
  mark(h==0x1234u?line:"bad_ioctl_handle\n");
  return getenv("IOCTL_FAIL") ? (void*)-1 : 0;
}
void GetImageBufferMemoryInfo(int *info){
  mark("meminfo\n");
  if(!info) return;
  info[0]=100; info[1]=50; info[2]=0; info[3]=30; info[4]=10; info[5]=5; info[6]=7;
}
void RequestImageBufferMemoryInfo(int *req){
  mark("memreq\n");
  if(!req) return;
  req[0]=1; req[1]=0xa00000;
}
int RequestImageBufferMemory(int size){
  mark("memalloc\n");
  return size==0xa00000 ? 0x9c400000 : -1;
}
uint32_t RegCallbackClient(char *name,notify_fn n,image_fn i,set_fn s){
  if(strlen(name)>9) return UINT32_MAX; ncb=n; icb=i; scb=s; mark("register\n"); return 7;
}
int RegImageSetCallbackId(uint32_t slot,uint32_t topic){
  mark(slot==7 && topic==0x04010005u?"subscribe-public\n":"bad_subscribe\n");
  if(slot!=7 || topic!=0x04010005u) return -2;
#ifdef REG_FAIL
  return -1;
#else
  return 0;
#endif
}
int RegNotifyCallbackId(uint32_t slot,uint32_t topic){
  char line[40]; snprintf(line,sizeof line,"notify:%x\n",topic);
  int ok = slot==7 && (topic==0x2010001u||topic==0x2010003u||topic==0x2010004u||
                       topic==0x2010005u||topic==0x2010006u||topic==0x2010007u);
  mark(ok?line:"bad_subscribe\n");
  if(!ok) return -2;
#ifdef REG_FAIL
  return -1;
#else
  return 0;
#endif
}
int mm_camera_capture_still(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,
 uint32_t f,uint32_t g,uint32_t h,uint32_t i,uint32_t j,uint32_t k,uint32_t l,
 uint32_t m,uint32_t n,uint32_t o,uint32_t p,uint32_t q){
  char line[200]; snprintf(line,sizeof line,"capture:%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q); mark(line);
  if(getenv("CALLBACK")){ scb(0x02010005,(void*)1,(void*)3,4); }
  return getenv("STILL_FAIL") ? -7 : 0;
}
''')
    (work / "sdk_missing.c").write_text((work / "sdk.c").read_text().replace(
        'int mm_camera_create(unsigned *h)', '#ifndef OMIT_CREATE\nint mm_camera_create(unsigned *h)').replace(
        'return 0; }\nvoid *mm_camera_ioctl', 'return 0; }\n#endif\nvoid *mm_camera_ioctl'))
    (work / "bridge.c").write_text(r'''
#include <stdio.h>
#include <stdlib.h>
static void mark(const char *s){FILE*f=fopen(getenv("TRACE"),"a");if(f){fputs(s,f);fclose(f);}}
int Mcb_send(unsigned short c,unsigned short a,unsigned short n,unsigned char*p){
 mark(c==16&&a==0&&n==0&&!p?"send\n":"bad_send\n"); return 0;
}
unsigned char _ZN16CMulticoreBridge18IsDestinationAliveEv(void){
 static int reads; ++reads; return getenv("BASELINE_ALIVE") ? 1 : reads >= 3;
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
    subprocess.run([cc, "-shared", "-fPIC", str(work / "sdk.c"), "-o", str(sdk)], check=True)
    subprocess.run([cc, "-shared", "-fPIC", str(work / "bridge.c"), "-o", str(bridge)], check=True)
    # Synthetic libdevice-node: the gyro history properties the gate reads.
    # GYRO_EMPTY=1 models the camera case where the devman plugin failed to
    # load and every property silently stays 0.
    (work / "devnode.c").write_text(r'''
#include <stdlib.h>
int device_get_property(int type,int prop,int *value){
  if(type!=2 || !value) return -1;
  if(getenv("GYRO_EMPTY")) return 0;
  if(prop==0x20) *value=8000000; else if(prop==0x18) *value=(int)0x9cd00000u; else if(prop==0x19) *value=1234;
  return 0;
}
''')
    subprocess.run([cc, "-shared", "-fPIC", str(work / "devnode.c"), "-o", str(devnode)], check=True)
    # Synthetic libapex: a CApex singleton with the info pointer at +0xc.
    (work / "apex.c").write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char instance[0x40];
static void mark(const char *s){FILE*f=fopen(getenv("TRACE"),"a");if(f){fputs(s,f);fclose(f);}}
void *_ZN5CApex11GetInstanceEv(void){ return instance; }
void _ZN5CApex10InitializeEP13tag_apex_info(void *self, void *info){
  memcpy((unsigned char*)self + 0xc, &info, sizeof info); mark("apex_init\n"); }
''')
    subprocess.run([cc, "-shared", "-fPIC", str(work / "apex.c"), "-o", str(apexlib)], check=True)
    # Synthetic libuser-spec: the RTOS preference area (0x80000 bytes) with the
    # user-spec magic at +0x40000, a /proc/self/maps stand-in describing it, and
    # the max-aperture query that reads [info + 0x6f0] like the camera's.
    (work / "userspec.c").write_text(r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char area[0x80000] __attribute__((aligned(4096)));
static void mark(const char *s){FILE*f=fopen(getenv("TRACE"),"a");if(f){fputs(s,f);fclose(f);}}
void *_ZN5CApex11GetInstanceEv(void);
uintptr_t pref_rtos_get_fd(void){
  uint32_t magic = getenv("APEX_NO_MAGIC") ? 0 : 0x330033u;
  FILE *m = fopen(getenv("MAPS_FILE"), "w");
  memcpy(area + 0x40000, &magic, 4);
  area[0x40000 + 0x4d4 + 0x6f0] = 0x2a;
  if (m) {
    if (!getenv("APEX_UNMAPPED"))
      fprintf(m, "%lx-%lx rw-s 85400000 00:05 1 /dev/mem\n",
              (unsigned long)(uintptr_t)area, (unsigned long)(uintptr_t)(area + sizeof area));
    fclose(m);
  }
  return (uintptr_t)area;
}
void _Z33Apex_Get_Max_Av_Value_For_CapturePjS_(unsigned *num, unsigned *den){
  unsigned char *info = 0;
  memcpy(&info, (unsigned char*)_ZN5CApex11GetInstanceEv() + 0xc, sizeof info);
  if (!info) { mark("apex_null_crash\n"); abort(); }
  mark(info >= area && info < area + sizeof area ? "apex_shared_info\n" : "apex_private_info\n");
  *num = info[0x6f0]; *den = 10;
}
''')
    subprocess.run([cc, "-shared", "-fPIC", str(work / "userspec.c"), str(apexlib), "-o", str(userspec)],
                   check=True)
    exe = work / "probe"
    suppressed_exe = work / "probe-suppressed"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", *inc,
                    "-Dclock_gettime=fake_clock", "-Dnanosleep=fake_sleep",
                    str(work / "probe.c"), str(work / "clock.c"), "-ldl", "-o", str(exe)], check=True)
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", *inc,
                    "-DSUPPRESS_THE_STILL", "-Dclock_gettime=fake_clock",
                    "-Dnanosleep=fake_sleep", str(work / "probe.c"),
                    str(work / "clock.c"), "-ldl", "-o", str(suppressed_exe)], check=True)

    def run(*args, _exe=exe, **env):
        trace, logfile = work / "trace", work / "run.log"
        trace.unlink(missing_ok=True); logfile.unlink(missing_ok=True)
        cmd = [str(_exe), "--diagnostic-one-still", "--library", str(sdk),
               "--log", str(logfile), "--free-bytes", "4294967297"]
        cmd += list(args)
        e = dict(os.environ, TRACE=str(trace), MAPS_FILE=str(mapsfile), **env)
        result = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=5)
        return result, trace.read_text().splitlines() if trace.exists() else [], logfile.read_text() if logfile.exists() else ""

    # CLI is rejected before dlopen/bootstrap; only exact invocation accepted.
    r, tr, _ = run("--extra")
    assert r.returncode == 2 and not tr
    r = subprocess.run([str(exe), "--diagnostic-one-still"], capture_output=True, text=True)
    assert r.returncode == 2

    # The build toggle rejects values outside 0/1 before invoking clang.
    bad_out = work / "bad-build"
    for invalid in ("anything", "", "01", " 1"):
        build_env = dict(os.environ, SUPPRESS_THE_STILL=invalid, CLANG="/no/such/clang")
        r = subprocess.run(["sh", str(camera / "build-one-still-probe.sh"), str(bad_out)],
                           env=build_env, capture_output=True, text=True)
        assert r.returncode == 2 and "must be exactly 0 or 1" in r.stderr
        assert not bad_out.exists()

    # Initial clock failure refuses before loading/bootstrapping the SDK.
    r, tr, log = run(CLOCK_FAIL="1")
    assert r.returncode == 1 and not tr and not any(x.startswith("capture:") for x in tr)
    assert "monotonic=unavailable" in log and "preflight failed" in log

    # Even with the bridge already alive, the pre-still clock check must pass.
    r, tr, log = run(BASELINE_ALIVE="1", CLOCK_FAIL_AFTER_CREATE="1")
    assert r.returncode == 1 and "send" not in tr and not any(x.startswith("capture:") for x in tr)
    assert "pre-still" in log

    # Gyro gate: an empty motion history (devman plugin not loaded) aborts
    # before any bootstrap or capture command.
    r, tr, log = run(GYRO_EMPTY="1")
    assert r.returncode == 1 and not tr, (r.stderr, tr, log)
    assert "motion history unavailable" in log
    r, tr, log = run(BASELINE_ALIVE="1")
    assert "gyro gate: history size/phys/pos first=8000000" in log

    # APEX gate: the singleton gets a private copy of the user-spec block and
    # the max-aperture query (the image-set handler's call) works before any
    # bootstrap; nothing in the shared area is used as the info pointer.
    r, tr, log = run(BASELINE_ALIVE="1")
    assert r.returncode == 0, (r.stderr, log)
    assert "apex gate: user-spec magic first=3342387" in log
    assert "apex gate: max aperture num/den first=42 second=10" in log
    assert tr.index("apex_init") < tr.index("instance"), tr
    assert "apex_private_info" in tr and "apex_shared_info" not in tr and "apex_null_crash" not in tr
    # Missing magic: refuse before bootstrap (the library's own init would
    # reset the shared settings block).
    r, tr, log = run(APEX_NO_MAGIC="1")
    assert r.returncode == 1 and not any(x in tr for x in ("apex_init", "instance", "create")), tr
    assert "user-spec magic missing; refusing" in log
    # Unmapped user-spec block: abort before reading it.
    r, tr, log = run(APEX_UNMAPPED="1")
    assert r.returncode == 1 and not any(x in tr for x in ("apex_init", "instance")), tr
    assert "not mapped readable" in log

    # An already-live bridge skips the explicit one-shot notification.
    r, tr, log = run(BASELINE_ALIVE="1")
    assert r.returncode == 0 and "send" not in tr and sum(x.startswith("capture:") for x in tr) == 1
    assert "no reply association claimed" in log

    # Successful path performs one notify and one exact raw17 still; bogus callback pointers are opaque.
    r, tr, log = run(CALLBACK="1")
    assert r.returncode == 0, (r.stderr, log)
    assert tr.count("send") == 1 and sum(x.startswith("capture:") for x in tr) == 1
    assert tr.count("ioctl:43") == 1, "the warm-boot-init ioctl must be sent exactly once"
    assert tr.count("ioctl:44") == 1, "the boot wakeup ioctl must be sent exactly once"
    assert tr.count("ioctl:1") == 1, "the lock3a ioctl must be sent exactly once"
    assert "subscribe-public" in tr and "bad_subscribe" not in tr
    assert sum(x.startswith("notify:") for x in tr) == 6, tr
    assert "notify:2010007" in tr
    assert "capture:4660,0,1,1,1,0,1,0,1,1,0,0,0,0,0,0,0" in tr
    assert "callback.image_set first=33619973" in log and "capture-shot: before probe hold\n" in log and "capture-shot: after probe hold\n" in log
    assert "unproven" in log and not list(work.glob("*.jpg"))

    # A nonzero wakeup return is recorded but does not suppress the still,
    # matching the stock application's fire-and-forget wakeup.
    r, tr, log = run(CALLBACK="1", IOCTL_FAIL="1")
    assert r.returncode == 0, (r.stderr, log)
    assert "warm-boot-init ioctl returned nonzero" in log
    assert sum(x.startswith("capture:") for x in tr) == 1, tr

    # The bisect build performs the same setup and notify, but never invokes
    # the still stub or emits a fabricated SDK call/return log.
    r, tr, log = run(CALLBACK="1", _exe=suppressed_exe)
    assert r.returncode == 0, (r.stderr, log)
    assert all(x in tr for x in ("instance", "initialize", "beauty", "create",
                                 "register", "subscribe-public", "send"))
    assert sum(x.startswith("notify:") for x in tr) == 6, tr
    assert tr.count("send") == 1 and not any(x.startswith("capture:") for x in tr)
    assert "no still command invoked" in log and "prior notify request was sent" in log
    assert "mm_camera_capture_still entering" not in log
    assert "mm_camera_capture_still returned" not in log
    assert not list(work.glob("*.jpg")) and not list(work.glob("*.jpeg"))

    # Nonzero capture return is indeterminate and never retried.
    r, tr, log = run(STILL_FAIL="1")
    assert r.returncode == 1 and sum(x.startswith("capture:") for x in tr) == 1
    assert "indeterminate; no retry" in log

    # Failures during the required post-call observation make the outcome nonzero.
    r, tr, log = run(CLOCK_FAIL_AFTER_CAPTURE="1")
    assert r.returncode == 1 and sum(x.startswith("capture:") for x in tr) == 1
    assert "observation incomplete" in log and "no retry" in log
    r, tr, log = run(SLEEP_FAIL_AFTER_CAPTURE="1")
    assert r.returncode == 1 and sum(x.startswith("capture:") for x in tr) == 1
    assert "observation incomplete" in log and "no retry" in log

    # Registration failure suppresses capture.
    subprocess.run([cc, "-shared", "-fPIC", "-DREG_FAIL", str(work / "sdk.c"), "-o", str(sdk)], check=True)
    r, tr, log = run()
    assert r.returncode == 1 and not any(x.startswith("capture:") for x in tr)
    assert "still suppressed" in log

    # Missing required symbol stops before Instance/bootstrap.
    subprocess.run([cc, "-shared", "-fPIC", "-DOMIT_CREATE", str(work / "sdk_missing.c"), "-o", str(sdk)], check=True)
    r, tr, log = run()
    assert r.returncode == 1 and not any(x in tr for x in ("instance", "initialize", "beauty", "create"))
    assert "bootstrap not entered" in log

print("one-still synthetic SDK/bridge tests passed")
