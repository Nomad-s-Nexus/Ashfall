const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const view=path.join(__dirname,'../deploy/PrismaUI_F4/views/Interface/FalloutAnomaly');
function load(name){
 const html=fs.readFileSync(path.join(view,name),'utf8'),elements={},timeouts=[];
 function element(){const e={children:[],style:{setProperty(){}},classes:new Set(),appendChild(c){this.children.push(c)},setAttribute(k,v){this[k]=v}};e.classList={add(c){e.classes.add(c)},remove(c){e.classes.delete(c)},toggle(c,on){on?e.classes.add(c):e.classes.delete(c)}};Object.defineProperty(e,'textContent',{get(){return (this.text||'')+this.children.map(c=>c.textContent).join('')},set(v){this.text=String(v);this.children=[]}});return e;}
 for(const match of html.matchAll(/id="([^"]+)"/g))elements[match[1]]=element();
 const context={document:{getElementById:id=>elements[id],createElement:element,documentElement:{},body:element()},ResizeObserver:class{observe(){}},innerWidth:1920,innerHeight:1080,setTimeout(fn){timeouts.push(fn)},setInterval(){throw Error('Polling prohibited')}};context.window=context;vm.createContext(context);
 for(const match of html.matchAll(/<script>([\s\S]*?)<\/script>/g))vm.runInContext(match[1],context);
 return {context,elements,html,timeouts};
}
const climate=load('hud_climate.html');
const needs=load('hud_needs.html');
needs.context.updateNeeds({hunger:0,thirst:63,sleepDebt:100});
assert.equal(needs.elements.hungerValue.textContent,'0%');
assert.equal(needs.elements.thirstValue.textContent,'63%');
assert.equal(needs.elements.sleepDebtValue.textContent,'100%');
assert.equal(needs.elements.sleepDebt.className,'status danger');
assert.match(needs.elements.sleepDebt['aria-label'],/severity 3 of 3/);
needs.context.updateNeeds('{invalid');
needs.context.updateNeeds({hunger:NaN,thirst:1,sleepDebt:1});
assert.equal(needs.elements.thirstValue.textContent,'63%');
needs.context.updateNeeds({hunger:12,thirst:20,sleepDebt:8});assert.ok(needs.elements.hunger.hidden&&needs.elements.thirst.hidden&&needs.elements.sleepDebt.hidden);assert.ok(!needs.elements.root.classes.has('visible'));needs.context.updateNeeds({hunger:33,thirst:34,sleepDebt:0});assert.equal(needs.elements.hunger.hidden,true);assert.equal(needs.elements.thirst.className,'status warn');assert.ok(needs.elements.root.classes.has('visible'));needs.context.setEditorPreview(1);
needs.context.updateNeeds({hunger:18,thirst:24,sleepDebt:32});
needs.context.setEditorPreview(0);
assert.equal(needs.elements.hungerValue.textContent,'18%');
needs.context.updateNeeds({hunger:-12,thirst:120,sleepDebt:50});
assert.equal(needs.elements.hungerValue.textContent,'0%');
assert.equal(needs.elements.thirstValue.textContent,'100%');
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0,risk:20,disease:0});assert.equal(needs.elements.risk.hidden,true);assert.equal(needs.elements.illness.hidden,true);assert.ok(!needs.elements.root.classes.has('visible'));
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0,risk:60,disease:0});assert.equal(needs.elements.risk.hidden,false);assert.equal(needs.elements.riskValue.textContent,'60%');assert.equal(needs.elements.risk.className,'status warn');assert.equal(needs.elements.risk['aria-label'],'Disease exposure 60%');assert.equal(needs.elements.illness.hidden,true);assert.ok(needs.elements.root.classes.has('visible'));
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0,risk:95,disease:0});assert.equal(needs.elements.risk.className,'status danger');
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0,risk:10,disease:10});assert.equal(needs.elements.risk.hidden,true);assert.equal(needs.elements.illness.hidden,false);assert.equal(needs.elements.illnessValue.textContent,'FEVER');assert.equal(needs.elements.illness.className,'status warn');assert.match(needs.elements.illness['aria-label'],/incubating/);
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0,risk:10,disease:13});assert.equal(needs.elements.illnessValue.textContent,'FEVER 3');assert.equal(needs.elements.illness.className,'status danger');assert.match(needs.elements.illness['aria-label'],/stage 3 of 3/);
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0,risk:10,disease:22});assert.equal(needs.elements.illnessValue.textContent,'WASTING 2');assert.equal(needs.elements.illness.className,'status danger');
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0,risk:10,disease:99});assert.equal(needs.elements.illnessValue.textContent,'WASTING 3');
needs.context.updateNeeds({hunger:0,thirst:0,sleepDebt:0});assert.equal(needs.elements.risk.hidden,true);assert.equal(needs.elements.illness.hidden,true);
climate.context.updateClimate({core:37,warmth:100,ambient:-5,wetness:62,wind:16,stage:0,heat:70,warming:.25,cooling:.05,insulation:41});
assert.equal(climate.elements.warmValue.textContent,'100%');
assert.equal(climate.elements.wetValue.textContent,'62%');
assert.equal(climate.elements.airValue.textContent,'FREEZING');
assert.equal(climate.elements.windValue.textContent,'STRONG');
assert.match(climate.elements.warm['aria-label'],/NEAR HEAT/);
climate.context.updateClimate({core:28,warmth:0,ambient:15,wetness:200,stage:5});
assert.equal(climate.elements.warmValue.textContent,'0%');assert.equal(climate.elements.wetValue.textContent,'100%');
assert.equal(climate.elements.warm.className,'status danger');
const filter=load('hud_filter.html');
filter.context.updateAir({contaminated:false,masked:false});assert.equal(filter.elements.breathValue.textContent,'CLEAN');assert.equal(filter.elements.filter.hidden,true);assert.ok(!filter.elements.root.classes.has('on'));filter.context.updateAir({contaminated:true,masked:true,charge:100,condition:100});assert.ok(filter.elements.root.classes.has('on'));filter.context.updateAir({contaminated:false,masked:true,charge:40,condition:100});assert.ok(filter.elements.root.classes.has('on'));filter.context.updateAir({contaminated:false,masked:true,charge:100,condition:100});assert.ok(!filter.elements.root.classes.has('on'));
for(const [payload,expected] of [[{contaminated:true},'EXPOSED'],[{contaminated:true,masked:true,charge:80,condition:50},'FILTERED'],[{contaminated:true,masked:true,failed:true,charge:0,condition:50},'EXPOSED'],[{contaminated:true,sealed:true},'SEALED'],[{contaminated:true,sheltered:true},'COVERED']]){filter.context.updateAir(payload);assert.equal(filter.elements.breathValue.textContent,expected);}
for(const [am,expected,cls] of [[0,'OFF','status warn'],[1,'ON','status '],[2,'HELD','status ']]){filter.context.updateAir({contaminated:true,autoMask:am});assert.equal(filter.elements.auto.hidden,false);assert.equal(filter.elements.autoValue.textContent,expected);assert.equal(filter.elements.auto.className,cls);}
filter.context.updateAir({contaminated:true});assert.equal(filter.elements.auto.hidden,true);
filter.context.updateAir({masked:true,charge:-1,condition:-1});assert.equal(filter.elements.filter.hidden,true);assert.equal(filter.elements.mask.hidden,true);
climate.context.updateClimate({core:37,warmth:100,wetness:0,ambient:21,wind:3,stage:0,warming:0,cooling:0});assert.equal(climate.elements.root.className,'');climate.context.updateClimate({core:35.8,warmth:87,wetness:62,ambient:-4,wind:12,stage:1});assert.match(climate.elements.root.className,/visible/);const toxic=load('hud_toxic.html');
toxic.context.updateToxic({fallback:true,health:50,healthMax:100,ap:20,apMax:100,rads:300,radsMax:1000,tissue:150,permanentTissue:40});
assert.equal(toxic.elements.healthValue.textContent,'50%');assert.equal(toxic.elements.radsValue.textContent,'30%');assert.equal(toxic.elements.tissueValue.textContent,'15%');
toxic.context.updateToxic({fallback:false,tissue:0});assert.equal(toxic.elements.health.hidden,true);assert.ok(!toxic.elements.root.classes.has('visible'));
for(const page of [climate,filter,toxic,needs]){assert.ok(!/class="bar|id=".*Bar"/.test(page.html));page.context.setLocalHUDLayout({x:.2,y:.3,s:1.2,r:0,hidden:true});assert.equal(page.elements.root.style.display,'none');}
for(const page of [climate,filter,toxic,needs]){page.timeouts.forEach(fn=>fn());assert.equal(page.elements.root.style.left,'384px');assert.equal(page.elements.root.style.top,'324px');}
const debug=load('radhud.html');vm.runInContext(fs.readFileSync(path.join(view,'rad_diagnostics.js'),'utf8'),debug.context);
debug.context.updateClimateInfo({core:37,warmth:100,ambient:4,wind:12,wetness:60,heat:70,insulation:41,cooling:.1,warming:.3});
assert.match(debug.elements.climateReadout.textContent,/Body warmth100%/);assert.match(debug.elements.climateEffect.textContent,/Normal warmth reached/);
debug.context.updateClimateInfo({core:36,warmth:89,warming:.3,cooling:.1});assert.match(debug.elements.climateEffect.textContent,/Body is warming/);
debug.context.updateClimateInfo({core:28,warmth:0,warming:0,cooling:.1});assert.match(debug.elements.climateEffect.textContent,/model minimum/);
debug.context.updateAirInfo({contaminated:true,healthLoss:2,filterDrain:0,radGain:0,chokingEnabled:true});assert.match(debug.elements.airReadout.textContent,/Breathing HP loss2.00\/sec/);
const css=fs.readFileSync(path.join(view,'radhud.css'),'utf8');
for(const id of ['#maskOverlay','#visorFog']){const block=(css.match(new RegExp(id.replace('#','\\#')+'\\s*\\{([\\s\\S]*?)\\}'))||[])[1]||'';assert.ok(!/mask-image/.test(block),'the visor effects must not rely on CSS mask-image (Ultralight support is unproven): '+id);}
assert.match(css,/url\("visor_fog\.png"\)/,'the fog must use the baked clear-zone PNG');
assert.match(htmlOf('radhud.html'),/id="maskOverlay"[^>]*src="gasmask_overlay\.png"/,'the overlay must use the baked clear-zone PNG');
assert.match(fs.readFileSync(path.join(view,'radhud.js'),'utf8'),/var MASK_PNG_ENABLED = false;[\s\S]*d\.on && MASK_PNG_ENABLED/,'the PNG visor stays disabled while the 3D mask view is deployed');
function htmlOf(name){return fs.readFileSync(path.join(view,name),'utf8');}
const zlib=require('node:zlib');
function readPngAlpha(file){
 const buf=fs.readFileSync(file);let off=8,w=0,h=0,bd=0,ct=0;const idat=[];
 while(off<buf.length){const len=buf.readUInt32BE(off),type=buf.toString('ascii',off+4,off+8);const data=buf.subarray(off+8,off+8+len);if(type==='IHDR'){w=data.readUInt32BE(0);h=data.readUInt32BE(4);bd=data[8];ct=data[9];}else if(type==='IDAT'){idat.push(data);}else if(type==='IEND'){break;}off+=12+len;}
 assert.equal(bd,8,'expected 8-bit PNG: '+file);assert.equal(ct,6,'expected RGBA PNG: '+file);
 const raw=zlib.inflateSync(Buffer.concat(idat)),stride=w*4,out=Buffer.alloc(h*stride),bpp=4;let pos=0;
 for(let y=0;y<h;y++){const filter=raw[pos++],row=raw.subarray(pos,pos+stride);pos+=stride;const cur=out.subarray(y*stride,(y+1)*stride),prev=y?out.subarray((y-1)*stride,y*stride):null;
  for(let x=0;x<stride;x++){const a=x>=bpp?cur[x-bpp]:0,b=prev?prev[x]:0,c=(prev&&x>=bpp)?prev[x-bpp]:0;let v=row[x];
   if(filter===1){v+=a;}else if(filter===2){v+=b;}else if(filter===3){v+=Math.floor((a+b)/2);}else if(filter===4){const p=a+b-c,pa=Math.abs(p-a),pb=Math.abs(p-b),pc=Math.abs(p-c);v+=(pa<=pb&&pa<=pc)?a:(pb<=pc?b:c);}cur[x]=v&0xff;}}
 return (fx,fy)=>out[Math.min(h-1,Math.round(fy*(h-1)))*stride+Math.min(w-1,Math.round(fx*(w-1)))*4+3];
}
for(const [name,file,clearPoints] of [['overlay','gasmask_overlay.png',[['top-left messages',0.20,0.15],['message edge',0.40,0.15],['lower centre glass',0.50,0.80],['centre readouts',0.50,0.50]]],['fog','visor_fog.png',[['top-left messages',0.10,0.10],['message edge',0.40,0.15],['bottom-centre XP',0.50,0.93],['centre readouts',0.50,0.50]]]]){const alpha=readPngAlpha(path.join(view,file));
 for(const [point,fx,fy] of clearPoints)
  assert.ok(alpha(fx,fy)<=8,`${name} must be clear over the ${point} (alpha ${alpha(fx,fy)})`);
 if(name==='overlay')for(const [point,fx,fy] of [['top-left frame corner',0.02,0.02],['bottom-centre frame',0.50,0.99],['left frame',0.01,0.50],['right frame',0.99,0.50],['bottom-left frame',0.05,0.90]])
  assert.ok(alpha(fx,fy)>=250,`${name} must keep the opaque visor frame at the ${point} (alpha ${alpha(fx,fy)})`);
 for(const [point,fx,fy] of [['bottom-left edge',0.05,0.90],['top-right edge',0.90,0.05],['right edge',0.85,0.10]])
  assert.ok(alpha(fx,fy)>=40,`${name} must keep the visor look over the ${point} (alpha ${alpha(fx,fy)})`);
}
const phosNeeds=load('hud_needs.html');
phosNeeds.context.setTheme('phosphor');
assert.ok(phosNeeds.context.document.body.classes.has('theme-phosphor'));
phosNeeds.context.updateNeeds({hunger:0,thirst:70,sleepDebt:80});
assert.equal(phosNeeds.elements.sleepDebt.className,'status danger');
assert.equal(phosNeeds.elements.sleepDebtCap.textContent,'EXHAUSTED');
assert.ok(Math.abs(phosNeeds.elements.sleepDebtRing['stroke-dashoffset']-125.66*0.8)<.01);
assert.equal(phosNeeds.elements.thirst.className,'status warn');
assert.equal(phosNeeds.elements.thirstCap.textContent,'PARCHED');
assert.equal(phosNeeds.elements.hunger.hidden,true);
phosNeeds.context.setTheme('classic');
assert.ok(!phosNeeds.context.document.body.classes.has('theme-phosphor'));
assert.equal(phosNeeds.elements.thirst.className,'status danger');
assert.equal(phosNeeds.elements.sleepDebt.className,'status danger');
const phosToxic=load('hud_toxic.html');
phosToxic.context.setTheme('phosphor');
phosToxic.context.updateToxic({fallback:true,health:50,healthMax:100,ap:20,apMax:100,rads:300,radsMax:1000,tissue:149,permanentTissue:400});
assert.equal(phosToxic.elements.health.hidden,true);assert.equal(phosToxic.elements.stamina.hidden,true);assert.equal(phosToxic.elements.rads.hidden,true);
assert.equal(phosToxic.elements.tissue.className,'status ');
assert.ok(Math.abs(phosToxic.elements.tissueRing['stroke-dashoffset']-125.66*0.851)<.01);
assert.ok(Math.abs(phosToxic.elements.scarRing['stroke-dashoffset']-144.51*0.6)<.01);
phosToxic.context.updateToxic({fallback:true,tissue:150});assert.equal(phosToxic.elements.tissue.className,'status warn');
phosToxic.context.updateToxic({fallback:true,tissue:600});assert.equal(phosToxic.elements.tissue.className,'status danger');
const classicToxic=load('hud_toxic.html');
classicToxic.context.updateToxic({fallback:true,health:50,healthMax:100,ap:20,apMax:100,rads:300,radsMax:1000,tissue:150,permanentTissue:400});
assert.equal(classicToxic.elements.health.hidden,false);assert.equal(classicToxic.elements.tissueRing['stroke-dashoffset'],undefined);
const phosFilter=load('hud_filter.html');
phosFilter.context.setTheme('phosphor');
phosFilter.context.updateAir({contaminated:true,masked:true,charge:20,condition:50});
assert.equal(phosFilter.elements.breathValue.textContent,'FILTERED');
assert.equal(phosFilter.elements.filter.className,'status danger');
assert.equal(phosFilter.elements.mask.className,'status warn');
assert.ok(Math.abs(phosFilter.elements.filterRing['stroke-dashoffset']-125.66*0.8)<.01);
assert.ok(Math.abs(phosFilter.elements.maskRing['stroke-dashoffset']-125.66*0.5)<.01);
filter.context.setKeys('{"replace":"F6","repair":"F7"}');
assert.equal(filter.elements.filterKey.textContent,'[F6]');assert.equal(filter.elements.filterKey.hidden,false);
assert.equal(filter.elements.maskKey.textContent,'[F7]');assert.equal(filter.elements.maskKey.hidden,false);
filter.context.setKeys({replace:'',repair:'F7'});
assert.equal(filter.elements.filterKey.hidden,true);assert.equal(filter.elements.maskKey.textContent,'[F7]');
filter.context.setKeys(null);
assert.equal(filter.elements.filterKey.hidden,true);assert.equal(filter.elements.maskKey.hidden,true);
climate.context.setTheme('phosphor');
assert.ok(climate.context.document.body.classes.has('theme-phosphor'));
climate.context.setTheme('classic');
assert.ok(!climate.context.document.body.classes.has('theme-phosphor'));
const radTheme={document:{body:{classList:{toggle(name,on){this.last=[name,on];}}}}};
radTheme.window=radTheme;vm.createContext(radTheme);
vm.runInContext(fs.readFileSync(path.join(view,'radhud.js'),'utf8'),radTheme);
radTheme.setTheme('phosphor');assert.deepEqual(radTheme.document.body.classList.last,['theme-phosphor',true]);
radTheme.setTheme('classic');assert.deepEqual(radTheme.document.body.classList.last,['theme-phosphor',false]);
console.log('Status HUD verified: warmth endpoints, environment, breathing gates, percentages, hidden layout, full diagnostics and visor clear zones, Phosphor theme rings, needs inversion, tissue thresholds and key hints');
