const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const viewDir = path.join(__dirname, '../deploy/PrismaUI_F4/views/Interface/FalloutAnomaly');
const html = fs.readFileSync(path.join(viewDir, 'radhud.html'), 'utf8');

const elements = {};
const timers = [];
const sentCommands = [];
let nowMs = 0;

function makeFakeElement() {
  const classes = new Set();
  return {
    children: [],
    style: { setProperty() {} },
    classes,
    appendChild(child) { this.children.push(child); },
    setAttribute() {},
    classList: {
      add(name) { classes.add(name); },
      remove(name) { classes.delete(name); },
      toggle(name, on) { if (on) classes.add(name); else classes.delete(name); },
      contains(name) { return classes.has(name); },
    },
  };
}

for (const match of html.matchAll(/id="([^"]+)"/g)) {
  elements[match[1]] = makeFakeElement();
}

const context = {
  document: {
    getElementById: (id) => elements[id],
    createElement: makeFakeElement,
    documentElement: {},
    addEventListener() {},
  },
  ResizeObserver: class { observe() {} },
  innerWidth: 1920,
  innerHeight: 1080,
  setTimeout(fn, delayMs) {
    timers.push({ fn, at: nowMs + delayMs, live: true });
    return timers.length - 1;
  },
  clearTimeout(id) {
    if (timers[id]) timers[id].live = false;
  },
  radDebug: (command) => sentCommands.push(command),
};
context.window = context;
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(viewDir, 'radhud.js'), 'utf8'), context);

function nextLiveTimerIndex(untilMs) {
  let best = -1;
  timers.forEach((timer, index) => {
    if (!timer.live || timer.at > untilMs) return;
    if (best < 0 || timer.at < timers[best].at) best = index;
  });
  return best;
}

function advanceSeconds(seconds) {
  const untilMs = nowMs + seconds * 1000;
  for (let index = nextLiveTimerIndex(untilMs); index >= 0; index = nextLiveTimerIndex(untilMs)) {
    const timer = timers[index];
    timer.live = false;
    nowMs = Math.max(nowMs, timer.at);
    timer.fn();
  }
  nowMs = untilMs;
}

function resetSent() {
  sentCommands.length = 0;
}

const showcase = context.RadShowcase;

assert.ok(
  html.includes('id="tabbtn-showcase"') && html.includes('id="tab-showcase"'),
  'tab markup'
);
assert.equal(elements.showcaseScenes.children.length, showcase.scenes.length, 'one button per scene');

showcase.run('ash');
advanceSeconds(1);
assert.deepEqual(sentCommands, ['storm:0']);
advanceSeconds(10);
assert.deepEqual(sentCommands, ['storm:0', 'skipwarn']);
advanceSeconds(60);
assert.equal(sentCommands.at(-1), 'endstorm');

resetSent();
showcase.startTour();
assert.deepEqual(sentCommands, ['god:on', 'close'], 'tour turns god mode on and closes the menu first');

showcase.stop();
advanceSeconds(1000);
assert.equal(sentCommands.length, 2, 'stop cancels everything still scheduled');

resetSent();
showcase.startTour();
context.SetDebugMenu(1);
advanceSeconds(1000);
assert.equal(sentCommands.length, 2, 'reopening the menu stops the tour');

resetSent();
showcase.startTour();
advanceSeconds(2000);
for (const scene of showcase.scenes) {
  assert.ok(sentCommands.includes(scene.steps[0][1]), 'tour plays ' + scene.id);
}
assert.equal(elements.showcaseStatus.textContent, 'Tour finished.');

console.log('showcase tests passed');
