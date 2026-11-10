/* Synthetic host-only test double. This does not model firmware behavior. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __APPLE__
#define SYMBOL(s) "_" s
#else
#define SYMBOL(s) s
#endif
static int token, beauty;
static void mark(const char *s) {
    const char *path = getenv("BOOTSTRAP_TRACE");
    FILE *f = path ? fopen(path, "a") : NULL;
    if (f) { fprintf(f, "%s\n", s); fclose(f); }
}
static int fail(const char *s) {
    const char *v = getenv("BOOTSTRAP_FAIL");
    return v && strcmp(v, s) == 0;
}
__attribute__((constructor)) static void loaded(void) { mark("load"); }
#ifndef OMIT_INSTANCE
void *stub_instance(void) __asm__(SYMBOL("_ZN11CMCBAdapter8InstanceEv"));
void *stub_instance(void) { mark("instance"); return fail("instance") ? NULL : &token; }
#endif
#ifndef OMIT_INIT
int stub_initialize(void *) __asm__(SYMBOL("_ZN11CMCBAdapter10InitializeEv"));
int stub_initialize(void *p) { mark(p == &token ? "init:correct" : "init:wrong"); return fail("init") ? -7 : 0; }
#endif
#ifndef OMIT_BEAUTY
void *stub_beauty(void) __asm__(SYMBOL("_ZN19CBeautyFaceIf_Linux11GetInstanceEv"));
void *stub_beauty(void) { mark("beauty"); return fail("beauty") ? NULL : &beauty; }
#endif
int mm_camera_create(unsigned *handle) { mark("create"); *handle = 1; return 0; }
int mm_camera_destroy(unsigned handle) { (void)handle; mark("destroy"); return 0; }
int mm_camera_capture_still(void) { return 0; }
unsigned RegCallbackClient(void) { mark("register"); return 0; }
