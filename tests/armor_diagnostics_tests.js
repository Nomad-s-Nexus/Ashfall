const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const view = path.join(__dirname, '../deploy/PrismaUI_F4/views/Interface/FalloutAnomaly');
class Element {
  constructor(tag) {
    this.tagName = tag;
    this.children = [];
    this.style = {};
    this.classes = new Set();
    this.classList = {
      add: name => this.classes.add(name),
      remove: name => this.classes.delete(name),
      toggle: (name, on) => on ? this.classes.add(name) : this.classes.delete(name)
    };
  }
  set textContent(text) { this.text = String(text); this.children = []; }
  get textContent() { return (this.text || '') + this.children.map(child => child.textContent).join(''); }
  set innerHTML(text) { throw new Error('Armor names must use textContent'); }
  appendChild(child) { this.children.push(child); }
  setAttribute(name, value) { this[name] = value; }
  querySelector(tag) { return this.children.find(child => child.tagName === tag); }
}
const elements = {};
const html = fs.readFileSync(path.join(view, 'radhud.html'), 'utf8');
for (const match of html.matchAll(/<(\w+)[^>]*\bid="([^"]+)"/g)) elements[match[2]] = new Element(match[1]);
const regions = ['head', 'torso', 'leftArm', 'rightArm', 'leftLeg', 'rightLeg'];
for (const region of regions) elements['armorRegion-' + region].appendChild(new Element('ul'));
const forbidden = () => { throw new Error('Read-only armor rendering must not poll or send commands'); };
const context = vm.createContext({
  document: {getElementById: id => elements[id], createElement: tag => new Element(tag)},
  setTimeout: forbidden, setInterval: forbidden, requestAnimationFrame: forbidden, radDebug: forbidden
});
context.window = context;
vm.runInContext(fs.readFileSync(path.join(view, 'radhud.js'), 'utf8'), context);
const names = id => elements[id].children.map(row => row.children[0].textContent);
const callout = region => elements['armorRegion-' + region].querySelector('ul');
const input = {insulation: 43, effectiveInsulation: 30, powerArmor: false, pieces: [
  {name: 'Pip-Boy', warmth: 45, insulation: 0},
  {name: 'Left arm', warmth: 45, insulation: 5, leftArm: true},
  {name: 'Fatigues', warmth: 45, insulation: 41, torso: true, leftArm: true, rightArm: true, leftLeg: true, rightLeg: true},
  {name: 'Zero override', warmth: 0, insulation: 0, head: true, override: true},
  {name: '<img src=x onerror=alert(1)>', warmth: 1, insulation: 0, head: true, override: true},
  {name: 'Wedding ring', warmth: 45, insulation: 0}
]};
const original = JSON.stringify(input);
context.setArmorInfo(original);
assert.equal(elements.armorInsulation.textContent, '43%');
assert.equal(elements.armorEffective.textContent, '30%');
assert.equal(elements.armorEffectiveMeter.style.width, '30%');
assert.equal(elements.armorPower.textContent, 'UNSEALED');
assert.ok(!elements.armorFigure.classes.has('power-armor'));
assert.equal(elements.armorFigure['aria-label'], 'Front-facing Vault suit reference schematic');
assert.deepEqual(names('armorList'), ['Fatigues', 'Left arm', '<img src=x onerror=alert(1)>']);
assert.deepEqual(names('armorNonThermalList'), ['Pip-Boy', 'Wedding ring', 'Zero override']);
assert.equal(elements.armorThermalCount.textContent, '3 RECORDS');
assert.equal(elements.armorNonThermalCount.textContent, '3 RECORDS');
assert.equal(elements.armorList.children[2].children[4].textContent, 'YES');
assert.equal(elements.armorNonThermalList.children[2].children[4].textContent, 'YES');
assert.equal(elements.armorList.children[1].children[3].textContent, 'L ARM');
assert.deepEqual(callout('leftArm').children.map(item => item.textContent), ['Fatigues', 'Left arm']);
assert.deepEqual(callout('rightArm').children.map(item => item.textContent), ['Fatigues']);
assert.equal(callout('head').textContent, '<img src=x onerror=alert(1)>');
assert.ok(regions.every(region => elements['armorRegion-' + region].classes.has('covered')));
context.setArmorInfo(input);
assert.equal(JSON.stringify(input), original);
assert.equal(elements.armorList.children.length, 3);
context.showTab('armor');
assert.ok(elements.debugPanel.classes.has('armor-view'));
context.showTab('rad');
assert.ok(!elements.debugPanel.classes.has('armor-view'));
context.showTab('armor');
assert.equal(elements.armorList.children.length, 3);

context.setArmorInfo({insulation: 50, effectiveInsulation: 85, powerArmor: true, inPowerArmor: true, pieces: []});
assert.equal(elements.armorPower.textContent, 'SEALED');
assert.equal(elements.armorPowerDetail.textContent, 'COMPLETE SUIT DETECTED');
assert.equal(elements.armorEffective.textContent, '85%');
assert.ok(elements.armorFigure.classes.has('power-armor'));
assert.equal(elements.armorFigure['aria-label'], 'Front-facing Power Armor reference schematic');
assert.equal(elements.armorList.textContent, 'NO WORN ARMOR');
assert.equal(elements.armorNonThermalList.textContent, 'NONE EQUIPPED');
for (const region of regions) {
  assert.equal(callout(region).textContent, 'NO THERMAL COVERAGE');
  assert.ok(!elements['armorRegion-' + region].classes.has('covered'));
}
context.setArmorInfo({powerArmor: false, inPowerArmor: true, pieces: []});
assert.ok(elements.armorFigure.classes.has('power-armor'));
assert.equal(elements.armorPower.textContent, 'UNSEALED');
context.setArmorInfo({powerArmor: true, inPowerArmor: false, pieces: []});
assert.ok(!elements.armorFigure.classes.has('power-armor'));
context.setArmorInfo({inPowerArmor: false, pieces: [{name: 'Flashlight', warmth: 45, insulation: 0}]});
assert.ok(!elements.armorFigure.classes.has('power-armor'));
assert.equal(elements.armorList.textContent, 'NO THERMAL CONTRIBUTION');
assert.deepEqual(names('armorNonThermalList'), ['Flashlight']);
for (const malformed of [null, undefined, 'null', '{bad', 7]) context.setArmorInfo(malformed);
assert.deepEqual(names('armorNonThermalList'), ['Flashlight']);
context.setArmorInfo({insulation: Infinity, effectiveInsulation: -5, pieces: [null, 'invalid', {name: '', warmth: 200, insulation: 999, torso: true}]});
assert.equal(elements.armorInsulation.textContent, '0%');
assert.equal(elements.armorEffective.textContent, '0%');
assert.equal(elements.armorList.children[0].children[1].textContent, '100%');
assert.deepEqual(names('armorList'), ['<unnamed>']);
context.setArmorInfo({pieces: {}});
assert.equal(elements.armorList.textContent, 'NO WORN ARMOR');
assert.ok(!elements.armorFigure.classes.has('power-armor'));
console.log('Armor diagnostics verified: bridge, read-only rendering, thermal grouping, six callouts, replacement and malformed input');
