// SPDX-License-Identifier: GPL-2.0-only
// Run the real shared UI script against a small DOM model, with no browser/USB.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const html = fs.readFileSync(path.join(__dirname, '../apextx-updater.html'), 'utf8');
const elements = new Map();
function element() {return {dataset:{},style:{},files:[],classList:{toggle(){}},appendChild(child){child.parentElement=this;},textContent:'',hidden:false,disabled:false,checked:false};}
for (const match of html.matchAll(/id="([^"]+)"/g)) elements.set('#'+match[1],element());
const translations = [...html.matchAll(/data-k="([^"]+)"/g)].map(m=>Object.assign(element(),{dataset:{k:m[1]}}));
const langs = ['en','es','de','cn'].map(lang=>Object.assign(element(),{dataset:{lang}}));
const modes = ['update','recover'].map(mode=>Object.assign(element(),{dataset:{mode}}));
const radios = ['NB4','NB4+'].map(value=>Object.assign(element(),{value}));
const document = {documentElement:{dataset:{platform:'darwin'}},
 getElementById(id) {return elements.get('#'+id);},
 querySelector(selector) {return elements.get(selector)||(selector.endsWith(':checked')?radios.find(r=>r.checked):null);},
 querySelectorAll(selector) {
  return selector==='[data-k]'?translations:selector==='#lang button'?langs:selector==='#tabs button'?modes:
   selector==='input[name="radioVariant"]'?radios:[...modes,...radios,elements.get('#rconfirm')];
 }};
let calls=[];
const context=vm.createContext({document,navigator:{language:'es'},localStorage:{getItem(){return null;},setItem(){}},
 TextDecoder,console,setTimeout(){},fetch:async(url,options)=>{calls.push({url,options});return {ok:true,json:async()=>({})};}});
const script=html.split('<script>')[1].split('</script>')[0].replace('applyLang(pick());poll();','applyLang(pick());');
vm.runInContext(script,context);
const run=code=>vm.runInContext(code,context);
const get=id=>elements.get('#'+id);
assert.equal(document.documentElement.lang,'es','Default to the system language');
assert.equal(run("localStorage.getItem=()=> 'de';pick()"),'de','Honor an explicitly chosen language');
assert.equal(run("localStorage.getItem=()=>null;navigator.language='de-DE';pick()"),'de');
assert.equal(run("navigator.language='fr-FR';pick()"),'en','Fall back to English for unsupported system languages');
assert.equal(run("navigator.language='zh-CN';pick()"),'cn');
for(const language of ['en','es','de','cn']) {
 assert.deepEqual(run('Object.keys(T.'+language+').sort()'),run('Object.keys(T.en).sort()'));
 for(const key of run('Object.keys(T.en)')) {
  assert.deepEqual(run('T.'+language+'['+JSON.stringify(key)+'].match(/\\{\\w+\\}/g)'),
                   run('T.en['+JSON.stringify(key)+'].match(/\\{\\w+\\}/g)'),language+': '+key);
 }
}
run("navigator.language='es';applyLang(pick())");
const offer={id:42,target:1,version:'0.1.0-alpha.4',prerelease:true,url:'https://github.com/',decision:'available'};
const device={state:'ready',target:1,serial:'A',model:'NB4',version:'0.1.0-alpha.3'};
function state(device,release=offer,extra={}) {run('snapshot='+JSON.stringify({device,release,...extra})+';running=!!snapshot.running;render();');}
assert.equal(calls.length,0,'Rendering must not initiate a transfer');
for(const status of ['absent','multiple','recovery','error']) {
 state({state:status,message:'USB error'},null);
 assert.equal(get('upgrade').disabled,true,status);
 assert.equal(get('start').disabled,true,status);
}
state(device);
assert.equal(get('upgrade').disabled,false);
assert.equal(get('readComponents').disabled,true,'Update mode must not receive serial inventory frames');
state({...device,state:'normal'});
assert.equal(get('readComponents').disabled,true,'Ordinary serial/debug mode is not maintenance');
state({...device,state:'normal',maintenance:true});
assert.equal(get('readComponents').disabled,false);
assert.equal(get('deviceTitle').textContent,'Modo ApexTX Flasher conectado');
assert.equal(get('deviceHelp').textContent,run('T.es.maintenanceHelp'));
assert.equal(calls.length,0,'Selecting maintenance never installs firmware');
state(device);
assert.equal(get('upgrade').textContent,'Actualizar a 0.1.0-alpha.4');
assert.equal(get('previewBadge').hidden,false);
state({...device,development:true});
assert.equal(get('installedVersion').textContent,'0.1.0-alpha.3 · dev');
assert.equal(get('upgrade').disabled,false,'A newer release can replace an older development build on explicit request');
for(const decision of ['current','ahead','development']) {
 state(device,{...offer,decision});
 assert.equal(get('upgrade').disabled,true,decision);
 assert.notEqual(get('upgrade').textContent,'Esperando a tu emisora');
}
state({...device,version:null},{...offer,decision:'unknown'});
assert.equal(get('installedVersion').textContent,'No disponible');
assert.equal(get('upgrade').textContent,'Instalar 0.1.0-alpha.4');
assert.equal(get('deviceHelp').textContent,'Tu emisora está lista. Mantenla encendida y conectada.','Do not show legacy-bootloader compatibility prose in the normal update UI');
assert.equal(get('releaseHelp').textContent.includes('Este bootloader'),false,'Do not repeat the unknown-version explanation');
state(device,null,{release_error:'offline'});
get('file').files=[{name:'firmware.bin',size:2097152}];
run('render()');
assert.equal(get('start').disabled,false,'Offline release lookup must preserve manual loading');
state(device,offer,{running:true});
assert.equal(get('upgrade').disabled,true);
assert.equal(get('start').disabled,true);
assert.equal(get('preview').disabled,true);
assert.equal(get('rstart').disabled,true);
assert.equal(modes.every(m=>m.disabled),true);
assert.equal(run("operationMessage({message:'Firmware verified. Confirm that the radio has returned to the home screen.',progress:100})"),'Firmware verificado. Comprueba que tu emisora muestra la pantalla de Inicio.');
state(device);
const bootOffer={...offer,decision:'bootloader',bootloader:1};
const plan={serial:'A',target:1,model:'NB4',version:'0.1.0-alpha.3',ready:false};
state(device,bootOffer);
assert.equal(get('bootloaderNotice').hidden,false);
assert.equal(get('upgrade').disabled,false);
assert.equal(get('upgrade').textContent,'Preparar instalación completa · 0.1.0-alpha.4');
for(const status of ['absent','recovery','multiple']) {
 state({state:status,serial:status==='recovery'?'B':undefined},bootOffer,{bootloader_install:plan,operation:'prepare_bootloader'});
 assert.equal(get('bootloaderGuide').hidden,false,'Guide survives the deliberate disconnect');
 assert.equal(get('bootloaderInstall').disabled,true,'Wait for the pinned radio in DFU');
 assert.equal(get('upgrade').hidden,true);
 assert.equal(get('preview').disabled,true);
 assert.equal(get('transfer').hidden,true,'A prepared download is not a completed installation');
}
state({state:'recovery',serial:'A'},bootOffer,{bootloader_install:{...plan,ready:true}});
assert.equal(get('bootloaderInstall').disabled,false);
assert.equal(get('bootloaderInstall').textContent,'Respaldar e instalar en Noble NB4');
assert.equal(calls.length,0,'Detecting DFU must never initiate an installation');
const components={id:'a'.repeat(32),serial:'A',target:1,package_version:'0.1.0-alpha.6',directory:'/private/session',backup:'/private/backup',approved:true,outcome:'partial',rf_assets:true,bootloader:{state:'current',current:1,proposed:1},
 selected:{application:true,module:true,receiver:true,resources:true,audio:false},steps:{application:{state:'current'},module:{state:'current',image:0,current:65569,proposed:65569,write_supported:false},receiver:{state:'pending'},resources:{state:'ready'},audio:{state:'deferred'}}};
state({...device,state:'normal',mode:'maintenance',maintenance:true},offer,{integrated:components});
assert.equal(get('stage_module').disabled,false,'Verified staging is a separate non-installing action');
assert.equal(get('install_module').disabled,true,'A normal build cannot install unqualified RF');
assert.equal(get('stage_receiver').disabled,true,'An unidentified receiver remains pending');
assert.equal(get('componentBackup').disabled,true,'Complete backup needs ROM DFU');
assert.equal(get('componentPackageDetails').open,false,'A prepared package is collapsed to keep the plan visible');
state({...device,state:'normal',mode:'maintenance',maintenance:true,serial:'B'},offer,{integrated:components});
assert.equal(get('stage_module').disabled,true,'A plan cannot be applied to another transmitter');
assert.equal(get('componentRead').disabled,true);
assert.equal(calls.length,0,'Rendering a plan cannot start a component operation');
state(device,offer,{integrated:{...components,rf_assets:false,bootloader:{state:'pending',proposed:1}}});
assert.equal(get('componentRead').disabled,false,'Update can read bootloader requirements without serial commands');
assert.equal(get('install_application').disabled,true,'Unknown bootloader requirements must be reviewed first');
state({...device,state:'normal',maintenance:true},offer,{integrated:{...components,rf_assets:false}});
assert.equal(get('stage_module').disabled,true,'A package without RF assets cannot prepare RF');
get('componentOnlineAudio').value='es';
state({...device,state:'normal',maintenance:true},{...offer,version:components.package_version,audio_languages:['es']},{integrated:components});
assert.equal(get('componentDownloadAudio').disabled,false,'One explicitly selected release voice pack can be prepared');
assert.equal(get('audioLanguage_en').disabled,true,'Only packs published for this release are offered');
state(device,offer,{integrated:components});
assert.equal(get('componentOnlineAudioBox').hidden,true,'Another release cannot replace the reviewed voice pack');
state(device);
for(const language of ['en','es','de']) {
 run(`applyLang('${language}')`);
 assert.equal(get('upgrade').textContent.includes('0.1.0-alpha.4'),true);
 assert.equal(translations.every(e=>e.textContent.length>0),true,'Every visible translation has text');
 assert.equal(run("operationMessage({message:'Waiting for the radio to finish writing flash...',progress:65})===T[lang].flashBusy"),true);
 assert.equal(run("operationMessage({operation:'recover',message:'Installation complete and verified. The radio is starting automatically.',progress:100})===T[lang].rautoDone"),true);
 assert.equal(run("operationMessage({operation:'recover',message:'Erasing flash 25%',progress:30})===T[lang].rerase"),true);
 assert.equal(run("T[lang].rduration.includes('5') && T[lang].rduration.includes('10')"),true);
 assert.equal(run("T[lang].dfuStep2.includes('USB') && T[lang].dfuStep2.includes('Update')"),true);
 assert.equal(run("operationMessage({operation:'bootloader',message:'Installation complete and verified. The radio is starting automatically.',progress:100})===T[lang].rautoDone"),true);
 for(const [detail,key] of [['The radio was writing flash memory.','usbLostInstalling'],['The firmware was already written and verified.','usbLostVerified'],['No flash memory had been written yet.','usbLostTransfer']]) {
  const status={error:true,message:'The USB connection to the radio was lost. '+detail+' ([Errno 5] Input/Output Error)'};
  assert.equal(run(`operationMessage(${JSON.stringify(status)})===T[lang].${key}`),true,'USB advice must be localized and specific to the installation phase');
 }
}
(async()=>{
 get('preview').checked=false;
 await get('preview').onchange();
 assert.equal(get('preview').checked,false,'Channel choice must not flash back during its request');
 calls=[];
 await get('upgrade').onclick();
 assert.equal(calls.length,1);
 assert.equal(calls[0].url,'/api/upgrade');
 assert.deepEqual(JSON.parse(calls[0].options.body),{serial:'A',target:1,release_id:42});
 assert.equal(get('upgrade').disabled,true);
 calls=[];
 state(device,bootOffer);
 await get('upgrade').onclick();
 assert.equal(calls.length,1);
 assert.equal(calls[0].url,'/api/bootloader/prepare','A bootloader update first downloads and prepares; it must not use normal Update');
 calls=[];
 state({state:'recovery',serial:'A'},bootOffer,{bootloader_install:{...plan,ready:true}});
 await get('bootloaderInstall').onclick();
 assert.equal(calls.length,1);
 assert.equal(calls[0].url,'/api/bootloader/install');
 assert.deepEqual(JSON.parse(calls[0].options.body),{serial:'A',target:1,release_id:42});
 calls=[];
 state({state:'absent'},bootOffer,{bootloader_install:plan});
 await get('bootloaderCancel').onclick();
 assert.equal(calls[0].url,'/api/bootloader/cancel');
 calls=[];
 const appOffer={version:'1.4.0',state:'available',available:'1.5.0',notes:'<script>unsafe</script>',supported:true,compatible:true,preview:false,release_url:'https://github.com/GastonGelhorn/apextx/releases/tag/flasher-v1.5.0'};
 state(device,offer,{app_update:{...appOffer,hidden:true}});
 assert.equal(get('appDialog').open,undefined,'A snoozed or skipped offer never opens the dialog by itself');
 assert.equal(get('appShow').hidden,false,'A snoozed offer stays reachable from the top bar');
 assert.equal(get('appCheck').hidden,true,'The check button gives way to the update pill');
 state(device,offer,{app_update:appOffer});
 assert.equal(calls.length,0,'App detection and rendering never download or install');
 assert.equal(get('appInstall').disabled,false);
 assert.equal(get('appUpdateNotes').textContent,'<script>unsafe</script>','Notes are text, not executable HTML');
 assert.equal(get('appDialog').open,true,'An available app update opens the update dialog automatically');
 assert.equal(get('appReleaseLink').href,appOffer.release_url);
 assert.equal(get('appDialogLead').textContent.includes('1.5.0')&&get('appDialogLead').textContent.includes('1.4.0'),true);
 state(device,offer,{app_update:{...appOffer,hidden:true}});
 assert.equal(get('appDialog').open,false,'Remind me later closes the dialog');
 get('appShow').onclick();
 assert.equal(get('appDialog').open,true,'Show update reopens a snoozed offer');
 state(device,offer,{app_update:{...appOffer,hidden:true}});
 assert.equal(get('appDialog').open,true,'A dialog opened on request stays open while snoozed');
 state(device,offer,{app_update:{...appOffer,available:undefined,state:'current'}});
 assert.equal(get('appDialog').open,false,'No offer, no dialog');
 for(const blocked of [{...appOffer,blocked:true},{...appOffer,supported:false},{...appOffer,compatible:false}]){
  state(device,offer,{app_update:blocked});
  assert.equal(get('appInstall').disabled,true);
  await get('appInstall').onclick();
  assert.equal(calls.length,0);
 }
 state(device,offer,{app_update:appOffer});
 await get('appInstall').onclick();
 assert.equal(calls.length,1);
 assert.equal(calls[0].url,'/api/app-update/install');
 assert.deepEqual(JSON.parse(calls[0].options.body),{version:'1.5.0'},'Installation location is decided by the installer, not the user');
 calls=[];
 await get('appLater').onclick();
 assert.equal(calls[0].url,'/api/app-update/later');
 calls=[];
 await get('appSkip').onclick();
 assert.equal(calls[0].url,'/api/app-update/skip');
 state(device,offer,{app_update:{...appOffer,state:'downloading',progress:42}});
 assert.equal(get('appInstall').disabled,true);
 assert.equal(get('appUpdateStatus').textContent.includes('42'),true);
 assert.equal(get('appDialog').open,true,'Download progress is shown in the dialog');
 assert.equal(get('appDialogState').textContent.includes('42'),true);
 assert.equal(get('appLater').hidden&&get('appSkip').hidden,true,'A running download cannot be deferred');
 state(device,offer,{app_update:{...appOffer,state:'error',error:'App network unavailable'}});
 assert.equal(get('appDialogError').textContent,'App network unavailable');
 state(device,offer,{app_update:{version:'1.4.0',state:'error',error:'Offline',supported:true,preview:false}});
 assert.equal(get('appUpdateStatus').textContent.includes('1.4.0'),true);
 assert.equal(get('appCheck').hidden,false,'Manual check stays available after a failed automatic check');
 assert.equal(get('appDialog').open,false,'A failed automatic check never opens a dialog');
 calls=[];
 const current={version:'1.4.0',state:'current',supported:true,preview:false,checks:3};
 state(device,offer,{app_update:current});
 assert.equal(get('appDialog').open,false,'Up to date after an automatic check is only shown in the footer');
 await get('appCheck').onclick();
 assert.equal(calls[0].url,'/api/app-update/check');
 assert.equal(get('appDialog').open,true,'A requested check opens the dialog at once');
 assert.equal(get('appDialogProgress').hidden,false,'…in its checking state');
 assert.equal(get('appClose').hidden,true);
 state(device,offer,{app_update:current});
 assert.equal(get('appDialogProgress').hidden,false,'The previous result is not mistaken for the new check');
 state(device,offer,{app_update:{...current,state:'checking',checks:4}});
 assert.equal(get('appDialogProgress').hidden,false);
 state(device,offer,{app_update:{...current,checks:4}});
 assert.equal(get('appDialogProgress').hidden,true);
 assert.equal(get('appClose').hidden,false,'Up to date is announced in the dialog');
 assert.equal(get('appDialogLead').textContent.includes('1.4.0'),true);
 get('appClose').onclick();
 assert.equal(get('appDialog').open,false);
 await get('appCheck').onclick();
 state(device,offer,{app_update:{...current,state:'error',error:'Offline',checks:5}});
 assert.equal(get('appTryAgain').hidden,false,'A failed requested check offers a retry');
 assert.equal(get('appDialogError').textContent,'Offline');
 state(device,offer,{app_update:{...appOffer,checks:6}});
 assert.equal(get('appDialog').open,true,'An offer found by a requested check is shown');
 assert.equal(get('appInstall').hidden,false);
 assert.equal(get('appTryAgain').hidden,true);
 await get('appLater').onclick();
 state(device,offer,{app_update:{...appOffer,checks:6,hidden:true}});
 assert.equal(get('appDialog').open,false);
 assert.equal(get('start').disabled,false,'An app update check failure preserves local radio updating');
 console.log('Shared flasher UI: connection states, all languages, offline/manual, version policy, busy guards, and explicit upgrade request passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
