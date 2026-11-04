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
    decodeImage:async src=>({src}),refreshAll(){},status(){}});
  install(scope,'applyThemeHistory');
  await scope.applyThemeHistory({name:'without image',bgImageSrc:null,bgLandscapeImageSrc:null});
  assert.equal(scope.state.bgImage,null,'Undoing image selection removes the portrait image');
  assert.equal(scope.state.bgLandscapeImage,null,'Undoing image selection removes the landscape image');
  assert.equal(scope.editorApplying,false);
}
(async()=>{
  await targetChanges();await saveCheckpoints();await backgroundUndo();
  console.log('Editor sessions: safe target switching, refresh preservation, exact save checkpoints and background undo passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
