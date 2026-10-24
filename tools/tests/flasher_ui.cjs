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
const langs = ['en','es','de'].map(lang=>Object.assign(element(),{dataset:{lang}}));
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
assert.equal(get('upgrade').textContent,'Actualizar a 0.1.0-alpha.4');
assert.equal(get('previewBadge').hidden,false);
for(const decision of ['current','ahead','development']) {
 state(device,{...offer,decision});
 assert.equal(get('upgrade').disabled,true,decision);
 assert.notEqual(get('upgrade').textContent,'Esperando a tu emisora');
}
state({...device,version:null},{...offer,decision:'unknown'});
assert.equal(get('installedVersion').textContent,'No disponible');
assert.equal(get('upgrade').textContent,'Instalar 0.1.0-alpha.4');
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
assert.equal(run("operationMessage({message:'Firmware verified. Confirm that the radio has returned to the home screen.',progress:100})"),'Firmware verificado. Confirma que tu emisora ha vuelto a Inicio.');
state(device);
for(const language of ['en','es','de']) {
 run(`applyLang('${language}')`);
 assert.equal(get('upgrade').textContent.includes('0.1.0-alpha.4'),true);
 assert.equal(translations.every(e=>e.textContent.length>0),true,'Every visible translation has text');
 assert.equal(run("operationMessage({message:'Waiting for the radio to finish writing flash...',progress:65})===T[lang].flashBusy"),true);
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
 console.log('Shared flasher UI: connection states, all languages, offline/manual, version policy, busy guards, and explicit upgrade request passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
