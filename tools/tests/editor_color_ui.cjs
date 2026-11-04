// SPDX-License-Identifier: GPL-2.0-only
// Exercise the production visual picker, colour handlers and export boundary.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../apextx-theme-editor.html'), 'utf8');
const source = html.slice(html.indexOf('const PICKER_WORDS='), html.indexOf('function segment('));
const historyScope={module:{exports:{}}};
const historyStart=html.indexOf('class EditorHistory');
vm.runInNewContext(html.slice(historyStart,html.indexOf('</script>',historyStart)),historyScope);
const History=historyScope.module.exports;
let document;
function element(tag){return {tag,children:[],value:'',style:{setProperty(key,value){this[key]=value;}},attributes:{},listeners:{},
 append(...children){this.children.push(...children);},setAttribute(key,value){this.attributes[key]=value;},addEventListener(key,callback){this.listeners[key]=callback;},
 focus(){document.activeElement=this;},showModal(){this.open=true;},close(){this.open=false;},setPointerCapture(){},getBoundingClientRect(){return {left:0,top:0,width:100,height:100};},
 querySelector(selector){for(const child of this.children){if((selector[0]==='.'&&child.className?.split(' ').includes(selector.slice(1)))||child.tag===selector)return child;const nested=child.querySelector?.(selector);if(nested)return nested;}return null;}
};}
document={activeElement:null,head:element('head'),body:element('body'),createElement:element,getElementById(id){function find(node){if(node.id===id)return node;for(const child of node.children){const found=find(child);if(found)return found;}return null;}return find(this.head)||find(this.body);}};
let value='F2F4FF',repaint=0;
const history=new History({colour:value});
const context=vm.createContext({document,lang:'en',normalLanguage:lang=>lang,ui:key=>({colour:'colour',hexColour:'hex colour'}[key]),bgCache:{old:true},drawTiles(){repaint++;},themeHistory:history,themeValue:()=>({colour:value}),historySample(){history.push({colour:value});}});
vm.runInContext(source,context);
const row=context.colorRow('PRIMARY1','Text',()=>value,next=>value=next),button=row.querySelector('button'),hex=row.querySelector('.hex'),swatch=row.querySelector('i');
assert.equal(row.tag,'div');assert.equal(button.attributes['aria-label'],'Text · Choose colour');assert.equal(button.attributes['aria-haspopup'],'dialog');
assert.equal(hex.attributes['aria-label'],'Text · hex colour');assert.equal(row.querySelector('details').open,undefined,'Hex controls are collapsed initially');
assert.equal(swatch.style.background,'#F2F4FF');assert.equal(hex.value,'#F2F4FF');
document.activeElement=hex;hex.value='#112233';hex.listeners.input();assert.equal(value,'112233');assert.equal(swatch.style.background,'#112233');assert.equal(context.bgCache,null);
hex.value='#12';hex.listeners.input();assert.equal(value,'112233');assert.equal(hex.value,'#12');hex.listeners.blur();assert.equal(hex.value,'#112233');
hex.value='abcdef';hex.listeners.input();assert.equal(value,'ABCDEF');hex.value='#000000';hex.listeners.input();assert.equal(value,'000000');
for(const colour of ['000000','FFFFFF','ABCDEF','FF0000','00FF00','0000FF','112233'])assert.equal(context.hsvToHex(context.rgbToHsv(colour)),colour,'HSV roundtrip '+colour);
value='F2F4FF';history.reset({colour:value});document.activeElement=button;button.onclick();const picker=context.createColorPicker();
assert.equal(history.entries.length,1,'Opening the picker never creates a change');assert.equal(picker.dialog.open,true);
let shortcutBlocked=false;picker.dialog.listeners.keydown({key:'z',metaKey:true,preventDefault(){},stopPropagation(){shortcutBlocked=true;}});assert.equal(shortcutBlocked,true,'Global undo must not mutate document history behind the modal');
context.updatePickerColour('EF4444');context.updatePickerColour('22C55E');context.updatePickerColour('3B82F6');
assert.equal(value,'3B82F6');assert.equal(history.entries.length,2,'Continuous visual edits make exactly one undo action');assert.equal(history.dirty,true,'Pending live preview is visible to close/save guards');
picker.cancel.onclick();assert.equal(value,'F2F4FF');assert.equal(history.entries.length,1);assert.equal(history.dirty,false);assert.equal(picker.dialog.open,false);
button.onclick();picker.swatchButtons[3].onclick();picker.field.onkeydown({key:'ArrowDown',shiftKey:true,preventDefault(){}});assert.notEqual(value,'EF4444');
picker.hue.value='120';picker.hue.oninput();picker.field.onpointerdown({button:0,pointerId:1,clientX:100,clientY:0});picker.field.onpointerup();assert.equal(value,'00FF00');
picker.apply.onclick();assert.equal(history.entries.length,2);assert.equal(history.dirty,true);assert.equal(history.undo().colour,'F2F4FF','One undo restores the colour before opening');
value='F2F4FF';history.reset({colour:value});button.onclick();picker.system.value='#000000';picker.system.oninput();let prevented=false;picker.dialog.oncancel({preventDefault(){prevented=true;}});assert.equal(prevented,true);assert.equal(value,'F2F4FF','Escape cancels native-picker edits too');
// Cancelling an option colour restores only that option and keeps other state.
let option='FFFFFF',unrelated='keep';const optionHistory=new History({option});
context.openColorPicker({label:'Widget',get:()=>option,set:colour=>option=colour,history:optionHistory,value:()=>({option}),onCancel(){option='FFFFFF';}});
context.updatePickerColour('ABCDEF');unrelated='changed elsewhere';context.finishColorPicker(false);assert.equal(option,'FFFFFF');assert.equal(unrelated,'changed elsewhere');assert.equal(optionHistory.dirty,false);
assert.match(html,/\$\('presetSearch'\)\.oninput=renderPresets/);assert.match(html,/\$\('presetGroup'\)\.onchange=renderPresets/);
async function performanceExports(){
  let root='',reads=0,exported=0,message='';
  const scope=vm.createContext({inApp:true,lang:'es',normalLanguage:value=>value,Blob,document:{createElement:element},toolbar:{append(){}},
    targets:[{path:'radio'}],target:()=>root,radioRoot:()=>root,q:encodeURIComponent,
    status(id,text){message=text;},async api(){reads++;return {format:'apextx-ui-perf-1',samples:[{}]};},
    async exportArtifact(){exported++;return '/exports/performance.json';},localizeEditorControls(){scope.renderPerformanceAction();}});
  const start=html.indexOf('const PERFORMANCE_WORDS='),end=html.indexOf('let sentDraft=',start);
  vm.runInContext(html.slice(start,end),scope);const button=vm.runInContext('perfButton',scope);
  scope.renderPerformanceAction();assert.equal(button.disabled,true);assert.match(button.title,/almacenamiento USB/);assert.equal(button.attributes['aria-description'],button.title);
  await scope.exportPerformanceCapture();assert.equal(reads,0,'Offline performance export never calls the radio API');assert.match(message,/Conecta la emisora/);
  for(const lang of ['en','es','de','cn']){scope.lang=lang;scope.renderPerformanceAction();assert.ok(button.title.length>20);}
  scope.lang='es';root='radio';scope.targets[0].disconnected=true;scope.renderPerformanceAction();assert.equal(button.disabled,true,'A preserved disconnected draft cannot export radio measurements');
  delete scope.targets[0].disconnected;scope.renderPerformanceAction();assert.equal(button.disabled,false);
  scope.api=async()=>{throw Object.assign(Error('raw filesystem exception'),{code:'perf_missing'});};
  await scope.exportPerformanceCapture();assert.match(message,/No se encontró una captura/);assert.equal(exported,0);assert.equal(button.disabled,false);
  let finish;scope.api=()=>{reads++;return new Promise(resolve=>{finish=resolve;});};
  const pending=scope.exportPerformanceCapture();assert.equal(button.disabled,true);await scope.exportPerformanceCapture();assert.equal(reads,1,'Repeated clicks cannot duplicate an export');
  finish({format:'apextx-ui-perf-1',samples:[{}]});await pending;
  assert.equal(exported,1);assert.equal(message,'/exports/performance.json');assert.equal(button.disabled,false);
}
async function exportsWork(){
  const bytes=new Uint8Array([137,80,78,71]),blob={size:bytes.length,async arrayBuffer(){return bytes.buffer;}};
  let saved;
  const exported=vm.createContext({inApp:true,Uint8Array,ui:key=>key,bytesB64:bytes=>Buffer.from(bytes).toString('base64'),async api(path,body){saved={path,body};return {ok:true,path:'/exports/test.png'};}});
  const start=html.indexOf('async function exportArtifact('),end=html.indexOf("$('saveRadio').onclick",start);
  vm.runInContext(html.slice(start,end),exported);
  assert.equal(await exported.exportArtifact(blob,'test.png','png'),'exportSaved /exports/test.png');
  assert.equal(saved.path,'/api/export');
  assert.equal(saved.body.artifact.name,'test.png');assert.equal(saved.body.artifact.kind,'png');
  assert.equal(saved.body.artifact.data,Buffer.from(bytes).toString('base64'));
  exported.api=async()=>{throw new Error('disk full');};
  await assert.rejects(exported.exportArtifact(blob,'test.png','png'),/disk full/,'A failed save must not announce success');
  exported.api=async()=>({ok:true});
  await assert.rejects(exported.exportArtifact(blob,'test.png','png'),/exportError/,'A native export requires a concrete saved path');
  await assert.rejects(exported.exportArtifact(null,'test.png','png'),/exportError/);
  let clicked=0,removed=0,revoked=0;
  exported.inApp=false;exported.document={body:{append(){}},createElement(){return {click(){clicked++;},remove(){removed++;}};}};
  exported.URL={createObjectURL(){return 'blob:preview';},revokeObjectURL(){revoked++;}};exported.setTimeout=fn=>fn();
  assert.equal(await exported.exportArtifact(blob,'test.png','png'),'downloadStarted');
  assert.equal(clicked,1);assert.equal(removed,1);assert.equal(revoked,1);
}
Promise.all([exportsWork(),performanceExports()]).then(()=>console.log('Editor colours and exports: visual picker, native export success/failure, browser fallback and physical-radio performance states passed.')).catch(error=>{console.error(error);process.exitCode=1;});
