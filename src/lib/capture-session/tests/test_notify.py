#!/usr/bin/env python3
"""Host-only tests of the fixed notify diagnostic with synthetic bridge/time."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
cc = os.environ.get('CC', 'cc')
with tempfile.TemporaryDirectory(prefix='notify-test-') as tmp:
    out = Path(tmp)
    bridge = out / 'bridge.so'
    original = (root / 'capture-shot.c').read_text()
    needle = '"/usr/lib/libmulticore-bridge.so"'
    assert original.count(needle) == 1
    (out / 'capture-shot.c').write_text(original.replace(needle, '"' + str(bridge) + '"'))
    (out / 'bridge.c').write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int reads;
static void trace(const char *s) { FILE *f = fopen(getenv("TRACE"), "a"); fputs(s, f); fclose(f); }
#ifndef NO_SEND
void Mcb_send(unsigned short c, unsigned short a, unsigned short n, unsigned char *p) {
  char b[128]; snprintf(b,sizeof b,"send:%u:%u:%u:%s\n",c,a,n,p ? "non-null" : "null"); trace(b);
}
#endif
#ifndef NO_ALIVE
unsigned char _ZN16CMulticoreBridge18IsDestinationAliveEv(void) { const char *m=getenv("MODE"); reads++; trace("alive\n");
  if (!strcmp(m,"baseline")) return 1;
  if (!strcmp(m,"rise") && reads >= 3) return 1;
  return 0;
}
#endif
''')
    (out / 'harness.c').write_text(r'''
#include "capture_libc.h"
#include <stdlib.h>
#include <string.h>
static int clock_calls, sleeps;
static int fake_clock(int id, capture_timespec *t) {
  const char *m=getenv("MODE"); (void)id; clock_calls++;
  if (!strcmp(m,"clock_before") || (!strcmp(m,"clock_after") && clock_calls==2)) return -1;
  t->tv_sec=sleeps / 20; t->tv_nsec=(sleeps % 20) * 50000000L; return 0;
}
static int fake_sleep(const capture_timespec *a, capture_timespec *b) {
  (void)a; (void)b; if (!strcmp(getenv("MODE"),"sleep")) return -1; sleeps++; return 0;
}
#define clock_gettime fake_clock
#define nanosleep fake_sleep
#define main capture_shot_main
#include "capture-shot.c"
#undef main
int main(void) {
  if (!strcmp(getenv("MODE"),"cli")) {
    char *argv[] = {"capture-shot", "--probe-notify", 0};
    return capture_shot_main(2, argv);
  }
  if (!strcmp(getenv("MODE"),"classify")) {
    if (strcmp(notify_change(1,0), "fall; cause unattributed")) return 11;
    return 0;
  }
  return probe_notify_alive() == -1 ? 10 : 0;
}
''')
    inc = [f'-I{root}', f'-I{root / "../still-request"}', f'-I{root / "../encoded-set"}']
    exe = out / 'harness'
    sources = [root / 'capture_session.c', root / 'capture_session_library.c',
               root / '../still-request/still_request.c', root / '../encoded-set/encoded_set.c']
    subprocess.run([cc, '-std=c99', '-Wall', '-Wextra', '-Werror', *inc,
                    str(out / 'harness.c'), *(str(p) for p in sources), '-ldl', '-pthread', '-o', str(exe)], check=True)
    def run(mode, omit=None):
        cmd = [cc, '-shared', '-fPIC', str(out / 'bridge.c'), '-o', str(bridge)]
        if omit: cmd.insert(1, '-D' + omit)
        subprocess.run(cmd, check=True)
        trace = out / 'trace'; trace.unlink(missing_ok=True)
        result = subprocess.run([str(exe)], env=dict(os.environ, MODE=mode, TRACE=str(trace)), capture_output=True, text=True)
        return result.returncode, trace.read_text().splitlines() if trace.exists() else [], result.stderr
    for omit in ('NO_SEND', 'NO_ALIVE'):
        rc, trace, msg = run('rise', omit)
        assert rc == 10 and not trace and 'missing required symbol' in msg
    for mode, expected in [('baseline', 'unusable nonzero baseline'), ('clock_before', 'clock unavailable')]:
        rc, trace, msg = run(mode)
        assert not any(x.startswith('send:') for x in trace) and expected in msg, (mode, rc, trace, msg)
    for mode, expected in [('clock_after', 'clock failed after send'), ('sleep', 'sleep interrupted or failed')]:
        rc, trace, msg = run(mode)
        assert rc == 10 and trace.count('send:16:0:0:null') == 1 and expected in msg and '5000 ms' not in msg
    rc, trace, msg = run('cli')
    assert rc == 2 and not trace and 'require --probe' in msg
    rc, trace, msg = run('classify')
    assert rc == 0 and not trace
    rc, trace, msg = run('rise')
    assert rc == 0 and trace.count('send:16:0:0:null') == 1 and 'before 0 after 1' in msg and 'unattributed' in msg
    rc, trace, msg = run('steady')
    assert rc == 0 and trace.count('send:16:0:0:null') == 1 and 'before 0 after 0 elapsed 5000 ms or more' in msg
    print('notify synthetic bridge checks passed')
