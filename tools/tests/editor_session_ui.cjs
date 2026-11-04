// SPDX-License-Identifier: GPL-2.0-only
// Run the editor's real session functions with controlled UI/network boundaries.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const html = fs.readFileSync(path.join(__dirname, '../apextx-theme-editor.html'), 'utf8');
const historyScope={module:{exports:{}}};
const historyStart=html.indexOf('class EditorHistory');
vm.runInNewContext(html.slice(historyStart,html.indexOf('</script>',historyStart)),historyScope);
const History=historyScope.module.exports;
function functionSource(name) {
  const match = html.match(new RegExp('(?:async )?function '+name+'\\([^]*?\\n\\}'));
  assert.ok(match, 'Production function exists: '+name);
  return match[0];
}
function install(scope, ...names) {
  for (const name of names) vm.runInContext(functionSource(name), scope);
}
function node(value='') {
  return {value,style:{},classList:{toggle(){}},children:[],appendChild(child){this.children.push(child);if(this.children.length===1)this.value=child.value;},
    get innerHTML(){return '';},set innerHTML(value){this.children=[];this.value='';}};
}
function session() {
  const nodes={target:node('A'),connDot:node(),connText:node()};
  const loads=[], prompts=[];
  const scope=vm.createContext({console,EditorHistory:History,activeTarget:'A',carDirty:false,car:{file:'model.yml'},
    themeHistory:new History({name:'baseline'}),appearanceHistory:new History({orientation:0}),
    targets:[{path:'A',settings:true},{path:'B',settings:true}],inApp:true,loadedFolder:'OldTheme',loadedThemeHashes:{old:'hash'},
    $:id=>nodes[id]||(nodes[id]=node()), t:key=>key,ui:key=>key,status(){},
    document:{querySelectorAll(){return [];},createElement(){return node();}},
    async api(){return [{path:'A',settings:true},{path:'B',settings:true}];},
    async loadThemeList(){loads.push(['themes',scope.radioRoot()]);},
    async loadCars(){loads.push(['cars',scope.radioRoot()]);},
    async loadRadio(){loads.push(['appearance',scope.radioRoot()]);scope.appearanceHistory.reset({orientation:0});},
    async editorLeaveCar(all){prompts.push({all,target:scope.target()});return false;}
  });
  scope.target=()=>nodes.target.value;
  scope.radioRoot=()=>scope.targets.find(t=>t.path===scope.target()&&t.settings)?.path||'';
  vm.runInContext(html.match(/^const hasEditorDrafts=.*;$/m)[0],scope);
  install(scope,'renderConnection','onTargetChanged','loadTargets');
  return {scope,nodes,loads,prompts};
}

async function targetChanges() {
  const cancelled=session();
  cancelled.scope.carDirty=true;
  cancelled.nodes.target.value='B';
  await cancelled.scope.onTargetChanged();
  assert.equal(cancelled.nodes.target.value,'A','Cancel restores the selected radio');
  assert.equal(cancelled.scope.activeTarget,'A');
  assert.deepEqual(cancelled.prompts,[{all:true,target:'A'}],'The prompt and Save operate on the original radio');
  assert.equal(cancelled.loads.length,0,'Cancelling must not reload or discard any document');

  const accepted=session();
  accepted.scope.themeHistory.push({name:'edited'});
  accepted.nodes.target.value='B';
  accepted.scope.editorLeaveCar=async all=>{
    assert.equal(all,true,'Theme/appearance edits must be included in a radio switch');
    assert.equal(accepted.scope.radioRoot(),'A','Saving pending edits targets the old radio');
    return true;
  };
  await accepted.scope.onTargetChanged();
  assert.equal(accepted.scope.activeTarget,'B');
  assert.equal(accepted.nodes.target.value,'B');
  assert.ok(accepted.loads.length>0);
  assert.ok(accepted.loads.every(([,target])=>target==='B'),'Only accepted radio switch loads new documents');

  const refresh=session();
  refresh.scope.appearanceHistory.push({orientation:1});
  await refresh.scope.loadTargets();
  assert.equal(refresh.scope.appearanceHistory.dirty,true,'Refreshing volume discovery preserves pending appearance');
  assert.equal(refresh.loads.length,0,'Unchanged target discovery does not reload documents');

  const disconnected=session();
  disconnected.scope.appearanceHistory.push({orientation:1});
  disconnected.scope.api=async()=>[{path:'B',settings:true}];
  await disconnected.scope.loadTargets();
  assert.equal(disconnected.nodes.target.value,'A','A disconnected radio retains its active draft');
  assert.equal(disconnected.scope.targets.find(t=>t.path==='A').disconnected,true);
  assert.equal(disconnected.loads.length,0,'Disconnect does not replace a draft with another radio');
}

function draftSession() {
  const scope=vm.createContext({console,EditorHistory:History,inApp:true,editorApplying:false,editorHistories:new Map(),editorHistoryKey:'',
    car:{file:'',name:'Demo',screenData:{0:{layoutId:'Layout1x1'}}},carDirty:false,
    state:{name:'baseline',view:'theme'},appearance:{rOrient:'0'},
    themeHistory:new History({name:'baseline'}),appearanceHistory:new History({rOrient:'0'}),
    document:{activeElement:{id:'name'}},undoButton:{},redoButton:{},radioRoot:()=>'',
    themeValue(){return {name:scope.state.name};},appearanceValue(){return {...scope.appearance};},
    async themeFiles(){return [];},bytesB64(){return '';},t:key=>key,status(){},showUnsaved(){}
  });
  install(scope,'historySample','saveEditorDraft');
  scope.historySample();
  return scope;
}
async function saveCheckpoints() {
  const fast=draftSession();
  fast.state.name='saved before next history tick';
  let written;
  fast.api=async(path,body)=>{written=JSON.parse(JSON.stringify(body.document));return {path:'draft.json'};};
  await fast.saveEditorDraft();
  fast.historySample();
  assert.equal(written.theme.name,fast.state.name);
  assert.equal(fast.themeHistory.dirty,false,'Immediate edit + Save must mark that exact state saved');

  const pending=draftSession();
  pending.state.name='persisted';pending.appearance.rOrient='1';
  let finish,started;
  const writing=new Promise(resolve=>started=resolve);
  pending.api=(path,body)=>{
    written=JSON.parse(JSON.stringify(body.document));started();
    return new Promise(resolve=>finish=()=>resolve({path:'draft.json'}));
  };
  const saving=pending.saveEditorDraft();
  await writing;
  pending.state.name='new change during write';pending.appearance.rOrient='0';pending.car.screenData[0].layoutId='Layout2x2';pending.carDirty=true;pending.historySample();
  finish();await saving;
  assert.equal(JSON.parse(pending.themeHistory.saved).name,written.theme.name,'Saved checkpoint is the persisted snapshot');
  assert.equal(pending.themeHistory.dirty,true,'Edits arriving while save is pending remain unsaved');
  assert.equal(pending.appearanceHistory.dirty,true,'Appearance edits arriving during save remain unsaved');
  assert.equal(pending.carDirty,true,'Screen edits arriving during save remain unsaved');

  const failed=draftSession();
  failed.state.name='unsaved';failed.api=async()=>{throw Error('Disk full');};
  await assert.rejects(failed.saveEditorDraft(),/Disk full/);
  assert.equal(failed.themeHistory.dirty,true,'A failed save retains the pending draft');
  assert.equal(JSON.parse(failed.themeHistory.saved).name,'baseline');
}

async function backgroundUndo() {
  const scope=vm.createContext({state:{bgImage:{src:'old'},bgLandscapeImage:{src:'wide'}},editorApplying:false,
    undoButton:{},redoButton:{},historySample(){},decodeImage:async src=>({src}),refreshAll(){},status(){}});
  install(scope,'applyThemeHistory');
  await scope.applyThemeHistory({name:'without image',bgImageSrc:null,bgLandscapeImageSrc:null});
  assert.equal(scope.state.bgImage,null,'Undoing image selection removes the portrait image');
  assert.equal(scope.state.bgLandscapeImage,null,'Undoing image selection removes the landscape image');
  assert.equal(scope.editorApplying,false);
}
function replacementSession() {
  const nodes={radioThemes:{value:'new-theme'},loadRadio:{},ymlFile:{},saveRadio:{onclick:async()=>{}}},dialogs=[];
  let reads=0;
  const scope=vm.createContext({state:{name:'saved',bgImage:null,bgLandscapeImage:null},themeHistory:new History({name:'saved'}),appearanceHistory:new History({}),
    carDirty:false,car:{file:'car.yml'},editorHistories:new Map(),editorHistoryKey:'',loadedFolder:'existing',loadedThemeHashes:{old:'hash'},originalBackgrounds:null,lang:'en',
    $:id=>nodes[id],target:()=>'/radio',radioRoot:()=>'/radio',q:encodeURIComponent,t:key=>key,status(){},refreshAll(){},backgroundKey:()=>'',
    themeValue(){return {name:scope.state.name};},historySample(){scope.themeHistory.push(scope.themeValue());},
    parseYml(value){scope.state.name=value;},api:async()=>{reads++;return {yml:'loaded',hashes:{new:'hash'}};},
    document:{body:{append(){}},createElement(tag){const element={tag,children:[],append(...children){this.children.push(...children);},showModal(){},close(){},remove(){}};if(tag==='dialog')dialogs.push(element);return element;}}
  });
  install(scope,'confirmThemeReplacement','editorLeaveCar');
  const start=html.indexOf("$('loadRadio').onclick ="),end=html.indexOf('function bytesB64',start);
  vm.runInContext(html.slice(start,end),scope);
  const importStart=html.indexOf("$('ymlFile').onchange ="),importEnd=html.indexOf('// ---------------------------------------------------------------------------',importStart);
  vm.runInContext(html.slice(importStart,importEnd),scope);
  return {scope,nodes,dialogs,get reads(){return reads;}};
}
async function themeReplacement() {
  const cancelled=replacementSession();cancelled.scope.state.name='just typed';
  const loading=cancelled.nodes.loadRadio.onclick();
  assert.equal(cancelled.dialogs.length,1,'Immediate unsampled edits must trigger the shared prompt');
  await cancelled.dialogs[0].children[3].onclick();await loading;
  assert.equal(cancelled.scope.state.name,'just typed');assert.equal(cancelled.scope.loadedFolder,'existing');
  assert.equal(cancelled.scope.themeHistory.dirty,true);assert.equal(cancelled.reads,0,'Cancellation never loads or replaces a theme');

  const failed=replacementSession();failed.scope.state.name='pending';
  const pending=failed.nodes.loadRadio.onclick();
  await failed.dialogs[0].children[1].onclick();
  assert.equal(failed.scope.state.name,'pending');assert.equal(failed.reads,0,'A save that leaves changes pending cannot replace the theme');
  await failed.dialogs[0].children[3].onclick();await pending;
  assert.equal(failed.scope.themeHistory.dirty,true);

  const clean=replacementSession();await clean.nodes.loadRadio.onclick();
  assert.equal(clean.dialogs.length,0,'Clean initial loads remain silent');assert.equal(clean.scope.state.name,'loaded');assert.equal(clean.scope.themeHistory.dirty,false);

  const imported=replacementSession();imported.scope.state.name='pending YAML draft';let fileReads=0;
  const event={target:{value:'theme.yml',files:[{text:async()=>{fileReads++;return 'new YAML';}}]}};
  const importing=imported.nodes.ymlFile.onchange(event);await imported.dialogs[0].children[3].onclick();await importing;
  assert.equal(imported.scope.state.name,'pending YAML draft');assert.equal(fileReads,0);assert.equal(event.target.value,'');
}
async function immediateClose() {
  const scope=vm.createContext({window:{},carDirty:false,themeHistory:{dirty:false},appearanceHistory:{dirty:false},
    historySample(){scope.themeHistory.dirty=true;},editorLeaveCar:async()=>false,api:async()=>{throw Error('Must not close');}});
  vm.runInContext(html.match(/^window\.editorRequestClose=.*;$/m)[0],scope);
  assert.equal(scope.window.editorRequestClose(),false,'Closing immediately after typing must sample the pending edit');
  await Promise.resolve();assert.equal(scope.window.editorClosePrompt,false);
}
async function overlappingUndo() {
  let finishDecode;
  const scope=vm.createContext({state:{name:'C',view:'theme'},editorApplying:false,undoButton:{},redoButton:{},
    themeHistory:new History({name:'A',bgImageSrc:null,bgLandscapeImageSrc:null}),
    historySample(){scope.undoButton.disabled=scope.themeHistory.index===0;scope.redoButton.disabled=scope.themeHistory.index+1===scope.themeHistory.entries.length;},
    $:()=>({classList:{contains:()=>false}}),decodeImage:()=>new Promise(resolve=>{finishDecode=resolve;}),refreshAll(){},status(){}});
  scope.themeHistory.push({name:'B',bgImageSrc:'image',bgLandscapeImageSrc:null});
  scope.themeHistory.push({name:'C',bgImageSrc:null,bgLandscapeImageSrc:null});
  install(scope,'applyThemeHistory','historyMove');
  scope.historyMove(false);assert.equal(scope.themeHistory.index,1);assert.equal(scope.editorApplying,true);
  assert.equal(scope.undoButton.disabled,true);assert.equal(scope.redoButton.disabled,true);
  scope.historyMove(false);assert.equal(scope.themeHistory.index,1,'Another undo cannot overtake image decoding');
  finishDecode({src:'image'});await new Promise(resolve=>setImmediate(resolve));
  assert.equal(scope.state.name,'B');assert.equal(scope.editorApplying,false);assert.equal(scope.undoButton.disabled,false);
  scope.historyMove(false);await new Promise(resolve=>setImmediate(resolve));
  assert.equal(scope.state.name,'A');assert.equal(scope.themeHistory.index,0);assert.equal(scope.undoButton.disabled,true);
}
(async()=>{
  await targetChanges();await saveCheckpoints();await backgroundUndo();await themeReplacement();await overlappingUndo();await immediateClose();
  console.log('Editor sessions: safe target/theme switching, exact saves, refresh preservation and serialized image undo passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
