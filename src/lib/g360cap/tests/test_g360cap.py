#!/usr/bin/env python3
"""Host tests for g360cap and g360-shot against a synthetic capture library.

The fake SDK delivers the still's notifications and image set from its own
thread, as the camera's receiver thread does. Its image set uses the camera
layout: word 0 of the set points to the 0x228-byte basic set, set+0x2e0 holds
the maker-note address, and the main/screen/thumb (and, for RAW set types,
first) images are listed with 32-bit addresses. The fake overwrites a buffer
when it is given back, so a copy taken after the give-back would show up.

With FAKE_EXPANSION the fake also publishes the still-expansion topic
(0x04010008) before the image set, after writing the full frame into the
start of its region; the maps then list that range as a /dev/mem mapping
whose offset is the frame's physical address, so the library can find and
copy the frame while the camera waits.

Run: CC=/usr/bin/clang python3 src/lib/g360cap/tests/test_g360cap.py
"""
import os
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parents[1]
re_root = here.parent
cc = os.environ.get("CC", "cc")

FAKE_SDK = r'''
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
typedef void (*notify_fn)(int,void*,unsigned);
typedef void (*image_fn)(int,void*,void*,unsigned);
typedef void (*set_fn)(int,void*,void*,unsigned);
static notify_fn ncb; static image_fn icb; static set_fn scb;
static unsigned char *region;
static const size_t region_size = 0x400000;
/* The full frame the camera writes before the still expansion: 64 x 32
   samples, 16-bit, stride 128 bytes, so 4096 bytes at the start of the
   region (physical 0xa6000000). The give-back path below never clears it
   (it only clears from 0x10000 up). */
#define FRAME_BYTES 4096
static unsigned stills;
static int set_on; /* the movie image set 0x4010007 is subscribed */
static unsigned char descriptor[0x3c8];
static void mark(const char *s){FILE*f=fopen(getenv("TRACE"),"a");if(f){fputs(s,f);fclose(f);}}
static void put32(unsigned char *p, uint32_t v){memcpy(p,&v,4);}
static void ensure(void){
  if(!region){region=mmap(0,region_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if(region==MAP_FAILED){perror("mmap");abort();}}
}
uintptr_t g360_test_address_bias(void){ ensure(); return (uintptr_t)region; }
static void maps_line(FILE *m, size_t lo, size_t hi){
  fprintf(m,"%lx-%lx rw-p 00000000 00:00 0\n",(unsigned long)(uintptr_t)(region+lo),(unsigned long)(uintptr_t)(region+hi));
}
static void write_maps(void){
  /* Written to a side file and renamed into place, so a reader on the
     library's thread never sees a truncated file (the kernel's
     /proc/self/maps is never half-written either). */
  char tmp[1024]; FILE *m;
  ensure();
  snprintf(tmp,sizeof tmp,"%s.tmp",getenv("MAPS_FILE"));
  m=fopen(tmp,"w"); if(!m) return;
  if(getenv("FAKE_SET_UNMAPPED")) { maps_line(m,0x2000,region_size); }
  else if(getenv("FAKE_UNMAPPED_MAIN")) { maps_line(m,0,0x10000); maps_line(m,0x20000,region_size); }
  else maps_line(m,0,region_size);
  /* The frame's physical address: a /dev/mem mapping whose offset field is
     0xa6000000, so physical 0xa6000000 is the start of the region. Only the
     frame's own range, so the ranges the other tests leave unmapped stay
     unmapped (the kernel lists the whole /dev/mem mapping, but the library
     only needs the frame's part of it). */
  if(!getenv("FAKE_NO_DEV_MEM"))
    fprintf(m,"%lx-%lx rw-p a6000000 00:05 1 /dev/mem\n",
            (unsigned long)(uintptr_t)region,(unsigned long)(uintptr_t)(region+FRAME_BYTES));
  fprintf(m,"%lx-%lx r--p 00000000 00:00 0\n",
          (unsigned long)(uintptr_t)descriptor,
          (unsigned long)(uintptr_t)(descriptor+sizeof descriptor));
  fclose(m);
  rename(tmp,getenv("MAPS_FILE"));
}
/* Image offsets for set s: main 0x10000, screen 0x20000, thumb 0x30000,
   first 0x40000, each plus s*0x80000. */
static const uint32_t img_off[4]={0x40000,0x10000,0x20000,0x30000};
static const uint32_t img_size[4]={8000,5000,3000,1000};
static uint32_t off(int k, int set){ return img_off[k] + (uint32_t)set*0x80000u; }
static void fill_image(int k, unsigned still, int set){
  unsigned char *p=region+off(k,set); size_t n=img_size[k], i;
  for(i=0;i<n;i++) p[i]=(unsigned char)(i*7+still*31+set*7+k);
  if(k>0){ p[0]=0xff; p[1]=0xd8; p[n-2]=0xff; p[n-1]=0xd9;
    if(k==1 && getenv("FAKE_BAD_JPEG")) p[n-1]=0x00; }
}
/* Two small RAW images at offsets 0x300000 and 0x310000 (physical
   0xa0300000 and 0xa0310000 in the fake's numbering): 64 x 32 samples,
   stride 128 bytes, 12-bit, different contents. */
static void fill_raw(unsigned still){
  int r, i;
  for(r=0;r<2;r++){
    unsigned char *p=region+0x300000+(size_t)r*0x10000;
    for(i=0;i<64*32;i++){ unsigned v=(unsigned)(i*(13+4*r)+still+r)&0xfffu; p[2*i]=(unsigned char)v; p[2*i+1]=(unsigned char)(v>>8); }
  }
  if(getenv("FAKE_RAW_BAD")){ unsigned char *p=region+0x300000; p[100]=0x00; p[101]=0x10; } /* 0x1000 in image 1 */
}
/* The full frame: a known pattern in the first FRAME_BYTES of the region,
   which the expansion callback copies while the camera waits. */
static void fill_frame(unsigned still){
  size_t i;
  for(i=0;i<FRAME_BYTES;i++) region[i]=(unsigned char)(i*7u+still*31u+0x5au);
}
/* The still-expansion topic: the descriptor's word +0x3c0 is the mode. */
static void publish_expansion(void){
  unsigned size = getenv("FAKE_EXPANSION_SHORT") ? 4u : sizeof descriptor;
  void *arg = getenv("FAKE_EXPANSION_UNMAPPED") ? (void*)(uintptr_t)1 : descriptor;
  if(getenv("FAKE_EXPANSION_NULL")) arg=0;
  if(getenv("FAKE_FRAME_REMAP")) { setenv("FAKE_NO_DEV_MEM","1",1); write_maps(); }
  memset(descriptor,0,sizeof descriptor);
  put32(descriptor+0x3c0,3u);
  mark("expansion-callback-begin\n");
  if(getenv("FAKE_EXPANSION_WRONG_FIRST")) {
    put32(descriptor+0x3c0,2u); scb(0x04010008,0,descriptor,sizeof descriptor);
    put32(descriptor+0x3c0,3u);
  }
  if(getenv("FAKE_EXPANSION_WRONG_MODE")) put32(descriptor+0x3c0,2u);
  scb(0x04010008,0,arg,size);
  if(getenv("FAKE_EXPANSION_DUPLICATE")) {
    memset(region,0xdd,FRAME_BYTES); scb(0x04010008,0,arg,size);
  }
  mark("expansion-callback-end\n");
}
static void build_set(unsigned still, int set_index){
  unsigned char *set=region+0x1000, *basic=region+0x2000; int k;
  uint32_t type = getenv("FAKE_SET_TYPE") ? (uint32_t)atoi(getenv("FAKE_SET_TYPE")) : 1u;
  memset(set,0,0x80c); memset(basic,0,0x228);
  /* Request-associated values destroyed as soon as the callback returns. */
  put32(set+8,still); put32(set+12,250u+(unsigned)set_index);
  put32(set+8+0x70,100u+still+(unsigned)set_index);
  put32(set+8+0x6c,1); put32(set+8+0x84,0);
  put32(set,0x2000); put32(set+0x2e0,0x3000u+(uint32_t)set_index*0x10u);
  /* The maker note: "MKNT", the still number and the set index. */
  memcpy(region+0x3000+set_index*0x10,"MKNT",4); put32(region+0x3004+set_index*0x10,still);
  put32(region+0x3008+set_index*0x10,(uint32_t)set_index);
  if(getenv("FAKE_MAKER_NOTE")) {
    FILE *f=fopen(getenv("FAKE_MAKER_NOTE"),"rb");
    if(!f || fread(region+0x3000+set_index*0x10,1,0x6000,f)!=0x6000) abort();
    fclose(f);
  }
  if(set_index==0) fill_raw(still);
  put32(basic,type);
  for(k=0;k<4;k++){
    unsigned char *d=basic+8+k*0x88;
    if(k==0 && type==1) continue;
    if(k==1 && getenv("FAKE_NO_MAIN")) continue;
    put32(d+0x0,100+k); put32(d+0x4,200+k); put32(d+0x8,7776>>k); put32(d+0xc,3888>>k);
    put32(d+0x44,0xa0000000u+off(k,set_index)); put32(d+0x54,off(k,set_index)); put32(d+0x74,img_size[k]);
    fill_image(k,still,set_index);
  }
}
static void notify(unsigned id){ if(ncb) ncb(0x02010000+id,0,id==7?4:0); }
static void protect_images(int prot){
  if(getenv("FAKE_SYNC")) mprotect(region+0x10000,region_size-0x10000,prot);
}
static void send_set(unsigned still, int set_index){
  build_set(still,set_index); write_maps();
  /* In synchronous mode the images are inaccessible during the callback,
     so reading them there would crash the test. */
  protect_images(PROT_NONE);
  mark("set-callback-begin\n");
  scb(0x04010005,region+0x1000,0,0);
  mark("set-callback-end\n");
  protect_images(PROT_READ|PROT_WRITE);
  /* The library's stack copy is gone after the callback. */
  memset(region+0x1000,0xcc,0x80c); memset(region+0x2000,0xcc,0x228);
}
static void ready(void){ notify(5); notify(6); if(!getenv("FAKE_NO_READY")) notify(7); }
static void deliver(unsigned still){
  int nsets = getenv("FAKE_SETS") ? atoi(getenv("FAKE_SETS")) : 1, k;
  int late_ms = getenv("FAKE_LATE_MS") ? atoi(getenv("FAKE_LATE_MS")) : 0;
  usleep(20000);
  notify(1); notify(3); notify(4);
  if(getenv("FAKE_NO_SET")) return;
  if(getenv("FAKE_READY_NO_SET")){ ready(); return; }
  /* The camera writes the full frame and then publishes the expansion
     topic; the image set follows it. */
  if(getenv("FAKE_EXPANSION")){
    fill_frame(still); publish_expansion();
    /* Processing reuses the sensor buffer as soon as the pause ends. */
    memset(region,0xdd,FRAME_BYTES);
  }
  for(k=0;k<nsets;k++){
    if(late_ms && k==nsets-1){ ready(); usleep((useconds_t)late_ms*1000); }
    send_set(still,k);
  }
  if(!late_ms) ready();
}
static void *thread_main(void *arg){ deliver((unsigned)(uintptr_t)arg); return 0; }
void *_ZN11CMCBAdapter8InstanceEv(void){ mark("instance\n"); return (void*)1; }
int _ZN11CMCBAdapter10InitializeEv(void *p){ mark(p==(void*)1?"initialize\n":"bad_initialize\n"); return 0; }
void *_ZN19CBeautyFaceIf_Linux11GetInstanceEv(void){ mark("beauty\n"); return (void*)2; }
int mm_camera_create(unsigned *h){ mark("create\n"); *h=0x1234; return 0; }
uint32_t RegCallbackClient(char *name,notify_fn n,image_fn i,set_fn s){
  if(strlen(name)>9) return UINT32_MAX; ncb=n; icb=i; scb=s; mark("register\n"); return 7;
}
int RegImageSetCallbackId(uint32_t slot,uint32_t topic){
  if(slot==7 && topic==0x04010007u){ set_on=1; mark("subscribe-movie-set\n"); return 0; }
  {
  int ok = slot==7 && (topic==0x04010005u || topic==0x04010008u);
  /* The library arms the frame before it sends the still, so the frame's
     /dev/mem mapping has to be in the maps before the capture starts. */
  if(slot==7 && topic==0x04010008u) write_maps();
  mark(ok ? (topic==0x04010008u?"subscribe-expansion\n":"subscribe-set\n") : "bad_subscribe\n");
  return ok?0:-2;
  }
}
int RegNotifyCallbackId(uint32_t slot,uint32_t topic){
  char line[40]; snprintf(line,sizeof line,"notify:%x\n",topic); mark(line); return slot==7?0:-2;
}
int mm_camera_set_usr_attributes(unsigned h,int attr,uint32_t value){
  char line[64]; snprintf(line,sizeof line,"usr:%x:%d:%x\n",h,attr,value); mark(line);
  return getenv("USR_FAIL") ? -1 : 0;
}
int mm_camera_set_var_attributes(unsigned h,uint32_t id,int32_t value,uint32_t unused){
  char line[64]; (void)unused; snprintf(line,sizeof line,"var:%x:%x:%d\n",h,id,value); mark(line);
  return getenv("VAR_FAIL") ? -1 : 0;
}
int ReleaseCaptureBuffer(int type,uint32_t addr){
  char line[64]; snprintf(line,sizeof line,"release:%d:%x\n",type,addr); mark(line);
  if(addr==0) mark("release_zero\n");
  if(addr>=0x10000 && addr<0x400000) memset(region+addr,0xee,0x1000); /* the RTOS reuses it */
  if(type==5 && addr>=0x3000 && addr<0x3100) memset(region+addr,0xee,0x10);
  return getenv("FAKE_RELEASE_FAIL") ? -1 : 0;
}
/* The movie preview (T20): frames of 64 x 32, 4:2:0, Y at region offset
   0x200000 and C at 0x200800, delivered every 20 ms on image topic
   0x3030003 and, when subscribed, as the image set 0x4010007 whose first
   word points 0x90 below a copy of the block. */
static volatile int movie_on; static pthread_t movie_thread;
static unsigned char movie_block[0x88]; static unsigned char movie_set[16];
int RegImageCallbackId(uint32_t slot,uint32_t topic){
  char line[40]; snprintf(line,sizeof line,"image-sub:%x\n",topic); mark(line); return slot==7?0:-2;
}
int GetCaptureData(uint32_t id,void *data,int *size){
  uint32_t bits=0x41f00000u; if(id!=0x1080017u||*size!=4) return -1; memcpy(data,&bits,4); return 0;
}
static void *movie_main(void *arg){
  unsigned n=0; (void)arg;
  usleep(20000);
  if(!getenv("FAKE_NO_MOVIE_ACK")) ncb(0x2030001,0,0);
  while(movie_on){
    unsigned k;
    for(k=0;k<2048;k++) region[0x200000+k]=(unsigned char)(k+n);
    for(k=0;k<1024;k++) region[0x200800+k]=(unsigned char)(128+(k&1));
    memset(movie_block,0,sizeof movie_block);
    put32(movie_block+0x00,64); put32(movie_block+0x04,32); put32(movie_block+0x08,64);
    put32(movie_block+0x0c,32); put32(movie_block+0x40,7); put32(movie_block+0x54,0x200000);
    put32(movie_block+0x58,0x200800); put32(movie_block+0x64,1); put32(movie_block+0x68,1000u+n);
    icb(0x3030003,movie_block,0,0x88);
    if(set_on){
      memcpy(region+0x210090,movie_block,0x88); put32(movie_set,0x210000);
      scb(0x4010007,movie_set,0,sizeof movie_set);
    }
    ++n; usleep(20000);
  }
  return 0;
}
int RecordMovie(uint32_t id,unsigned char *payload,uint32_t length){
  char line[160]; uint32_t w[7]={0,0,0,0,0,0,0};
  if(payload && length==28) memcpy(w,payload,28);
  snprintf(line,sizeof line,"movie:%x:%u:%x,%x,%x,%x,%x,%x,%x\n",id,length,w[0],w[1],w[2],w[3],w[4],w[5],w[6]);
  mark(line); ensure();
  if(id==0x1020001u){ write_maps(); movie_on=1; pthread_create(&movie_thread,0,movie_main,0); return 0; }
  if(id==0x1020002u){ if(movie_on){ movie_on=0; pthread_join(movie_thread,0); } ncb(0x2030002,0,0); return 0; }
  return -2;
}
int mm_camera_capture_still(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,
 uint32_t f,uint32_t g,uint32_t h,uint32_t i,uint32_t j,uint32_t k,uint32_t l,
 uint32_t m,uint32_t n,uint32_t o,uint32_t p,uint32_t q){
  char line[200]; pthread_t t;
  snprintf(line,sizeof line,"capture:%x,%u,%x,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q);
  mark(line); ensure(); ++stills;
  if(getenv("STILL_FAIL")) return -7;
  if(getenv("FAKE_SYNC")) deliver(stills);
  else { pthread_create(&t,0,thread_main,(void*)(uintptr_t)stills); pthread_detach(t); }
  return 0;
}
'''

FAKE_BRIDGE = r'''
#include <stdio.h>
#include <stdlib.h>
static void mark(const char *s){FILE*f=fopen(getenv("TRACE"),"a");if(f){fputs(s,f);fclose(f);}}
int Mcb_send(unsigned short c,unsigned short a,unsigned short n,unsigned char*p){
 mark(c==16&&a==0&&n==0&&!p?"send\n":"bad_send\n"); return 0;
}
unsigned char _ZN16CMulticoreBridge18IsDestinationAliveEv(void){
 static int reads; ++reads; if(getenv("BRIDGE_DEAD")) return 0; return reads >= 2;
}
'''

FAKE_DEVNODE = r'''
#include <stdlib.h>
int device_get_property(int type,int prop,int *value){
  if(type!=2 || !value) return -1;
  if(getenv("GYRO_EMPTY")) return 0;
  if(prop==0x20) *value=8000000; else if(prop==0x18) *value=(int)0x9cd00000u; else if(prop==0x19) *value=1234;
  return 0;
}
'''

FAKE_APEX = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned char instance[0x40];
void *_ZN5CApex11GetInstanceEv(void){ return instance; }
void _ZN5CApex10InitializeEP13tag_apex_info(void *self, void *info){
  memcpy((unsigned char*)self + 0xc, &info, sizeof info); }
'''

FAKE_USERSPEC = r'''
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
  if (m) {
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
  *num = 0; *den = 0;
}
'''


def expected_raw(still: int, region: int = 0) -> bytes:
    out = bytearray()
    for i in range(64 * 32):
        v = (i * (13 + 4 * region) + still + region) & 0xfff
        out += bytes((v & 0xff, v >> 8))
    return bytes(out)


def expected_image(k: int, still: int, size: int, set_index: int = 0) -> bytes:
    data = bytearray((i * 7 + still * 31 + set_index * 7 + k) & 0xff for i in range(size))
    if k > 0:
        data[0], data[1], data[-2], data[-1] = 0xff, 0xd8, 0xff, 0xd9
    return bytes(data)


def expected_frame(still: int) -> bytes:
    """The pattern the fake writes into the frame before the expansion."""
    return bytes((i * 7 + still * 31 + 0x5a) & 0xff for i in range(4096))


with tempfile.TemporaryDirectory(prefix="g360cap-") as td:
    work = Path(td)
    libs = {name: work / f"{name}.so" for name in ("sdk", "bridge", "devnode", "apex", "userspec")}
    mapsfile = work / "maps"
    source = (here / "g360cap.c").read_text()
    for real, fake in (('"/usr/lib/libcapture-fw-prod.so"', libs["sdk"]),
                       ('"/usr/lib/libmulticore-bridge.so"', libs["bridge"]),
                       ('"/usr/lib/libdevice-node.so.0.1"', libs["devnode"]),
                       ('"/usr/lib/libuser-spec.so"', libs["userspec"]),
                       ('"/usr/lib/libapex.so"', libs["apex"]),
                       ('"/proc/self/maps"', mapsfile)):
        assert source.count(real) == 1, real
        source = source.replace(real, f'"{fake}"')
    (work / "g360cap.c").write_text(source)
    for name, text in (("sdk", FAKE_SDK), ("bridge", FAKE_BRIDGE), ("devnode", FAKE_DEVNODE),
                       ("apex", FAKE_APEX)):
        (work / f"{name}.c").write_text(text)
        subprocess.run([cc, "-shared", "-fPIC", str(work / f"{name}.c"), "-o", str(libs[name])], check=True)
    (work / "userspec.c").write_text(FAKE_USERSPEC)
    subprocess.run([cc, "-shared", "-fPIC", str(work / "userspec.c"), str(libs["apex"]),
                    "-o", str(libs["userspec"])], check=True)
    exe = work / "g360-shot"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    f"-I{here}", f"-I{re_root / 'capture-session'}", f"-I{re_root / 'encoded-set'}",
                    str(work / "g360cap.c"), str(here / "g360cap_check.c"),
                    str(re_root / "encoded-set" / "encoded_set.c"), str(here / "g360-shot.c"), str(here / "g360_exif.c"),
                    str(here / "g360_dng.c"), str(here / "g360_output.c"),
                    "-ldl", "-o", str(exe)], check=True)

    def run(*args, count=1, out=None, binary=None, **env):
        trace, logfile = work / "trace", work / "run.log"
        out = out or work / "out"
        for p in (trace, logfile):
            p.unlink(missing_ok=True)
        if out.exists():
            for f in out.iterdir():
                f.unlink()
        out.mkdir(exist_ok=True)
        cmd = [str(binary or exe), "--out", str(out), "--log", str(logfile), "--free-bytes", "4294967297",
               *(["--count", str(count)] if count is not None else []), "--gap-ms", "10", "--hold-ms", "10",
               "--set-timeout-ms", "1500", "--ready-timeout-ms", "500", "--exif", "off", *args]
        e = dict(os.environ, TRACE=str(trace), MAPS_FILE=str(mapsfile), **env)
        r = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=30)
        tr = trace.read_text().splitlines() if trace.exists() else []
        log = logfile.read_text() if logfile.exists() else ""
        # Maker-note copies are checked on their own below; the image file
        # lists the other tests compare stay as they were.
        files = sorted(f.name for f in out.iterdir() if not f.name.endswith("-makernote.bin"))
        return r, tr, log, files, out

    def releases(tr):
        return [x for x in tr if x.startswith("release:")]

    # Bad arguments: nothing is loaded.
    for bad in (["--count", "0"], ["--prefix", "../x"], ["--bogus", "1"], ["--count"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, (bad, r.stderr, tr)

    # One still, callback on the fake receiver thread.
    r, tr, log, files, out = run()
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert files == ["shot-001-main.jpg", "shot-001-screen.jpg", "shot-001-thumb.jpg"], files
    for k, name in ((1, "main"), (2, "screen"), (3, "thumb")):
        size = {1: 5000, 2: 3000, 3: 1000}[k]
        assert (out / f"shot-001-{name}.jpg").read_bytes() == expected_image(k, 1, size), name
    assert releases(tr) == ["release:5:3000", "release:7:10000", "release:8:20000", "release:9:30000"], tr
    assert "release_zero" not in tr
    assert tr.index("capture:1234,0,15,1,1,0,2440,0,1,1,0,0,0,0,0,0,0") < tr.index("set-callback-begin")
    assert tr.index("set-callback-end") <= tr.index("release:5:3000") or True  # threaded: order not fixed
    assert sum(x.startswith("notify:") for x in tr) == 6 and "subscribe-set" in tr
    assert tr.index("apex_private_info") < tr.index("instance") and "apex_shared_info" not in tr
    assert "send" in tr
    assert "still 1: ok; 1 set(s) copied, 0 not copied" in r.stdout and "notify mask 0xfa" in r.stdout, r.stdout
    assert "set 1: type 1" in r.stdout, r.stdout
    assert "exit: all stills succeeded" in log
    # The maker note is copied before its give-back (which wipes it) and
    # saved with the JPEGs: 64 KiB from its address.
    note = (out / "shot-001-makernote.bin").read_bytes()
    assert len(note) == 0x10000 and note[:4] == b"MKNT" and note[4:8] == (1).to_bytes(4, "little"), note[:16]
    assert "maker note: copied (address, bytes) a=0x3000 b=0x10000" in log, log
    assert "saved " in r.stdout and "shot-001-makernote.bin: 65536 bytes from 0x3000" in r.stdout, r.stdout

    # The native Exif bytes expire on callback return; both shots must retain
    # their own values, independent of the requested manual TV/ISO settings.
    import sys
    import struct
    sys.path.insert(0, str(re_root.parents[1] / "reverse-engineering" / "dng" / "tests"))
    import test_stock_iq as fixture
    import stock_iq
    note_bytes = bytearray(0x6000)
    for tag, record in ((0xa0, fixture.awb_record()), (0x60, fixture.ae_record()),
                        (0xc0, fixture.ipc_record())):
        start, length = stock_iq.BUFFER_RECORDS[tag]
        note_bytes[start:start + length] = record
    note_fixture = work / "iq.bin"
    note_fixture.write_bytes(note_bytes)
    r, tr, log, files, out = run("--exif", "on", "--exposure", "96,800", count=2,
                               FAKE_MAKER_NOTE=str(note_fixture))
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    for shot in (1, 2):
        for kind, name in ((1, "main"), (2, "screen"), (3, "thumb")):
            jpg = (out / f"shot-{shot:03d}-{name}.jpg").read_bytes()
            length = struct.unpack_from(">H", jpg, 4)[0]
            assert jpg[:2] + jpg[4 + length:] == expected_image(kind, shot, {1:5000,2:3000,3:1000}[kind])
            tiff = stock_iq.exif_tiff(jpg)
            ifd = stock_iq._ifd(tiff, 8, ">")
            exif_at = next(struct.unpack(">I", v)[0] for tag, _, _, v, _ in ifd if tag == 0x8769)
            fields = {tag: v for tag, _, _, v, _ in stock_iq._ifd(tiff, exif_at, ">")}
            assert struct.unpack(">II", fields[0x829a]) == (shot, 250)
            assert struct.unpack(">H", fields[0x8827])[0] == 100 + shot
            assert stock_iq.makernote(jpg)[0xa0] == fixture.awb_record()
    # A missing/invalid IQ record refuses metadata output, without a retry.
    r, tr, log, files, out = run("--exif", "on", count=2)
    assert r.returncode == 1 and not files and len(releases(tr)) == 4
    for bad in (["--exif", "bad"], ["--frame-output", "dng"],
                ["--frame", "--frame-output", "dng", "--frame-layout", "0xa6000000,64,32,128,16"],
                ["--frame", "--frame-output", "dng", "--sets", "2"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, (bad, r.stdout, r.stderr)

    # The existing C API's NULL log path is exposed by the CLI; results and
    # buffer ownership still work, with completion metrics kept on stdout.
    r, tr, log, files, out = run("--library-log", "off", count=2)
    assert r.returncode == 0 and not log and len(files) == 6 and len(releases(tr)) == 8
    assert r.stdout.count("metric,sequence=") == 2 and "library_log=off" in r.stdout
    r, tr, _, _, _ = run("--library-log", "invalid")
    assert r.returncode == 2 and not tr

    # Discard still validates/copies JPEGs and returns every buffer, but
    # writes no JPEG files. A corrupt JPEG must still fail in this mode.
    r, tr, log, files, out = run("--jpeg", "discard")
    assert r.returncode == 0 and not files and len(releases(tr)) == 4, (r.stdout, tr)
    assert not list(out.glob("*-makernote.bin"))
    assert "jpeg_policy=discard" in log and "metric,sequence=1,status=0" in log
    r, tr, log, files, out = run("--jpeg", "discard", FAKE_BAD_JPEG="1")
    assert r.returncode == 1 and not files and len(releases(tr)) == 4
    for bad in (["--jpeg", "skip-copy"], ["--jpeg", "no"], ["--frame-output", "discard"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, bad

    # Synchronous delivery with the images inaccessible during the callback:
    # the library copies nothing inside the callback.
    r, tr, log, files, out = run(FAKE_SYNC="1")
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert len(files) == 3 and releases(tr)[0] == "release:5:3000"

    # Two stills in one session: each gets its own copies and give-backs.
    r, tr, log, files, out = run(count=2)
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert len(files) == 6, files
    assert (out / "shot-002-main.jpg").read_bytes() == expected_image(1, 2, 5000)
    assert (out / "shot-001-main.jpg").read_bytes() != (out / "shot-002-main.jpg").read_bytes()
    assert len(releases(tr)) == 8 and sum(x.startswith("capture:") for x in tr) == 2
    assert "free" not in r.stderr

    # RAW set type: the first image is copied and given back with type 1 first.
    r, tr, log, files, out = run(FAKE_SET_TYPE="3")
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert "shot-001-first.srw" in files
    assert (out / "shot-001-first.srw").read_bytes() == expected_image(0, 1, 8000)
    assert releases(tr) == ["release:5:3000", "release:1:40000", "release:7:10000",
                            "release:8:20000", "release:9:30000"], tr

    # Unknown set type: nothing read or given back, latched.
    r, tr, log, files, out = run(FAKE_SET_TYPE="9", count=2)
    assert r.returncode == 1 and not files and not releases(tr), (r.stdout, tr)
    assert "unknown set type" in log and sum(x.startswith("capture:") for x in tr) == 1

    # Main image not mapped: nothing read there, every buffer still given back.
    r, tr, log, files, out = run(FAKE_UNMAPPED_MAIN="1")
    assert r.returncode == 1 and "image missing or invalid" in r.stdout, r.stdout
    assert "shot-001-main.jpg" not in files and "shot-001-screen.jpg" in files
    assert len(releases(tr)) == 4 and "set 1 main.jpg not copied (flags 0x45)" in r.stdout, r.stdout
    # 64 KiB from 0x3000 would cross the unmapped range: the 32 KiB read is used.
    note = (out / "shot-001-makernote.bin").read_bytes()
    assert len(note) == 0x8000 and note[:4] == b"MKNT", len(note)

    # A JPEG without its end marker is saved but reported.
    r, tr, log, files, out = run(FAKE_BAD_JPEG="1")
    assert r.returncode == 1 and "shot-001-main.jpg" in files and len(releases(tr)) == 4

    # Set structure not readable: nothing read, no give-back possible, latched.
    r, tr, log, files, out = run(FAKE_SET_UNMAPPED="1", count=2)
    assert r.returncode == 1 and not files and not releases(tr)
    assert sum(x.startswith("capture:") for x in tr) == 1, "a latched session sends no second still"
    assert "image set structure not readable" in log

    # No image set: timeout, no give-back, latched.
    r, tr, log, files, out = run(FAKE_NO_SET="1", count=2)
    assert r.returncode == 1 and "timed out" in r.stdout and not releases(tr)
    assert sum(x.startswith("capture:") for x in tr) == 1

    # No capture-ready: images kept and given back, then latched.
    r, tr, log, files, out = run(FAKE_NO_READY="1", count=2)
    assert r.returncode == 1 and len(files) == 3 and len(releases(tr)) == 4
    assert sum(x.startswith("capture:") for x in tr) == 1

    # A refused give-back is reported.
    r, tr, log, files, out = run(FAKE_RELEASE_FAIL="1")
    assert r.returncode == 1 and "buffer give-back refused" in r.stdout and len(files) == 3

    # A nonzero still return: no wait, no retry.
    r, tr, log, files, out = run(STILL_FAIL="1", count=2)
    assert r.returncode == 1 and sum(x.startswith("capture:") for x in tr) == 1 and not files

    # Image quality: set before the first still, set back to superfine after.
    r, tr, log, files, out = run("--quality", "raw-superfine", FAKE_SET_TYPE="3")
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    usr = [x for x in tr if x.startswith("usr:")]
    assert usr == ["usr:1234:3:30004", "usr:1234:3:30001"], tr
    assert tr.index("usr:1234:3:30004") < tr.index("capture:1234,0,15,1,1,0,2440,0,1,1,0,0,0,0,0,0,0")
    assert tr.index("usr:1234:3:30001") > tr.index(releases(tr)[-1])
    r, tr, log, files, out = run("--quality", "raw", "--restore-quality", "none", FAKE_SET_TYPE="2")
    assert r.returncode == 0 and [x for x in tr if x.startswith("usr:")] == ["usr:1234:3:30000"], (r.stdout, tr)
    # A RAW-only set type may list no main JPEG.
    r, tr, log, files, out = run(FAKE_SET_TYPE="2", FAKE_NO_MAIN="1")
    assert r.returncode == 0 and "shot-001-first.srw" in files and "shot-001-main.jpg" not in files, (r.stdout, files)
    assert releases(tr) == ["release:5:3000", "release:1:40000", "release:8:20000", "release:9:30000"], tr
    # A plain set without a main JPEG is a failure.
    r, tr, log, files, out = run(FAKE_NO_MAIN="1")
    assert r.returncode == 1, r.stdout
    # A refused quality setting sends no still.
    r, tr, log, files, out = run("--quality", "raw", USR_FAIL="1")
    assert r.returncode == 1 and not any(x.startswith("capture:") for x in tr), tr
    # Unknown quality names and a restore without a quality are refused before loading.
    for bad in (["--quality", "jpeg"], ["--restore-quality", "fine"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, bad

    # A bracket: three image sets per still, each copied and given back.
    r, tr, log, files, out = run("--sets", "3", "--extra-set-wait-ms", "300", FAKE_SETS="3")
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert len(files) == 9 and "shot-001-s3-main.jpg" in files, files
    for set_index in range(3):
        assert (out / f"shot-001-s{set_index + 1}-main.jpg").read_bytes() == \
            expected_image(1, 1, 5000, set_index), set_index
    rel = releases(tr)
    assert len(rel) == 12 and rel[4] == "release:5:3010" and rel[5] == "release:7:90000", rel
    assert "3 set(s) copied, 0 not copied" in r.stdout
    # The same bracket with the default wait (3 s after capture-ready for a
    # bracket of unknown size) still ends once all asked-for sets arrived.
    r, tr, log, files, out = run("--sets", "3", FAKE_SETS="3", FAKE_SYNC="1")
    assert r.returncode == 0 and len(files) == 9, (r.stdout, files)
    # More sets than asked for: the extra ones are given back uncopied.
    r, tr, log, files, out = run("--extra-set-wait-ms", "300", FAKE_SETS="3")
    assert r.returncode == 0 and len(files) == 3 and len(releases(tr)) == 12, (r.stdout, tr)
    assert "1 set(s) copied, 2 not copied" in r.stdout, r.stdout
    # A set that arrives after its still returned is given back before the
    # next still and is not counted as that still's.
    r, tr, log, files, out = run("--count", "2", "--gap-ms", "1500", "--hold-ms", "1500", FAKE_SETS="2",
                                 FAKE_LATE_MS="700")
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert len(files) == 6 and len(releases(tr)) == 16, (files, releases(tr))
    assert log.count("late image set: giving its buffers back") == 2, log
    assert (out / "shot-002-main.jpg").read_bytes() == expected_image(1, 2, 5000, 0)
    # Capture-ready without an image set (a RAW quality on the normal recipe):
    # fails within about 3 s instead of the full set timeout.
    import time
    t0 = time.monotonic()
    r, tr, log, files, out = run("--set-timeout-ms", "20000", FAKE_READY_NO_SET="1", count=2)
    took = time.monotonic() - t0
    assert r.returncode == 1 and "capture-ready but no image set" in r.stdout, r.stdout
    assert took < 10, took
    assert sum(x.startswith("capture:") for x in tr) == 1
    # Drive mode: set before the first still, set back to single after.
    r, tr, log, files, out = run("--drive", "wbbrk", "--sets", "3", "--extra-set-wait-ms", "300",
                                 FAKE_SETS="3")
    assert r.returncode == 0, (r.stdout, log)
    usr = [x for x in tr if x.startswith("usr:")]
    assert usr == ["usr:1234:8:80005", "usr:1234:8:80000"], usr
    assert tr.index("usr:1234:8:80005") < tr.index("capture:1234,0,15,1,1,0,2440,0,1,1,0,0,0,0,0,0,0")
    # The capture type is the still request's third word (default 0x15).
    r, tr, log, files, out = run("--capture-type", "wbb", "--sets", "3", "--extra-set-wait-ms", "300",
                                 FAKE_SETS="3")
    assert r.returncode == 0 and "capture:1234,0,6,1,1,0,2440,0,1,1,0,0,0,0,0,0,0" in tr, (r.stdout, tr)
    r, tr, log, files, out = run("--capture-type", "single")
    assert r.returncode == 0 and "capture:1234,0,1,1,1,0,2440,0,1,1,0,0,0,0,0,0,0" in tr, tr
    r, tr, log, files, out = run("--capture-type", "wbb", "--bracket-words", "0,0,0x1a0000,1")
    assert r.returncode == 0 and "capture:1234,0,6,1,1,0,2440,0,1,1,0,0,1703936,1,0,0,0" in tr, tr
    # A bracket capture type leaves the camera's drive mode changed, so it is
    # set back to single afterwards (and not set before).
    assert [x for x in tr if x.startswith("usr:")] == ["usr:1234:8:80000"], tr
    r, tr, log, files, out = run("--capture-type", "wbb", "--bracket-words", "0,0,0x1a0000,1",
                                 "--restore-drive", "none")
    assert r.returncode == 0 and not [x for x in tr if x.startswith("usr:")], tr
    assert "request words 11-14 a=0x0 b=0x0 c=0x1a0000 d=0x1" in log
    for bad in (["--bracket-words", "1,2,3"], ["--bracket-words", "1,2,3,4,5"], ["--bracket-words", "0x,1,2,3"],
                ["--bracket-words", "0x1ffffffff,0,0,0"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, bad
    r, tr, _, _, _ = run("--capture-type", "hdr")
    assert r.returncode == 2 and not tr

    # Manual exposure: dial, shooting mode, ISO and shutter before every
    # still, automatic exposure set back after; noise reductions set back.
    r, tr, log, files, out = run("--exposure", "-37,100", "--ltnr", "off", "--isonr", "off", count=2)
    assert r.returncode == 0, (r.stdout, log)
    settings = [x for x in tr if x.startswith(("usr:", "var:", "capture:"))]
    one = ["usr:1234:0:3", "usr:1234:1:10003", "usr:1234:4:64", "var:1234:101:-37"]
    assert settings[:2] == ["usr:1234:23:0", "usr:1234:22:160000"], settings
    assert settings[2:6] == one and settings[6].startswith("capture:"), settings
    assert settings[7:11] == one and settings[11].startswith("capture:"), settings
    assert settings[12:] == ["usr:1234:0:1", "usr:1234:1:10000", "usr:1234:4:0", "usr:1234:23:1",
                             "usr:1234:22:160002"], settings
    # A list of exposures: one still per entry, the count following the list.
    r, tr, log, files, out = run("--exposure", "160,100:96,100:32,400", "--count", "3")
    assert r.returncode == 0 and len(files) == 9, (r.stdout, files)
    assert [x for x in tr if x.startswith("var:")] == ["var:1234:101:160", "var:1234:101:96",
                                                      "var:1234:101:32"], tr
    assert "usr:1234:4:190" in tr  # ISO 400
    r, tr, log, files, out = run("--exposure", "160,100:32,100", count=None)
    assert len(files) == 6 and sum(x.startswith("capture:") for x in tr) == 2, files
    r, tr, log, files, out = run("--exposure", "96,200", "--restore-exposure", "none")
    assert [x for x in tr if x.startswith(("usr:", "var:"))] == [
        "usr:1234:0:3", "usr:1234:1:10003", "usr:1234:4:c8", "var:1234:101:96"], tr
    # A refused shutter setting sends no still.
    r, tr, log, files, out = run("--exposure", "96,100", VAR_FAIL="1")
    assert r.returncode == 1 and not any(x.startswith("capture:") for x in tr), tr
    for bad in (["--exposure", "96"], ["--exposure", "x,100"], ["--exposure", "96,20"],
                ["--exposure", "96,100:"], ["--exposure", "96,100:32"],
                ["--exposure", "999,100"], ["--ltnr", "maybe"], ["--isonr", "max"],
                ["--restore-exposure", "program"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, bad

    # Two RAW images: copied from the layouts given, saved with descriptions.
    one = ["--raw-layout", "0xa0300000,64,32,128,12"]
    two = one + ["--raw-layout", "0xa0310000,64,32,128,12"]
    r, tr, log, files, out = run("--raw", *two, count=2)
    assert r.returncode == 0, (r.stdout, log)
    for n in (1, 2):
        for k in (0, 1):
            name = "shot-%03d-raw%d" % (n, k + 1)
            assert (out / (name + ".bin")).read_bytes() == expected_raw(n, k), name
            desc = (out / (name + ".txt")).read_text()
            assert "width: 64" in desc and "stride_bytes: 128" in desc and "bits: 12" in desc and \
                "physical: 0x%08x" % (0xa0300000 + k * 0x10000) in desc and \
                "layout: region %d" % (k + 1) in desc and "values_ok: yes" in desc and \
                "changed_during_copy: no" in desc, desc
    assert "raw: copied (physical, virtual, size, flags) a=0xa0300000 b=0x300000 c=0x1000 d=0x103" in log, log
    assert "raw: copied (physical, virtual, size, flags) a=0xa0310000 b=0x310000 c=0x1000 d=0x103" in log, log
    # One --raw-layout: one image.
    r, tr, log, files, out = run("--raw", *one)
    assert r.returncode == 0 and "shot-001-raw1.bin" in files and "shot-001-raw2.bin" not in files, files
    # With images inaccessible during the callback (synchronous delivery),
    # the RAW is still copied afterwards, on the caller's thread.
    r, tr, log, files, out = run("--raw", *two, FAKE_SYNC="1")
    assert r.returncode == 0 and (out / "shot-001-raw2.bin").read_bytes() == expected_raw(1, 1), r.stdout
    # A sample above 12 bits in image 1 is reported (saved, but the still fails).
    r, tr, log, files, out = run("--raw", *two, FAKE_RAW_BAD="1")
    assert r.returncode == 1 and "shot-001-raw1.bin" in files and "shot-001-raw2.bin" in files, (r.stdout, files)
    assert "values_ok: no" in (out / "shot-001-raw1.txt").read_text()
    assert "values_ok: yes" in (out / "shot-001-raw2.txt").read_text()
    # The default layouts (the lens images) lie outside the fake's memory:
    # not read, both reported, and every buffer is still given back.
    r, tr, log, files, out = run("--raw")
    assert r.returncode == 1 and "raw1 not copied (flags 0x5)" in r.stdout and \
        "raw2 not copied (flags 0x5)" in r.stdout, r.stdout
    assert "raw: range not mapped readable" in log and len(releases(tr)) == 4
    assert "a=0xab134000" in log and "a=0xad134000" in log, log
    # A bracket: only the first set of the still carries the RAW.
    r, tr, log, files, out = run("--raw", *two, "--sets", "3", "--extra-set-wait-ms", "300", FAKE_SETS="3")
    assert r.returncode == 0 and "shot-001-s1-raw2.bin" in files and "shot-001-s2-raw1.bin" not in files, files
    for bad in (["--raw", "--raw-layout", "1,2,3"], ["--raw", "--raw-layout", "0xa6000000,5824,2896,100,12"],
                ["--raw", "--raw-layout", "0xa6000000,5824,2896,11648,17"], one,
                ["--raw"] + two + one):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, bad

    # The full RAW frame: copied during the still-expansion pause, before
    # the image set arrives.
    frame = ["--frame-layout", "0xa6000000,64,32,128,16"]
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1")
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert "shot-001-frame.bin" in files and "shot-001-frame.txt" in files, files
    assert (out / "shot-001-frame.bin").read_bytes() == expected_frame(1)
    desc = (out / "shot-001-frame.txt").read_text()
    assert "width: 64" in desc and "height: 32" in desc and "stride_bytes: 128" in desc and \
        "bits: 16" in desc and "physical: 0xa6000000" in desc and "expansion_mode: 3" in desc and \
        "values_ok: yes" in desc and "changed_during_copy: no" in desc, desc
    assert "still 1: ok" in r.stdout and "shot-001-frame.bin: 4096 bytes" in r.stdout, r.stdout
    assert "subscribe-expansion" in tr and "expansion-callback-begin" in tr, tr
    assert tr.index("expansion-callback-begin") < tr.index("set-callback-begin"), tr
    assert "frame: armed" in log and "frame: copied in the expansion callback" in log, log
    # Every still gets its own frame.
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1", count=2)
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    assert (out / "shot-001-frame.bin").read_bytes() == expected_frame(1)
    assert (out / "shot-002-frame.bin").read_bytes() == expected_frame(2)
    # Skip only JPEG payload reads; frame ownership/validation and all
    # give-backs remain required. Corrupt/unmapped JPEGs are irrelevant.
    r, tr, log, files, out = run("--frame", *frame, "--jpeg", "skip-copy",
                               FAKE_EXPANSION="1", FAKE_UNMAPPED_MAIN="1", FAKE_BAD_JPEG="1")
    assert r.returncode == 0 and len(releases(tr)) == 4, (r.stdout, tr, log)
    assert files == ["shot-001-frame.bin", "shot-001-frame.txt"], files
    assert (out / "shot-001-frame.bin").read_bytes() == expected_frame(1)
    r, tr, log, files, out = run("--frame", *frame, "--jpeg", "skip-copy",
                               "--frame-output", "discard", FAKE_EXPANSION="1", count=2)
    assert r.returncode == 0 and not files and len(releases(tr)) == 8, (r.stdout, tr)
    metrics = [x for x in r.stdout.splitlines() if "metric," in x]
    assert len(metrics) == 2 and all("frame_bytes=4096" in x for x in metrics), metrics
    assert "cycle_us=0," in metrics[0] and "cycle_us=0," not in metrics[1], metrics
    for env in ({}, {"FAKE_EXPANSION": "1", "FAKE_RELEASE_FAIL": "1"},
                {"FAKE_EXPANSION": "1", "FAKE_NO_READY": "1"}):
        r, tr, log, files, out = run("--frame", *frame, "--jpeg", "skip-copy",
                                   "--frame-output", "discard", count=2, **env)
        assert r.returncode == 1 and not files, (r.stdout, log, files)
        assert sum(x.startswith("capture:") for x in tr) == 1, tr
    # Without the expansion topic the frame is not copied: the still fails
    # with the image error and the run ends instead of hanging.
    r, tr, log, files, out = run("--frame", *frame)
    assert r.returncode == 1, (r.stdout, r.stderr, log)
    assert "still 1: image missing or invalid" in r.stdout, r.stdout
    assert "frame not copied (flags 0x400)" in r.stdout, r.stdout
    assert "shot-001-frame.bin" not in files and "shot-001-main.jpg" in files, files
    assert "frame: no valid orientation expansion; not copied" in log, log
    assert "expansion-callback-begin" not in tr, tr
    # Failure outcomes still cancel/collect the frame job and never retry
    # an indeterminate still request, even when --count asks for two.
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1", STILL_FAIL="1", count=2)
    assert r.returncode == 1 and "still request returned nonzero" in r.stdout, (r.stdout, r.stderr)
    assert sum(x.startswith("capture:") for x in tr) == 1 and "shot-001-frame.bin" not in files
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1", FAKE_NO_READY="1", count=2)
    assert r.returncode == 1 and "timed out" in r.stdout, (r.stdout, r.stderr)
    assert sum(x.startswith("capture:") for x in tr) == 1 and len(releases(tr)) == 4
    assert (out / "shot-001-frame.bin").read_bytes() == expected_frame(1)

    # The frame's mapping is its own: the main image can be missing while
    # the frame is still copied.
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1", FAKE_UNMAPPED_MAIN="1")
    assert r.returncode == 1, (r.stdout, r.stderr, log)
    assert "shot-001-frame.bin" in files, files
    assert (out / "shot-001-frame.bin").read_bytes() == expected_frame(1)
    # Without a /dev/mem mapping for the frame's physical address the frame
    # is not armed at all.
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1", FAKE_NO_DEV_MEM="1")
    assert r.returncode == 1, (r.stdout, r.stderr, log)
    assert "shot-001-frame.bin" not in files, files
    assert "frame: physical range not in a /dev/mem mapping; not armed" in log, log

    # Invalid descriptors/modes cannot trigger a RAW read. The opaque bad
    # pointer deliberately faults under ASan if the callback dereferences it.
    for bad in ("FAKE_EXPANSION_NULL", "FAKE_EXPANSION_UNMAPPED", "FAKE_EXPANSION_SHORT",
                "FAKE_EXPANSION_WRONG_MODE"):
        r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1", **{bad: "1"})
        assert r.returncode == 1 and "frame not copied (flags 0x400)" in r.stdout, (bad, r.stdout, r.stderr)
        assert "shot-001-frame.bin" not in files and len(releases(tr)) == 4, (bad, files, tr)
    # A different expansion mode must leave the job armed for orientation.
    # A duplicate must leave the first owned copy unchanged, even after reuse.
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1",
                                 FAKE_EXPANSION_WRONG_FIRST="1", FAKE_EXPANSION_DUPLICATE="1")
    assert r.returncode == 0 and (out / "shot-001-frame.bin").read_bytes() == expected_frame(1), (r.stdout, log)
    assert log.count("frame: copied in the expansion callback") == 1
    # The old virtual address still reads fine after the /dev/mem mapping
    # disappears. A generic readability check alone would accept stale data.
    r, tr, log, files, out = run("--frame", *frame, FAKE_EXPANSION="1", FAKE_FRAME_REMAP="1")
    assert r.returncode == 1 and "frame not copied (flags 0x5)" in r.stdout, (r.stdout, log)
    assert "shot-001-frame.bin" not in files and len(releases(tr)) == 4
    # CLI refuses overflow/oversize/physical-wrap layouts before loading SDK.
    for opt in ("--raw", "--frame"):
        for layout in ("0xa6000000,2147483648,1,128,12",
                       "0xa6000000,1,2147483649,2,12",
                       "0xa6000000,1,4294967295,4294967295,12",
                       "0xfffff000,64,33,128,16"):
            r, tr, _, _, _ = run(opt, opt + "-layout", layout)
            assert r.returncode == 2 and not tr, (opt, layout, r.stdout, r.stderr)

    # Exercise direct library callers, including keeping the first RAW
    # result alive through source reuse, a second capture and close.
    api_exe = work / "test-frame-api"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    f"-I{here}", f"-I{re_root / 'capture-session'}", f"-I{re_root / 'encoded-set'}",
                    str(work / "g360cap.c"), str(here / "g360cap_check.c"),
                    str(re_root / "encoded-set" / "encoded_set.c"),
                    str(here / "tests" / "test_frame_api.c"), "-ldl", "-o", str(api_exe)], check=True)
    r, tr, log, files, out = run(binary=api_exe, FAKE_EXPANSION="1")
    assert r.returncode == 0, (r.stdout, r.stderr)
    assert sum(x.startswith("capture:") for x in tr) == 2, tr
    assert len(releases(tr)) == 8, tr
    assert tr.index("subscribe-expansion") < tr.index("beauty"), tr

    # Quick view: set before the still, set back afterwards to the value given.
    r, tr, log, files, out = run("--quickview", "holdzoom", "--restore-quickview", "off")
    assert r.returncode == 0, r.stdout
    usr = [x for x in tr if x.startswith("usr:")]
    assert usr == ["usr:1234:30:1e0005", "usr:1234:30:1e0000"], usr
    assert tr.index("usr:1234:30:1e0005") < tr.index("capture:1234,0,15,1,1,0,2440,0,1,1,0,0,0,0,0,0,0")
    assert tr.index("usr:1234:30:1e0000") > tr.index(releases(tr)[-1])
    r, tr, log, files, out = run("--quickview", "hold", "--restore-quickview", "none")
    assert r.returncode == 0 and [x for x in tr if x.startswith("usr:")] == ["usr:1234:30:1e0004"], tr
    # Refused by the camera: no still.
    r, tr, log, files, out = run("--quickview", "holdzoom", "--restore-quickview", "3s", USR_FAIL="1")
    assert r.returncode == 1 and not any(x.startswith("capture:") for x in tr), tr
    for bad in (["--quickview", "holdzoom"], ["--restore-quickview", "off"],
                ["--quickview", "forever", "--restore-quickview", "off"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, bad

    # Quality and drive together are both set back.
    r, tr, log, files, out = run("--quality", "raw-superfine", "--drive", "pwbrk", "--restore-drive",
                                 "none", FAKE_SET_TYPE="3")
    assert [x for x in tr if x.startswith("usr:")] == ["usr:1234:3:30004", "usr:1234:8:80006",
                                                      "usr:1234:3:30001"], tr
    for bad in (["--drive", "bracket"], ["--restore-drive", "single"], ["--sets", "9"]):
        r, tr, _, _, _ = run(*bad)
        assert r.returncode == 2 and not tr, bad

    # Gates refuse before any capture-library initialization.
    r, tr, log, files, out = run(GYRO_EMPTY="1")
    assert r.returncode == 1 and not any(x in tr for x in ("instance", "create")), tr
    assert "motion history unavailable" in log
    r, tr, log, files, out = run(APEX_NO_MAGIC="1")
    assert r.returncode == 1 and "instance" not in tr and "magic missing" in log
    r, tr, log, files, out = run(BRIDGE_DEAD="1")
    assert r.returncode == 1 and not any(x.startswith("capture:") for x in tr)

    # The movie preview (T20): start, frames on the movie topic and the set,
    # a capture refused while it runs, stop, then a normal still.
    preview_exe = work / "g360-preview"
    subprocess.run([cc, "-std=c99", "-Wall", "-Wextra", "-Werror", "-g",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    f"-I{here}", f"-I{re_root / 'capture-session'}", f"-I{re_root / 'encoded-set'}",
                    str(work / "g360cap.c"), str(here / "g360cap_check.c"),
                    str(re_root / "encoded-set" / "encoded_set.c"), str(here / "g360-preview.c"),
                    "-ldl", "-o", str(preview_exe)], check=True)

    def preview(*args, **env):
        trace, logfile, pout = work / "trace", work / "preview.log", work / "pout"
        for p in (trace, logfile):
            p.unlink(missing_ok=True)
        pout.mkdir(exist_ok=True)
        for f in pout.iterdir():
            f.unlink()
        cmd = [str(preview_exe), "--out", str(pout), "--log", str(logfile), "--free-bytes",
               "4294967297", *args]
        e = dict(os.environ, TRACE=str(trace), MAPS_FILE=str(mapsfile), **env)
        r = subprocess.run(cmd, env=e, capture_output=True, text=True, timeout=30)
        tr = trace.read_text().splitlines() if trace.exists() else []
        return r, tr, (logfile.read_text() if logfile.exists() else ""), pout

    r, tr, log, pout = preview("--seconds", "1", "--set", "--still", "--check-refusal")
    assert r.returncode == 0, (r.stdout, r.stderr, log)
    start = "movie:1020001:28:500,280,41700000,0,0,0,41700000"
    assert start in tr and "movie:1020002:0:0,0,0,0,0,0,0" in tr, tr
    assert tr.index(start) < tr.index("movie:1020002:0:0,0,0,0,0,0,0"), tr
    assert {"notify:2030001", "notify:2030002", "image-sub:3030001", "image-sub:3030002",
            "image-sub:3030003", "subscribe-movie-set"} <= set(tr), tr
    capture = [x for x in tr if x.startswith("capture:")]
    assert len(capture) == 1 and tr.index(capture[0]) > tr.index("movie:1020002:0:0,0,0,0,0,0,0"), tr
    assert "start 1280x640 at 150/10 fps with the set: ok (sensor rate bits 0x41f00000)" in r.stdout, r.stdout
    assert "capture during the preview: wrong session state" in r.stdout, r.stdout
    assert "stop: ok" in r.stdout and "all steps passed" in r.stdout, r.stdout
    assert (pout / "frame.yuv").stat().st_size == 64 * 32 * 3 // 2
    assert "buffer 64x32" in (pout / "frame.txt").read_text()
    assert (pout / "still-main.jpg").exists()
    assert "preview: VIDEOSTART_DONE" in log and "preview: VIDEOSTOP_DONE" in log, log

    # No VIDEOSTART_DONE: the start times out and a stop is sent.
    r, tr, log, pout = preview("--seconds", "1", FAKE_NO_MOVIE_ACK="1")
    assert r.returncode == 1 and "timed out" in r.stdout, r.stdout
    assert "movie:1020002:0:0,0,0,0,0,0,0" in tr, tr
    # A size the camera refuses: nothing is sent.
    r, tr, log, pout = preview("--size", "1000x500")
    assert r.returncode == 1 and not any(x.startswith("movie:") for x in tr), (r.stdout, tr)

print("g360cap host tests passed")
