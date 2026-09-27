const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const view=path.join(__dirname,'../deploy/PrismaUI_F4/views/Interface/FalloutAnomaly');
function load(name){
 const html=fs.readFileSync(path.join(view,name),'utf8'),elements={},timeouts=[];
 function element(){const e={children:[],style:{setProperty(){}},classes:new Set(),appendChild(c){this.children.push(c)},setAttribute(k,v){this[k]=v}};e.classList={add(c){e.classes.add(c)},remove(c){e.classes.delete(c)},toggle(c,on){on?e.classes.add(c):e.classes.delete(c)}};Object.defineProperty(e,'textContent',{get(){return (this.text||'')+this.children.map(c=>c.textContent).join('')},set(v){this.text=String(v);this.children=[]}});return e;}
 for(const match of html.matchAll(/id="([^"]+)"/g))elements[match[1]]=element();
 const context={document:{getElementById:id=>elements[id],createElement:element,documentElement:{}},ResizeObserver:class{observe(){}},innerWidth:1920,innerHeight:1080,setTimeout(fn){timeouts.push(fn)},setInterval(){throw Error('Polling prohibited')}};context.window=context;vm.createContext(context);
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
assert.match(needs.elements.sleepDebt['aria-label'],/observation only/);
needs.context.updateNeeds('{invalid');
needs.context.updateNeeds({hunger:NaN,thirst:1,sleepDebt:1});
assert.equal(needs.elements.thirstValue.textContent,'63%');
needs.context.setEditorPreview(1);
needs.context.updateNeeds({hunger:18,thirst:24,sleepDebt:32});
needs.context.setEditorPreview(0);
assert.equal(needs.elements.hungerValue.textContent,'18%');
needs.context.updateNeeds({hunger:-12,thirst:120,sleepDebt:50});
assert.equal(needs.elements.hungerValue.textContent,'0%');
assert.equal(needs.elements.thirstValue.textContent,'100%');
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
filter.context.updateAir({contaminated:false,masked:false});assert.equal(filter.elements.breathValue.textContent,'CLEAN');assert.equal(filter.elements.filter.hidden,true);
for(const [payload,expected] of [[{contaminated:true},'EXPOSED'],[{contaminated:true,masked:true,charge:80,condition:50},'FILTERED'],[{contaminated:true,masked:true,failed:true,charge:0,condition:50},'EXPOSED'],[{contaminated:true,sealed:true},'SEALED'],[{contaminated:true,sheltered:true},'COVERED']]){filter.context.updateAir(payload);assert.equal(filter.elements.breathValue.textContent,expected);}
filter.context.updateAir({masked:true,charge:-1,condition:-1});assert.equal(filter.elements.filter.hidden,true);assert.equal(filter.elements.mask.hidden,true);
const toxic=load('hud_toxic.html');
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
console.log('Status HUD verified: warmth endpoints, environment, breathing gates, percentages, hidden layout and full diagnostics');
