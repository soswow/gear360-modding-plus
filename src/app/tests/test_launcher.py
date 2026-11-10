#!/usr/bin/env python3
"""Launcher lifecycle against an isolated fake camera; never contacts a device."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parents[1]
SOURCE = (HERE / "start.sh").read_text()
CAPTURE_HASH = "fba8947358ff1fd666fca794d2af77e77a61964e5dd022c04c499ae7b4cc8cc7"
BRIDGE_HASH = "9129761c6ee9f6b62968d3a8307a8b5fbf02e1493323658c42d9ebe587673a20"

HARNESS = r'''
import fcntl, hashlib, json, os, pathlib, sys, time
root = pathlib.Path(os.environ['LAUNCHER_TEST_ROOT'])
cfg = json.loads((root/'config.json').read_text())
name = pathlib.Path(sys.argv[0]).name
args = sys.argv[1:]
def trace(event):
    with (root/'trace.jsonl').open('a') as f: f.write(json.dumps(event)+'\n')
trace([name]+args)
if name == 'uname':
    print(cfg.get('arch','armv7l'))
elif name == 'df':
    flash = args[-1] == str(root/'usr')
    if cfg.get('df_fail') or (flash and cfg.get('flash_df_fail')): sys.exit(1)
    print('Filesystem 1B-blocks Used Available Use% Mounted on')
    print('card 9999999999 0 '+str(cfg.get('flash_free' if flash else 'free',1000000000))+' 0% card')
elif name == 'openssl':
    path=args[-1]
    if path == '/usr/lib/libcapture-fw-prod.so': digest='CAPTURE_HASH'
    elif path == '/usr/lib/libmulticore-bridge.so': digest='BRIDGE_HASH'
    else: digest=hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()
    if cfg.get('library_bad') and path.startswith('/usr/lib/'): digest='0'*64
    print('SHA256('+path+')= '+digest)
elif name == 'cat':
    for arg in args:
        path=pathlib.Path(arg)
        if path == root/'proc/self/cgroup' and cfg.get('escape_works',True) and (root/'cgroup/tasks').is_file() and (root/'cgroup/tasks').read_text():
            print('1:name=systemd:/system.slice/gear360-mod')
        else:
            try: sys.stdout.write(path.read_text())
            except OSError: sys.exit(1)
elif name == 'sleep':
    pass
elif name == 'flock':
    try: fcntl.flock(int(args[-1]),fcntl.LOCK_EX|fcntl.LOCK_NB)
    except OSError: sys.exit(1)
elif name == 'systemctl':
    if args == ['--no-block','stop','di-camera-app.service']:
        if cfg.get('require_escape') and not (root/'cgroup/tasks').read_text(): sys.exit(3)
        if cfg.get('stop_fail'): sys.exit(1)
        (root/'stopped').touch()
        if not cfg.get('keep_stock'):
            (root/'proc/123/comm').unlink(missing_ok=True)
        if cfg.get('change_boot'):
            (root/'proc/stat').write_text('btime 2\n')
    elif args == ['show','-p','ActiveState','-p','MainPID','di-camera-app.service']:
        if cfg.get('state_fail') or (cfg.get('state_fail_after_stop') and (root/'stopped').exists()): sys.exit(1)
        state=cfg.get('state','inactive')
        if (root/'stopped').exists() and not cfg.get('hang_stop'): state='inactive'
        pid=cfg.get('pid',123 if state=='active' else 0)
        if state=='inactive' and (root/'stopped').exists(): pid=0
        print('ActiveState='+state+'\nMainPID='+str(pid))
    else: sys.exit('unexpected systemctl arguments')
elif name == 'g360-app' and args == ['--loading']:
    for fd in (7,8,9): os.fstat(fd)
    trace(['loading', (root/'stopped').exists()])
elif name == 'g360-app':
    queue = [] if cfg.get('no_queue') else ['--queue',str(root/'usr/g360-app/queue'),'--queue-free-bytes',str(cfg.get('flash_free',1000000000))]
    assert args == ['--root',str(root/'card/G360'),'--free-bytes',str(cfg.get('free',1000000000)-5242880),'--log',str(root/'card/G360/state/capture.log')]+queue,args
    for fd in (7,8,9): os.fstat(fd)
    trace(['launched',args,os.environ.get('TZ')])
    while cfg.get('hold_app') and not (root/'release').exists(): time.sleep(.02)
else:
    sys.exit('unexpected mock command '+name)
'''.replace('CAPTURE_HASH',CAPTURE_HASH).replace('BRIDGE_HASH',BRIDGE_HASH)


class Camera:
    def __init__(self, path, **cfg):
        self.path = path
        for name in ('bin','install','locks','proc/self','proc/123','card','cgroup','usr'):
            (path/name).mkdir(parents=True,exist_ok=True)
        self.cfg = cfg
        (path/'config.json').write_text(json.dumps(cfg))
        (path/'proc/stat').write_text('btime 1\n')
        (path/'proc/self/cgroup').write_text('1:name=systemd:/system.slice/'+
            ('di-camera-app.service' if cfg.get('inside') else 'gear360-mod')+'\n')
        (path/'cgroup/tasks').write_text('')
        if cfg.get('state') == 'active': (path/'proc/123/comm').write_text('di-camera-app\n')
        mock = '#!'+sys.executable+'\n'+HARNESS
        for name in ('uname','df','openssl','cat','sleep','flock','systemctl'):
            p=path/'bin'/name; p.write_text(mock); p.chmod(0o755)
        binary=path/'install/g360-app'; binary.write_text(mock); binary.chmod(0o755)
        (path/'install/g360-app.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
        script=SOURCE.replace('PATH=/usr/sbin:/usr/bin:/sbin:/bin',
            'PATH='+shlex.quote(str(path/'bin'))+':/usr/bin:/bin')
        for original,replacement in (
            ('/opt/storage/sdcard',str(path/'card')),
            ('flash=/opt/usr','flash='+shlex.quote(str(path/'usr'))),
            ('/proc/',str(path/'proc')+'/'),
            ('/sys/fs/cgroup/systemd/system.slice/gear360-mod',str(path/'cgroup')),
            ('/tmp/gear360-',str(path/'locks/gear360-')),
            ('/usr/bin/openssl',str(path/'bin/openssl'))):
            script=script.replace(original,replacement)
        self.script=path/'install/start.sh'; self.script.write_text(script)
        self.env=dict(os.environ,LAUNCHER_TEST_ROOT=str(path))

    def run(self,*args):
        return subprocess.run(['/bin/sh',str(self.script),*args],env=self.env,capture_output=True,text=True,timeout=15)

    def trace(self):
        p=self.path/'trace.jsonl'
        return [json.loads(line) for line in p.read_text().splitlines()] if p.exists() else []

    def log(self):
        p=self.path/'card/G360/state/app.log'
        return p.read_text() if p.exists() else ''

    def stopped(self): return (self.path/'stopped').exists()
    def launched(self): return any(x[0]=='launched' for x in self.trace())


def main():
    with tempfile.TemporaryDirectory(prefix='g360-launcher-') as td:
        root=Path(td); index=0
        def new(**cfg):
            nonlocal index
            index+=1
            return Camera(root/str(index),**cfg)
        for cfg in ({},{'state':'active'},{'state':'active','inside':True,'require_escape':True}):
            c=new(**cfg); result=c.run()
            assert result.returncode==0,(cfg,result.stderr,c.log())
            assert c.launched() and c.stopped()==(cfg.get('state')=='active')
            loads = [x[1] for x in c.trace() if x[0] == 'loading']
            assert loads == ([False, True] if cfg.get('state') == 'active' else [False]), loads
            assert [x[0] for x in c.trace()].index('loading') < [x[0] for x in c.trace()].index('launched')
            if cfg.get('inside'): assert (c.path/'cgroup/tasks').read_text().strip().isdigit()
        # Every preflight failure must leave the stock service running.
        for cfg in ({'arch':'x86_64'},{'free':1},{'free':'bad'},{'df_fail':True},
                    {'library_bad':True},{'state':'activating'},{'state':'active','pid':0},
                    {'state_fail':True}):
            c=new(**cfg); result=c.run()
            assert result.returncode!=0 and not c.stopped() and not c.launched(),(cfg,result.stderr,c.log())
        for content in ('invalid\n','0'*64+'\n','a'*63+'\n'):
            c=new(state='active'); (c.path/'install/g360-app.sha256').write_text(content)
            assert c.run().returncode!=0 and not c.stopped() and not c.launched()
        c=new(state='active'); (c.path/'proc/234').mkdir(); (c.path/'proc/234/comm').write_text('g360-shot\n')
        assert c.run().returncode!=0 and not c.stopped()
        for lock in ('controls','screen','experiments'):
            c=new(state='active')
            with (c.path/f'locks/gear360-{lock}.lock').open('w') as f:
                fcntl.flock(f.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
                assert c.run().returncode!=0 and not c.stopped() and not c.launched()
        c=new(state='active',inside=True,escape_works=False)
        assert c.run().returncode!=0 and not c.stopped() and not c.launched()
        c=new(state='active',inside=True); (c.path/'cgroup/tasks').unlink()
        assert c.run().returncode!=0 and not c.stopped()
        for group_text in ('', 'unidentified\n'):
            c=new(state='active'); (c.path/'proc/self/cgroup').write_text(group_text)
            assert c.run().returncode!=0 and not c.stopped() and not c.launched()
        for cfg in ({'stop_fail':True},{'hang_stop':True},{'keep_stock':True},
                    {'change_boot':True},{'state_fail_after_stop':True}):
            c=new(state='active',**cfg); result=c.run()
            assert result.returncode!=0 and not c.launched(),(cfg,result.stderr,c.log())
            if cfg.get('hang_stop'): assert sum(x[0]=='sleep' for x in c.trace())==30
        c=new(); assert c.run('--root','/elsewhere').returncode==2 and not c.trace()
        # The flash queue is optional: without it the app starts with sweeps off.
        c=new(flash_free=2500000000); assert c.run().returncode==0 and c.launched()
        assert (c.path/'usr/g360-app').is_dir()
        c=new(no_queue=True,flash_df_fail=True); result=c.run()
        assert result.returncode==0 and c.launched() and 'sweeps off' in c.log(),(result.stderr,c.log())
        c=new(no_queue=True); (c.path/'usr').rmdir(); (c.path/'usr').write_text('not a directory')
        result=c.run(); assert result.returncode==0 and c.launched() and 'sweeps off' in c.log(),(result.stderr,c.log())
        # Wi-Fi is the app's setting now: the launcher never runs wifi-join.sh,
        # even with the card's network file.
        c=new(state='active'); d=c.path/'card/mods/g360wifi'; d.mkdir(parents=True)
        (d/'wifi-join.sh').write_text('touch "$LAUNCHER_TEST_ROOT/wifi-call"\n')
        (c.path/'card/wifi').write_text('net\npassword1\n')
        result=c.run(); assert result.returncode==0 and c.launched(),(result.stderr,c.log())
        assert not (c.path/'wifi-call').exists(),c.log()
        # Only a valid bundled name changes the app's timezone environment.
        for name, expected in [('Australia/Sydney\n', True), ('../escape\n', False),
                               ('Unknown/Place\n', False), ('Australia/Sydney\nUTC\n', False)]:
            c=new(); zone=c.path/'install/zoneinfo-2026f/Australia/Sydney'
            zone.parent.mkdir(parents=True); zone.write_text('fixture')
            (c.path/'card/timezone').write_text(name)
            assert c.run().returncode==0, c.log()
            launched=next(x for x in c.trace() if x[0]=='launched')
            assert launched[2] == (':'+str(zone) if expected else None), (name,launched,c.log())
        # A successful exec retains every flock until the app actually exits.
        c=new(hold_app=True)
        process=subprocess.Popen(['/bin/sh',str(c.script)],env=c.env,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            deadline=time.monotonic()+8
            while time.monotonic()<deadline and not c.launched(): time.sleep(.02)
            assert c.launched(),'first app did not start'
            result=c.run()
            assert result.returncode!=0 and sum(x[0]=='launched' for x in c.trace())==1
            (c.path/'release').touch()
            assert process.wait(timeout=5)==0
        finally:
            if process.poll() is None: process.terminate(); process.wait(timeout=5)
        print(f'launcher isolated lifecycle, locks, hashes, cgroup escape and failure tests passed ({index} cases)')


if __name__=='__main__': main()
