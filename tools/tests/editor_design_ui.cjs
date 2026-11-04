// SPDX-License-Identifier: GPL-2.0-only
// Exercise production design actions against the firmware's real catalogue.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../apextx-theme-editor.html'), 'utf8');
const firmware = JSON.parse(fs.readFileSync(path.join(__dirname, '../apextx-theme-editor-catalog.json'), 'utf8'));
const historyScope = {module:{exports:{}}};
const historyStart = html.indexOf('class EditorHistory');
vm.runInNewContext(html.slice(historyStart, html.indexOf('</script>',historyStart)), historyScope);
const History = historyScope.module.exports;
const plain = value => JSON.parse(JSON.stringify(value));
function setup() {
 const initial={0:{LayoutId:'Layout1x1',layoutData:{zones:{0:{widgetName:'"Text"',widgetData:{options:{99:{future:'preserve'}}}}},future:'preserve'}}};
 const scope=vm.createContext({console,car:{screenData:plain(initial)},selScreen:'0',selZone:0,editorHistoryKey:'model',editorHistories:new Map([['model',new History(initial)]]),
  catalog:plain(firmware),state:{colors:{FOCUS:'00CCFF',WARNING:'FF3300'},nb4:{STEERING:'ABCDEF'},bgPattern:'image',bgImage:{src:'unchanged'},cardOpacity:100},
  qt:value=>JSON.stringify(value),zoneCount:layout=>layout.racing?4:layout.zones.length,
  designWord:key=>key,status(){},buildScreens(){},refreshAll(){},dirtyCar(){scope.carDirty=true;}});
 scope.layoutById=id=>scope.catalog.layouts.find(layout=>layout.id===id);
 scope.widgetById=id=>scope.catalog.widgets.find(widget=>widget.id===id);
 scope.screenKeys=()=>Object.keys(scope.car.screenData).sort((a,b)=>a-b);
 scope.curScreen=()=>scope.car.screenData[scope.selScreen];
 scope.historySample=()=>scope.editorHistories.get('model').push(scope.car.screenData);
 scope.rgb=hex=>[0,2,4].map(i=>parseInt(hex.slice(i,i+2),16));
 scope.themeValue=()=>plain(scope.state);
 scope.themeHistory=new History(scope.state);
 const definitions=html.slice(html.indexOf('const SCREEN_DESIGNS ='),html.indexOf('function buildScreenDesigns()'));
 vm.runInContext(definitions+'\nthis.designs=SCREEN_DESIGNS;this.cards=CARD_LIBRARY;',scope);
 vm.runInContext(html.slice(html.indexOf('function surfaceColors('),html.indexOf('function buildSurfaceStyles()')),scope);
 return scope;
}
for(const design of setup().designs){
 const s=setup();assert.equal(s.availableScreenDesign(design),true,design.id+' must use registered layout and widgets');
 const old=JSON.stringify(s.car.screenData[0]);assert.equal(s.appendScreenDesign(design.id),true);
 assert.equal(JSON.stringify(s.car.screenData[0]),old,'Adding a design preserves the original screen');
 assert.equal(s.selScreen,'1');assert.equal(s.car.screenData[1].LayoutId,design.layout);
 assert.deepEqual(Object.values(s.car.screenData[1].layoutData.zones).map(z=>JSON.parse(z.widgetName)),Array.from(design.widgets));
 assert.equal(Object.keys(s.editorHistories.get('model').undo()).length,1,'One undo removes one complete design');
 const blocked=setup();blocked.catalog.widgets=blocked.catalog.widgets.filter(w=>w.id!==design.widgets[0]);
 assert.equal(blocked.appendScreenDesign(design.id),false,'Capabilities are checked again when applying');assert.equal(blocked.screenKeys().length,1);
}
{
 const s=setup();assert.equal(s.duplicateCurrentScreen(),true);assert.deepEqual(plain(s.car.screenData[1]),plain(s.car.screenData[0]));
 s.car.screenData[1].layoutData.zones[0].widgetData.options[99].future='modified';
 assert.equal(s.car.screenData[0].layoutData.zones[0].widgetData.options[99].future,'preserve','Duplicate must not share option objects');
 while(s.screenKeys().length<10)s.duplicateCurrentScreen();
 assert.equal(s.duplicateCurrentScreen(),false);assert.equal(s.appendScreenDesign('race'),false);assert.equal(s.screenKeys().length,10);
 assert.deepEqual(s.screenKeys(),Array.from({length:10},(_,i)=>String(i)),'Screen indices remain contiguous');
}
{
 for(const action of [s=>s.appendScreenDesign('race'),s=>s.duplicateCurrentScreen()]){
  const s=setup();s.car.screenData['2']={LayoutId:'Layout1x1',layoutData:{zones:{},future:'keep slot 2'}};
  const old0=JSON.stringify(s.car.screenData['0']),old2=JSON.stringify(s.car.screenData['2']);
  assert.equal(action(s),true);assert.equal(s.selScreen,'1');
  assert.equal(JSON.stringify(s.car.screenData['0']),old0);assert.equal(JSON.stringify(s.car.screenData['2']),old2,'Sparse screen imports must never overwrite an existing slot');
 }
}
{
 const s=setup();for(const ids of Object.values(s.cards))for(const id of ids)assert.ok(s.widgetById(id),id);
 for(const id of ['graphite','midnight','paper','tinted','glass','contrast']){
  const before=plain(s.state);const style=s.surfaceColors(id);s.applySurfaceStyle(id);
  assert.equal(s.state.cardOpacity,style.opacity);assert.ok(style.opacity>=30&&style.opacity<=100);
  for(const value of Object.values(style.colors))assert.match(value,/^[0-9A-F]{6}$/);
  assert.deepEqual(plain(s.state.nb4),before.nb4);assert.deepEqual(plain(s.state.bgImage),before.bgImage);assert.equal(s.state.bgPattern,before.bgPattern);
  assert.equal(s.state.colors.WARNING,before.colors.WARNING,'Surface styling preserves warning colours');
 }
}
console.log('Editor designs: supported templates, capability recheck, preserved screens, undo, deep duplication, ten-screen limit, card catalogue and surface styles passed.');
