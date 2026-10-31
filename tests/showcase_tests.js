const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const view=path.join(__dirname,'../deploy/PrismaUI_F4/views/Interface/FalloutAnomaly');
const html=fs.readFileSync(path.join(view,'radhud.html'),'utf8');
const elements={},timers=[],sent=[];
function element(){const e={children:[],style:{setProperty(){}},classes:new Set(),appendChild(c){this.children.push(c)},setAttribute(){}};e.classList={add(c){e.classes.add(c)},remove(c){e.classes.delete(c)},toggle(c,on){on?e.classes.add(c):e.classes.delete(c)},contains(c){return e.classes.has(c)}};return e;}
for(const m of html.matchAll(/id="([^"]+)"/g))elements[m[1]]=element();
let now=0;
const context={document:{getElementById:id=>elements[id],createElement:element,documentElement:{},addEventListener(){}},ResizeObserver:class{observe(){}},innerWidth:1920,innerHeight:1080,
 setTimeout(fn,ms){timers.push({fn,at:now+ms,live:true});return timers.length-1},clearTimeout(id){if(timers[id])timers[id].live=false},radDebug:c=>sent.push(c)};
context.window=context;vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(view,'radhud.js'),'utf8'),context);
function advance(seconds){const end=now+seconds*1000;for(;;){let next=-1;timers.forEach((t,i)=>{if(t.live&&t.at<=end&&(next<0||t.at<timers[next].at))next=i});if(next<0)break;const t=timers[next];t.live=false;now=Math.max(now,t.at);t.fn()}now=end}
const S=context.RadShowcase;
assert.ok(html.includes('id="tabbtn-showcase"')&&html.includes('id="tab-showcase"'),'tab markup');
assert.equal(elements.showcaseScenes.children.length,S.scenes.length,'one button per scene');
S.run('ash');advance(1);assert.deepEqual(sent,['storm:0']);advance(10);assert.deepEqual(sent,['storm:0','skipwarn']);advance(60);assert.equal(sent.at(-1),'endstorm');
sent.length=0;S.startTour();assert.equal(sent[0],'close','tour closes the menu first');
S.stop();advance(1000);assert.equal(sent.length,1,'stop cancels everything still scheduled');
sent.length=0;S.startTour();context.SetDebugMenu(1);advance(1000);assert.equal(sent.length,1,'reopening the menu stops the tour');
sent.length=0;S.startTour();advance(2000);
for(const s of S.scenes)assert.ok(sent.includes(s.steps[0][1]),'tour plays '+s.id);
assert.equal(elements.showcaseStatus.textContent,'Tour finished.');
console.log('showcase tests passed');
