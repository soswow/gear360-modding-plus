#!/usr/bin/env python3
"""power.c against fake libdevice-node and libdeviced: order of the stock
power-off steps and what each failure does."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
NODE = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void trace(const char *s,int a,int b,int c) {
    FILE *f=fopen(getenv("POWER_TEST_TRACE"),"a"); fprintf(f,s,a,b,c); fputc('\n',f); fclose(f);
}
int device_set_property(int device,int property,int value) {
    const char *fail=getenv("POWER_TEST_FAIL");
    trace("SET %d 0x%x %d",device,property,value);
    if(fail && !strcmp(fail,"flag") && property==0x15) return -1;
    if(fail && !strcmp(fail,"state") && property==4) return -1;
    return 0;
}
'''
DEVICED = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int deviced_request_power_off(void) {
    const char *fail=getenv("POWER_TEST_FAIL");
    FILE *f=fopen(getenv("POWER_TEST_TRACE"),"a"); fputs("DEVICED POWEROFF\n",f); fclose(f);
    return fail && !strcmp(fail,"refused") ? -1 : 4;
}
'''
DRIVER = r'''
#include "power.h"
#include <stdio.h>
int main(void) {
    g360_power_result r;
    int status=g360_power_off(200,&r);
    printf("%d %d %d %d\n",status,r.flag,r.deviced,r.state);
    return 0;
}
'''
CASES = {
    # case: (expected trace, status, flag, deviced, state)
    "ok": (["SET 4 0x15 1", "DEVICED POWEROFF", "SET 4 0x4 0"], -4, 0, 4, 0),
    "refused": (["SET 4 0x15 1", "DEVICED POWEROFF", "SET 4 0x4 0"], -4, 0, -1, 0),
    "flag": (["SET 4 0x15 1"], -2, -1, -1000, -1000),
    "state": (["SET 4 0x15 1", "DEVICED POWEROFF", "SET 4 0x4 0"], -3, 0, 4, -1),
    "no-deviced": (["SET 4 0x15 1", "SET 4 0x4 0"], -4, 0, -1000, 0),
    "no-node": ([], -1, -1000, -1000, -1000),
}


def main():
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory(prefix="g360-power-test-") as td:
        work = Path(td)
        (work / "node.c").write_text(NODE)
        (work / "deviced.c").write_text(DEVICED)
        (work / "driver.c").write_text(DRIVER)
        node, deviced = work / "libdevice-node.so.0.1", work / "libdeviced.so.0.1.0"
        for src, lib in (("node.c", node), ("deviced.c", deviced)):
            subprocess.run([cc, "-shared", "-fPIC", str(work / src), "-o", str(lib)], check=True)
        builds = {}
        for name, node_path, deviced_path in (("both", node, deviced), ("no-deviced", node, work / "missing.so"),
                                               ("no-node", work / "missing.so", deviced)):
            exe = work / f"power-{name}"
            subprocess.run([cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            f'-DG360_POWER_NODE_LIBRARY="{node_path}"',
                            f'-DG360_POWER_DEVICED_LIBRARY="{deviced_path}"',
                            "-I" + str(ROOT / "src/app"), "-I" + str(ROOT / "src/lib/capture-session"),
                            str(ROOT / "src/app/power.c"), str(work / "driver.c"), "-o", str(exe)], check=True)
            builds[name] = exe
        for case, (steps, *values) in CASES.items():
            trace = work / f"{case}.txt"
            env = dict(os.environ, POWER_TEST_TRACE=str(trace), POWER_TEST_FAIL=case)
            exe = builds[case if case in builds else "both"]
            r = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=30, check=True)
            got = trace.read_text().splitlines() if trace.exists() else []
            assert got == steps, (case, got)
            assert [int(x) for x in r.stdout.split()] == values, (case, r.stdout)
    print("power-off order (flag 1, deviced PowerOff, state 0) and failure handling passed")


if __name__ == "__main__":
    main()
