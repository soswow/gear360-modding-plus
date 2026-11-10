#!/usr/bin/env python3
"""Tabulate RTOS liveview-restart milestones for every recorded trial.

Run from output/decompile/rea/. Reads each pass-*/rec-rtoslog.txt (and the
plan-step RTOS logs), finds the last "[restartLiveview] Change" line and prints
the time in ms of each later milestone, plus the time of the last logged line.
"""
import re,glob,sys
MS=[('LVSTART','Rcv cmd    ...ID_LIVEVIEW_START'),('waitEP_end','waitFinishingStillEP_end'),('initLLLV','initLLLV'),('SetNextImage','SetNextImage'),('SensorCb1','SensorCb(1)'),('SensA','Sensor [A] Module'),('SensB','Sensor [B] Module'),('Disable','DisableStream'),('fc1','frame count ->'),('EncEnd','Enc Work is Ended'),('ENC_SET','ENC_SET Ring'),('fcdone','frame count of previous mode is done'),('Enable','] EnableStream'),('Readout','VideoFrameReadout'),('PPstill','PP Path Still'),('SensorCb2','SensorCb(2)')]
def lines(f):
    b=open(f,'rb').read().replace(b'\x00',b'')
    t=re.sub(r'\x1b\[[0-9;]*m','',b.decode('utf-8','replace'))
    for l in t.split('\n'):
        m=re.match(r'\s*\[\s*0:\s*([0-9.]+)\]\s*(\S+)\s*\(A7\s*(\d+)\):\s*(.*)',l)
        if m: yield float(m.group(1)),m.group(4)
print('%-34s'%'run'+''.join('%9s'%k for k,_ in MS)+'   last')
for f in sorted(glob.glob('pass-*/rec-rtoslog.txt')+glob.glob('pass-plan-*/*rtoslog*.txt')):
    L=list(lines(f))
    rs=[i for i,x in enumerate(L) if 'restartLiveview] Change' in x[1]]
    if not rs: continue
    i0=rs[-1]; r=L[i0][0]
    seen={}
    for ts,msg in L[i0:]:
        for k,p in MS:
            if k not in seen and p in msg: seen[k]=(ts-r)*1000
    last=(L[-1][0]-r)*1000
    print('%-34s'%f.split('/')[0][-34:]+''.join(('%9.1f'%seen[k]) if k in seen else '        -' for k,_ in MS)+'  %7.1f'%last)
