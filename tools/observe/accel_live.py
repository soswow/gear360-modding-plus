#!/usr/bin/env python3
"""Live view of the camera's motion history (needs accelserve running on the camera).

    python3 accel_live.py [--host CAMERA] [--port 7700] [--http 8765]

Sends a keep-alive datagram to the camera every second, receives the samples
accelserve streams back (u32 first_index, u32 count, count*20 raw bytes) and
serves a live chart at http://127.0.0.1:8765/. Prints sample rate and any lost
datagrams (gaps in the ring index) to the terminal. Writes nothing to the camera.
"""
import argparse, collections, json, socket, struct, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

SLOTS = 400000
NAMES = ["w2 hi", "w2 lo", "w3 hi", "w3 lo", "w4 hi", "w4 lo"]  # accel X, accel Y, rot Y, accel Z, rot Z, rot X
samples = collections.deque(maxlen=4000)   # (seq, camera timestamp, [6 values], host arrival time)
lock = threading.Lock()
stats = {"packets": 0, "samples": 0, "lost": 0, "t0": None}


def s16(v):
    return v - 65536 if v & 0x8000 else v


def receiver(sock):
    expect = None
    seq = 0
    while True:
        try:
            data, _ = sock.recvfrom(2048)
        except OSError:
            time.sleep(0.2)
            continue
        if len(data) < 8:
            continue
        first, count = struct.unpack_from("<II", data)
        if len(data) < 8 + 20 * count:
            continue
        if expect is not None and first != expect:
            stats["lost"] += (first - expect) % SLOTS
        expect = (first + count) % SLOTS
        stats["packets"] += 1
        with lock:
            for i in range(count):
                w = struct.unpack_from("<5I", data, 8 + 20 * i)
                vals = [s16(w[2] >> 16), s16(w[2] & 0xFFFF), s16(w[3] >> 16),
                        s16(w[3] & 0xFFFF), s16(w[4] >> 16), s16(w[4] & 0xFFFF)]
                seq += 1
                samples.append((seq, w[0], vals, time.monotonic()))
            stats["samples"] += count


def keepalive(sock, addr):
    while True:
        try:
            sock.sendto(b"hi", addr)
        except OSError:
            pass  # e.g. "No route to host" while the network blips; keep trying
        time.sleep(1)


PAGE = """<!doctype html><meta charset=utf-8><title>Gear 360 motion</title>
<style>body{font:13px system-ui;margin:12px;background:#111;color:#ddd}
canvas{width:100%;height:360px;display:block;background:#1b1b1b;margin-bottom:8px;cursor:pointer}
.chip{background:#111;border:2px solid #ddd;color:#ddd;border-radius:12px;padding:1px 9px;margin-left:6px;font:12px ui-monospace,Menlo,monospace;cursor:pointer}</style>
<div><span id=info style="font-family:ui-monospace,Menlo,monospace;white-space:pre">waiting for data…</span> <span id=chips></span> <button onclick=resetRange()>reset min/max</button> <button onclick=zeroHeading()>zero heading</button> <button onclick=recalGyro()>recalibrate gyro (hold still)</button></div><div style="display:flex;gap:8px;align-items:flex-start"><div id=c style="flex:1 1 55%;min-width:0"></div><div style="flex:1 1 45%;min-width:0"><canvas id=pose3d style="height:728px"></canvas></div></div>
<script>
// channel order after remap: 0 accel X, 1 accel Y, 2 accel Z, 3 rot X, 4 rot Y, 5 rot Z
const ORDER=[0,1,3,5,2,4]; // channel j = raw value ORDER[j] (w2hi,w2lo,w3lo,w4lo,w3hi,w4hi)
const AX=['X','Y','Z'],COL=['#e66','#6c6','#6af'];
const GROUPS=[{name:'accel',ch:[0,1,2],g:true},{name:'rot',ch:[3,4,5],g:false}];  // accel in g, rot in °/s
const P=(v,w)=>String(v).padStart(w,' ');
const F=(G,v)=>G.g?P((v/8192).toFixed(3),7)+' g':P((v*0.07).toFixed(1),7)+' °/s';  // LSM6DS3: ±4 g, 8192 counts/g; gyro ±2000 °/s, 0.07 °/s per count
const WIN=2000,AVG=40;  // 10 s window and 0.2 s average at 200/s
let buf=[],last=0;
GROUPS.forEach((G,gi)=>{G.lo=1e9;G.hi=-1e9;G.on=true;G.snap=null;
  G.cv=document.createElement('canvas');G.cv.width=Math.round(innerWidth*devicePixelRatio);G.cv.height=Math.round(360*devicePixelRatio);document.getElementById('c').append(G.cv);
  const b=document.createElement('button');b.textContent=G.name;b.className='chip';
  const paint=()=>{b.style.opacity=G.on?1:.5;b.style.textDecoration=G.on?'none':'line-through'};
  const tog=()=>{G.on=!G.on;paint()};b.onclick=tog;G.cv.onclick=tog;paint();document.getElementById('chips').append(b)});
function resetRange(){GROUPS.forEach(G=>{G.lo=1e9;G.hi=-1e9})}

// ---- pose: Mahony filter on the LSM6DS3 (accel ±4 g = 8192/g, gyro 0.07 °/s per count).
// Body frame = sensor frame: gyro X,Y,Z share the accel axes, right-handed (fitted on a hand-moved recording).
// q rotates body vectors into the world frame (world Z up). Tilt is held by gravity; heading drifts slowly.
const DEG=Math.PI/180,GSC=0.07*DEG,CAL=300;   // CAL samples (~1.5 s) to measure gyro bias at start
const pose={q:null,bias:[0,0,0],cal:{n:0,s:[0,0,0]},lastT:null,still:0};
const qmul=(a,b)=>[a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3],a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2],a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1],a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0]];
const qrot=(q,v)=>{const r=qmul(qmul(q,[0,v[0],v[1],v[2]]),[q[0],-q[1],-q[2],-q[3]]);return[r[1],r[2],r[3]]};
const qinv=q=>[q[0],-q[1],-q[2],-q[3]];
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2],len=a=>Math.sqrt(dot(a,a)),nrm=a=>{const l=len(a)||1;return a.map(x=>x/l)};
function qFromAccel(a){const n=nrm(a),z=[0,0,1],ax=cross(n,z),s=len(ax),c=dot(n,z);
  if(s<1e-6)return c>0?[1,0,0,0]:[0,1,0,0];const th=Math.atan2(s,c),k=ax.map(x=>x/s);return[Math.cos(th/2),...k.map(x=>x*Math.sin(th/2))]}
function recalGyro(){pose.cal={n:0,s:[0,0,0]}}
function updatePose(s){
  const a=[s[2][0],s[2][1],s[2][2]],g=[s[2][3],s[2][4],s[2][5]];
  let dt=1/203;if(pose.lastT!==null){const d=((s[1]-pose.lastT)>>>0)*1e-6;if(d>0.002&&d<0.02)dt=d}pose.lastT=s[1];
  if(pose.cal.n<CAL){pose.cal.n++;for(let i=0;i<3;i++)pose.cal.s[i]+=g[i];
    if(pose.cal.n===CAL)pose.bias=pose.cal.s.map(x=>x/CAL);
    if(!pose.q)pose.q=qFromAccel(a);if(pose.cal.n<CAL)return}
  const w=g.map((x,i)=>(x-pose.bias[i])*GSC),an=len(a)/8192;
  if(an>0.85&&an<1.15){  // gravity correction only when not accelerating hard
    const e=cross(nrm(a),qrot(qinv(pose.q),[0,0,1])),Kp=1.5;for(let i=0;i<3;i++)w[i]+=Kp*e[i]}
  // slow bias tracking while the camera is still
  if(Math.abs(an-1)<0.03&&len(g.map((x,i)=>x-pose.bias[i]))<20){if(++pose.still>200)for(let i=0;i<3;i++)pose.bias[i]+=0.002*(g[i]-pose.bias[i])}else pose.still=0;
  const q=pose.q,dq=qmul(q,[0,w[0],w[1],w[2]]);
  pose.q=nrm4(q.map((x,i)=>x+0.5*dt*dq[i]))}
const nrm4=q=>{const l=Math.hypot(...q);return q.map(x=>x/l)};
// view: eye on azimuth AZ, elevation EL, looking at the origin, world Z up
const AZ=-60*DEG,EL=22*DEG,EYE=[7*Math.cos(EL)*Math.cos(AZ),7*Math.cos(EL)*Math.sin(AZ),7*Math.sin(EL)];
const VF=nrm(EYE.map(x=>-x)),VR=nrm(cross(VF,[0,0,1])),VU=cross(VR,VF);
function zeroHeading(){if(!pose.q)return;  // turn the world so the front lens (-Y) faces the viewer
  const f=qrot(pose.q,[0,-1,0]),yaw=Math.atan2(f[1],f[0]),want=Math.atan2(EYE[1],EYE[0]),d=want-yaw;
  pose.q=nrm4(qmul([Math.cos(d/2),0,0,Math.sin(d/2)],pose.q))}
// camera body in body coordinates: head (upright means -Z up), grip below; front lens and screen on -Y
function box(x0,x1,y0,y1,z0,z1){const v=[[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],[x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1]];
  return[[0,1,2,3],[4,7,6,5],[0,4,5,1],[2,6,7,3],[1,5,6,2],[0,3,7,4]].map(f=>f.map(i=>v[i]))}
const BODY=[...box(-.55,.55,-.45,.45,-1.15,-.05).map(f=>({f,c:[70,70,78]})),...box(-.24,.24,-.17,.17,-.05,.95).map(f=>({f,c:[55,55,62]}))];
const LENSES=[{y:-.45,n:[0,-1,0],c:'#9cf',label:'front lens'},{y:.45,n:[0,1,0],c:'#567',label:'rear lens'}];
function proj(p,W,H){const d=[p[0]-EYE[0],p[1]-EYE[1],p[2]-EYE[2]],z=dot(d,VF),f=Math.min(W,H*0.75)*1.6;return[W/2+f*dot(d,VR)/z,H*0.55-f*dot(d,VU)/z,z]}
function drawPose(){
  const c=document.getElementById('pose3d'),g=c.getContext('2d'),dpr=devicePixelRatio;
  if(c.width!==Math.round(c.clientWidth*dpr)||c.height!==Math.round(c.clientHeight*dpr)){c.width=Math.round(c.clientWidth*dpr);c.height=Math.round(c.clientHeight*dpr)}
  const W=c.width,H=c.height;g.fillStyle='#1b1b1b';g.fillRect(0,0,W,H);
  // floor grid
  g.strokeStyle='#2c2c2c';g.lineWidth=dpr;
  for(let i=-3;i<=3;i++){for(const [a,b] of [[[i,-3,-1.6],[i,3,-1.6]],[[-3,i,-1.6],[3,i,-1.6]]]){const A=proj(a,W,H),B=proj(b,W,H);g.beginPath();g.moveTo(A[0],A[1]);g.lineTo(B[0],B[1]);g.stroke()}}
  g.font=(13*dpr)+'px ui-monospace,Menlo,monospace';
  if(!pose.q){g.fillStyle='#ddd';g.fillText('waiting for data…',10*dpr,20*dpr);return}
  if(pose.cal.n<CAL){g.fillStyle='#fc4';g.fillText('hold the camera still: measuring gyro bias '+Math.round(100*pose.cal.n/CAL)+'%',10*dpr,20*dpr)}
  const q=pose.q,toW=v=>qrot(q,v);
  // faces, painter's order, simple shading
  const faces=BODY.map(({f,c})=>{const pw=f.map(toW),n=nrm(cross(pw[1].map((x,i)=>x-pw[0][i]),pw[2].map((x,i)=>x-pw[0][i]))),
    ctr=[0,1,2].map(i=>pw.reduce((s,p)=>s+p[i],0)/4),vis=dot(n,ctr.map((x,i)=>EYE[i]-x))>0;
    return{pw,n,vis,depth:dot(ctr.map((x,i)=>x-EYE[i]),VF),c}}).filter(x=>x.vis).sort((a,b)=>b.depth-a.depth);
  for(const F of faces){const sh=0.55+0.45*Math.max(0,dot(F.n,nrm([.4,-.5,.8])));
    g.fillStyle='rgb('+F.c.map(x=>Math.round(x*sh*1.6)).join(',')+')';g.strokeStyle='#999';g.beginPath();
    F.pw.forEach((p,i)=>{const s=proj(p,W,H);i?g.lineTo(s[0],s[1]):g.moveTo(s[0],s[1])});g.closePath();g.fill();g.stroke()}
  for(const L of LENSES){const nw=toW(L.n),ctr=toW([0,L.y,-.6]);if(dot(nw,ctr.map((x,i)=>EYE[i]-x))<=0)continue;
    g.fillStyle=L.c;g.beginPath();for(let i=0;i<=24;i++){const t=i/24*2*Math.PI,s=proj(toW([.32*Math.cos(t),L.y*1.01,-.6+.32*Math.sin(t)]),W,H);i?g.lineTo(s[0],s[1]):g.moveTo(s[0],s[1])}g.fill()}
  // body axes
  const o=proj([0,0,0],W,H);
  [['X','#e66'],['Y','#6c6'],['Z','#6af']].forEach(([nm,col],i)=>{const v=[0,0,0];v[i]=1.9;const e=proj(toW(v),W,H);
    g.strokeStyle=col;g.fillStyle=col;g.lineWidth=3*dpr;g.beginPath();g.moveTo(o[0],o[1]);g.lineTo(e[0],e[1]);g.stroke();
    const ang=Math.atan2(e[1]-o[1],e[0]-o[0]),hl=12*dpr;g.beginPath();g.moveTo(e[0],e[1]);
    g.lineTo(e[0]-hl*Math.cos(ang-.4),e[1]-hl*Math.sin(ang-.4));g.lineTo(e[0]-hl*Math.cos(ang+.4),e[1]-hl*Math.sin(ang+.4));g.fill();
    g.font='bold '+(15*dpr)+'px system-ui';g.fillText(nm,e[0]+6*dpr,e[1]-6*dpr)});
  // angles: front lens = -Y, camera up = -Z, right = +X
  const f=toW([0,-1,0]),u=toW([0,0,-1]),r=toW([1,0,0]),P=(v)=>String(v.toFixed(1)).padStart(7);
  const heading=Math.atan2(f[1],f[0])/DEG,pitch=Math.asin(Math.max(-1,Math.min(1,f[2])))/DEG,roll=Math.atan2(r[2],u[2])/DEG;
  g.font=(13*dpr)+'px ui-monospace,Menlo,monospace';g.fillStyle='#ddd';
  const y0=(pose.cal.n<CAL?40:20)*dpr;g.fillText('front lens',10*dpr,y0);g.fillText('heading'+P(heading)+'°',10*dpr,y0+18*dpr);g.fillText('pitch  '+P(pitch)+'°',10*dpr,y0+36*dpr);g.fillText('roll   '+P(roll)+'°',10*dpr,y0+54*dpr);
  g.fillStyle='#888';g.fillText('gyro bias '+pose.bias.map(x=>x.toFixed(1)).join(' ')+' counts',10*dpr,H-28*dpr);g.fillText('heading drifts: use "zero heading"',10*dpr,H-10*dpr);
}
let head=0,tPrev=0;
function draw(now){
  requestAnimationFrame(draw);
  drawPose();
  const dt=tPrev?Math.min((now-tPrev)/1000,0.1):0;tPrev=now;
  if(!buf.length)return;
  // playhead advances at the sample rate each frame, eased toward 200 ms behind the newest sample (no jerks from network batching)
  const target=last-40;
  head+=dt*200;head+=(target-head)*Math.min(1,dt*2);
  if(!head||Math.abs(head-target)>400)head=target;
  const base=buf[0][0],end=head-base,start=end-WIN,i0=Math.max(0,Math.floor(start)),i1=Math.min(buf.length-1,Math.floor(end));
  const dpr=devicePixelRatio;
  GROUPS.forEach(G=>{
    const c=G.cv;if(c.width!==Math.round(c.clientWidth*dpr))c.width=Math.round(c.clientWidth*dpr);
    const g=c.getContext('2d');g.clearRect(0,0,c.width,c.height);
    if(G.on){  // a disabled group keeps drawing its last snapshot
      const vals=G.ch.map(k=>{const v=[];for(let i=i0;i<=i1;i++)v.push(buf[i][2][k]);return v});
      const avg=G.ch.map(k=>{let s=0,n=0;for(let i=Math.max(0,i1-AVG+1);i<=i1;i++){s+=buf[i][2][k];n++}return s/n});
      G.snap={vals,off:i0-start,avg};
    }
    if(!G.snap)return;
    let lo=G.lo,hi=G.hi;if(hi-lo<200){const m=(hi+lo)/2;lo=m-100;hi=m+100}
    const top=44*dpr,H=c.height-top-8*dpr;
    G.snap.vals.forEach((vs,a)=>{
      g.strokeStyle=G.on?COL[a]:'#666';g.lineWidth=dpr;g.beginPath();
      vs.forEach((v,j)=>{const x=(j+G.snap.off)/WIN*c.width,y=top+H-(v-lo)/(hi-lo)*H;j?g.lineTo(x,y):g.moveTo(x,y)});
      g.stroke()});
    g.font=(13*dpr)+'px ui-monospace,Menlo,monospace';
    g.fillStyle=G.on?'#ddd':'#777';
    g.fillText(G.name+(G.on?'':' (off)')+'   min '+F(G,G.lo)+'   max '+F(G,G.hi)+'  (shared)',8,16*dpr);
    let x=8*dpr;
    G.snap.avg.forEach((v,a)=>{const t=AX[a]+': '+F(G,v)+'    ';g.fillStyle=G.on?COL[a]:'#777';g.fillText(t,x,35*dpr);x+=g.measureText(t).width});
  });
}
requestAnimationFrame(draw);
async function poll(){
  try{const r=await fetch('/data?since='+last);const j=await r.json();
    for(const s of j.samples){s[2]=ORDER.map(i=>s[2][i]);updatePose(s);buf.push(s);last=s[0];
      GROUPS.forEach(G=>{if(!G.on)return;for(const k of G.ch){const v=s[2][k];if(v<G.lo)G.lo=v;if(v>G.hi)G.hi=v}})}
    if(buf.length>WIN*3)buf=buf.slice(-WIN*2);
    document.getElementById('info').textContent='samples '+P(j.stats.samples,8)+'  packets '+P(j.stats.packets,7)+'  lost '+P(j.stats.lost,5)+'  rate '+P(j.rate.toFixed(0),4)+'/s';
  }catch(e){}
  setTimeout(poll,30)}
poll();
</script>"""


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        if self.path.startswith("/data"):
            since = int(self.path.split("since=")[1]) if "since=" in self.path else 0
            with lock:
                new = [s for s in samples if s[0] > since]
                rate = 0.0
                if len(samples) > 50:
                    dt = samples[-1][3] - samples[0][3]
                    rate = (len(samples) - 1) / dt if dt > 0 else 0.0
            body = json.dumps({"samples": new[-1500:], "stats": stats, "rate": rate}).encode()
            ctype = "application/json"
        else:
            body, ctype = PAGE.encode(), "text/html"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host")
    ap.add_argument("--port", type=int, default=7700)
    ap.add_argument("--http", type=int, default=8765)
    a = ap.parse_args()
    host = a.host
    if not host:
        import camsh
        host = camsh.HOST
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    threading.Thread(target=receiver, args=(sock,), daemon=True).start()
    threading.Thread(target=keepalive, args=(sock, (host, a.port)), daemon=True).start()
    print(f"camera {host}:{a.port}; open http://127.0.0.1:{a.http}/")
    ThreadingHTTPServer(("127.0.0.1", a.http), Handler).serve_forever()


if __name__ == "__main__":
    main()
