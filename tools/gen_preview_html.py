#!/usr/bin/env python3
# generate SkyEngine level interactive preview HTML (embedded JSON data)
import json, os

import sys
BASE = sys.argv[1] if len(sys.argv)>1 else '.'
LEVELS = {}
for L in ['DayHubCave', 'Prairie_Village', 'Prairie_ButterflyFields']:
    with open(f'{BASE}/{L}/preview.json') as f:
        d = json.load(f)
  # decimated: tri groups [x,y,z,r,g,b]*3
    LEVELS[L] = {'tri': d['tri'], 'water': d.get('water', [])}

# data goes to an external JS file: a multi-MB inline <script> is not executed by some browsers
data_js = 'const LEVELS = ' + json.dumps(LEVELS, separators=(',', ':'))

html = """<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>SkyEngine level preview</title>
<style>
  body { margin:0; overflow:hidden; background:#0b0e1a; font-family:system-ui,sans-serif; color:#cfd6e8; }
  canvas { display:block; cursor:grab; }
  canvas:active { cursor:grabbing; }
  #ui { position:fixed; top:12px; left:12px; z-index:10; background:rgba(10,14,26,.82); border:1px solid #2a3a5a; border-radius:10px; padding:10px 14px; font-size:13px; }
  #ui h1 { margin:0 0 8px; font-size:15px; color:#9fb8e8; letter-spacing:.5px; }
  #btns { display:flex; gap:6px; flex-wrap:wrap; }
  #btns button { background:#1a2540; color:#c8d4f0; border:1px solid #33456e; border-radius:6px; padding:5px 10px; font-size:12px; cursor:pointer; }
  #btns button.active { background:#3b5a9e; border-color:#6f9df0; color:#fff; }
  #hint { margin-top:8px; font-size:11px; color:#7f90b8; }
  #stats { position:fixed; bottom:10px; left:12px; font-size:11px; color:#6f80a8; z-index:10; background:rgba(10,14,26,.6); padding:4px 10px; border-radius:6px; }
</style>
</head>
<body>
<div id="ui">
 <h1>SkyEngine level preview</h1>
  <div id="btns"></div>
 <div id="hint">drag rotate · wheel scale · click switch level</div>
</div>
<div id="stats"></div>
<canvas id="cv"></canvas>
<script src="preview_data.js"></script>
<script>
(function(){
  const cv = document.getElementById('cv');
  const ctx = cv.getContext('2d');
  let W=0,H=0, dpr=1;
  function resize(){
    dpr = Math.min(window.devicePixelRatio||1, 1.5);
    W = window.innerWidth; H = window.innerHeight;
    cv.width = W*dpr; cv.height = H*dpr;
    cv.style.width = W+'px'; cv.style.height = H+'px';
  }
  window.addEventListener('resize', resize); resize();

  const keys = Object.keys(LEVELS);
  let cur = keys[0];
  const btns = document.getElementById('btns');
  keys.forEach(k=>{
    const b = document.createElement('button');
    b.textContent = k;
    b.onclick = ()=>{ cur=k; for(const x of btns.children) x.classList.remove('active'); b.classList.add('active'); render(); };
    btns.appendChild(b);
  });
  btns.children[0].classList.add('active');

    // view state
  let yaw=0.65, pitch=0.30, zoom=1.0;
  let drag=false, ox=0, oy=0;
  cv.addEventListener('mousedown',e=>{ drag=true; ox=e.clientX; oy=e.clientY; });
  window.addEventListener('mouseup',()=>drag=false);
  window.addEventListener('mousemove',e=>{
    if(!drag) return;
    yaw += (e.clientX-ox)*0.005; pitch += (e.clientY-oy)*0.005;
    pitch = Math.max(-1.2, Math.min(1.2, pitch));
    ox=e.clientX; oy=e.clientY; render();
  });
  cv.addEventListener('wheel',e=>{ e.preventDefault(); zoom *= e.deltaY>0?0.92:1.08; zoom=Math.max(0.2,Math.min(6,zoom)); render(); }, {passive:false});
  cv.addEventListener('touchstart',e=>{ if(e.touches.length===1){ drag=true; ox=e.touches[0].clientX; oy=e.touches[0].clientY; } }, {passive:true});
  cv.addEventListener('touchmove',e=>{
    if(drag && e.touches.length===1){
      const t=e.touches[0];
      yaw += (t.clientX-ox)*0.005; pitch += (t.clientY-oy)*0.005;
      pitch = Math.max(-1.2, Math.min(1.2, pitch));
      ox=t.clientX; oy=t.clientY; render();
    }
  }, {passive:true});
  window.addEventListener('touchend',()=>drag=false);

    // sky gradient (Sky blue-purple)
  function drawSky(){
    const g = ctx.createLinearGradient(0,0,0,H);
    g.addColorStop(0,'#27336b'); g.addColorStop(0.5,'#4a5a9e'); g.addColorStop(1,'#9e9ec4');
    ctx.fillStyle = g; ctx.fillRect(0,0,W,H);
  }

 // world→camera→screen
  let camR=0;
  function project(x,y,z){
    const cy=Math.cos(yaw), sy=Math.sin(yaw), cp=Math.cos(pitch), sp=Math.sin(pitch);
    const x1 = x*cy + z*sy;
    const z1 = -x*sy + z*cy;
    const y1 = y;
    // pitch: around x axis
    const y2 = y1*cp - z1*sp;
    const z2 = y1*sp + z1*cp;
    const dist = camR*2.6/zoom;
    if(z2 > -0.05*dist) return null;
    // logical coords: ctx is already scaled by dpr, so no dpr here
    const f = (H*0.9)/(z2+dist);
    return [W/2 + x1*f, H/2 - y2*f, z2];
  }

  let cached=null;
  function buildScene(){
    const L = LEVELS[cur];
    const tri = L.tri;
    let mnx=1e30,mny=1e30,mnz=1e30,mxx=-1e30,mxy=-1e30,mxz=-1e30;
    // tri layout: one array of 18 per triangle = [x,y,z,r,g,b] * 3 vertices
    for(const t of tri){
      mnx=Math.min(mnx,t[0]);mny=Math.min(mny,t[1]);mnz=Math.min(mnz,t[2]);
      mxx=Math.max(mxx,t[12]);mxy=Math.max(mxy,t[13]);mxz=Math.max(mxz,t[14]);
    }
    camR = Math.max(mxx-mnx, mxz-mnz, mxy-mny)*0.7;
    // scene center
    const cx=(mnx+mxx)/2, cy2=(mny+mxy)/2, cz=(mnz+mxz)/2;
    // triangle list (decimated render: every 2nd)
    const list=[];
    tri.forEach((t,idx)=>{
      if(idx%2) return;
      list.push([t[0]-cx,t[1]-cy2,t[2]-cz, t[6]-cx,t[7]-cy2,t[8]-cz, t[12]-cx,t[13]-cy2,t[14]-cz, t[3],t[4],t[5]]);
    });
    const water = (L.water||[]).map(w=>[w[0]-cx,w[1]-cy2,w[2]-cz,w[3],w[4]]);
    return {list, water, cx, cy2, cz};
  }

  function render(){
    if(!cached || cached.name!==cur) { cached = buildScene(); cached.name = cur; }
    const S = cached;
    ctx.setTransform(dpr,0,0,dpr,0,0);
    drawSky();
    const cy=Math.cos(yaw), sy=Math.sin(yaw), cp=Math.cos(pitch), sp=Math.sin(pitch);
    const dist = S.camR*2.6/zoom;
 // project+depth
    const out=[];
    for(const t of S.list){
      const p=project(t[0],t[1],t[2]);
      const q=project(t[3],t[4],t[5]);
      const r=project(t[6],t[7],t[8]);
      if(!p||!q||!r) continue;
    // no backface cull: mesh winding is not guaranteed, draw both sides for inspection
      const depth=(p[2]+q[2]+r[2])/3;
      out.push([p,q,r,depth,t[9],t[10],t[11]]);
    }
    out.sort((a,b)=>b[3]-a[3]);
    ctx.lineWidth=1; ctx.strokeStyle='rgba(0,0,0,0.0)';
    for(const t of out){
      ctx.fillStyle = 'rgb('+t[4]+','+t[5]+','+t[6]+')';
      ctx.beginPath();
      ctx.moveTo(t[0][0],t[0][1]); ctx.lineTo(t[1][0],t[1][1]); ctx.lineTo(t[2][0],t[2][1]);
      ctx.closePath(); ctx.fill();
    }
    // water (semi-alpha teal)
    for(const w of S.water){
      const hx=w[3], hz=w[4], y0=w[1];
      const pts=[[w[0]-hx,y0,w[2]-hz],[w[0]+hx,y0,w[2]-hz],[w[0]+hx,y0,w[2]+hz],[w[0]-hx,y0,w[2]+hz]];
      const pr=[];
      let all=true;
      for(const p of pts){ const q=project(p[0],p[1],p[2]); if(!q){all=false;break;} pr.push(q); }
      if(!all) continue;
      ctx.globalAlpha=0.55;
      ctx.fillStyle='rgb(105,145,134)';
      ctx.beginPath(); ctx.moveTo(pr[0][0],pr[0][1]); ctx.lineTo(pr[1][0],pr[1][1]); ctx.lineTo(pr[2][0],pr[2][1]);
      ctx.closePath(); ctx.fill();
      ctx.beginPath(); ctx.moveTo(pr[0][0],pr[0][1]); ctx.lineTo(pr[2][0],pr[2][1]); ctx.lineTo(pr[3][0],pr[3][1]);
      ctx.closePath(); ctx.fill();
      ctx.globalAlpha=1;
    }
    document.getElementById('stats').textContent = cur+' · tris '+S.list.length+' · drag rotate · wheel scale';
  }

  let raf=0;
  function schedule(){ cancelAnimationFrame(raf); raf=requestAnimationFrame(render); }
  render();
})();
</script>
</body>
</html>
"""

out = f'{BASE}/preview.html'
with open(out, 'w') as f:
    f.write(html)
data_out = f'{BASE}/preview_data.js'
with open(data_out, 'w') as f:
    f.write(data_js)
print(f'generated {out} (+{data_out}, {os.path.getsize(data_out)/1024/1024:.1f} MB)')
