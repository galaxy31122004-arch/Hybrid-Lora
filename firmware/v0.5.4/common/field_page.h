#pragma once

#include <Arduino.h>

// Page served by FieldWeb (field_web.h) at http://<gateway IP>:8080.
// A field instrument for one hand in sunlight: light monochrome theme, one
// family (Archivo). The signature element is the strip at the top: one tick
// per packet, height = SNR, a cross = lost. Leaflet, the map tiles and the
// font come from the internet (the phone's hotspot has data); packets and GPS
// work without them (strip, table, KML/CSV download).
// Records are kept in the phone's localStorage until "Xoá dữ liệu".
static const char kFieldPageHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#f3f5f4">
<meta name="color-scheme" content="only light">
<title>Đo tầm LoRa</title>
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Archivo:wdth,wght@62..125,300..800&display=swap">
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.css">
<style>
:root{--paper:#f3f5f4;--ink:#121615;--graphite:#5e6664;--ash:#a9b0ae;--rule:#d5dad8;--white:#fff;color-scheme:only light}
.leaflet-tile-pane{filter:grayscale(1) contrast(.92) brightness(1.04)}
*{box-sizing:border-box}
html,body{margin:0;background:var(--paper);color:var(--ink);font-family:Archivo,system-ui,sans-serif;font-size:16px;line-height:1.45;-webkit-text-size-adjust:100%}
body{padding:0 0 calc(24px + env(safe-area-inset-bottom))}
:focus-visible{outline:2px solid var(--ink);outline-offset:2px}
/* signature: packet strip */
.strip{position:sticky;top:0;z-index:500;background:var(--paper);padding:10px 16px 0;border-bottom:1px solid var(--rule)}
#strip{display:block;width:100%;height:72px}
.verdict{display:flex;align-items:baseline;gap:12px;padding:4px 0 10px}
#word{font-size:40px;line-height:1;font-weight:760;font-stretch:72%;letter-spacing:-.01em}
#word.quiet{color:var(--graphite);font-weight:500}
#detail{font-size:15px;color:var(--graphite);font-variant-numeric:tabular-nums}
#map{height:46vh;min-height:260px;background:#e6eae8}
.facts{display:grid;grid-template-columns:repeat(3,1fr);margin:0 16px;border-bottom:1px solid var(--rule)}
.fact{padding:10px 0 12px}
.fact+.fact{padding-left:12px;border-left:1px solid var(--rule)}
.fact b{display:block;font-size:22px;font-weight:600;font-stretch:85%;font-variant-numeric:tabular-nums;line-height:1.2}
.fact span{font-size:13px;color:var(--graphite)}
.fact.warn b{text-decoration:underline;text-decoration-thickness:2px;text-underline-offset:4px}
#msg{margin:10px 16px 0;font-size:14px}
#msg:empty{display:none}
#msg{padding:10px 12px;border:1px solid var(--ink);background:var(--white)}
.actions{display:flex;flex-wrap:wrap;gap:8px;margin:14px 16px 0}
button{font:inherit;font-size:15px;font-weight:600;min-height:44px;padding:0 16px;border-radius:22px;border:1px solid var(--ink);background:var(--white);color:var(--ink)}
button.primary{background:var(--ink);color:var(--white)}
button.ghost{border-color:var(--ash);color:var(--graphite);background:transparent}
.key{display:flex;flex-wrap:wrap;gap:4px 14px;margin:14px 16px 0;font-size:13px;color:var(--graphite)}
.key i{display:inline-block;width:11px;height:11px;border-radius:50%;margin-right:5px;vertical-align:-1px}
h2{font-size:17px;font-weight:650;margin:22px 16px 6px}
table{width:calc(100% - 32px);margin:0 16px;border-collapse:collapse;font-size:14px;font-variant-numeric:tabular-nums}
td,th{padding:7px 0;border-bottom:1px solid var(--rule);text-align:right}
th{font-weight:500;color:var(--graphite);font-size:13px}
td:first-child,th:first-child{text-align:left}
td.lost{color:var(--graphite)}
.leaflet-container{font-family:inherit;background:#e6eae8}
.x{font:800 16px/16px Archivo,system-ui;color:var(--ink);text-align:center;text-shadow:0 0 2px #fff,0 0 2px #fff}
.gwm{font:700 18px/18px Archivo,system-ui;color:var(--ink);text-align:center;text-shadow:0 0 3px #fff}
@media (prefers-reduced-motion:reduce){*{transition:none!important}}
</style></head><body>
<div class="strip" aria-live="polite">
 <canvas id="strip" aria-label="Các gói gần nhất: cao là SNR tốt, dấu nhân là mất"></canvas>
 <div class="verdict"><span id="word" class="quiet">Đang chờ gói</span><span id="detail"></span></div>
</div>
<div id="map"></div>
<div class="facts">
 <div class="fact" id="fGps"><b id="gps">–</b><span id="gpsS">GPS chưa có</span></div>
 <div class="fact" id="fPdr"><b id="pdr">–</b><span id="pdrS">20 gói gần nhất</span></div>
 <div class="fact" id="fDist"><b id="dist">–</b><span id="distS">chưa đặt gateway</span></div>
</div>
<div id="msg" role="status"></div>
<div class="actions">
 <button id="bGw" class="primary">Đặt gateway ở đây</button>
 <button id="bFollow" aria-pressed="true">Bản đồ theo tôi</button>
</div>
<div class="actions">
 <button id="bKml">Tải KML cho Google Maps</button>
 <button id="bCsv">Tải CSV</button>
 <button id="bClear" class="ghost">Xoá dữ liệu</button>
</div>
<div class="key">
 <span><i style="background:#121615"></i>Rõ, SNR từ 0 dB</span>
 <span><i style="background:#a9b0ae"></i>Được, −10 đến 0</span>
 <span><i style="border:2px solid #121615"></i>Yếu, −15 đến −10</span>
 <span><i style="border:2px dotted #5e6664"></i>Sát giới hạn</span>
 <span><b style="color:#121615">✕</b> Mất</span>
</div>
<h2>Gói gần nhất</h2>
<table><thead><tr><th>Giờ</th><th>Gói</th><th>SNR</th><th>RSSI</th><th>Cách GW</th></tr></thead><tbody id="tb"></tbody></table>
<script src="https://cdnjs.cloudflare.com/ajax/libs/leaflet/1.9.4/leaflet.min.js"></script>
<script>
"use strict";
const $=id=>document.getElementById(id);
const KEY="hl-range-v1";
let fixes=[],recs=[],pending=[],lastId=0,lastNow=null,off=null,bestRtt=1e9,gw=null,prev={},follow=true;
let lastPollOk=0,lastFix=null,dirty=false,map=null,me=null,gwMark=null,pts=null;
try{const s=JSON.parse(localStorage.getItem(KEY)||"null");if(s){recs=s.recs||[];gw=s.gw||null;lastId=s.lastId||0;lastNow=s.lastNow??null;}}catch(e){}
function save(){if(!dirty)return;dirty=false;try{localStorage.setItem(KEY,JSON.stringify({recs:recs.slice(-15000),gw,lastId,lastNow}));}catch(e){say("Điện thoại hết chỗ lưu. Tải KML hoặc CSV ngay rồi xoá dữ liệu.");}}
function say(t){$("msg").textContent=t||"";}
function hav(a,b){const R=6371000,r=Math.PI/180,p1=a[0]*r,p2=b[0]*r,dp=p2-p1,dl=(b[1]-a[1])*r;const h=Math.sin(dp/2)**2+Math.cos(p1)*Math.cos(p2)*Math.sin(dl/2)**2;return 2*R*Math.asin(Math.sqrt(h));}
function cls(r){if(r.st==="lost")return"lost";return r.snr>=0?"good":r.snr>=-10?"fair":r.snr>=-15?"weak":"edge";}
function hms(t){return new Date(t).toLocaleTimeString("vi-VN",{hour12:false});}
function distOf(r){return gw&&r.lat!=null?Math.round(hav([gw.lat,gw.lon],[r.lat,r.lon])):null;}
function m(d){return d==null?"–":d>=1000?(d/1000).toFixed(2).replace(".",",")+" km":d+" m";}
// ---- map (grey base so the ink marks read in sunlight)
if(window.L){
 map=L.map("map",{zoomControl:false}).setView([16,107],5);
 // Esri street tiles, shown in grey (CSS filter). CARTO needs an API key now
 // and tile.openstreetmap.org did not load from the test network (2026-11-16).
 L.tileLayer("https://server.arcgisonline.com/ArcGIS/rest/services/World_Street_Map/MapServer/tile/{z}/{y}/{x}",{maxZoom:20,maxNativeZoom:19,attribution:"Tiles © Esri"}).addTo(map);
 pts=L.layerGroup().addTo(map);
 map.on("dragstart",()=>setFollow(false));
}else{$("map").innerHTML='<p style="padding:16px;color:#5e6664">Không tải được bản đồ vì điện thoại chưa có internet. Gói và vị trí vẫn được ghi; tải KML để xem trên Google Maps.</p>';}
const STY={good:{color:"#121615",fillColor:"#121615",fillOpacity:1,weight:1,radius:6},
 fair:{color:"#7d8583",fillColor:"#a9b0ae",fillOpacity:1,weight:1,radius:6},
 weak:{color:"#121615",fillColor:"#fff",fillOpacity:.6,weight:2,radius:6},
 edge:{color:"#5e6664",fillOpacity:0,weight:2,dashArray:"1 4",radius:6}};
function drawRec(r){
 if(!pts||r.lat==null)return;
 const c=cls(r),d=distOf(r);
 const pop=`<b>Gói ${r.seq}</b>, ${hms(r.t)}<br>`+(r.st==="lost"?"Mất":`SNR ${r.snr} dB, RSSI ${r.rssi} dBm<br>Lệch tần ${r.ferr} Hz`)+(d!=null?`<br>${m(d)} tới gateway`:"")+`<br>GPS sai số ${Math.round(r.acc||0)} m`;
 const mk=c==="lost"?L.marker([r.lat,r.lon],{icon:L.divIcon({className:"x",html:"✕",iconSize:[16,16]}),keyboard:false})
  :L.circleMarker([r.lat,r.lon],STY[c]);
 mk.bindPopup(pop).addTo(pts);
}
function drawGw(){if(!map||!gw)return;if(gwMark)gwMark.setLatLng([gw.lat,gw.lon]);else gwMark=L.marker([gw.lat,gw.lon],{icon:L.divIcon({className:"gwm",html:"◎",iconSize:[18,18]})}).addTo(map).bindPopup("Gateway");}
function redraw(){if(pts){pts.clearLayers();recs.forEach(drawRec);}drawGw();}
redraw();
function setFollow(v){follow=v;$("bFollow").setAttribute("aria-pressed",v);$("bFollow").textContent=v?"Bản đồ theo tôi":"Bản đồ đứng yên";if(v&&lastFix&&map)map.setView([lastFix.lat,lastFix.lon]);}
// ---- GPS
function onFix(p){
 lastFix={t:p.timestamp||Date.now(),lat:p.coords.latitude,lon:p.coords.longitude,acc:p.coords.accuracy};
 fixes.push(lastFix);if(fixes.length>4000)fixes.splice(0,1000);
 if(map){if(!me)me=L.circleMarker([lastFix.lat,lastFix.lon],{radius:9,color:"#121615",weight:2,fillColor:"#fff",fillOpacity:.5}).addTo(map);else me.setLatLng([lastFix.lat,lastFix.lon]);
  if(follow)map.setView([lastFix.lat,lastFix.lon],Math.max(map.getZoom(),16));}
 resolve();
}
function onGpsErr(e){
 if(!window.isSecureContext)say("Chrome chặn vị trí vì trang này là http. Mở chrome://flags/#unsafely-treat-insecure-origin-as-secure, thêm "+location.origin+", chọn Enabled rồi khởi động lại Chrome.");
 else if(e.code===1)say("Trang chưa được phép đọc vị trí. Cho phép Vị trí cho Chrome và cho trang này, rồi tải lại trang.");
 else say("Chưa lấy được vị trí: "+e.message+". Ra chỗ thoáng và bật Vị trí chính xác.");
}
if(navigator.geolocation)navigator.geolocation.watchPosition(onFix,onGpsErr,{enableHighAccuracy:true,maximumAge:0,timeout:20000});
else onGpsErr({message:"trình duyệt không hỗ trợ"});
function posAt(t){
 if(!fixes.length)return Date.now()-t<5000?undefined:null;
 const i=fixes.findIndex(f=>f.t>=t);
 if(i>0){const a=fixes[i-1],b=fixes[i],k=b.t===a.t?0:(t-a.t)/(b.t-a.t);
  if(b.t-a.t<=30000)return{lat:a.lat+(b.lat-a.lat)*k,lon:a.lon+(b.lon-a.lon)*k,acc:Math.max(a.acc,b.acc)};}
 if(i<0&&Date.now()-t<5000)return undefined; // wait for the next fix
 const n=i<0?fixes[fixes.length-1]:fixes[i];
 return Math.abs(n.t-t)<=20000?{lat:n.lat,lon:n.lon,acc:n.acc}:null;
}
function resolve(){
 const keep=[];
 for(const r of pending){const p=posAt(r.t);if(p===undefined){keep.push(r);continue;}
  if(p){r.lat=p.lat;r.lon=p.lon;r.acc=p.acc;}recs.push(r);drawRec(r);dirty=true;}
 pending=keep;
}
// ---- packets from the gateway
async function poll(){
 const t0=Date.now();
 try{
  const res=await fetch("/api/packets?after="+lastId,{cache:"no-store"});const j=await res.json();
  const rtt=Date.now()-t0;
  if(off===null||rtt<=bestRtt*1.5+30){off=t0+rtt/2-j.now;bestRtt=Math.min(bestRtt,rtt);}
  if(lastNow!==null&&j.now<lastNow){lastId=0;prev={};} // gateway restarted
  lastNow=j.now;lastPollOk=Date.now();
  for(const p of j.packets){
   lastId=Math.max(lastId,p.id);
   const t=p.ms+off,pr=prev[p.node];
   if(p.lost&&pr)for(let i=1;i<=p.lost;i++)pending.push({t:pr.t+(t-pr.t)*i/(p.lost+1),node:p.node,seq:(pr.seq+i)&65535,st:"lost"});
   prev[p.node]={t,seq:p.seq};
   pending.push({t,node:p.node,seq:p.seq,st:"ok",rssi:p.rssi,snr:p.snr,ferr:p.ferr,tx:p.tx,off:p.off});
  }
  resolve();
 }catch(e){}
 setTimeout(poll,1000);
}
poll();
// ---- the strip: last 60 packets, newest on the right
function drawStrip(){
 const c=$("strip"),dpr=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
 if(c.width!==w*dpr){c.width=w*dpr;c.height=h*dpr;}
 const g=c.getContext("2d");g.setTransform(dpr,0,0,dpr,0,0);g.clearRect(0,0,w,h);
 const all=recs.concat(pending).sort((a,b)=>a.t-b.t).slice(-60),step=w/60,base=h-4;
 g.strokeStyle="#d5dad8";g.lineWidth=1;g.beginPath();g.moveTo(0,base+.5);g.lineTo(w,base+.5);g.stroke();
 const y0=base-(0+20)/32*(base-4);g.setLineDash([2,3]);g.beginPath();g.moveTo(0,y0);g.lineTo(w,y0);g.stroke();g.setLineDash([]);
 all.forEach((r,i)=>{const x=w-(all.length-i)*step+step/2;
  if(r.st==="lost"){g.strokeStyle="#121615";g.lineWidth=2;const s=Math.min(6,step*.4),cy=base-s-3;g.beginPath();g.moveTo(x-s,cy-s);g.lineTo(x+s,cy+s);g.moveTo(x+s,cy-s);g.lineTo(x-s,cy+s);g.stroke();return;}
  const k=Math.max(.06,Math.min(1,(r.snr+20)/32)),c2=cls(r);
  g.fillStyle=c2==="good"?"#121615":c2==="fair"?"#7d8583":"#a9b0ae";
  g.fillRect(x-Math.max(1,step*.28),base-k*(base-4),Math.max(2,step*.56),k*(base-4));});
}
// ---- panel
function ui(){
 const now=Date.now(),ok=recs.filter(r=>r.st==="ok"),lr=ok[ok.length-1];
 const linkAge=(now-lastPollOk)/1000,pktAge=lr?(now-lr.t)/1000:null;
 let word="Đang chờ gói",quiet=true,detail="";
 if(!lastPollOk||linkAge>=5){word="Không thấy gateway";detail="Điện thoại phải vào cùng Wi-Fi với gateway";}
 else if(pktAge==null){word="Đang chờ gói";detail="Node phát mỗi ~5 s";}
 else if(pktAge>20){word="Mất tín hiệu";quiet=false;detail=`gói cuối ${Math.round(pktAge)} s trước`;}
 else{const c=cls(lr);word={good:"Nghe rõ",fair:"Nghe được",weak:"Yếu",edge:"Sát giới hạn"}[c];quiet=false;
  detail=`SNR ${String(lr.snr).replace(".",",")} dB, RSSI ${lr.rssi} dBm`;}
 $("word").textContent=word;$("word").className=quiet?"quiet":"";$("detail").textContent=detail;
 if(lastFix){$("gps").textContent="± "+Math.round(lastFix.acc)+" m";const a=Math.round((now-lastFix.t)/1000);$("gpsS").textContent=a<3?"GPS đang cập nhật":`GPS ${a} s trước`;
  $("fGps").classList.toggle("warn",lastFix.acc>30||now-lastFix.t>15000);}
 const w=recs.slice(-20);if(w.length){const g=w.filter(r=>r.st==="ok").length;$("pdr").textContent=Math.round(100*g/w.length)+" %";$("pdrS").textContent=`${ok.length} nhận, ${recs.length-ok.length} mất`;$("fPdr").classList.toggle("warn",g/w.length<.9);}
 if(gw&&lastFix){$("dist").textContent=m(Math.round(hav([gw.lat,gw.lon],[lastFix.lat,lastFix.lon])));
  const far=Math.max(0,...ok.map(distOf).filter(d=>d!=null));$("distS").textContent="xa nhất còn nghe "+m(far);}
 $("tb").innerHTML=recs.slice(-12).reverse().map(r=>{const lost=r.st==="lost";
  return`<tr><td${lost?' class="lost"':""}>${hms(r.t)}</td><td>${r.seq}</td><td>${lost?"mất":r.snr}</td><td>${lost?"":r.rssi}</td><td>${m(distOf(r))}</td></tr>`;}).join("");
 drawStrip();
}
setInterval(ui,1000);setInterval(save,5000);addEventListener("resize",drawStrip);ui();
// ---- actions
$("bGw").onclick=()=>{if(!lastFix){say("Chưa có vị trí GPS. Chờ vài giây ở chỗ thoáng rồi bấm lại.");return;}gw={lat:lastFix.lat,lon:lastFix.lon};dirty=true;drawGw();redraw();say(`Đã đặt gateway ở đây, sai số GPS ${Math.round(lastFix.acc)} m.`);};
$("bFollow").onclick=()=>setFollow(!follow);
$("bClear").onclick=()=>{const b=$("bClear");if(!recs.length)return;
 if(!b.dataset.arm){b.dataset.arm=1;b.textContent="Bấm lần nữa để xoá "+recs.length+" điểm";setTimeout(()=>{delete b.dataset.arm;b.textContent="Xoá dữ liệu";},4000);return;}
 recs=[];pending=[];dirty=true;save();redraw();delete b.dataset.arm;b.textContent="Xoá dữ liệu";say("Đã xoá dữ liệu trên điện thoại.");};
function stamp(){const d=new Date(recs.length?recs[0].t:Date.now()),p=n=>String(n).padStart(2,"0");return`${d.getFullYear()}${p(d.getMonth()+1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}`;}
function dl(name,type,text){const a=document.createElement("a");a.href=URL.createObjectURL(new Blob([text],{type}));a.download=name;document.body.appendChild(a);a.click();setTimeout(()=>{URL.revokeObjectURL(a.href);a.remove();},1000);say("Đã tải "+name+".");}
$("bCsv").onclick=()=>{const h="time_local,node,seq,status,class,lat,lon,gps_acc_m,dist_m,rssi_dbm,snr_db,ferr_hz,tx_dbm,offset_db";
 const rows=recs.map(r=>[new Date(r.t).toLocaleString("sv-SE").replace(" ","T"),r.node,r.seq,r.st,cls(r),r.lat?.toFixed(6)??"",r.lon?.toFixed(6)??"",r.acc!=null?Math.round(r.acc):"",distOf(r)??"",r.rssi??"",r.snr??"",r.ferr??"",r.tx??"",r.off??""].join(","));
 dl(`range-${stamp()}.csv`,"text/csv",h+"\n"+rows.join("\n"));};
$("bKml").onclick=()=>{
 // Google Maps base is light and busy: the KML uses colour so the marks stand out there.
 const S={good:["ff00c000","SNR >= 0 dB"],fair:["ff00e0ff","SNR -10..0 dB"],weak:["ff0080ff","SNR -15..-10 dB"],edge:["ff0000ff","SNR < -15 dB"],lost:["ff000000","lost"]};
 const k=['<?xml version="1.0" encoding="UTF-8"?>','<kml xmlns="http://www.opengis.net/kml/2.2"><Document>',`<name>Hybrid-LoRa range ${stamp()}</name>`];
 for(const[n,[c]]of Object.entries(S))k.push(`<Style id="${n}"><IconStyle><color>${c}</color><scale>0.6</scale><Icon><href>http://maps.google.com/mapfiles/kml/shapes/${n==="lost"?"forbidden":"shaded_dot"}.png</href></Icon></IconStyle></Style>`);
 if(gw)k.push(`<Placemark><name>Gateway</name><Point><coordinates>${gw.lon},${gw.lat}</coordinates></Point></Placemark>`);
 for(const[n,[,label]]of Object.entries(S)){k.push(`<Folder><name>${label}</name>`);
  for(const r of recs.filter(r=>r.lat!=null&&cls(r)===n)){const d=distOf(r);
   const desc=`${hms(r.t)} seq ${r.seq} | `+(r.st==="lost"?"LOST":`RSSI ${r.rssi} dBm, SNR ${r.snr} dB, FERR ${r.ferr} Hz`)+(d!=null?` | ${d} m from gateway`:"")+` | GPS ${Math.round(r.acc||0)} m`;
   k.push(`<Placemark><name>${r.seq}</name><description>${desc}</description><TimeStamp><when>${new Date(r.t).toISOString()}</when></TimeStamp><styleUrl>#${n}</styleUrl><Point><coordinates>${r.lon.toFixed(6)},${r.lat.toFixed(6)}</coordinates></Point></Placemark>`);}
  k.push("</Folder>");}
 k.push("</Document></kml>");dl(`range-${stamp()}.kml`,"application/vnd.google-earth.kml+xml",k.join("\n"));};
// keep the screen on while measuring (needs the secure-origin flag too)
let wake=null;async function keepAwake(){try{if(navigator.wakeLock&&!wake){wake=await navigator.wakeLock.request("screen");wake.addEventListener("release",()=>wake=null);}}catch(e){}}
document.addEventListener("click",keepAwake);document.addEventListener("visibilitychange",()=>{if(document.visibilityState==="visible")keepAwake();});
</script></body></html>)HTML";
