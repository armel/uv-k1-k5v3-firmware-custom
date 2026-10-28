"use strict";
// Optional real Edge test using its DevTools pipe. No server or npm package.
const fs=require("node:fs"),path=require("node:path"),assert=require("node:assert/strict"),{spawn}=require("node:child_process"),{pathToFileURL}=require("node:url");
const root=path.resolve(__dirname,".."),out=path.join(__dirname,"out-editor"),downloads=path.join(out,"downloads");
fs.mkdirSync(downloads,{recursive:true});
fs.rmSync(path.join(out,"browser-results.json"),{force:true});
const downloaded=path.join(downloads,"CWQSOAuto-personnalise.app");fs.rmSync(downloaded,{force:true});
const edge=process.env.EDGE_EXE||"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
if(!fs.existsSync(edge))throw new Error("Set EDGE_EXE to the local Edge executable");
const profile=fs.mkdtempSync(path.join(out,"edge-profile-"));
const browser=spawn(edge,["--headless=new","--remote-debugging-pipe",`--user-data-dir=${profile}`,"--no-first-run","--disable-background-networking","--disable-extensions","--disable-sync","--window-size=1280,1000","about:blank"],{stdio:["ignore","ignore","pipe","pipe","pipe"],windowsHide:true});
let buffer=Buffer.alloc(0),id=0,stderr="";const waiting=new Map(),exceptions=[],requests=[],completedDownloads=new Set();
browser.stderr.on("data",chunk=>stderr+=chunk.toString());
browser.on("error",error=>{console.error(error);process.exitCode=1;});
browser.stdio[4].on("data",chunk=>{
 buffer=Buffer.concat([buffer,chunk]);let end;
 while((end=buffer.indexOf(0))>=0){
  const message=JSON.parse(buffer.subarray(0,end).toString());buffer=buffer.subarray(end+1);
  if(message.id && waiting.has(message.id)){
   const task=waiting.get(message.id);waiting.delete(message.id);clearTimeout(task.timeout);
   message.error?task.reject(new Error(JSON.stringify(message.error))):task.resolve(message.result);
  }
  if(message.method==="Runtime.exceptionThrown")exceptions.push(message.params);
  if(message.method==="Network.requestWillBeSent")requests.push(message.params.request.url);
  if(message.method==="Browser.downloadProgress" && message.params.state==="completed")completedDownloads.add(message.params.guid);
 }
});
function send(method,params={},sessionId){return new Promise((resolve,reject)=>{
 const key=++id;const timeout=setTimeout(()=>{waiting.delete(key);reject(new Error(`${method}: timeout. ${stderr}`));},20000);
 waiting.set(key,{resolve,reject,timeout});browser.stdio[3].write(JSON.stringify({id:key,method,params,...(sessionId?{sessionId}:{})})+"\0");
});}
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
(async()=>{
 const {targetId}=await send("Target.createTarget",{url:"about:blank"});
 const {sessionId}=await send("Target.attachToTarget",{targetId,flatten:true});
 const call=(method,params)=>send(method,params,sessionId);
 const evaluate=async expression=>{const result=await call("Runtime.evaluate",{expression,returnByValue:true,awaitPromise:true});if(result.exceptionDetails)throw new Error(JSON.stringify(result.exceptionDetails));return result.result.value;};
 async function until(expression){for(let i=0;i<160;i++){if(await evaluate(expression))return;await pause(50);}const state=await evaluate("JSON.stringify({status:document.getElementById('file-status')?.textContent,error:document.getElementById('file-error')?.textContent})");throw new Error(`UI timeout: ${expression}; ${state}`);}
 await call("Runtime.enable");await call("Page.enable");await call("Network.enable");
 await call("Network.emulateNetworkConditions",{offline:true,latency:0,downloadThroughput:-1,uploadThroughput:-1});
 await send("Browser.setDownloadBehavior",{behavior:"allow",downloadPath:downloads,eventsEnabled:true});
 await call("Page.navigate",{url:pathToFileURL(path.join(root,"editor.html")).href});
 await until("document.readyState==='complete' && typeof Bank16==='object' && !document.getElementById('download').disabled");
 assert.equal(await evaluate("document.querySelectorAll('textarea').length"),16);
 assert.equal(await evaluate("document.getElementById('msg-1').value"),"CQ CQ CQ DE F4XXX F4XXX K");
 assert.match(await evaluate("document.getElementById('file-status').textContent"),/Référence embarquée.*names16/);
 const original=fs.readFileSync(path.join(root,"CWQSOAuto.app"));
 async function download(){
  const completedBefore=completedDownloads.size;
  fs.rmSync(downloaded,{force:true});
  await evaluate("document.getElementById('download').click()");
  await until("!document.getElementById('download').disabled && document.getElementById('file-status').textContent.includes('téléchargée')");
  for(let i=0;i<100;i++){if(completedDownloads.size>completedBefore && fs.existsSync(downloaded)&&fs.statSync(downloaded).size===6434)break;await pause(50);}
  assert.ok(completedDownloads.size>completedBefore,"Browser download must finish before reimport");
  assert.ok(fs.existsSync(downloaded));assert.equal(fs.statSync(downloaded).size,6434);
 }
 assert.equal(await evaluate("document.querySelectorAll('input[id^=name-]').length"),16);
 assert.equal(await evaluate("document.getElementById('name-1').value"),"CQ");
 // Initial download needs no imported .app and is byte-identical.
 await download();assert.deepEqual(fs.readFileSync(downloaded),original);
 await evaluate("document.getElementById('msg-15').value='é';document.getElementById('msg-15').dispatchEvent(new Event('input'))");
 assert.equal(await evaluate("document.getElementById('download').disabled"),true);
 assert.match(await evaluate("document.getElementById('error-15').textContent"),/MSG 15.*é/);
 await evaluate("document.getElementById('msg-15').value='AGN?';document.getElementById('msg-15').dispatchEvent(new Event('input'));document.getElementById('msg-4').value='{RST}'.repeat(64);document.getElementById('msg-4').dispatchEvent(new Event('input'))");
 assert.equal(await evaluate("document.getElementById('count-4').textContent"),"64 / 64 octets");
 await evaluate("document.getElementById('msg-4').value+='{RST}';document.getElementById('msg-4').dispatchEvent(new Event('input'))");
 assert.equal(await evaluate("document.getElementById('download').disabled"),true);
 await evaluate("for(const id of ['msg-1','msg-2']){const f=document.getElementById(id);f.value=f.value.replaceAll('F4XXX','F5XYZ').replace('SEB','ALICE');f.dispatchEvent(new Event('input'));}document.getElementById('msg-4').value='TEST WEB {RST}';document.getElementById('msg-4').dispatchEvent(new Event('input'))");
 await evaluate("for(let i=1;i<=16;i++){const f=document.getElementById('name-'+i);f.value='POTA '+i;f.dispatchEvent(new Event('input'));}");
 await evaluate("document.getElementById('name-16').value='ABCDEFGHIJKLM';document.getElementById('name-16').dispatchEvent(new Event('input'))");
 assert.equal(await evaluate("document.getElementById('download').disabled"),true);
 await evaluate("document.getElementById('name-16').value='POTA 16';document.getElementById('name-16').dispatchEvent(new Event('input'))");
 await download();const custom=fs.readFileSync(downloaded);
 assert.deepEqual(custom.subarray(64,64+4084),original.subarray(64,64+4084));
 const doc=await call("DOM.getDocument");const {nodeId}=await call("DOM.querySelector",{nodeId:doc.root.nodeId,selector:"#app-file"});
 await call("DOM.setFileInputFiles",{nodeId,files:[downloaded]});await until("!document.getElementById('download').disabled && document.getElementById('file-status').textContent.includes('names16')");
 assert.equal(await evaluate("document.getElementById('msg-1').value"),"CQ CQ CQ DE F5XYZ F5XYZ K");
 assert.equal(await evaluate("document.getElementById('msg-2').value"),"DE F5XYZ UR {RST} {RST} NAME ALICE HW?");
 assert.equal(await evaluate("document.getElementById('msg-4').value"),"TEST WEB {RST}");
 assert.equal(await evaluate("Array.from({length:16},(_,i)=>document.getElementById('name-'+(i+1)).value).join('|')"),Array.from({length:16},(_,i)=>'POTA '+(i+1)).join('|'));
 const legacy=path.resolve(root,"../../../archive/apps/v6.3.0/CWQSOAuto.app");
 await call("DOM.setFileInputFiles",{nodeId,files:[legacy]});await until("!document.getElementById('download').disabled && document.getElementById('file-status').textContent.includes('Ancien format sans noms')");
 assert.equal(await evaluate("[...document.querySelectorAll('input[id^=name-]')].every(x=>x.disabled)"),true);
 const bad=Buffer.from(original);bad[0]=0;const badFile=path.join(out,"corrupt.app");fs.writeFileSync(badFile,bad);
 await call("DOM.setFileInputFiles",{nodeId,files:[badFile]});await until("document.getElementById('file-status').textContent==='Fichier refusé.'");
 assert.equal(await evaluate("document.getElementById('download').disabled && [...document.querySelectorAll('textarea')].every(x=>x.disabled)"),true);
 await evaluate("document.getElementById('reset-reference').click()");await until("!document.getElementById('download').disabled");
 assert.equal(await evaluate("document.getElementById('msg-1').value"),"CQ CQ CQ DE F4XXX F4XXX K");
 await call("Emulation.setDeviceMetricsOverride",{width:1280,height:1000,deviceScaleFactor:1,mobile:false});
 const shot=await call("Page.captureScreenshot",{format:"png",captureBeyondViewport:true});fs.writeFileSync(path.join(out,"desktop.png"),Buffer.from(shot.data,"base64"));
 await call("Emulation.setDeviceMetricsOverride",{width:390,height:844,deviceScaleFactor:1,mobile:true});
 assert.equal(await evaluate("document.documentElement.scrollWidth<=window.innerWidth"),true);
 const mobile=await call("Page.captureScreenshot",{format:"png"});fs.writeFileSync(path.join(out,"mobile.png"),Buffer.from(mobile.data,"base64"));
 assert.deepEqual(exceptions,[]);assert.equal(requests.some(url=>/^https?:/.test(url)),false);
 const result={browser:"Microsoft Edge headless, file://",network_forced_offline:true,external_requests:0,embedded_bootstrap:true,embedded_download_identical:true,fields:16,names:16,names_roundtrip:true,name_overflow_blocked:true,legacy_names_disabled:true,invalid_char_blocked:true,exact_64_tokens:true,overflow_blocked:true,code_preserved:true,custom_callsign_reimport:true,bad_file_clears_state:true,reset_defaults:true,mobile_no_horizontal_overflow:true,passed:true};
 fs.writeFileSync(path.join(out,"browser-results.json"),JSON.stringify(result,null,2));console.log(JSON.stringify(result));
 await send("Browser.close");
})().catch(error=>{console.error(error);process.exitCode=1;}).finally(()=>{for(const task of waiting.values())clearTimeout(task.timeout);waiting.clear();browser.kill();});
