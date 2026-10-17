/* Data-only theme renderer. Never evaluates code from imported packages. */
const ThemePreview=(()=>{
 let theme,background,faces,portraits,fonts,logo,mode='dashboard',rpm=0,auto=true,start=0,last=0,raf=0;
 const el=id=>document.getElementById(id),ctx=el('preview-canvas').getContext('2d');
 const decode=s=>Uint8Array.from(atob(s),c=>c.charCodeAt(0));
 function raster(s,w,h){const bytes=decode(s);if(bytes.length!==w*h*2)throw Error('图片尺寸错误');const canvas=document.createElement('canvas');canvas.width=w;canvas.height=h;const c=canvas.getContext('2d'),im=c.createImageData(w,h);for(let i=0;i<w*h;i++){const v=bytes[i*2]|bytes[i*2+1]<<8;im.data.set([(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31,255],i*4)}c.putImageData(im,0,0);return canvas}
 function bitmap(text,x,y,index,color){const f=fonts[index];let at=x;for(const ch of text){const g=f.glyphs[ch];if(!g)continue;const [offset,left,top,w,h,advance]=g;if(w&&h){const c=document.createElement('canvas');c.width=w;c.height=h;const k=c.getContext('2d'),im=k.createImageData(w,h),rgb=color.match(/[a-f0-9]{2}/gi).map(x=>parseInt(x,16));for(let i=0;i<w*h;i++)im.data.set([...rgb,f.bytes[offset+i]],i*4);k.putImageData(im,0,0);ctx.drawImage(c,at+left,y+top)}at+=advance}}
 function width(text,index){return [...text].reduce((n,c)=>n+(fonts[index].glyphs[c]?.[5]||0),0)}
 async function check(t){
  if(t.format!=='suki-gauge-theme'||t.schemaVersion!==1||t.target?.width!==480||t.target?.height!==320||t.target?.engine!=='suki-gauge-v1')throw Error('当前预览支持 ILI9488 480×320 layout-v1 主题');
  const canonical=v=>Array.isArray(v)?'['+v.map(canonical).join(',')+']':v&&typeof v==='object'?'{'+Object.keys(v).sort().map(k=>JSON.stringify(k)+':'+canonical(v[k])).join(',')+'}':JSON.stringify(v);
  const plain={...t};delete plain.integrity;const hash=[...new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(canonical(plain))))].map(v=>v.toString(16).padStart(2,'0')).join('');
  if(hash!==t.integrity?.payloadSha256)throw Error('主题完整性校验失败');
  for(const color of Object.values(t.palette))if(!/^#[0-9a-f]{6}$/i.test(color))throw Error('主题颜色格式错误');
  if(t.resources.bitmapFonts.length!==4)throw Error('主题字体错误');
  for(const f of t.resources.bitmapFonts){const bytes=decode(f.mask);if(bytes.length>100000)throw Error('字体资源过大');for(const g of Object.values(f.glyphs)){if(!Array.isArray(g)||g.length!==6||g.some(v=>!Number.isInteger(v)))throw Error('字体格式错误');const[o,l,top,w,h,a]=g;if(o<0||w<0||h<0||w>120||h>120||o+w*h>bytes.length||a<0||a>120)throw Error('字体资源越界')}}
 }
 function dashboard(now){const p=theme.palette;ctx.drawImage(background,0,0);bitmap('SIMULATION',114,44,3,p.cyan);bitmap('ENGINE RPM',28,82,2,p.muted);const value=String(Math.round(rpm));bitmap(value,187-width(value,0),92,0,p.yellow);bitmap('RPM',203,151,2,p.white);
  for(let i=0;i<12;i++){ctx.fillStyle=i<rpm/7000*12?(rpm>=6000?p.pink:rpm>=5000?p.yellow:p.cyan):p.muted;ctx.globalAlpha=i<rpm/7000*12?1:.18;ctx.fillRect(14+i*38,18,22,7)}ctx.globalAlpha=1;
  ctx.drawImage(faces[rpm>=5000?1:0],336,117);
  const values=[32+rpm/1500,89,3+rpm/100,12+rpm/100,5+rpm/100],names=['IAT','ECT','MAF','LOAD','TPS'];
  for(let i=0;i<5;i++){const x=16+i*92;bitmap(names[i],x,231,2,p.muted);bitmap(i===2?values[i].toFixed(1):String(Math.round(values[i])),x,251,1,p.white);ctx.fillStyle=p.cyan;ctx.fillRect(x,290,Math.min(73,values[i]/(i===2?200:120)*73),3)}
  ctx.strokeStyle=p.cyan;ctx.beginPath();for(let x=0;x<190;x++){const y=208-((Math.sin((x+now/40)/30)+1)/2)*Math.max(4,rpm/6500*28);x?ctx.lineTo(22+x,y):ctx.moveTo(22+x,y)}ctx.stroke();
 }
 function boot(now){const t=Math.min((now-start)/3800,1),p=theme.palette;ctx.fillStyle=p.bg;ctx.fillRect(0,0,480,320);ctx.globalAlpha=Math.min(1,t*4);ctx.drawImage(portraits[rpm>=5000?1:0],110,36);ctx.globalAlpha=1;bitmap(theme.boot.labels.top,18,12,3,p.cyan);bitmap(theme.boot.labels.footer,18,287,3,p.muted);ctx.fillStyle=p.cyan;ctx.fillRect(18,313,444*t,3);el('preview-status').textContent=t<1?'开机动画近似预览 · '+Math.round(t*100)+'%':'开机动画完成，已切回仪表';if(t>=1)mode='dashboard';}
 function frame(now){if(!el('preview').open){cancelAnimationFrame(raf);return}if(auto&&mode==='dashboard'){rpm=(rpm+Math.min(100,now-(last||now))*0.65)%6501;el('preview-slider').value=String(rpm)}last=now;el('preview-rpm').value=String(Math.round(rpm));mode==='boot'?boot(now):dashboard(now);raf=requestAnimationFrame(frame)}
 async function open(t){await check(t);const a=t.resources;const prepared={background:raster(a.background,480,320),faces:a.character.faces.map(s=>raster(s,100,49)),portraits:a.boot.portraits.map(s=>raster(s,260,230)),fonts:a.bitmapFonts.map(f=>({...f,bytes:decode(f.mask)}))};theme=t;({background,faces,portraits,fonts}=prepared);mode='dashboard';rpm=0;last=0;el('preview-name').textContent=t.name+' · 主题预览';el('preview-status').textContent='数据为模拟值；本地预览文件不会自动上传。';if(!el('preview').open)el('preview').showModal();cancelAnimationFrame(raf);raf=requestAnimationFrame(frame)}
 el('preview-slider').oninput=e=>{rpm=Number(e.target.value);auto=false;el('preview-auto').textContent='开启自动爬升'};
 el('preview-auto').onclick=()=>{auto=!auto;el('preview-auto').textContent=auto?'暂停自动爬升':'开启自动爬升'};
 el('preview-dashboard').onclick=()=>{mode='dashboard';el('preview-status').textContent='模拟仪表 · 0–6500 rpm'};
 el('preview-boot').onclick=()=>{mode='boot';start=performance.now()};
 el('preview').addEventListener('close',()=>cancelAnimationFrame(raf));
 return {open};
})();
