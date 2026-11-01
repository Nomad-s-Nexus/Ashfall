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
showcase.run('fever');
advanceSeconds(1);
assert.deepEqual(sentCommands, ['time:freeze', 'needs:-1:-1:-1:1:2'], 'needs scene freezes time before applying');
advanceSeconds(20);
assert.deepEqual(sentCommands.slice(-2), ['needs:-1:-1:-1:0:0', 'time:resume'], 'needs scene reverts only what it set, then resumes the clock');

const freezingScenes = showcase.scenes.filter((s) => s.steps.some(([, c]) => c === 'time:freeze'));
assert.deepEqual(
  Array.from(freezingScenes, (s) => s.id),
  ['peckish', 'starving', 'exhausted', 'fever', 'wasting'],
  'the needs and disease scenes are the ones that freeze time'
);
for (const scene of freezingScenes) {
  assert.equal(scene.steps.at(-1)[1], 'time:resume', 'last command of ' + scene.id + ' resumes the clock');
  assert.equal(scene.steps.at(-1)[0], 0, 'resume of ' + scene.id + ' follows the revert at the same delay slot');
  resetSent();
  showcase.run(scene.id);
  advanceSeconds(60);
  assert.equal(sentCommands.at(0), 'time:freeze', scene.id + ' freezes first');
  assert.equal(sentCommands.at(-1), 'time:resume', scene.id + ' ends with the resume');
  assert.equal(sentCommands.filter((c) => c === 'time:resume').length, 1, scene.id + ' resumes exactly once');
  assert.equal(sentCommands.filter((c) => c === 'time:freeze').length, 1, scene.id + ' freezes exactly once');
  const revert = scene.steps[2][1];
  assert.equal(sentCommands.at(-2), revert, scene.id + ' reverts before it resumes');

  resetSent();
  showcase.run(scene.id);
  advanceSeconds(3);
  assert.equal(sentCommands.length, 2, scene.id + ' is mid-play');
  showcase.stop();
  assert.deepEqual(sentCommands.slice(2), [revert, 'time:resume'], 'stop mid-' + scene.id + ' sends the revert and the resume');
  advanceSeconds(1000);
  assert.equal(sentCommands.length, 4, 'nothing fires after stop mid-' + scene.id);
  showcase.stop();
  assert.equal(sentCommands.length, 4, 'a second stop sends nothing more');

  resetSent();
  showcase.run(scene.id);
  advanceSeconds(3);
  showcase.run('ash');
  assert.deepEqual(sentCommands.slice(2, 4), [revert, 'time:resume'], 'starting another scene mid-' + scene.id + ' sends the revert and the resume first');
  advanceSeconds(1);
  assert.equal(sentCommands.at(-1), 'storm:0', 'the new scene then plays');
  showcase.stop();

  resetSent();
  showcase.run(scene.id);
  advanceSeconds(3);
  context.SetDebugMenu(1);
  assert.deepEqual(sentCommands.slice(2), [revert, 'time:resume'], 'reopening the menu mid-' + scene.id + ' sends the revert and the resume');
  advanceSeconds(1000);
}

resetSent();
showcase.run('ash');
advanceSeconds(3);
showcase.stop();
assert.deepEqual(sentCommands, ['storm:0'], 'stop on a scene that never freezes sends nothing extra');

resetSent();
showcase.run('fever');
advanceSeconds(60);
resetSent();
showcase.stop();
assert.deepEqual(sentCommands, [], 'stop after a needs scene finished sends nothing');

resetSent();
showcase.startTour();
let tourElapsed = 0;
while (!sentCommands.includes('time:freeze') && tourElapsed < 2000) { advanceSeconds(1); tourElapsed += 1; }
assert.ok(sentCommands.includes('time:freeze'), 'tour reaches a needs scene');
showcase.stop();
assert.equal(sentCommands.at(-1), 'time:resume', 'stopping the tour inside a needs scene resumes the clock');
advanceSeconds(1000);
resetSent();
showcase.run('needsclear');
advanceSeconds(1);
assert.deepEqual(sentCommands, ['needs:0:0:0:0:0', 'time:resume'], 'clear needs resumes the clock');

for (const scene of showcase.scenes.filter((s) => s.steps.some(([, c]) => c.startsWith('needs:')))) {
  for (const [, command] of scene.steps) {
    if (!command.startsWith('needs:')) continue;
    assert.match(command, /^needs:(-1|[0-9.]+):(-1|[0-9.]+):(-1|[0-9.]+):(-1|[0-2]):[0-3]$/, 'well-formed needs command in ' + scene.id);
  }
}

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
  for (const [, command] of scene.steps) {
    assert.ok(sentCommands.includes(command), 'tour plays ' + scene.id + ': ' + command);
  }
}
assert.equal(elements.showcaseStatus.textContent, 'Tour finished.');

console.log('showcase tests passed');
