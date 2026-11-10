#!/usr/bin/env python3
"""App orchestration against real durable storage and synthetic device/capture APIs."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[3]
STUB = r'''
#define _POSIX_C_SOURCE 200809L
#include "app_platform.h"
#include "g360cap.h"
#include "g360_output.h"
#include "g360ctl.h"
#include "g360display.h"
#include "power.h"
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_t ui_thread, capture_thread;
static int shot_outstanding; /* a background shot sound not yet reaped */
static int owner_set, ready, capturing, shots, frees, click_ready, click_step, busy_step, busy_seen, in_capture, power_sent;
static int hold_step, worn_shown, worn_step; static unsigned long long hold_start;
static unsigned long long ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (unsigned long long)t.tv_sec*1000u+(unsigned long long)t.tv_nsec/1000000u; }
static int once_mode(void) { return getenv("APP_TEST_ONCE")!=0; }
static int is(const char *s);
static int holds(void) { return is("sweep") || is("power-sweep") || is("hold-no-space") || is("worn-click"); }
static const char *scenario;
static int alerts_played; static unsigned long long pause_until;
static void trace(const char *format,...) {
    FILE *f; va_list ap;
    pthread_mutex_lock(&lock);
    f=fopen(getenv("APP_TEST_TRACE"),"a"); assert(f);
    va_start(ap,format); vfprintf(f,format,ap); va_end(ap); fputc('\n',f); assert(!fclose(f));
    pthread_mutex_unlock(&lock);
}
static void delay(unsigned ms) { struct timespec t={ms/1000,(long)(ms%1000)*1000000}; nanosleep(&t,0); }
static int is(const char *s) { if(!scenario) scenario=getenv("APP_TEST_SCENARIO"); return !strcmp(scenario,s); }
static void owner(void) { assert(owner_set && pthread_equal(capture_thread,pthread_self())); }
int g360display_clear(void) { ui_thread=pthread_self(); scenario=getenv("APP_TEST_SCENARIO"); trace("CLEAR"); return 0; }
static char last_text[64], last_left[32];
/* READY's place shows what is left (a bare number, drawn double size, or
   "CARD FULL"). It
   is traced as LEFT <count> when the count changes, and as READY in DISPLAY
   lines and W= steps, so scenarios don't depend on the card budget. */
static const char *ready_form(const char *text,char *out,size_t size) {
    const char *nl=strchr(text,'\n'); char first[32]; size_t n=nl?(size_t)(nl-text):strlen(text), k=0;
    if(n>=sizeof first) return text;
    memcpy(first,text,n); first[n]=0;
    while(first[k]>='0' && first[k]<='9') ++k;
    if(strcmp(first,"CARD FULL") && !(k>0 && first[k]==0 && !nl)) return text;
    if(strcmp(first,last_left)) { trace("LEFT %s",first); memcpy(last_left,first,n+1); }
    snprintf(out,size,"READY%s",nl?nl:"");
    return out;
}
int g360display_show(const char *text) {
    int stop_now=0; char flat[64], canon[64]; size_t k;
    text=ready_form(text,canon,sizeof canon);
    /* The --power-off process is a new image: no clear, no UI thread yet. */
    assert(!ui_thread || pthread_equal(ui_thread,pthread_self()));
    /* Lines are traced with '|'; each line must fit the real screen. */
    { size_t line=0, lines=1; for(k=0;text[k];++k) {
        if(text[k]=='\n') { ++lines; line=0; continue; }
        assert(strchr("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ /:-.+",text[k]) && ++line<=12); }
      assert(lines<=3); }
    for(k=0;text[k] && k<sizeof flat-1;++k) flat[k]=text[k]=='\n'?'|':text[k];
    flat[k]=0; trace("DISPLAY %s",flat);
    pthread_mutex_lock(&lock); memcpy(last_text,flat,sizeof flat); pthread_mutex_unlock(&lock);
    if(is("display-fail") && !strcmp(text,"CAPTURING...")) return -1;
    pthread_mutex_lock(&lock);
    if(!strcmp(text,"READY")) { ++ready; capturing=0; stop_now=!is("once") && !once_mode() && !getenv("APP_TEST_SCRIPT") && !strstr(scenario,"fail") && strncmp(scenario,"power",5) && frees>=(is("two")?2:1); }
    if(!strcmp(text,"CAPTURING...") || !strcmp(text,"SWEEP 1/7")) { capturing=1; busy_step=0; }
    if(!strncmp(text,"SAVING",6)) capturing=0;
    if(!strncmp(text,"FLASH W",7)) worn_shown=1;
    pthread_mutex_unlock(&lock);
    if(stop_now) raise(SIGTERM);
    return 0;
}
/* The status corner: at most two text lines below it; each new status is
   traced as STATUS <wifi> <battery> <charging>. */
static char last_status[64];
int g360display_show_status(const char *text,const g360display_status *s) {
    static const char *const wifi[]={"off","home","ap"};
    char now[64]; size_t k, lines=1; int changed;
    for(k=0;text[k];++k) if(text[k]=='\n') ++lines;
    assert(lines<=2 && s->wifi>=0 && s->wifi<=2 && s->battery>=-1 && s->battery<=100);
    static int last_mode;
    int mode_changed;
    static int last_timer=-1; int timer_changed;
    assert(s->mode==G360DISPLAY_MODE_SINGLE || s->mode==G360DISPLAY_MODE_SWEEP);
    assert(s->timer==0 || s->timer==1);
    { static int last_raw=-1, last_sound=-1; int raw_changed, sound_changed;
      assert((s->raw==0 || s->raw==1) && (s->sound==0 || s->sound==1));
      pthread_mutex_lock(&lock); raw_changed=s->raw!=last_raw; last_raw=s->raw; sound_changed=s->sound!=last_sound; last_sound=s->sound; pthread_mutex_unlock(&lock);
      if(raw_changed) trace("RAW ICON %s",s->raw?"ON":"OFF");
      if(sound_changed) trace("NOTES ICON %s",s->sound?"ON":"OFF"); }
    pthread_mutex_lock(&lock); timer_changed=s->timer!=last_timer; last_timer=s->timer; pthread_mutex_unlock(&lock);
    if(timer_changed) trace("TIMER ICON %s",s->timer?"ON":"OFF");
    snprintf(now,sizeof now,"%s %d %d",wifi[s->wifi],s->battery,s->charging);
    pthread_mutex_lock(&lock); mode_changed=s->mode!=last_mode; last_mode=s->mode; pthread_mutex_unlock(&lock);
    if(mode_changed) trace("MODE ICON %s",s->mode==G360DISPLAY_MODE_SWEEP?"SWEEP":"SINGLE");
    pthread_mutex_lock(&lock); changed=strcmp(now,last_status)!=0; if(changed) memcpy(last_status,now,sizeof now); pthread_mutex_unlock(&lock);
    if(changed) trace("STATUS %s",now);
    { static int last_frame=-1; int frame_changed;
      assert(s->charge_frame>=0 && s->charge_frame<G360DISPLAY_CHARGE_FRAMES && (s->charging || s->charge_frame==0));
      pthread_mutex_lock(&lock); frame_changed=s->charge_frame!=last_frame; last_frame=s->charge_frame; pthread_mutex_unlock(&lock);
      if(frame_changed && getenv("APP_TEST_FRAMES")) trace("FRAME %d",s->charge_frame); }
    return g360display_show(text);
}
/* F=<path>=<text> writes a file under APP_TEST_STATUS_ROOT; \n and \t in
   the text are a newline and a tab. */
static void write_status_file(const char *step) {
    const char *eq=strchr(step,'='), *p; char path[512], temporary[520]; FILE *f;
    assert(eq && getenv("APP_TEST_STATUS_ROOT"));
    snprintf(path,sizeof path,"%s/%.*s",getenv("APP_TEST_STATUS_ROOT"),(int)(eq-step),step);
    /* Replaced whole, so the app never reads half a file. */
    snprintf(temporary,sizeof temporary,"%s.new",path);
    f=fopen(temporary,"w"); assert(f);
    for(p=eq+1;*p;++p) {
        if(p[0]=='\\' && p[1]=='n') { fputc('\n',f); ++p; }
        else if(p[0]=='\\' && p[1]=='t') { fputc('\t',f); ++p; }
        else fputc(*p,f);
    }
    assert(!fclose(f) && !rename(temporary,path)); trace("FILE %.*s",(int)(eq-step),step);
}
int g360ctl_buttons_open(const char *device) { assert(pthread_equal(ui_thread,pthread_self())); assert(!strcmp(device,"synthetic")); trace("BUTTONS OPEN"); return 42; }
void g360ctl_buttons_close(int fd) { assert(fd==42 && pthread_equal(ui_thread,pthread_self())); trace("BUTTONS CLOSE"); }
/* APP_TEST_SCRIPT: steps separated by ';' -- W=<screen> waits until the
   screen shows it ('|' between lines), S=<status> until the status corner
   shows it, C=<text> until the screen contains it, A=<n> until n alert sounds were played, P=<ms> pauses, F=<path>=<text>
   writes a status file, K=<KEY>,<value> sends a key event, STOP sends SIGTERM. */
static size_t script_at;
static int scripted(g360ctl_button_event *event) {
    const char *script=getenv("APP_TEST_SCRIPT"), *step=script+script_at, *end;
    char token[512]; size_t n; int stop_now=0, sent=0;
    static const struct { const char *name; g360ctl_key key; unsigned code; } keys[]={
        {"CAPTURE",G360CTL_KEY_CAPTURE,126},{"MENU",G360CTL_KEY_MENU,127},{"MENU_HOLD",G360CTL_KEY_MENU_HOLD,215},
        {"POWER",G360CTL_KEY_POWER,73},{"POWER_HOLD",G360CTL_KEY_POWER_HOLD,116}};
    if(!*step) return 0;
    end=strchr(step,';'); n=end?(size_t)(end-step):strlen(step);
    assert(n<sizeof token); memcpy(token,step,n); token[n]=0;
    if(!strncmp(token,"W=",2)) {
        pthread_mutex_lock(&lock); sent=!strcmp(last_text,token+2); pthread_mutex_unlock(&lock);
        if(!sent) return 0;
    } else if(!strncmp(token,"C=",2)) {
        pthread_mutex_lock(&lock); sent=strstr(last_text,token+2)!=0; pthread_mutex_unlock(&lock);
        if(!sent) return 0;
    } else if(!strncmp(token,"S=",2)) {
        pthread_mutex_lock(&lock); sent=!strcmp(last_status,token+2); pthread_mutex_unlock(&lock);
        if(!sent) return 0;
    } else if(!strncmp(token,"A=",2)) {
        pthread_mutex_lock(&lock); sent=alerts_played>=atoi(token+2); pthread_mutex_unlock(&lock);
        if(!sent) return 0;
    } else if(!strncmp(token,"P=",2)) {
        if(!pause_until) pause_until=ms()+(unsigned long long)atoi(token+2);
        if(ms()<pause_until) return 0;
        pause_until=0;
    } else if(!strncmp(token,"F=",2)) write_status_file(token+2);
    else if(!strcmp(token,"STOP")) stop_now=1;
    else {
        char *comma=strchr(token,','); unsigned k;
        assert(!strncmp(token,"K=",2) && comma); *comma=0;
        for(k=0;k<sizeof keys/sizeof keys[0] && strcmp(keys[k].name,token+2);++k) {}
        assert(k<sizeof keys/sizeof keys[0]);
        memset(event,0,sizeof *event); event->key=keys[k].key; event->code=keys[k].code; event->value=atoi(comma+1);
        sent=2; trace("KEY %s %d",keys[k].name,event->value);
    }
    script_at+=n+(end?1:0);
    if(stop_now) { trace("SCRIPT STOP"); raise(SIGTERM); }
    return sent==2;
}
int g360ctl_buttons_next(int fd,int timeout,g360ctl_button_event *event,int *dropped) {
    int value=-1, drop=0, mark_busy=0, power=0;
    if(getenv("APP_TEST_SCRIPT")) { assert(fd==42 && timeout==50); delay(1); *dropped=0; return scripted(event); }
    static const int initial[]={0,2,1,0,1,2,0};
    assert(fd==42 && timeout==50 && pthread_equal(ui_thread,pthread_self()));
    delay(1);
    pthread_mutex_lock(&lock);
    if(is("power") && ready && !power_sent) { power_sent=1; power=1; value=1; }
    else if(is("power-sweep") && in_capture && shots==2 && !power_sent) { power_sent=1; power=1; value=1; }
    else if(holds() && ready && !capturing && hold_step<2) {
        /* Hold Capture past the sweep threshold, then release. */
        if(hold_step==0) { value=1; hold_step=1; hold_start=ms(); }
        else if(ms()-hold_start>=1100) { value=0; hold_step=2; }
    } else if(is("worn-click") && worn_shown && worn_step<1) {
        value=1; ++worn_step;   /* a press after the warning */
    } else if(capturing) {
        if(!busy_seen) { busy_seen=1; mark_busy=1; }
        if(busy_step<3) { static const int busy[]={1,2,0}; value=busy[busy_step++]; }
        else if(is("power-busy") && in_capture && !power_sent) { power_sent=1; power=1; value=1; }
    } else if(ready && !is("once") && !once_mode() && !holds() && !strstr(scenario,"fail") && !is("power")) {
        if(click_ready!=ready) { click_ready=ready; click_step=0; }
        if(click_step<7) { value=initial[click_step]; drop=click_step==2; ++click_step; }
    }
    pthread_mutex_unlock(&lock);
    if(mark_busy) trace("UI POLLING WHILE BUSY");
    *dropped=drop;
    if(value<0) return 0;
    memset(event,0,sizeof *event); event->value=value;
    if(power) { event->key=G360CTL_KEY_POWER_HOLD; event->code=116; trace("BUTTON POWER HOLD"); return 1; }
    event->key=G360CTL_KEY_CAPTURE; event->code=126;
    trace("BUTTON %d dropped=%d",value,drop); return 1;
}
int g360_open(const g360_open_options *o) {
    capture_thread=pthread_self(); owner_set=1;
    assert(!pthread_equal(capture_thread,ui_thread) && o->frame_pause && o->free_bytes);
    trace("OPEN"); return G360_OK;
}
int g360_set_auto_exposure(void) { owner(); assert(!shot_outstanding); trace("AUTO"); return 0; }
int g360_set_manual_exposure(int32_t tv,uint32_t iso) { owner(); trace("MANUAL %d %u",(int)tv,(unsigned)iso); return 0; }
int g360_set_image_quality(uint32_t v) { owner(); assert(v==G360_QUALITY_SUPERFINE); return 0; }
int g360_set_drive_mode(uint32_t v) { owner(); assert(v==G360_DRIVE_SINGLE); return 0; }
int g360_set_quickview(uint32_t v) { owner(); assert(v==G360_QUICKVIEW_OFF); return 0; }
int g360_set_user_attribute(int a,uint32_t v) {
    owner();
    assert(a==G360_ATTR_WB && v==0x50000u); return 0;
}
int g360_service(uint32_t ms) { owner(); (void)ms; delay(1); return 0; }
void g360_set_free_bytes(uint64_t n) { owner(); assert(n); trace("SPACE"); }
void g360_close(void) { owner(); trace("CLOSE"); }
const char *g360_status_text(int s) { return s==0?"ok":"synthetic failure"; }

/* The shot LEDs: no LEDs here; the flash thread just runs its course. */
int g360ctl_leds_read(const char *root,g360ctl_led_state *states,int max) { (void)root; (void)states; (void)max; return 0; }
int g360ctl_led_set(const char *root,int index,uint32_t brightness) { (void)root; if(index<=1) trace("LED %d %u",index,(unsigned)brightness); return 0; }
int g360ctl_leds_restore(const char *root,const g360ctl_led_state *states,int count) { (void)root; (void)states; (void)count; return 0; }
int g360ctl_sound_play(const char *player,const char *file) {
    assert(!strcmp(player,G360CTL_PAPLAY));
    if(strstr(file,"/powerOFF.wav")) { assert(pthread_equal(ui_thread,pthread_self())); trace("SOUND POWER OFF"); return 0; }
    { static const char *const alert[]={"low_battery.wav","13_N_NoBattery.wav","BT_connected.wav","BT_disconnected.wav","charger_connect.wav"}; unsigned k;
      for(k=0;k<sizeof alert/sizeof alert[0];++k) {
        char name[40]; snprintf(name,sizeof name,"/%s",alert[k]);
        if(strstr(file,name)) {
            assert(!pthread_equal(ui_thread,pthread_self()) && !pthread_equal(capture_thread,pthread_self()));
            assert(strstr(file,G360CTL_SOUND_DIR "/")==file);
            pthread_mutex_lock(&lock); ++alerts_played; pthread_mutex_unlock(&lock);
            trace("SOUND ALERT %s",alert[k]); return 0;
        }
      } }
    owner();
    if(strstr(file,"/hdr_start.wav")) { trace("SOUND HDR START"); return 0; }
    if(strstr(file,"/hdr_end.wav")) { trace("SOUND HDR END"); return 0; }
    if(strstr(file,"/preview_sound.wav")) { trace("SOUND DONE"); return 0; }
    if(strstr(file,"/shutter_continous.wav")) { trace("SOUND SWEEP DONE"); return 0; }
    assert(strstr(file,"/shutter.wav"));
    trace("SOUND"); delay(15); return is("sound-fail")?1:0;
}
/* The self-timer sounds run in the background, from the UI thread. */
/* The sweep's shot sound plays in the background from the capture worker;
   each must be reaped (or stopped) before the next starts. */
int g360ctl_sound_start(const char *player,const char *file) {
    assert(!strcmp(player,G360CTL_PAPLAY));
    if(strstr(file,"/07_N_AF.wav")) { owner(); assert(!shot_outstanding); shot_outstanding=1; trace("SOUND SHOT"); return 4343; }
    assert(pthread_equal(ui_thread,pthread_self()));
    if(strstr(file,"/timer_last2sec.wav")) trace("TIMER SOUND LAST2SEC");
    else { assert(strstr(file,"/timer.wav")); trace("TIMER SOUND TICK"); }
    return 4242;
}
int g360ctl_sound_reap(int pid) { if(pid==4343) { owner(); shot_outstanding=0; return 1; } assert(pid==4242); return 0; } /* still playing */
void g360ctl_sound_stop(int pid) { if(pid==4343) { owner(); shot_outstanding=0; return; } assert(pid==4242); trace("TIMER SOUND STOP"); }
/* The camera's movie preview: 64 x 32 NV12 frames every 20 ms on a thread
   of its own, like the capture library's. */
static volatile int preview_on; static pthread_t preview_thread;
static void (*preview_cb)(const g360_preview_frame*,void*);
static unsigned char preview_y[64*32], preview_c[64*16];
static void *preview_main(void *unused) {
    unsigned n=0; (void)unused;
    while(preview_on) {
        g360_preview_frame f; unsigned char block[G360_PREVIEW_BLOCK_BYTES]={0};
        memset(preview_y,(int)(16+n%200),sizeof preview_y); memset(preview_c,128,sizeof preview_c);
        memset(&f,0,sizeof f); f.topic=G360_PREVIEW_TOPIC_SET; f.buffer_width=64; f.buffer_height=32;
        f.width=64; f.height=32; f.address_type=7; f.side=1; f.y=preview_y; f.c=preview_c;
        f.y_bytes=sizeof preview_y; f.c_bytes=sizeof preview_c; f.block=block;
        preview_cb(&f,0); ++n; delay(20);
    }
    return 0;
}
int g360_preview_start(const g360_preview_options *o) {
    owner(); assert(!preview_on && o->callback && o->with_set && o->width==1280 && o->height==640 && o->fps_x10==150);
    if(getenv("APP_TEST_PREVIEW_FAIL")) { trace("PREVIEW START FAILED"); return G360_E_TIMEOUT; }
    trace("PREVIEW START"); preview_cb=o->callback; preview_on=1;
    assert(!pthread_create(&preview_thread,0,preview_main,0)); return G360_OK;
}
int g360_preview_stop(uint32_t ms) {
    owner(); (void)ms; if(!preview_on) return G360_OK;
    preview_on=0; assert(!pthread_join(preview_thread,0)); trace("PREVIEW STOP"); return G360_OK;
}
int g360_preview_running(void) { return preview_on; }
static void allocate(g360_image *i) { i->data=malloc(16); assert(i->data); memset(i->data,0x5a,16); i->size=16; i->flags=G360_IMAGE_COPIED; }
int g360_capture_shot(const g360_capture_options *o,g360_shot *shot) {
    owner(); assert(!preview_on); assert(o->max_sets==1 && (o->frame==1 || (o->frame==0 && !o->skip_jpeg_copy)));
    trace(!o->frame?"CAPTURE JPEG":o->skip_jpeg_copy?"CAPTURE RAW":"CAPTURE");
    pthread_mutex_lock(&lock); in_capture=1; pthread_mutex_unlock(&lock);
    memset(shot,0,sizeof *shot); shot->set_count=1;
    if(o->frame) allocate(&shot->frame.image);
    allocate(&shot->sets[0].maker_note); if(!o->skip_jpeg_copy) allocate(&shot->sets[0].images[0]);
    shot->sets[0].images[0].width=5792; shot->sets[0].images[0].height=2896;
    delay(30); ++shots; return is("capture-fail") || (is("sweep-fail") && shots==3)?G360_E_IMAGE:0;
}
void g360_exif_from_still(const g360_still *still,const g360_image *jpeg,g360_exif_info *info) {
    owner(); assert(still->maker_note.data[0]==0x5a && (!jpeg || jpeg->data[0]==0x5a));
    memset(info,0,sizeof *info); if(jpeg) { info->width=jpeg->width; info->height=jpeg->height; }
    info->iso=125; info->exposure_numerator=1; info->exposure_denominator=100;
    trace("EXIF");
}
/* The real time from the TV: the stub writes TV/10000000 to show it was used. */
void g360_exif_exposure_from_tv(int32_t tv,uint32_t *numerator,uint32_t *denominator) {
    owner(); *numerator=(uint32_t)tv; *denominator=10000000u; trace("REAL TV %ld",(long)tv);
}
static int save(const char *path,const char *kind,const char *date) {
    FILE *f=fopen(path,"r"); assert(f && fgetc(f)==EOF); assert(!fclose(f));
    trace("SAVE %s %s",kind,path); delay(15);
    if(is("save-fail") && !strcmp(kind,"JPEG")) return G360_E_IO;
    f=fopen(path,"w"); assert(f); if(!strcmp(kind,"JPEG")) { fputc(255,f); fputc(216,f); }
    fprintf(f,"%s\n%s\n",kind,date?date:"");
    if(!strcmp(kind,"JPEG")) { fputc(255,f); fputc(217,f); } assert(!fclose(f)); return 0;
}
/* The pose packet reaches the writers as a NUL-terminated string or NULL; every
   save traces what it got (XMP none, or XMP pitch/roll/samples). */
static void trace_xmp(const char *xmp) {
    const char *p, *r, *n;
    if(!xmp) { trace("XMP none"); return; }
    p=strstr(xmp,"GPano:PosePitchDegrees=\""); r=strstr(xmp,"GPano:PoseRollDegrees=\""); n=strstr(xmp,"g360:Samples=\"");
    assert(p && r && n && !strncmp(xmp,"<?xpacket begin",15) && strlen(xmp)<2048);
    { int pl=(int)strcspn(p+24,"\""), rl=(int)strcspn(r+23,"\""), nl=(int)strcspn(n+14,"\"");
      trace("XMP pitch %.*s roll %.*s samples %.*s",pl,p+24,rl,r+23,nl,n+14); }
}
int g360_save_dng_full_xmp(const g360_frame *frame,const g360_image *note,const g360_exif_info *info,
                           const char *description,const char *xmp,const char *path) {
    owner(); assert(frame->image.data[0]==0x5a && note->data[0]==0x5a && info); trace_xmp(xmp);
    /* Every DNG carries the shot's exposure and date; sweep frames say which. */
    assert(info->iso==125 && (info->exposure_denominator==100 || info->exposure_denominator==10000000) && strlen(info->datetime)==19);
    if(description) { assert(!strncmp(description,"HDR SWEEP ",10) && strstr(description," FRAME ")); trace("DESC %s",description); }
    return save(path,"DNG",0);
}
int g360_save_jpeg_exif_xmp(const g360_image *jpeg,const g360_image *note,const g360_exif_info *info,const char *xmp,const char *path) {
    owner(); trace_xmp(xmp); assert(jpeg->data[0]==0x5a && note->data[0]==0x5a && info->iso==125 && (info->exposure_denominator==100 || info->exposure_denominator==10000000));
    assert(strlen(info->datetime)==19 && info->datetime[4]==':' && info->datetime[10]==' ');
    trace("DATE %s",info->datetime); return save(path,"JPEG",info->datetime);
}
int g360_power_off(unsigned wait,g360_power_result *r) {
    char cwd[256]; int fd, open_fds=0;
    /* Runs in the relaunched copy: nothing may stay open but 0-2. */
    assert(!owner_set && !ui_thread && wait==10000 && getcwd(cwd,sizeof cwd));
    for(fd=3;fd<256;++fd) if(fcntl(fd,F_GETFD)!=-1) ++open_fds;
    trace("POWER SEQUENCE cwd=%s fds=%d",cwd,open_fds); r->flag=0; r->deviced=4; r->state=0; return G360_POWER_E_STILL_ON;
}
void g360_shot_free(g360_shot *shot) {
    owner(); assert(shot->sets[0].maker_note.data);
    free(shot->frame.image.data); free(shot->sets[0].maker_note.data); free(shot->sets[0].images[0].data); memset(shot,0,sizeof *shot);
    pthread_mutex_lock(&lock); ++frees; pthread_mutex_unlock(&lock); trace("FREE");
}
'''


FAKE_TURBOJPEG = r'''
#include <stdlib.h>
#include <string.h>
static unsigned count;
void *tjInitCompress(void) { return (void*)1; }
int tjCompress2(void *h,unsigned char *src,int w,int pitch,int ht,int pf,unsigned char **jpeg,unsigned long *size,int sub,int q,int flags) {
    unsigned char *b=malloc(16); (void)h; (void)pitch;
    if(pf!=0 || sub!=2 || q<1 || q>100 || flags!=0 || !src) return -1;
    b[0]=0xff; b[1]=0xd8; b[2]=(unsigned char)w; b[3]=(unsigned char)(w>>8); b[4]=(unsigned char)ht; b[5]=(unsigned char)(ht>>8);
    b[6]=src[0]; memcpy(b+7,&count,4); ++count; memset(b+11,0,3); b[14]=0xff; b[15]=0xd9;
    *jpeg=b; *size=16; return 0;
}
void tjFree(unsigned char *b) { free(b); }
'''
PREVIEW_PORT = None


def preview_flags(work):
    """The preview's port (a free one), a short idle stop and the fake encoder."""
    global PREVIEW_PORT
    import socket
    lib = work/"libturbojpeg-fake.so"
    if not lib.exists():
        (work/"tj.c").write_text(FAKE_TURBOJPEG)
        subprocess.run([os.environ.get("CC","cc"),"-shared","-fPIC",str(work/"tj.c"),"-o",str(lib)],check=True)
    if PREVIEW_PORT is None:
        with socket.socket() as probe:
            probe.bind(("127.0.0.1",0)); PREVIEW_PORT=probe.getsockname()[1]
    return [f"-DG360_PREVIEW_PORT={PREVIEW_PORT}","-DG360_PREVIEW_IDLE_MS=1500u",f'-DG360_TURBOJPEG_LIBRARY="{lib}"']


def photo_text(path):
    data=path.read_bytes()
    if data.startswith(b"\xff\xd8") and data.endswith(b"\xff\xd9"): data=data[2:-2]
    return data.decode()


def main():
    with tempfile.TemporaryDirectory(prefix="g360-app-test-") as td:
        work=Path(td)
        stub=work/"stub.c"; stub.write_text(STUB)
        # The Wi-Fi script's stand-in records what the app asks for, in the
        # file the APP_TEST_WIFI variable names (if any).
        (work/"wifi-join.sh").write_text('[ -n "${APP_TEST_WIFI:-}" ] && echo "$*" >>"$APP_TEST_WIFI"\nexit 0\n')
        # The station list tool's stand-in prints the file "wl-out" in the status root.
        (work/"wl").write_text('#!/bin/sh\nf="$APP_TEST_STATUS_ROOT/wl-out"\n[ -r "$f" ] || exit 1\ncat "$f"\n')
        (work/"wl").chmod(0o755)
        exe=work/"app"
        includes=[ROOT/"src/app",ROOT/"src/lib/capture-session",ROOT/"src/lib/g360cap",
                  ROOT/"src/lib/g360ctl",ROOT/"src/lib/g360display"]
        copy, log = work/"poweroff-copy", work/"poweroff.log"
        subprocess.run([os.environ.get("CC","cc"),"-std=c11","-O1","-g","-Wall","-Wextra","-Werror",
                        f'-DG360_POWER_SELF="{exe}"',f'-DG360_POWER_COPY="{copy}"',f'-DG360_POWER_LOG="{log}"',
                        "-DG360_QUEUE_FRAME_BYTES=UINT64_C(5)","-DG360_STATUS_EVERY_MS=100u","-DG360_CHARGE_FRAME_MS=3600000u","-DG360_ALERT_EVERY_MS=100u",
                        f'-DG360_WL_PATH="{work/"wl"}"',
                        f'-DG360_STATUS_ROOT="{work/"no-status"}"',f'-DG360_MOTION_HISTORY="{work/"no-motion"}"',
                        f'-DG360_WIFI_SCRIPT="{work/"wifi-join.sh"}"',f'-DG360_WIFI_FILE="{work/"wifi"}"',f'-DG360_REMOTE_SOCKET="{work/"app.sock"}"',
                        "-fsanitize=address,undefined","-fno-sanitize-recover=all","-pthread",
                        *["-I"+str(p) for p in includes],str(ROOT/"src/app/main.c"),
                        str(ROOT/"src/app/storage.c"),str(ROOT/"src/app/queue.c"),str(ROOT/"src/app/wear.c"),
                        str(ROOT/"src/app/settings.c"),str(ROOT/"src/app/status.c"),str(ROOT/"src/app/alerts.c"),str(ROOT/"src/app/wifi.c"),str(ROOT/"src/app/remote.c"),
                        str(ROOT/"src/app/preview.c"),*preview_flags(work),
                        str(ROOT/"src/app/power_relaunch.c"),str(ROOT/"src/app/motion.c"),str(ROOT/"src/lib/g360cap/g360_pose.c"),str(stub),"-lm","-o",str(exe)],check=True)
        for scenario in ("once","two","capture-fail","save-fail","sound-fail","display-fail","power","power-busy"):
            root=work/scenario; trace=work/(scenario+".txt")
            env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO=scenario)
            args=[str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic"]
            if scenario not in ("two","power","power-busy"): args += ["--once"]
            r=subprocess.run(args,env=env,capture_output=True,text=True,timeout=15)
            lines=trace.read_text().splitlines()
            assert f"LEFT {LEFT_1GB}" in lines and f"APP UI {LEFT_1GB}" in r.stdout,(scenario,lines,r.stdout)
            expected=0 if scenario in ("once","two") else 1  # power: the stub stays on
            assert r.returncode==expected,(scenario,r.returncode,r.stdout,r.stderr,lines)
            assert lines.count("OPEN")==1 and lines.count("AUTO")==1 and lines.count("CLOSE")==1,lines
            captures=lines.count("CAPTURE")
            assert captures==(2 if scenario=="two" else 0 if scenario in ("sound-fail","display-fail","power") else 1),lines
            assert lines.count("FREE")==captures,lines
            if scenario.startswith("power"):
                # Power held: the in-flight shot is saved, the worker closes, then
                # the UI thread shows POWER OFF, plays the sound and runs the sequence.
                # The sequence itself runs in the relaunched copy, from /, with no
                # inherited descriptors and its output in the log.
                tail=lines[lines.index("BUTTON POWER HOLD"):]
                assert "DISPLAY POWER OFF" in tail and tail[-2:]==["POWER SEQUENCE cwd=/ fds=0","DISPLAY ERROR"],lines
                order=[tail.index(x) for x in ("DISPLAY POWER OFF","BUTTONS CLOSE","SOUND POWER OFF")]
                assert order==sorted(order) and tail.index("CLOSE")<tail.index("SOUND POWER OFF"),lines
                assert "APP POWER HOLD" in r.stdout and f"APP POWER OFF from {copy}" in r.stdout and "ERROR" not in r.stdout,r.stdout
                assert copy.read_bytes()==exe.read_bytes() and os.access(copy,os.X_OK)
                assert log.read_text()=="APP POWER OFF sequence\nAPP ERROR power off: status -4 flag 0 deviced 4 state 0\n",log.read_text()
                copy.unlink(); log.unlink()
                if scenario=="power-busy":
                    assert lines.index("CAPTURE")<lines.index("BUTTON POWER HOLD")<lines.index("FREE")<lines.index("CLOSE"),lines
                    assert sorted(p.suffix for p in (root/"photos").iterdir())==[".dng",".jpg"],lines
                    assert all(p.stat().st_size for p in (root/"photos").iterdir())
                else:
                    assert lines.count("DISPLAY READY")==1 and "SOUND" not in lines,lines
            elif scenario in ("once","two"):
                count=2 if scenario=="two" else 1
                assert lines.count("DISPLAY READY")==count+1 and lines.count("SOUND")==count,lines
                assert "UI POLLING WHILE BUSY" in lines,lines
                photos=sorted((root/"photos").iterdir())
                assert len(photos)==count*2,photos
                for seq in range(1,count+1):
                    pair=[p for p in photos if p.stem.endswith(f"-{seq:05d}")]
                    assert {p.suffix for p in pair}=={".jpg",".dng"}
                    for p in pair:
                        assert re.fullmatch(r"\d{8}-\d{6}-\d{5}",p.stem),p
                        assert photo_text(p).startswith("JPEG\n" if p.suffix==".jpg" else "DNG\n")
                    jpg=next(p for p in pair if p.suffix==".jpg")
                    stamp=jpg.stem[:15]
                    date=f"{stamp[:4]}:{stamp[4:6]}:{stamp[6:8]} {stamp[9:11]}:{stamp[11:13]}:{stamp[13:15]}"
                    assert photo_text(jpg)=="JPEG\n"+date+"\n"
                for i,line in enumerate(lines):
                    if line=="SOUND": assert "DISPLAY CAPTURING..." in lines[:i]
                # The "capture done" sound comes after the files are written.
                assert lines.count("SOUND DONE")==count and lines.count("SOUND SHOT")==0,lines
                last_save=max(i for i,l in enumerate(lines) if l.startswith("SAVE "))
                assert max(i for i,l in enumerate(lines) if l=="SOUND DONE")>last_save,lines
            else:
                assert lines.count("DISPLAY READY")==1,lines
                assert lines.count("SOUND")== (0 if scenario=="display-fail" else 1),lines
                if scenario in ("capture-fail","sound-fail","display-fail"):
                    assert not any(x.startswith("SAVE ") for x in lines),lines
        sweeps(work,exe,copy,log)
        pose_scenario(work,exe)
        print("pose: no motion history = no XMP and a log line; a history file = XMP (pitch, roll, samples) in the JPEG and DNG of a still; stale records skipped")
        print("app startup, owned outputs/dates, two-shot session, busy/drop/repeat filtering, worker affinity, failure and Power-hold checks passed")
        print("sweep by hold, maintenance sweep, sweep failure, Power hold in a sweep, queue recovery and no-flash-space checks passed")
        print("flash wear: logged after a sweep, FLASH WEAR 4 / FLASH WORN instead of READY, cleared by a press")
        settings_scenarios(work,exe)
        print("settings screen: Menu press, Menu/Capture/Power navigation, stored and reloaded settings, HDR mode, 6 shots, straight to the card")
        output_scenarios(work,exe)
        print("Output: three persisted menu choices; exact files, same-stem pairs and exposure rows for single photos and card/flash sweeps")
        status_scenario(work,exe)
        print("status corner: home network, charging, full on a cable, access point, unreadable battery, no Wi-Fi; redrawn and logged on change")
        animation_scenario(work,includes)
        print("charging animation: the battery's walls step through six frames while charging, none redrawn otherwise; side LED off / yellow (orange) / green")
        alerts_scenario(work,exe)
        print("sounds: home network joined, charger, 10% x3 and 20% warnings, access point stations, links lost")
        left_scenario(work,exe)
        print("stills or sweeps left instead of READY: counted from the card budget, singular, CARD FULL after the last still")
        remote_scenario(work,exe)
        preview_scenario(work,exe)
        print("remote: state, settings (stored, icons, Wi-Fi), capture and sweep as a click, countdown and cancel, "
              "refusals (bad request, busy, countdown, settings open, Wi-Fi), auto photos with the delay "
              "overlapping, stopped by Power; app off")
        wifi_scenarios(work,exe)
        print("Wi-Fi setting: applied at start (not chosen = HOME with the network file), OFF/AP/HOME chosen and stored, "
              "HOME only with the file, applied again when chosen again, skipped after a wifi-join.sh restart")


# What READY's place shows: the stills left in the card budget, as main.c
# counts them (this build's sweep frames are 5 bytes).
SPACE_RESERVE=134217728
SHOT_BUDGET=33732608+40960+96*1024*1024+40000
SHOT_TYPICAL=33732608+40960+6*1024*1024+40000+4096
FRAME_CARD_BUDGET=5+32768
# RAW OFF: a JPEG alone.
JPEG_BUDGET=96*1024*1024+40000
JPEG_TYPICAL=6*1024*1024+40000+4096


def left(free,sweep_frames=0,raw=True,output=None):
    output=output or ("raw+jpeg" if raw else "jpeg")
    has_raw,has_jpeg=output!="jpeg",output!="raw"
    remaining=free>>10<<10
    if sweep_frames:
        need=(sweep_frames*FRAME_CARD_BUDGET if has_raw else 0)+(JPEG_BUDGET+(sweep_frames-1)*JPEG_TYPICAL if has_jpeg else 0)
        each=(sweep_frames*FRAME_CARD_BUDGET if has_raw else 0)+(sweep_frames*JPEG_TYPICAL if has_jpeg else 0)
    else:
        need=(33732608+40960 if has_raw else 0)+(JPEG_BUDGET if has_jpeg else 0)
        each=(33732608+40960 if has_raw else 0)+(JPEG_TYPICAL if has_jpeg else 0)
    n=0 if remaining<SPACE_RESERVE+need else 1+(remaining-SPACE_RESERVE-need)//each
    return "CARD FULL" if n==0 else str(min(n,99999))


LEFT_1GB=left(1000000000)
SWEEP=[(41,100),(59,100),(90,100),(122,100),(154,100),(186,100),(218,100)]
SWEEP6=SWEEP


def press(key): return f"K={key},1;K={key},0"


def run_script(work,exe,name,root,qdir,script,extra=()):
    trace=work/(name+".txt")
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO=name,APP_TEST_SCRIPT=script)
    args=[str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic",*extra]
    if qdir: args+=["--queue",str(qdir),"--queue-free-bytes","5000000000"]
    r=subprocess.run(args,env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    once="--sweep-once" in extra or "--once" in extra  # exits by itself
    assert r.returncode==0 and (once or "SCRIPT STOP" in lines),(name,r.returncode,r.stdout,r.stderr,lines[-30:])
    return r,lines


def write_history(path,position,pitch_up=(0.0,0.0,-1.0),stale=()):
    """A fake motion history: 8,000,000 bytes of 20-byte records, 5 ms apart,
    resting at the given 'up' vector (the driver's calibration, as the history has it), stale records as given."""
    import math, struct
    off,sc=(0,0,0),(8197,8197,8197)
    ax=[round(off[k]+sc[k]*pitch_up[k]) for k in range(3)]
    with open(path,"wb") as f:
        f.truncate(8000000)
        for i in range(position-1000,position+1):
            stamp=(1000000+4900*i)&0xffffffff
            if i in stale: stamp=(stamp-2000000000)&0xffffffff
            w2=((ax[0]&0xffff)<<16)|(ax[1]&0xffff)
            w3=(0<<16)|(ax[2]&0xffff)
            f.seek(i*20); f.write(struct.pack("<5I",stamp,0,w2,w3,0))
    return path


def pose_scenario(work,exe):
    import math
    pos=work/"motion-pos"; pos.write_bytes((5000).to_bytes(4,"little"))
    a=math.radians(20)
    hist=write_history(work/"motion-hist",5000,(-math.sin(a),0.0,-math.cos(a)))
    extra=("--motion-history",str(hist),"--motion-position",str(pos))
    # A still: the JPEG and the DNG both get the packet; the log says so.
    root=work/"pose-once"; qdir=work/"pose-once-q"
    r,lines=run_script(work,exe,"once",root,None,"",extra=("--once",*extra)) if False else (None,None)
    trace=work/"pose-once.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="once")
    r=subprocess.run([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic","--once",*extra],
                     env=env,capture_output=True,text=True,timeout=15)
    lines=trace.read_text().splitlines()
    assert r.returncode==0,(r.stdout,r.stderr,lines)
    assert lines.count("XMP pitch 0.00 roll 20.00 samples 50")==2,lines
    assert "APP POSE 50 samples pitch 0.0 roll 20.0 up -0.342 0.000 -0.940 gyro 0.0 dps" in r.stdout,r.stdout
    # The same with the position file missing: no pose, no XMP, the shot is saved anyway.
    root=work/"pose-none"; trace=work/"pose-none.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="once")
    r=subprocess.run([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic","--once",
                      "--motion-history",str(hist),"--motion-position",str(work/"missing")],
                     env=env,capture_output=True,text=True,timeout=15)
    lines=trace.read_text().splitlines()
    assert r.returncode==0 and lines.count("XMP none")==2 and "APP POSE none: motion history unavailable" in r.stdout,(r.stdout,lines)
    assert sorted(p.suffix for p in (root/"photos").iterdir())==[".dng",".jpg"]
    # Stale records inside the window (last lap's) are skipped, not averaged in.
    hist2=write_history(work/"motion-hist2",5000,(-math.sin(a),0.0,-math.cos(a)),stale=range(4960,4990,3))
    root=work/"pose-stale"; trace=work/"pose-stale.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="once")
    r=subprocess.run([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic","--once",
                      "--motion-history",str(hist2),"--motion-position",str(pos)],
                     env=env,capture_output=True,text=True,timeout=15)
    lines=trace.read_text().splitlines()
    used=[l for l in lines if l.startswith("XMP ")]
    assert r.returncode==0 and len(used)==2 and used[0]==used[1] and "roll 20.00" in used[0],(r.stdout,lines)
    assert not used[0].endswith("samples 50"),used  # some were skipped


def settings_scenarios(work,exe):
    root=work/"settings"; qdir=work/"settings-flash"/"queue"; qdir.parent.mkdir()
    # Open with a Menu press; Menu moves, Capture opens and selects, Power leaves.
    # Mode HDR is selected; SAVE is changed to CARD but left with Power, so
    # it stays FLASH. Then one Capture press takes the 7-shot sweep.
    script=";".join(["W=READY",press("MENU"),"W=SETTINGS 1/6|MODE: SINGLE",
        press("CAPTURE"),"W=MODE|- SINGLE -",press("MENU"),"W=MODE|- HDR -",press("CAPTURE"),
        "W=SETTINGS 1/6|MODE: HDR",press("MENU"),"W=OUTPUT 2/6|RAW+JPEG",
        press("MENU"),"W=SETTINGS 3/6|SAVE: FLASH",press("CAPTURE"),"W=SAVE TO|- FLASH -",press("MENU"),
        "W=SAVE TO|- CARD -",press("POWER"),"W=READY",
        press("MENU"),press("POWER"),             # outside the settings: nothing
        press("CAPTURE"),"W=SWEEP 7/7","W=READY","STOP"])
    r,lines=run_script(work,exe,"settings",root,qdir,script)
    # READY's place: stills in single mode, then sweeps of 7 in HDR mode.
    counts=[l for l in lines if l.startswith("LEFT ")]
    assert counts[:2]==[f"LEFT {LEFT_1GB}",f"LEFT {left(1000000000,7)}"],counts
    assert "APP SETTINGS default: mode SINGLE, output RAW+JPEG, save flash first" in r.stdout,r.stdout
    assert "APP SETTINGS saved: mode HDR, output RAW+JPEG, save flash first" in r.stdout,r.stdout
    assert "straight to the card" not in r.stdout.split("APP SETTINGS closed")[1],r.stdout
    # The corner's mode icon follows the saved mode: one camera, then the pair.
    icons=[l for l in lines if l.startswith("MODE ICON ")]
    assert icons==["MODE ICON SINGLE","MODE ICON SWEEP"],icons
    manual=[l for l in lines if l.startswith("MANUAL ")]
    assert manual==[f"MANUAL {tv} {iso}" for tv,iso in SWEEP6] and lines.count("CAPTURE")==7,lines
    assert "DISPLAY SAVING 7/7" in lines and queued(qdir)==[],lines
    photos=sorted(p.name for p in (root/"photos").iterdir())
    assert len(photos)==15 and photos[-1].endswith("-sweep.csv"),photos
    rows=(root/"photos"/photos[-1]).read_text().splitlines()
    assert [r_.split(",")[2:4] for r_ in rows[1:]]==[[str(tv),str(iso)] for tv,iso in SWEEP6 for _ in range(2)],rows
    assert sorted(p.name for p in (root/"state").iterdir() if p.name.startswith("settings"))==["settings.0"]

    # The next launch starts with the stored settings. SAVE is set to CARD;
    # the sweep then writes straight to the card, without the queue.
    script=";".join(["W=READY",press("MENU"),"W=SETTINGS 1/6|MODE: HDR",press("MENU"),press("MENU"),
        "W=SETTINGS 3/6|SAVE: FLASH",press("CAPTURE"),"W=SAVE TO|- FLASH -",press("MENU"),press("CAPTURE"),
        "W=SETTINGS 3/6|SAVE: CARD",press("POWER"),"W=READY",press("CAPTURE"),"W=SWEEP 7/7",
        "W=READY","STOP"])
    r,lines=run_script(work,exe,"settings-reload",root,qdir,script)
    assert "APP SETTINGS loaded: mode HDR, output RAW+JPEG, save flash first" in r.stdout,r.stdout
    assert "APP SETTINGS saved: mode HDR, output RAW+JPEG, save straight to the card" in r.stdout,r.stdout
    assert ": 7 frames, RAW+JPEG, straight to the card" in r.stdout and lines.count("CAPTURE")==7,r.stdout
    saves=[l for l in lines if l.startswith("SAVE DNG")]
    assert len(saves)==7 and all(str(root/"photos") in l for l in saves),saves
    assert not any(l.startswith("DISPLAY SAVING") for l in lines) and queued(qdir)==[],lines
    assert r.stdout.count("APP SAVED ")==14,r.stdout
    photos=sorted(p.name for p in (root/"photos").iterdir())
    # Two sweeps in the same second share one list (it is appended to).
    dngs=[n for n in photos if n.endswith(".dng")]; csvs=[n for n in photos if n.endswith(".csv")]
    assert len(dngs)==14 and len(csvs)==len({n[:15] for n in dngs}),photos
    assert sum(len((root/"photos"/n).read_text().splitlines())-1 for n in csvs)==28,photos

    # Back to single mode: a click is one still again; a hold is still a sweep.
    script=";".join(["W=READY",press("MENU"),"W=SETTINGS 1/6|MODE: HDR",press("CAPTURE"),press("MENU"),
        press("CAPTURE"),"W=SETTINGS 1/6|MODE: SINGLE",press("POWER"),"W=READY",press("CAPTURE"),
        "W=CAPTURING...","W=READY","STOP"])
    r,lines=run_script(work,exe,"settings-single",root,qdir,script)
    assert lines.count("CAPTURE")==1 and "CAPTURE RAW" not in lines and "SOUND" in lines,lines

    # The delay (5th setting): OFF, 2 S, 5 S, 10 S. With 2 S, a click shows
    # the countdown, then the still is taken; the watch icon appears with
    # the setting. Then 10 S and a press during the countdown cancels it.
    to_delay=[press("MENU"),"W=SETTINGS 1/6|MODE: SINGLE",*[press("MENU")]*4,
              "W=SETTINGS 5/6|DELAY: OFF"]
    script=";".join(["W=READY",*to_delay,press("CAPTURE"),"W=DELAY|- OFF -",press("MENU"),"W=DELAY|- 2 S -",
        press("CAPTURE"),"W=SETTINGS 5/6|DELAY: 2 S",press("POWER"),"W=READY",press("CAPTURE"),
        "W=CAPTURE IN|2","W=CAPTURE IN|1","W=CAPTURING...","W=READY","STOP"])
    r,lines=run_script(work,exe,"settings-delay",root,qdir,script)
    assert "APP SETTINGS saved: mode SINGLE, output RAW+JPEG, save straight to the card, Wi-Fi" in r.stdout,r.stdout
    assert "delay 2 S" in r.stdout and "APP DELAY 2 s before the capture" in r.stdout and "APP DELAY over: capture starts" in r.stdout,r.stdout
    assert [l for l in lines if l.startswith("TIMER ICON")]==["TIMER ICON OFF","TIMER ICON ON"],lines
    assert lines.count("CAPTURE")==1 and lines.index("DISPLAY CAPTURE IN|2")<lines.index("DISPLAY CAPTURING..."),lines
    # A 2 S delay is only the last two seconds: the warning sound, once, on the press.
    assert [l for l in lines if l.startswith("TIMER SOUND")]==["TIMER SOUND LAST2SEC"],lines
    assert lines.index("TIMER SOUND LAST2SEC")<lines.index("DISPLAY CAPTURING..."),lines
    # The next launch still has 2 S. Set 10 S, click, then cancel with a press.
    script=";".join(["W=READY",*to_delay[:2],"W=SETTINGS 1/6|MODE: SINGLE",*[press("MENU")]*4,
        "W=SETTINGS 5/6|DELAY: 2 S",press("CAPTURE"),"W=DELAY|- 2 S -",press("MENU"),"W=DELAY|- 5 S -",press("MENU"),
        "W=DELAY|- 10 S -",press("CAPTURE"),"W=SETTINGS 5/6|DELAY: 10 S",press("POWER"),"W=READY",
        press("CAPTURE"),"W=CAPTURE IN|10","W=CAPTURE IN|9",press("CAPTURE"),"W=READY","STOP"])
    r,lines=run_script(work,exe,"settings-delay-cancel",root,qdir,script)
    assert "APP SETTINGS loaded: mode SINGLE, output RAW+JPEG, save straight to the card, Wi-Fi" in r.stdout and "delay 2 S" in r.stdout,r.stdout
    assert "APP DELAY cancelled" in r.stdout and "APP DELAY over" not in r.stdout and "CAPTURE" not in lines,r.stdout
    # 10 S: a tick on the press and one for every second after it; each new sound ends the last, and a cancel ends the sound.
    assert [l for l in lines if l.startswith("TIMER SOUND")]==["TIMER SOUND TICK","TIMER SOUND STOP","TIMER SOUND TICK","TIMER SOUND STOP"],lines

    # Sounds (6th setting): ON by default; OFF is stored, and with it no sound
    # plays: not the shutter, not the countdown (the 2 S delay is still stored).
    script=";".join(["W=READY",press("MENU"),"W=SETTINGS 1/6|MODE: SINGLE",*[press("MENU")]*4,"W=SETTINGS 5/6|DELAY: 10 S",
        press("CAPTURE"),"W=DELAY|- 10 S -",press("MENU"),press("MENU"),"W=DELAY|- 2 S -",press("CAPTURE"),"W=SETTINGS 5/6|DELAY: 2 S",
        press("MENU"),"W=SETTINGS 6/6|SOUNDS: ON",press("CAPTURE"),"W=SOUNDS|- ON -",press("MENU"),"W=SOUNDS|- OFF -",press("CAPTURE"),"W=SETTINGS 6/6|SOUNDS: OFF",
        press("POWER"),"W=READY",press("CAPTURE"),"W=CAPTURE IN|2","W=CAPTURE IN|1","W=CAPTURING...","W=READY","STOP"])
    r,lines=run_script(work,exe,"settings-sounds-off",root,qdir,script)
    assert "APP SETTINGS saved:" in r.stdout and "delay 2 S, sounds OFF" in r.stdout,r.stdout
    assert lines.count("CAPTURE")==1 and not [l for l in lines if l.startswith("SOUND") or l.startswith("TIMER SOUND")],lines
    # The notes icon: on at the start, gone once the sounds are OFF.
    assert [l for l in lines if l.startswith("NOTES ICON")]==["NOTES ICON ON","NOTES ICON OFF"],lines
    # The next launch still has sounds OFF; turn them back ON and the sound returns.
    script=";".join(["W=READY",press("MENU"),"W=SETTINGS 1/6|MODE: SINGLE",*[press("MENU")]*5,"W=SETTINGS 6/6|SOUNDS: OFF",
        press("CAPTURE"),"W=SOUNDS|- OFF -",press("MENU"),"W=SOUNDS|- ON -",press("CAPTURE"),"W=SETTINGS 6/6|SOUNDS: ON",
        press("POWER"),"W=READY",press("CAPTURE"),"W=CAPTURE IN|2","W=CAPTURE IN|1","W=CAPTURING...","W=READY","STOP"])
    r,lines=run_script(work,exe,"settings-sounds-on",root,qdir,script)
    assert "sounds OFF" in r.stdout.split("APP SETTINGS saved")[0] and "SOUND" in lines and "TIMER SOUND LAST2SEC" in lines,(r.stdout,lines)

    # A Menu press during a capture opens nothing.
    script=";".join(["W=READY",press("CAPTURE"),"W=CAPTURING...",press("MENU"),"W=READY","STOP"])
    r,lines=run_script(work,exe,"settings-busy",root,qdir,script)
    assert "APP SETTINGS opened" not in r.stdout and lines.count("CAPTURE")==1,r.stdout

    # A Power hold inside the settings still turns the camera off.
    trace=work/"settings-power.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="settings-power",
             APP_TEST_SCRIPT="W=READY;K=MENU,1;K=MENU,0;W=SETTINGS 1/6|MODE: SINGLE;"+press("CAPTURE")+";W=MODE|- SINGLE -;K=POWER_HOLD,1")
    r=subprocess.run([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic"],
                     env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    assert r.returncode==1 and lines[-2:]==["POWER SEQUENCE cwd=/ fds=0","DISPLAY ERROR"],(r.stdout,lines)
    assert "DISPLAY POWER OFF" in lines and "APP SETTINGS saved" not in r.stdout,(r.stdout,lines)
    (work/"poweroff-copy").unlink(); (work/"poweroff.log").unlink()

    # Maintenance overrides for unattended trials, not stored: a sweep
    # straight to the card without a flash queue.
    root2=work/"override"
    r,lines=run_script(work,exe,"override",root2,None,"",
                       ("--sweep-once","--sweep-save","card"))
    assert lines.count("CAPTURE")==7 and ": 7 frames, RAW+JPEG, straight to the card" in r.stdout,r.stdout
    assert not (root2/"state"/"settings.0").exists()


def output_scenarios(work,exe):
    # All menu choices fit on the two-line display, cycle RAW/JPEG/BOTH, and persist.
    root=work/"output-menu"
    script=";".join(["W=READY",press("MENU"),"W=SETTINGS 1/6|MODE: SINGLE",press("MENU"),"W=OUTPUT 2/6|RAW+JPEG",
        press("CAPTURE"),"W=OUTPUT|- RAW+JPEG -",press("MENU"),"W=OUTPUT|- RAW -",press("MENU"),"W=OUTPUT|- JPEG -",
        press("CAPTURE"),"W=OUTPUT 2/6|JPEG",press("POWER"),"W=READY","STOP"])
    r,lines=run_script(work,exe,"output-menu",root,None,script)
    assert "output JPEG" in r.stdout and [l for l in lines if l.startswith("RAW ICON")]==["RAW ICON ON","RAW ICON OFF"],(r.stdout,lines)
    r,lines=run_script(work,exe,"output-reload",root,None,"",("--once",))
    assert "APP SETTINGS loaded: mode SINGLE, output JPEG" in r.stdout and lines.count("CAPTURE JPEG")==1,r.stdout
    assert sorted(p.suffix for p in (root/"photos").iterdir())==[".jpg"]
    # Each output applies identically to a still, a direct sweep, and a flash-first sweep.
    for output in ("raw","jpeg","raw+jpeg"):
        for route in ("single","card","flash"):
            tag="output-"+output.replace("+","-")+"-"+route
            root=work/tag; qdir=work/(tag+"-flash")/"queue"; qdir.parent.mkdir()
            extra=("--once",) if route=="single" else ("--sweep-once","--sweep-save",route)
            r,lines=run_script(work,exe,tag,root,qdir,"",(*extra,"--output",output,"--sounds","off"))
            count=1 if route=="single" else 7
            capture={"raw":"CAPTURE RAW","jpeg":"CAPTURE JPEG","raw+jpeg":"CAPTURE"}[output]
            assert lines.count(capture)==count and lines.count("FREE")==count,(output,route,lines)
            photos=list((root/"photos").iterdir())
            dngs=sorted(p for p in photos if p.suffix==".dng"); jpgs=sorted(p for p in photos if p.suffix==".jpg")
            assert len(dngs)==(count if output!="jpeg" else 0) and len(jpgs)==(count if output!="raw" else 0),(output,route,photos)
            if output=="raw+jpeg": assert [p.stem for p in dngs]==[p.stem for p in jpgs],photos
            assert queued(qdir)==[] and not [l for l in lines if "SOUND" in l],lines
            assert ("RAW ICON OFF" in lines)==(output=="jpeg"),lines
            assert not (root/"state"/"settings.0").exists(),"maintenance override must not persist"
            if route!="single":
                rows=next(p for p in photos if p.suffix==".csv").read_text().splitlines()[1:]
                expected=[]
                for k,(tv,iso) in enumerate(SWEEP6,1):
                    files=[p.name for p in dngs+jpgs if p.stem.endswith(f"-{k:05d}")]
                    expected.extend(f"{n},{k},{tv},{iso},{tv}/10000000,125" for n in files)
                assert sorted(rows)==sorted(expected),(rows,expected)
                use_queue=route=="flash" and output!="jpeg"
                assert any(l.startswith("DISPLAY SAVING") for l in lines)==use_queue,lines
                assert all((str(qdir) if use_queue else str(root/"photos")) in l for l in lines if l.startswith("SAVE ")),lines


    # Recovery admits the complete pair's actual size, even when the current
    # setting is RAW-only. A large orphan JPEG must not consume the card reserve.
    root=work/"output-recover-space"; qdir=work/"output-recover-space-q"; qdir.mkdir()
    stamp="20261108-120000"
    (qdir/f"{stamp}-1.dng").write_bytes(b"DNG\n\n")
    with open(qdir/f"{stamp}-1.jpg","wb") as f:
        f.write(b"\xff\xd8"); f.seek(40*1024*1024-2); f.write(b"\xff\xd9")
    trace=work/"output-recover-space.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="once")
    r=subprocess.run([str(exe),"--root",str(root),"--free-bytes",str(SPACE_RESERVE+33732608+40960+1024),
        "--event-device","synthetic","--queue",str(qdir),"--queue-free-bytes","5000000000","--once","--output","raw"],
        env=env,capture_output=True,text=True,timeout=30)
    assert r.returncode==1 and "APP ERROR card space" in r.stdout,(r.stdout,r.stderr)
    assert not list((root/"photos").iterdir()) and len(queued(qdir))==2
    assert "CAPTURE RAW" not in trace.read_text()

    # --store-settings: stores the overrides and exits (no screen, no capture);
    # the next run uses them. Refused while the app holds the storage lock.
    import fcntl
    root3=work/"raw-store"; trace=work/"raw-store.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="raw-store")
    store=[str(exe),"--root",str(root3),"--store-settings"]
    r=subprocess.run(store+["--sounds","off","--raw","off"],env=env,capture_output=True,text=True,timeout=30)
    assert r.returncode==0 and "APP SETTINGS stored: mode SINGLE, output JPEG" in r.stdout and "sounds OFF" in r.stdout,(r.stdout,r.stderr)
    assert not trace.exists() and (root3/"state"/"settings.0").exists() and not list((root3/"photos").iterdir())
    r,lines=run_script(work,exe,"raw-store-run",root3,None,"",("--once",))
    assert "APP SETTINGS loaded: mode SINGLE, output JPEG" in r.stdout and "sounds OFF" in r.stdout,r.stdout
    assert lines.count("CAPTURE JPEG")==1 and not [l for l in lines if "SOUND" in l],lines
    before=(root3/"state"/"settings.0").read_bytes(),(root3/"state"/"settings.1").exists()
    with open(root3/"state"/"storage.lock","w") as held:
        fcntl.flock(held,fcntl.LOCK_EX|fcntl.LOCK_NB)
        r=subprocess.run(store+["--sounds","on"],env=env,capture_output=True,text=True,timeout=30)
    assert r.returncode==1 and "APP SETTINGS NOT STORED: storage already in use" in r.stdout,(r.stdout,r.stderr)
    assert ((root3/"state"/"settings.0").read_bytes(),(root3/"state"/"settings.1").exists())==before
    # A JPEG-only still on the last of the card: 1 SHOT, then CARD FULL.
    free=SPACE_RESERVE+JPEG_BUDGET+30000   # this build's JPEG still charges 44,112 bytes
    assert left(free,raw=False)=="1" and left(free-44112,raw=False)=="CARD FULL"
    trace=work/"raw-left.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="raw-left",
             APP_TEST_SCRIPT=";".join(["W=READY",press("CAPTURE"),"W=CAPTURING...","W=READY","STOP"]))
    r=subprocess.run([str(exe),"--root",str(work/"raw-left"),"--free-bytes",str(free),"--event-device","synthetic","--raw","off"],
                     env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    assert r.returncode==0 and [l for l in lines if l.startswith("LEFT ")]==["LEFT 1","LEFT CARD FULL"],(r.stdout,lines)


ROUTE_HEADER="Iface\\tDestination\\tGateway \\tFlags\\tRefCnt\\tUse\\tMetric\\tMask\\t\\tMTU\\tWindow\\tIRTT\\n"


def status_scenario(work,exe):
    """The status corner follows files changed while the app runs (read every
    100 ms in this build). Paths are under a fake root."""
    st=work/"status-root"
    files={"sys/class/power_supply/battery/capacity":"87\n","sys/class/power_supply/battery/status":"Discharging\n",
           "sys/class/power_supply/ac/online":"0\n","sys/class/power_supply/usb/online":"0\n",
           "sys/class/net/wlan0/operstate":"up\n","proc/1/comm":"systemd\n","proc/250/comm":"sh\n",
           # The camera on the home network, 6 November.
           "proc/net/route":ROUTE_HEADER.replace("\\t","\t").replace("\\n","\n")+
               "wlan0\t00000000\t0100A8C0\t0003\t0\t0\t0\t00000000\t0\t0\t0\n"
               "wlan0\t0000A8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n"}
    for name,text in files.items():
        (st/name).parent.mkdir(parents=True,exist_ok=True); (st/name).write_text(text)
    battery="sys/class/power_supply/battery"
    script=";".join(["W=READY","S=home 87 0",
        f"F={battery}/status=Charging\\n","S=home 87 1",
        f"F={battery}/status=Full\\n","S=home 87 0",            # full, no cable: no bolt
        "F=sys/class/power_supply/ac/online=1\\n","S=home 87 1",  # full on a cable
        "F=proc/net/route="+ROUTE_HEADER+"wlan0\\t002BA8C0\\t00000000\\t0001\\t0\\t0\\t0\\t00FFFFFF\\t0\\t0\\t0\\n",
        "S=off 87 1",                                           # the network, no hostapd yet
        "F=proc/250/comm=hostapd\\n","S=ap 87 1",
        f"F={battery}/capacity=9\\n","S=ap 9 1",
        f"F={battery}/capacity=abc\\n","S=ap -1 0",
        "F=proc/250/comm=sh\\n","S=off -1 0","W=READY","STOP"])
    trace=work/"status.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="status",APP_TEST_SCRIPT=script,
             APP_TEST_STATUS_ROOT=str(st))
    r=subprocess.run([str(exe),"--root",str(work/"status"),"--free-bytes","1000000000","--event-device","synthetic",
                      "--status-root",str(st)],env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    assert r.returncode==0 and "SCRIPT STOP" in lines,(r.returncode,r.stdout,r.stderr,lines[-20:])
    statuses=[l for l in lines if l.startswith("STATUS ")]
    assert statuses==["STATUS home 87 0","STATUS home 87 1","STATUS home 87 0","STATUS home 87 1","STATUS off 87 1",
                      "STATUS ap 87 1","STATUS ap 9 1","STATUS ap -1 0","STATUS off -1 0"],statuses
    # Each change redraws the same text; the UI log line is for text changes only.
    assert lines.count("DISPLAY READY")==len(statuses),lines
    # The side LEDs (0 orange, 1 green): off, yellow while Charging, off for Full
    # without a cable, green for Full on a cable, and off again when the app ends.
    assert [l for l in lines if l.startswith("LED ")]==[
        "LED 0 0","LED 1 0","LED 0 255","LED 1 0","LED 0 0","LED 1 0","LED 0 0","LED 1 255","LED 0 0","LED 1 0"],lines
    assert r.stdout.count(f"APP UI {LEFT_1GB}")==1,r.stdout
    log=[l for l in r.stdout.splitlines() if l.startswith("APP STATUS ")]
    assert log==["APP STATUS Wi-Fi home network, battery 87%","APP STATUS Wi-Fi home network, battery 87%, charging",
                 "APP STATUS Wi-Fi home network, battery 87%","APP STATUS Wi-Fi home network, battery 87%, charging",
                 "APP STATUS Wi-Fi none, battery 87%, charging","APP STATUS Wi-Fi access point, battery 87%, charging",
                 "APP STATUS Wi-Fi access point, battery 9%, charging","APP STATUS Wi-Fi access point, battery unreadable",
                 "APP STATUS Wi-Fi none, battery unreadable"],log


def animation_scenario(work,td_includes):
    """The battery icon's walls rise frame by frame while charging, and the
    screen is redrawn for the frames only then (a build with 40 ms frames)."""
    exe=work/"app-frames"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-O1","-g","-Wall","-Wextra","-Werror",
                    "-DG360_STATUS_EVERY_MS=100u","-DG360_CHARGE_FRAME_MS=40u","-DG360_ALERT_EVERY_MS=100u",
                    f'-DG360_STATUS_ROOT="{work/"no-status"}"',f'-DG360_MOTION_HISTORY="{work/"no-motion"}"',f'-DG360_WL_PATH="{work/"wl"}"',
                    f'-DG360_WIFI_SCRIPT="{work/"wifi-join.sh"}"',f'-DG360_WIFI_FILE="{work/"wifi"}"',f'-DG360_REMOTE_SOCKET="{work/"app.sock"}"',
                    "-DG360_QUEUE_FRAME_BYTES=UINT64_C(5)",
                    "-fsanitize=address,undefined","-fno-sanitize-recover=all","-pthread",
                    *["-I"+str(p) for p in td_includes],str(ROOT/"src/app/main.c"),
                    str(ROOT/"src/app/storage.c"),str(ROOT/"src/app/queue.c"),str(ROOT/"src/app/wear.c"),
                    str(ROOT/"src/app/settings.c"),str(ROOT/"src/app/status.c"),str(ROOT/"src/app/alerts.c"),str(ROOT/"src/app/wifi.c"),str(ROOT/"src/app/remote.c"),
                    str(ROOT/"src/app/preview.c"),*preview_flags(work),
                    str(ROOT/"src/app/power_relaunch.c"),str(ROOT/"src/app/motion.c"),str(ROOT/"src/lib/g360cap/g360_pose.c"),str(work/"stub.c"),"-lm","-o",str(exe)],check=True)
    st=work/"anim-root"
    header=ROUTE_HEADER.replace("\\t","\t").replace("\\n","\n")
    files={"sys/class/power_supply/battery/capacity":"50\n","sys/class/power_supply/battery/status":"Charging\n",
           "sys/class/power_supply/ac/online":"1\n","sys/class/power_supply/usb/online":"0\n",
           "sys/class/net/wlan0/operstate":"down\n","proc/1/comm":"systemd\n","proc/net/route":header}
    for name,text in files.items():
        (st/name).parent.mkdir(parents=True,exist_ok=True); (st/name).write_text(text)
    battery="sys/class/power_supply/battery"
    script=";".join(["W=READY","P=700",f"F={battery}/status=Discharging\\n","S=off 50 0","P=300","STOP"])
    trace=work/"anim.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="status",APP_TEST_SCRIPT=script,
             APP_TEST_STATUS_ROOT=str(st),APP_TEST_FRAMES="1")
    r=subprocess.run([str(exe),"--root",str(work/"anim"),"--free-bytes","1000000000","--event-device","synthetic",
                      "--status-root",str(st)],env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    assert r.returncode==0 and "SCRIPT STOP" in lines,(r.returncode,r.stdout,r.stderr,lines[-20:])
    frames=[int(l.split()[1]) for l in lines if l.startswith("FRAME ")]
    off=max(i for i,l in enumerate(lines) if l.startswith("FRAME "))
    # Several full turns while charging, each frame following the last (0..5, 0..5),
    # then frame 0 once and no more redraws after the charger goes.
    charging=frames[:-1] if frames[-1]==0 else frames
    assert len(charging)>=7,frames
    assert all(b==(a+1)%6 for a,b in zip(charging,charging[1:])),frames
    assert frames[-1]==0 and lines.index("STATUS off 50 0")<off+1,frames
    assert lines.count("STATUS home 50 1")==0 and "STATUS off 50 1" in lines,lines


def alerts_scenario(work,exe):
    """Battery, charger and Wi-Fi sounds, played by their own thread as the
    status files change (alerts.h has the rules)."""
    st=work/"alerts-root"
    header=ROUTE_HEADER.replace("\\t","\t").replace("\\n","\n")
    home=header+"wlan0\t00000000\t0100A8C0\t0003\t0\t0\t0\t00000000\t0\t0\t0\n"+\
         "wlan0\t0000A8C0\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n"
    files={"sys/class/power_supply/battery/capacity":"87\n","sys/class/power_supply/battery/status":"Discharging\n",
           "sys/class/power_supply/ac/online":"0\n","sys/class/power_supply/usb/online":"0\n",
           "sys/class/net/wlan0/operstate":"up\n","proc/1/comm":"systemd\n","proc/250/comm":"sh\n","proc/net/route":home}
    for name,text in files.items():
        (st/name).parent.mkdir(parents=True,exist_ok=True); (st/name).write_text(text)
    battery="sys/class/power_supply/battery"
    ap_route="F=proc/net/route="+ROUTE_HEADER+"wlan0\\t002BA8C0\\t00000000\\t0001\\t0\\t0\\t0\\t00FFFFFF\\t0\\t0\\t0\\n"
    script=";".join(["W=READY","S=home 87 0","A=1",              # on the home network at start: connected
        f"F={battery}/status=Charging\\n","F=sys/class/power_supply/ac/online=1\\n","S=home 87 1","A=2",   # a charger goes in
        f"F={battery}/capacity=9\\n","S=home 9 1","P=400",                # low, but charging: silent
        f"F={battery}/status=Discharging\\n","F=sys/class/power_supply/ac/online=0\\n","S=home 9 0","A=5",  # unplugged at 9%: 3 x
        "P=400",
        f"F={battery}/capacity=87\\n","S=home 87 0","P=300",
        f"F={battery}/capacity=20\\n","S=home 20 0","A=6",     # 20%: low battery once
        f"F={battery}/capacity=19\\n","S=home 19 0","P=400",    # no repeat
        ap_route,"F=proc/250/comm=hostapd\\n","S=ap 19 0","A=7",   # left the home network
        "P=400",                                               # access point, nobody on it: silent
        "F=wl-out=assoclist AA:BB:CC:DD:EE:01\\n","A=8",         # someone connects
        "F=wl-out=assoclist AA:BB:CC:DD:EE:01\\nassoclist AA:BB:CC:DD:EE:02\\n","A=9",   # a second one
        "F=wl-out=assoclist AA:BB:CC:DD:EE:02\\n","A=10",      # one leaves
        "F=wl-out=","A=11",                                    # the last leaves
        "F=proc/250/comm=sh\\n","F=proc/net/route="+home.replace("\n","\\n").replace("\t","\\t"),"S=home 19 0","A=12",  # home again
        "F=proc/net/route="+ROUTE_HEADER,"S=off 19 0","A=13","STOP"])  # the network is lost
    trace=work/"alerts.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="alerts",APP_TEST_SCRIPT=script,
             APP_TEST_STATUS_ROOT=str(st))
    r=subprocess.run([str(exe),"--root",str(work/"alerts"),"--free-bytes","1000000000","--event-device","synthetic",
                      "--status-root",str(st)],env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    assert r.returncode==0 and "SCRIPT STOP" in lines,(r.returncode,r.stdout,r.stderr,lines[-20:])
    played=[l[len("SOUND ALERT "):] for l in lines if l.startswith("SOUND ALERT ")]
    assert played==(["BT_connected.wav","charger_connect.wav"]+["13_N_NoBattery.wav"]*3+
        ["low_battery.wav","BT_disconnected.wav","BT_connected.wav","BT_connected.wav","BT_disconnected.wav",
         "BT_disconnected.wav","BT_connected.wav","BT_disconnected.wav"]),played
    assert r.stdout.count("APP ALERT ")==len(played),r.stdout

    # Sounds OFF: the same status changes play nothing (and are not replayed later).
    off=work/"alerts-off-root"
    for name,text in files.items():
        (off/name).parent.mkdir(parents=True,exist_ok=True); (off/name).write_text(text)
    script=";".join(["W=READY","S=home 87 0","A=1",press("MENU"),"W=SETTINGS 1/6|MODE: SINGLE",*[press("MENU")]*5,
        "W=SETTINGS 6/6|SOUNDS: ON",press("CAPTURE"),"W=SOUNDS|- ON -",press("MENU"),"W=SOUNDS|- OFF -",press("CAPTURE"),
        "W=SETTINGS 6/6|SOUNDS: OFF",press("POWER"),"W=READY",
        f"F={battery}/capacity=9\\n","S=home 9 0","P=500",f"F={battery}/status=Charging\\n","F=sys/class/power_supply/ac/online=1\\n",
        "S=home 9 1","P=500","STOP"])
    trace=work/"alerts-off.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="alerts",APP_TEST_SCRIPT=script,APP_TEST_STATUS_ROOT=str(off))
    r=subprocess.run([str(exe),"--root",str(work/"alerts-off"),"--free-bytes","1000000000","--event-device","synthetic",
                      "--status-root",str(off)],env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    assert r.returncode==0 and "SCRIPT STOP" in lines,(r.returncode,r.stdout,r.stderr,lines[-20:])
    played=[l[len("SOUND ALERT "):] for l in lines if l.startswith("SOUND ALERT ")]
    assert played==["BT_connected.wav"] and r.stdout.count("APP ALERT ")==1,(played,r.stdout)


def remote_scenario(work,exe):
    """The web page's requests, through `app --remote`, against a running app."""
    import json
    root=work/"remote"; calls=work/"remote.calls"; trace=work/"remote.txt"
    (work/"wifi").unlink(missing_ok=True)
    # Once auto photos every 61 s count down in minutes: Power stops them.
    # Then Menu opens the settings for a while, and Power closes them.
    script=";".join(["W=READY","C=AUTO IN|1:0","P=500",press("POWER"),"W=READY","P=300",press("MENU"),
                     "P=1500",press("POWER"),"W=READY"])
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="remote",APP_TEST_SCRIPT=script,APP_TEST_WIFI=str(calls))
    app=subprocess.Popen([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic"],
                         env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)

    # The page's CGI, built for this socket.
    cgi=work/"g360-remote"
    subprocess.run([os.environ.get("CC","cc"),"-std=c11","-O1","-g","-Wall","-Wextra","-Werror",
                    f'-DG360_REMOTE_SOCKET="{work/"app.sock"}"',f"-DG360_PREVIEW_PORT={PREVIEW_PORT}",
                    "-fsanitize=address,undefined","-fno-sanitize-recover=all",
                    "-I"+str(ROOT/"src/app"),"-I"+str(ROOT/"src/lib/capture-session"),
                    str(ROOT/"src/app/remote_cgi.c"),str(ROOT/"src/app/remote.c"),"-o",str(cgi)],check=True)

    def ask(query):
        r=subprocess.run([str(cgi)],env=dict(os.environ,QUERY_STRING=query),capture_output=True,timeout=10)
        head,sep,body=r.stdout.decode().partition("\r\n\r\n")
        assert r.returncode==0 and sep and head=="Content-Type: application/json\r\nCache-Control: no-store",(query,r)
        assert body.endswith("\n") and body.count("\n")==1,(query,r)
        return json.loads(body)

    def until(test,query="cmd=state",seconds=10):
        end=time.time()+seconds
        while time.time()<end:
            s=ask(query)
            if test(s): return s
            time.sleep(.05)
        raise AssertionError((query,s))

    try:
        s=until(lambda s:s.get("state")=="ready")
        assert s["app"]=="on" and s["ok"] and s["error"]=="" and s["menu"] is False and s["screen"]==LEFT_1GB,s
        assert s["settings"]=={"mode":"single","output":"raw+jpeg","save":"flash","wifi":"ap","delay":0,"sounds":"on"},s
        assert s["wifi_home"] is False and s["left"]==int(LEFT_1GB) and s["left_unit"]=="shots" and s["auto"]=={"every":0,"taken":0,"next":0},s
        assert s["saved"]==0 and s["countdown"]==0 and s["network"]=="none",s
        # Settings: stored at once, shown on the camera.
        s=ask("cmd=set&output=raw"); assert s["ok"] and s["settings"]["output"]=="raw",s
        s=ask("cmd=set&output=raw%2Bjpeg"); assert s["ok"] and s["settings"]["output"]=="raw+jpeg",s
        s=ask("cmd=set&raw=off"); assert s["ok"] and s["settings"]["output"]=="jpeg" and s["screen"]==left(1000000000,raw=False),s
        s=ask("cmd=set&shots=6"); assert not s["ok"] and s["error"]=="bad request" and "shots" not in s["settings"],s
        s=ask("cmd=set&delay=2"); assert s["ok"] and s["settings"]["delay"]==2,s
        s=ask("cmd=set&raw=maybe"); assert not s["ok"] and s["error"]=="bad request" and s["settings"]["output"]=="jpeg",s
        # Wi-Fi is shown, never changed from the remote.
        for value in ("off","ap","home"):
            s=ask("cmd=set&wifi="+value); assert not s["ok"] and s["error"]=="bad request" and s["settings"]["wifi"]=="ap",s
        assert wifi_calls(calls,1)==["apply ap"]  # at start only (not chosen, no file)
        # Capture with a delay: the countdown, as a click; a second capture or
        # a setting meanwhile is refused; cancel ends it.
        s=ask("cmd=capture"); assert s["ok"] and s["state"]=="ready" and s["countdown"]==2 and s["screen"]=="CAPTURE IN\n2",s
        s=ask("cmd=capture"); assert not s["ok"] and s["error"]=="countdown running",s
        s=ask("cmd=set&mode=hdr"); assert not s["ok"] and s["error"]=="countdown running" and s["settings"]["mode"]=="single",s
        s=ask("cmd=cancel"); assert s["ok"] and s["countdown"]==0,s
        s=ask("cmd=cancel"); assert not s["ok"] and s["error"]=="no countdown",s
        # The countdown runs out: the capture starts.
        s=ask("cmd=capture"); assert s["ok"] and s["countdown"]==2,s
        s=until(lambda s:s["saved"]==1 and s["state"]=="ready")
        s=ask("cmd=set&delay=0"); assert s["ok"],s
        # Capture now; a request while it runs is refused as busy.
        s=ask("cmd=capture"); assert s["ok"] and s["state"]=="capturing" and s["screen"]=="CAPTURING...",s
        s=ask("cmd=set&raw=on"); assert not s["ok"] and s["error"]=="busy" and s["settings"]["output"]=="jpeg",s
        s=until(lambda s:s["saved"]==2 and s["state"]=="ready")
        # A sweep from single mode (the hold): 6 JPEG frames.
        s=ask("cmd=capture&sweep=1"); assert s["ok"] and s["state"]=="sweeping",s
        s=until(lambda s:s["saved"]==3 and s["state"]=="ready")
        # Auto photos with the delay overlapping: every 3 s, delay 2 s. The
        # first press at once (its countdown); the next press is due 1 s after
        # the capture starts, so its countdown ends 3 s after the first.
        s=ask("cmd=set&delay=2"); assert s["ok"],s
        s=ask("cmd=auto&every=3"); assert s["ok"] and s["auto"]["every"]==3 and s["auto"]["taken"]==1 and s["countdown"]==2,s
        assert s["screen"]=="CAPTURE IN\n2",s
        s=until(lambda s:s["saved"]==4 and s["state"]=="ready")
        # Between shots the screen counts down to the next press.
        assert s["auto"]["next"]<=1 and s["screen"]=="AUTO IN\n1",s
        s=until(lambda s:s["auto"]["taken"]==2)
        assert s["countdown"] in (1,2),s
        # Stopping auto photos cancels their countdown.
        s=ask("cmd=auto&every=0"); assert s["ok"] and s["auto"]["every"]==0 and s["countdown"]==0 and s["saved"]==4,s
        # Every 3 s with no delay: the screen counts down to the capture, and
        # the 2 s warning plays before it.
        s=ask("cmd=set&delay=0"); assert s["ok"],s
        s=ask("cmd=auto&every=3"); assert s["ok"] and s["auto"]["taken"]==1,s
        s=until(lambda s:s["state"]=="ready" and s["screen"]=="AUTO IN\n3")
        s=until(lambda s:s["auto"]["taken"]==2)
        s=ask("cmd=auto&every=0"); assert s["ok"],s
        s=until(lambda s:s["saved"]==6 and s["state"]=="ready")
        # Every 61 s: minutes on the screen; Power (the script) stops them.
        s=ask("cmd=auto&every=61"); assert s["ok"] and s["auto"]["taken"]==1,s
        s=until(lambda s:s["auto"]["every"]==0)
        assert s["auto"]["taken"]==1 and s["saved"]==7,s
        # The settings open on the camera: a setting is refused until closed.
        s=until(lambda s:s["menu"])
        s=ask("cmd=set&mode=hdr"); assert not s["ok"] and s["error"]=="settings open on the camera",s
        s=until(lambda s:not s["menu"])
        s=ask("cmd=set&mode=hdr"); assert s["ok"] and s["settings"]["mode"]=="hdr" and s["left_unit"]=="sweeps",s
        # Shutter and ISO (the remote's, in memory only) for single stills.
        assert s["exposure"]=={"shutter":0,"iso":0},s
        s=ask("cmd=set&shutter=250"); assert not s["ok"] and s["error"]=="bad request",s
        s=ask("cmd=set&shutter=159"); assert not s["ok"] and s["error"]=="single mode only" and s["exposure"]["shutter"]==0,s
        s=ask("cmd=set&iso=400"); assert not s["ok"] and s["error"]=="single mode only" and s["exposure"]["iso"]==0,s
        s=ask("cmd=set&iso=auto"); assert not s["ok"] and s["error"]=="bad request",s
        s=ask("cmd=set&mode=single"); assert s["ok"] and s["settings"]["mode"]=="single",s
        base=s["saved"]
        # Auto is auto for both: no ISO until a shutter is set.
        s=ask("cmd=set&iso=800"); assert not s["ok"] and s["error"]=="set a shutter first" and s["exposure"]=={"shutter":0,"iso":0},s
        # A set shutter starts at ISO 100; the ISO can then be changed and is kept for the next shutter.
        s=ask("cmd=set&shutter=159"); assert s["ok"] and s["exposure"]=={"shutter":159,"iso":100},s
        s=ask("cmd=set&iso=800"); assert s["ok"] and s["exposure"]=={"shutter":159,"iso":800},s
        s=ask("cmd=capture"); assert s["ok"],s
        s=until(lambda s:s["saved"]==base+1 and s["state"]=="ready")
        s=ask("cmd=set&shutter=69"); assert s["ok"] and s["exposure"]=={"shutter":69,"iso":800},s
        s=ask("cmd=capture"); assert s["ok"],s
        s=until(lambda s:s["saved"]==base+2 and s["state"]=="ready")
        # Back to auto: ISO auto too, and the next still is plain automatic.
        s=ask("cmd=set&shutter=auto"); assert s["ok"] and s["exposure"]=={"shutter":0,"iso":0},s
        s=ask("cmd=set&iso=200"); assert not s["ok"] and s["error"]=="set a shutter first",s
        s=ask("cmd=capture"); assert s["ok"],s
        s=until(lambda s:s["saved"]==base+3 and s["state"]=="ready")
        s=ask("cmd=set&shutter=101"); assert s["ok"] and s["exposure"]=={"shutter":101,"iso":100},s
        # A sweep (the button) puts both back to automatic.
        s=ask("cmd=capture&sweep=1"); assert s["ok"],s
        s=until(lambda s:s["saved"]==base+4 and s["state"]=="ready")
        assert s["exposure"]=={"shutter":0,"iso":0},s
        # So does the HDR mode, after a choice made in single mode.
        s=ask("cmd=set&shutter=69"); assert s["ok"] and s["exposure"]["shutter"]==69,s
        s=ask("cmd=set&mode=hdr"); assert s["ok"] and s["exposure"]=={"shutter":0,"iso":0},s
    finally:
        app.terminate()
        out,err=app.communicate(timeout=20)
    lines=trace.read_text().splitlines()
    assert app.returncode==0,(app.returncode,out,err,lines[-30:])
    # Off: no socket, and the client says so.
    assert not (work/"app.sock").exists()
    assert ask("cmd=state")=={"app":"off"}
    assert "APP REMOTE listening on" in out and "APP REMOTE cmd=set&raw=off" in out and "APP REMOTE cmd=capture\n" in out,out
    assert "APP DELAY cancelled (remote)" in out and "APP AUTO every 3 s" in out and "APP DELAY cancelled (auto photos stopped)" in out,out
    assert out.count("APP AUTO stopped (remote) after 2 presses")==2 and "APP AUTO stopped (Power pressed) after 1 presses" in out,out
    # The 2 s warning: only with no delay, between that run's two presses.
    runs=out.split("APP AUTO every ")[1:]
    assert len(runs)==3 and "2 s warning" not in runs[0] and "2 s warning" not in runs[2],out
    assert runs[1].index("APP AUTO press 1")<runs[1].index("APP AUTO 2 s warning")<runs[1].index("APP AUTO press 2"),out
    assert "DISPLAY AUTO IN|1:01" in lines or "DISPLAY AUTO IN|1:00" in lines,lines
    assert "RAW ICON OFF" in lines and "TIMER ICON ON" in lines and "MODE ICON SWEEP" in lines,lines
    # Exposures: 1/1000 (TV 159) at ISO 800, 1/20 (TV 69) at ISO 800, each followed by the automatic
    # exposure; the plain automatic still sets nothing; the 7-frame sweep after them sets its own.
    def around_capture(k):
        """The capture after trace line K comes before the next AUTO."""
        j=lines.index("AUTO",k+1)
        assert any(l.startswith("CAPTURE") for l in lines[k+1:j]),lines[k-2:j+2]
    for m in ("MANUAL 159 800","MANUAL 69 800"): around_capture(lines.index(m))
    # Their Exif gets the real time from that TV; the automatic still keeps the firmware's.
    for m,tv in (("MANUAL 159 800",159),("MANUAL 69 800",69)):
        j=lines.index(m); assert f"REAL TV {tv}" in lines[j:lines.index("AUTO",j+1)],lines[j:j+12]
    assert "APP EXPOSURE TV 159 (1/1000 s) ISO 800" in out and "APP EXPOSURE TV 69 (1/20 s) ISO 800" in out and "shutter auto" not in out,out
    # Delay sounds came from the remote press and the auto press, like a click's.
    assert lines.count("TIMER SOUND LAST2SEC")>=3,lines
    # The stored settings survive: a fresh launch loads them.
    r=subprocess.run([str(exe),"--root",str(root),"--store-settings"],capture_output=True,text=True,timeout=10)
    assert "APP SETTINGS loaded: mode HDR, output JPEG, save flash first, Wi-Fi AP (not chosen), delay OFF, sounds ON" in r.stdout,r.stdout
    photos=sorted(p.name for p in (root/"photos").iterdir())
    assert not any(p.endswith(".dng") for p in photos),photos


def preview_scenario(work,exe):
    """The live preview: asked for by the page, MJPEG on its port, stopped for
    a capture and started again, stopped by the page and when nobody watches."""
    import json
    import socket
    root=work/"preview"; trace=work/"preview.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="preview",APP_TEST_SCRIPT="W=READY")
    app=subprocess.Popen([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic"],
                         env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
    cgi=work/"g360-remote"  # built by remote_scenario for the same socket

    def ask(query):
        r=subprocess.run([str(cgi)],env=dict(os.environ,QUERY_STRING=query),capture_output=True,timeout=10)
        return json.loads(r.stdout.decode().partition("\r\n\r\n")[2])

    def until(test,query="cmd=state",seconds=10):
        end=time.time()+seconds
        while time.time()<end:
            s=ask(query)
            if test(s): return s
            time.sleep(.05)
        raise AssertionError((query,s))

    def lines():
        return trace.read_text().splitlines() if trace.exists() else []

    def viewer():
        c=socket.create_connection(("127.0.0.1",PREVIEW_PORT),timeout=5)
        c.sendall(b"GET /preview.mjpg HTTP/1.1\r\nHost: camera\r\n\r\n")
        return c, c.makefile("rb")

    def part(f):
        """One multipart part: the JPEG's bytes."""
        assert f.readline()==b"--g360frame\r\n"
        assert f.readline()==b"Content-Type: image/jpeg\r\n"
        length=f.readline(); assert length.startswith(b"Content-Length: "),length
        assert f.readline()==b"\r\n"
        body=f.read(int(length[16:])); assert f.read(2)==b"\r\n"
        return body

    try:
        s=until(lambda s:s.get("state")=="ready")
        assert s["preview"]=={"state":"off","port":PREVIEW_PORT,"viewers":0,"frames":0,"error":""},s
        # Nobody may watch before it's asked for.
        c,f=viewer(); assert f.readline().startswith(b"HTTP/1.0 503"); c.close()
        r=subprocess.run([str(cgi)],env=dict(os.environ,QUERY_STRING="cmd=stream"),capture_output=True,timeout=15)
        assert r.returncode==0 and (r.stdout.startswith(b"HTTP/1.0 503") or r.stdout.startswith(b"Status: 503")),r.stdout
        s=ask("cmd=preview&on=2"); assert not s["ok"] and s["error"]=="bad request",s
        s=ask("cmd=preview&on=1"); assert s["ok"] and s["preview"]["state"] in ("starting","on"),s
        s=until(lambda s:s["preview"]["state"]=="on")
        c,f=viewer()
        head=b""
        while not head.endswith(b"\r\n\r\n"): head+=f.read(1)
        assert head.startswith(b"HTTP/1.0 200 OK\r\n") and b"multipart/x-mixed-replace; boundary=g360frame" in head,head
        first=part(f); second=part(f)
        # Half the 64 x 32 frame, 4:2:0, from the fake encoder.
        assert first[:2]==b"\xff\xd8" and first[-2:]==b"\xff\xd9" and first[2:6]==bytes([32,0,16,0]),first
        assert second[7:11]!=first[7:11],(first,second)
        assert until(lambda s:s["preview"]["viewers"]==1)["preview"]["frames"]>=2
        # The page's CGI relays the same stream (cmd=stream), status line and all.
        relay=subprocess.Popen([str(cgi)],env=dict(os.environ,QUERY_STRING="cmd=stream&_=1"),stdout=subprocess.PIPE)
        head=b""
        while not head.endswith(b"\r\n\r\n"): head+=relay.stdout.read(1)
        assert head.startswith(b"HTTP/1.0 200 OK\r\n") and b"boundary=g360frame" in head,head
        relayed=part(relay.stdout); assert relayed[:2]==b"\xff\xd8" and relayed[2:6]==bytes([32,0,16,0]),relayed
        until(lambda s:s["preview"]["viewers"]==2)
        relay.kill(); relay.wait()
        until(lambda s:s["preview"]["viewers"]==1)
        # A capture stops the camera's preview first and starts it after.
        s=ask("cmd=capture"); assert s["ok"],s
        s=until(lambda s:s["saved"]==1 and s["state"]=="ready" and s["preview"]["state"]=="on")
        t=lines(); cap=t.index("CAPTURE")
        assert "PREVIEW STOP" in t[:cap] and "PREVIEW START" in t[cap:],t
        part(f)  # the same viewer keeps getting frames
        # The page stops it: the viewer's stream ends.
        s=ask("cmd=preview&on=0"); assert s["ok"] and s["preview"]["state"]=="off",s
        rest=f.read(); assert len(rest)<1000,len(rest)  # what was in flight, then the close
        c.close()
        until(lambda s:s["preview"]["viewers"]==0)
        until(lambda s:lines().count("PREVIEW STOP")==2)
        # Asked for and nobody watches: it stops by itself.
        s=ask("cmd=preview&on=1"); assert s["ok"],s
        until(lambda s:s["preview"]["state"]=="on")
        s=until(lambda s:s["preview"]["state"]=="off",seconds=6)
        until(lambda s:lines().count("PREVIEW STOP")==3)
    finally:
        app.terminate()
        out,err=app.communicate(timeout=20)
    t=lines()
    assert app.returncode==0,(app.returncode,out,err,t[-30:])
    assert f"APP PREVIEW listening on port {PREVIEW_PORT}" in out and "APP REMOTE cmd=preview&on=1" in out,out
    assert "APP PREVIEW paused for the capture: ok" in out and "APP PREVIEW stopped (no viewer for 1 s)" in out,out
    assert t.count("PREVIEW START")==3 and t.count("PREVIEW STOP")==3 and t.index("PREVIEW STOP",t.index("PREVIEW START",t.index("CAPTURE")))<t.index("CLOSE"),t


def wifi_calls(path,count):
    """The applies the stand-in recorded; they run in the background."""
    end=time.time()+10
    while time.time()<end:
        calls=path.read_text().splitlines() if path.exists() else []
        if len(calls)>=count: break
        time.sleep(.05)
    time.sleep(.3)
    return path.read_text().splitlines() if path.exists() else []


def wifi_scenarios(work,exe):
    root=work/"wifi-settings"; netfile=work/"wifi"
    netfile.write_text("net\npassword1\n")

    def run(name,script,count,extra_env=None):
        calls=work/(name+".calls")
        trace=work/(name+".txt")
        env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO=name,APP_TEST_SCRIPT=script,
                 APP_TEST_WIFI=str(calls),**(extra_env or {}))
        r=subprocess.run([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic"],
                         env=env,capture_output=True,text=True,timeout=60)
        lines=trace.read_text().splitlines()
        assert r.returncode==0 and "SCRIPT STOP" in lines,(name,r.returncode,r.stdout,r.stderr,lines[-20:])
        return r,wifi_calls(calls,count)

    # Not chosen yet, with the network file: HOME, applied at start. Then
    # OFF, AP, and AP once more (choosing it again applies it again).
    open_wifi=[press("MENU"),"W=SETTINGS 1/6|MODE: SINGLE",*[press("MENU")]*3]
    script=";".join(["W=READY",*open_wifi,"W=SETTINGS 4/6|WIFI: HOME",press("CAPTURE"),"W=WIFI|- HOME -",
        press("MENU"),"W=WIFI|- OFF -",press("CAPTURE"),"W=SETTINGS 4/6|WIFI: OFF",
        press("CAPTURE"),"W=WIFI|- OFF -",press("MENU"),"W=WIFI|- AP -",press("CAPTURE"),"W=SETTINGS 4/6|WIFI: AP",
        press("CAPTURE"),"W=WIFI|- AP -",press("CAPTURE"),"W=SETTINGS 4/6|WIFI: AP",press("POWER"),"W=READY","STOP"])
    r,calls=run("wifi-choose",script,4)
    assert calls==["apply home","apply off","apply ap","apply ap"],(calls,r.stdout)
    assert "APP SETTINGS default: mode SINGLE, output RAW+JPEG, save flash first, Wi-Fi HOME (not chosen)" in r.stdout,r.stdout
    assert "Wi-Fi OFF, delay OFF, sounds ON\n" in r.stdout and r.stdout.count("APP SETTINGS saved")==2,r.stdout
    assert "APP WIFI start: apply home started" in r.stdout and "APP WIFI chosen: apply ap started" in r.stdout,r.stdout

    # The next launch applies the stored AP; HOME is offered with the file.
    script=";".join(["W=READY",*open_wifi,"W=SETTINGS 4/6|WIFI: AP",press("CAPTURE"),"W=WIFI|- AP -",
        press("MENU"),"W=WIFI|- HOME -",press("CAPTURE"),"W=SETTINGS 4/6|WIFI: HOME",press("POWER"),"W=READY","STOP"])
    r,calls=run("wifi-home",script,2)
    assert calls==["apply ap","apply home"] and "loaded: mode SINGLE, output RAW+JPEG, save flash first, Wi-Fi AP" in r.stdout,(calls,r.stdout)

    # Without the network file, the stored HOME is AP, and HOME isn't offered.
    netfile.unlink()
    script=";".join(["W=READY",*open_wifi,"W=SETTINGS 4/6|WIFI: AP",press("CAPTURE"),"W=WIFI|- AP -",
        press("MENU"),"W=WIFI|- OFF -",press("MENU"),"W=WIFI|- AP -",press("POWER"),"W=READY","STOP"])
    r,calls=run("wifi-nofile",script,1)
    assert calls==["apply ap"] and "Wi-Fi AP" in r.stdout,(calls,r.stdout)

    # Restarted by wifi-join.sh itself (after join, revert or ap-cycle): no
    # apply at start, so a revert is not undone.
    netfile.write_text("net\npassword1\n")
    r,calls=run("wifi-skip","W=READY;STOP",0,{"G360_WIFI_SKIP_APPLY":"1"})
    assert calls==[] and "APP WIFI start: not applied" in r.stdout,(calls,r.stdout)
    # Maintenance runs never touch Wi-Fi.
    calls_file=work/"wifi-once.calls"
    env=dict(os.environ,APP_TEST_TRACE=str(work/"wifi-once.txt"),APP_TEST_SCENARIO="once",APP_TEST_WIFI=str(calls_file))
    r=subprocess.run([str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic","--once"],
                     env=env,capture_output=True,text=True,timeout=60)
    assert r.returncode==0 and wifi_calls(calls_file,0)==[] and "APP WIFI" not in r.stdout,(r.stdout,r.stderr)


def left_scenario(work,exe):
    """One still left: the screen says 1 SHOT, then CARD FULL after it."""
    free=SPACE_RESERVE+SHOT_BUDGET+50000   # this build's still charges 85,088 bytes
    assert left(free)=="1" and left(free-85088)=="CARD FULL"
    trace=work/"left.txt"
    env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO="left",
             APP_TEST_SCRIPT=";".join(["W=READY",press("CAPTURE"),"W=CAPTURING...","W=READY","STOP"]))
    r=subprocess.run([str(exe),"--root",str(work/"left"),"--free-bytes",str(free),"--event-device","synthetic"],
                     env=env,capture_output=True,text=True,timeout=60)
    lines=trace.read_text().splitlines()
    assert r.returncode==0 and "SCRIPT STOP" in lines,(r.returncode,r.stdout,lines[-20:])
    assert [l for l in lines if l.startswith("LEFT ")]==["LEFT 1","LEFT CARD FULL"],lines
    assert "APP UI 1" in r.stdout and "APP UI CARD FULL" in r.stdout and lines.count("CAPTURE")==1,r.stdout


def wear_dir(work,name,life_a,life_b,pre_eol=1):
    data=bytearray(512); data[192]=7; data[267]=pre_eol; data[268]=life_a; data[269]=life_b
    d=work/name; (d/"mmc0:0001").mkdir(parents=True); (d/"mmc0:0001"/"ext_csd").write_text(data.hex()+"\n")
    for other in ("clock","ios","regs","state"): (d/other).write_text("x")
    return d


def queued(qdir):
    return sorted(p.name for p in qdir.iterdir()) if qdir.exists() else []


def sweeps(work,exe,copy,log):
    for scenario in ("sweep","sweep-once","sweep-fail","power-sweep","recover","hold-no-space",
                     "sweep-worn","sweep-worn-out","worn-click"):
        root=work/scenario; trace=work/(scenario+".txt"); qdir=work/(scenario+"-flash")/"queue"
        qdir.parent.mkdir()
        env=dict(os.environ,APP_TEST_TRACE=str(trace),APP_TEST_SCENARIO=scenario)
        args=[str(exe),"--root",str(root),"--free-bytes","1000000000","--event-device","synthetic",
              "--queue",str(qdir),"--queue-free-bytes","5000000000" if scenario!="hold-no-space" else "1000"]
        initial_left=LEFT_1GB if scenario=="recover" else left(1000000000,output="raw")
        if scenario!="recover": args += ["--output","raw"]
        if scenario in ("sweep-once","sweep-fail","sweep-worn","sweep-worn-out"):
            args.append("--sweep-once"); env["APP_TEST_ONCE"]="1"
        life={"sweep":(1,1),"sweep-worn":(1,4),"sweep-worn-out":(11,3),"worn-click":(4,2)}.get(scenario)
        if life: args+=["--wear-dir",str(wear_dir(work,scenario+"-mmc",*life))]
        if scenario=="recover":
            args.append("--once"); env["APP_TEST_ONCE"]="1"
            qdir.mkdir()
            (qdir/"20261105-101010-1.dng").write_bytes(b"DNG\n\n")
            (qdir/"20261105-101010-2.dng").write_bytes(b"DN")          # interrupted write
            (qdir/"20261105-101010-3.dng").write_bytes(b"DNG\n\n")
            (qdir/"20261105-101010.csv").write_text("g360-sweep 1\nframe,tv,iso,exposure_numerator,exposure_denominator,actual_iso\n"
                                                    "1,48,1600,1,8,1600\n3,48,100,1,8,100\n")
        r=subprocess.run(args,env=env,capture_output=True,text=True,timeout=30)
        lines=trace.read_text().splitlines()
        expected=1 if scenario in ("sweep-fail","power-sweep") else 0
        assert r.returncode==expected,(scenario,r.returncode,r.stdout,r.stderr,lines)
        assert lines.count("OPEN")==1 and lines.count("CLOSE")==1,lines
        raw=lines.count("CAPTURE RAW")
        manual=[l for l in lines if l.startswith("MANUAL ")]
        photos=sorted(p.name for p in (root/"photos").iterdir()) if (root/"photos").exists() else []
        if scenario in ("sweep","sweep-once"):
            # Hold past a second: the hint, then 7 RAW-only frames at the sweep's
            # exposures, each saved to the flash before the next, then moved.
            if scenario=="sweep":
                assert lines.index("DISPLAY HDR SWEEP")<lines.index("BUTTON 0 dropped=0")<lines.index("SOUND HDR START"),lines
                assert "UI POLLING WHILE BUSY" in lines,lines
            assert raw==7 and "CAPTURE" not in lines and lines.count("FREE")==7,lines
            assert manual==[f"MANUAL {tv} {iso}" for tv,iso in SWEEP],manual
            assert lines.count("AUTO")==2 and lines.index("SOUND HDR END")>max(i for i,l in enumerate(lines) if l=="CAPTURE RAW"),lines
            assert lines.index("SOUND HDR START")<lines.index("CAPTURE RAW"),lines
            # One AF sound before each frame's capture; the end sound as soon as the
            # last frame is captured (the camera can move), before it is written out.
            shots=[i for i,l in enumerate(lines) if l=="SOUND SHOT"]
            caps=[i for i,l in enumerate(lines) if l=="CAPTURE RAW"]
            assert len(shots)==7 and all(a<b for a,b in zip(shots,caps)) and all(c<n for c,n in zip(caps[:-1],shots[1:])),lines
            first_saves=[i for i,l in enumerate(lines) if l.startswith("SAVE DNG ")][:7]
            assert caps[-1]<lines.index("SOUND HDR END")<first_saves[-1],lines
            descs=[l for l in lines if l.startswith("DESC ")]
            assert [d.split()[-1] for d in descs]==[f"{k}/7" for k in range(1,8)],descs
            frames=[l for l in lines if l.startswith("SAVE DNG ")]
            assert len(frames)==7 and all(str(work/(scenario+"-flash")/"queue") in l for l in frames),frames
            for k in range(1,8): assert f"DISPLAY SWEEP {k}/7" in lines and f"DISPLAY SAVING {k}/7" in lines,lines
            assert lines.index("SOUND HDR END")<lines.index("DISPLAY SAVING 1/7")<lines.index("DISPLAY SAVING 7/7")<len(lines)-1-lines[::-1].index("DISPLAY READY"),lines
            dngs=[n for n in photos if n.endswith(".dng")]
            assert len(dngs)==7 and len(photos)==8 and not any(n.endswith(".jpg") for n in photos),photos
            stamp=dngs[0][:15]
            assert all(n.startswith(stamp) for n in photos) and f"{stamp}-sweep.csv" in photos,photos
            assert [n[16:21] for n in dngs]==[f"{k:05d}" for k in range(1,8)],dngs
            assert all((root/"photos"/n).read_bytes()==b"DNG\n\n" for n in dngs)
            rows=(root/"photos"/f"{stamp}-sweep.csv").read_text().splitlines()
            assert rows[0]=="file,frame,tv,iso,exposure_time,actual_iso" and len(rows)==8,rows
            for k,(tv,iso) in enumerate(SWEEP,1):
                assert rows[k]==f"{stamp}-{k:05d}.dng,{k},{tv},{iso},{tv}/10000000,125",rows
            assert queued(qdir)==[],queued(qdir)
            assert r.stdout.count("APP SAVED ")==7 and "APP SWEEP MOVED" in r.stdout,r.stdout
            # The burst sound closes the sweep: after the capture end sound and the last save.
            assert lines.count("SOUND SWEEP DONE")==1 and lines.index("SOUND HDR END")<lines.index("DISPLAY SAVING 7/7")<lines.index("SOUND SWEEP DONE"),lines
            if scenario=="sweep":
                assert "APP FLASH LIFE A 1 B 1 pre-EOL 1\n" in r.stdout and not any(l.startswith("DISPLAY FLASH") for l in lines),r.stdout
            else:  # no debugfs on the host: logged, no warning
                assert "APP FLASH LIFE unreadable from /sys/kernel/debug/mmc0" in r.stdout,r.stdout
            assert r.stdout.index("APP SWEEP SAVED")<r.stdout.index("APP FLASH LIFE")<r.stdout.rindex(f"APP UI {initial_left}"),r.stdout
        elif scenario=="sweep-fail":
            # Frame 3 fails: frames 1-2 and their exposures stay on the flash
            # for the next start; automatic exposure is set back.
            assert raw==3 and lines.count("FREE")==3 and lines.count("AUTO")==2 and "SOUND HDR END" not in lines and "SOUND SWEEP DONE" not in lines,lines
            stamp=queued(qdir)[0][:15]
            assert queued(qdir)==[f"{stamp}-1.dng",f"{stamp}-2.dng",f"{stamp}.csv"],queued(qdir)
            assert (qdir/f"{stamp}.csv").read_text().splitlines()[2:]==["1,41,100,41,10000000,125","2,59,100,59,10000000,125"]
            assert photos==[] and "APP ERROR sweep capture" in r.stdout,(photos,r.stdout)
        elif scenario=="power-sweep":
            # Power held during frame 3: that frame finishes, the rest is skipped,
            # the frames stay on the flash, then the power-off runs.
            assert raw in (2,3) and lines.count("FREE")==raw and lines.count("AUTO")==2,lines
            stamp=queued(qdir)[0][:15]
            assert queued(qdir)==[f"{stamp}-{k}.dng" for k in range(1,raw+1)]+[f"{stamp}.csv"],queued(qdir)
            assert len((qdir/f"{stamp}.csv").read_text().splitlines())==2+raw
            assert photos==[] and lines[-2:]==["POWER SEQUENCE cwd=/ fds=0","DISPLAY ERROR"],(photos,lines)
            copy.unlink(); log.unlink()
        elif scenario=="recover":
            # Leftover frames move before READY; the interrupted one is dropped.
            assert r.stdout.index("APP SWEEP MOVED 20261105-101010")<r.stdout.index(f"APP UI {initial_left}"),r.stdout
            assert lines.index("OPEN")<lines.index("DISPLAY READY"),lines
            assert photos==["20261105-101010-00001.dng","20261105-101010-00002.dng","20261105-101010-sweep.csv"]+\
                [n for n in photos if n.endswith(("-00003.dng","-00003.jpg"))],photos
            assert len(photos)==5 and lines.count("CAPTURE")==1 and raw==0,photos
            assert (root/"photos"/"20261105-101010-sweep.csv").read_text().splitlines()[1:]==[
                "20261105-101010-00001.dng,1,48,1600,1/8,1600","20261105-101010-00002.dng,3,48,100,1/8,100"]
            assert queued(qdir)==[] and "1 incomplete dropped" in r.stdout,r.stdout
        elif scenario in ("sweep-worn","sweep-worn-out","worn-click"):
            # The flash's life estimate at 4 or more: after the sweep the screen
            # warns instead of READY; a Capture press clears it.
            text="FLASH WORN" if scenario=="sweep-worn-out" else "FLASH WEAR 4"
            assert raw==7 and len(photos)==8,(lines,photos)
            assert lines.index("DISPLAY SAVING 7/7")<lines.index(f"DISPLAY {text}"),lines
            assert lines.count("DISPLAY READY")==(2 if scenario=="worn-click" else 1),lines
            a,b={"sweep-worn":(1,4),"sweep-worn-out":(11,3),"worn-click":(4,2)}[scenario]
            assert f"APP FLASH LIFE A {a} B {b} pre-EOL 1: warning" in r.stdout,r.stdout
            if scenario=="worn-click":
                after=lines[lines.index(f"DISPLAY {text}"):]
                assert after.index("BUTTON 1 dropped=0")<after.index("DISPLAY READY"),lines
        elif scenario=="hold-no-space":
            # Without flash space a sweep goes straight to the card.
            assert "APP FLASH QUEUE unavailable (flash space): sweeps go straight to the card" in r.stdout,r.stdout
            assert "APP SWEEP the flash queue is unavailable: saving straight to the card" in r.stdout,r.stdout
            assert raw==7 and "CAPTURE" not in lines and not any(l.startswith("DISPLAY SAVING") for l in lines),lines
            assert all(str(root/"photos") in l for l in lines if l.startswith("SAVE DNG")),lines
            assert len(photos)==8 and queued(qdir)==[],photos


if __name__=="__main__": main()
