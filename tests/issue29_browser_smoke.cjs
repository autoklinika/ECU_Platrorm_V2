// Real Chromium browser smoke with fully MOCKED loopback API.
// No hardware operations or production kiosk interaction.
const {spawn}=require('node:child_process');
const fs=require('node:fs');
const PORT=8895, DEBUG=9326, HOST='127.0.0.1';
const delay=ms=>new Promise(r=>setTimeout(r,ms));
const childServer=spawn('/usr/bin/python3',['-m','http.server',String(PORT),'--bind',HOST,'--directory','/home/ecu/ECU_V2_INTEGRATION/webgui'],{stdio:'ignore'});
const childBrowser=spawn('/usr/bin/chromium',['--headless=new','--no-sandbox','--disable-dev-shm-usage','--disable-gpu','--no-first-run','--user-data-dir=/tmp/ecu-issue29-cdp-smoke-'+process.pid,'--remote-debugging-port='+DEBUG,'about:blank'],{stdio:'ignore'});
const counts={connect:0,parameters:0,latest:0,about:0};
let at=Date.now(),generation=0,exceptions=[],pending=new Map(),socket,nextId=0;
function answer(path,method){
  if(path.endsWith('/kiosk/v1/about')) {counts.about++;return [200,{api_version:'v1',read_only:true,build_revision:'smoke'}]}
  if(path.endsWith('/kiosk/v1/interfaces')) return [200,{interfaces:[{name:'can0',up:false,bus_off:false,fd_enabled:false,listen_only:false,bitrate:250000,data_bitrate:0}]}];
  if(path.endsWith('/kiosk/v1/dut')) return [503,{code:'backend_unavailable'}];
  if(path.endsWith('/kiosk/v1/bench/daf-sac/connect')&&method==='POST'){counts.connect++;return [200,{profile_id:0xDAF00025,bitrate:250000,vin:null,vin_status:'UNPROGRAMMED_FF17',software:'1973214',hardware:'K075169'}]}
  if(path.endsWith('/kiosk/v1/bench/daf-sac/parameters/read')&&method==='POST'){counts.parameters++;at=Date.now();generation++;return [200,{bitrate:250000,profile_id:0xDAF00025,parameters_status:'completed',parameters_published:true,parameter_capture_floor_ms:at,parameter_captured_at_unix_ms:at,parameter_completed_generation:generation}]}
  if(path.endsWith('/kiosk/v1/readouts/daf-sac/parameters/latest')){counts.latest++;return [200,{source:'completed_application_operation',live:false,captured_at_unix_ms:at,profile_id:0xDAF00025,completed_generation:generation,parameters:{permanent_voltage_v:22.4,ignition_voltage_v:22.4,pgn_feae_observed:true,pressure1_bar:null,pressure2_bar:null}}]}
  if(path.endsWith('/kiosk/v1/readouts/dtc/latest')) return [503,{code:'backend_unavailable'}];
  return [404,{code:'not_found'}];
}
async function send(method,params={}){
 const id=++nextId;
 const p=new Promise((resolve,reject)=>pending.set(id,{resolve,reject}));
 socket.send(JSON.stringify({id,method,params}));
 return p;
}
async function evalJS(expression){
 const r=await send('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});
 return r.result?.result?.value??r.result?.result?.description??null;
}
(async()=>{
try{
 let tab;
 for(let i=0;i<35;i++){
  try{const pages=await(await fetch('http://127.0.0.1:'+DEBUG+'/json/list')).json();tab=pages.find(x=>x.type==='page');if(tab)break;}catch{}
  await delay(150);
 }
 if(!tab)throw Error('Headless chromium did not expose CDP target');
 socket=new WebSocket(tab.webSocketDebuggerUrl);
 await new Promise((resolve,reject)=>{socket.addEventListener('open',resolve,{once:true});socket.addEventListener('error',reject,{once:true})});
 socket.addEventListener('message',async(event)=>{
  let m=JSON.parse(event.data);
  if(m.id){const p=pending.get(m.id);if(p){pending.delete(m.id);(m.error?p.reject:p.resolve)(m.error??m)}return}
  if(m.method==='Runtime.exceptionThrown') exceptions.push(m.params.exceptionDetails?.exception?.description??m.params.exceptionDetails?.text);
  if(m.method==='Fetch.requestPaused'){
    const params=m.params,uri=params.request.url;
    const [status,data]=answer(uri,params.request.method);
    const body=Buffer.from(JSON.stringify({schema_version:1,[status===200?'data':'error']:data})).toString('base64');
    try{await send('Fetch.fulfillRequest',{requestId:params.requestId,responseCode:status,responseHeaders:[{name:'Content-Type',value:'application/json'},{name:'Cache-Control',value:'no-store'}],body})}catch(e){exceptions.push('Fetch.fulfillRequest '+String(e))}
  }
 });
 await send('Runtime.enable');
 await send('Page.enable');
 await send('Fetch.enable',{patterns:[{urlPattern:'*kiosk/v1/*',requestStage:'Request'}]});
 await send('Page.navigate',{url:'http://'+HOST+':'+PORT+'/'});
 await delay(650);
 const clockStart=await evalJS("document.getElementById('local-clock')?.textContent");
 await delay(2400);
 const clockEnd=await evalJS("document.getElementById('local-clock')?.textContent");
 // The clock MUST advance without waiting for API responses or CAN.
 const toSecond=t=>Number((t??'').slice(-2));
 const clockAdvanced=(toSecond(clockEnd)-toSecond(clockStart)+60)%60;
 await evalJS("location.hash='#/truck-daf'");
 await delay(250);
 const before=await evalJS("({route:location.hash,hidden:document.hidden,visibility:document.visibilityState,button:!!document.querySelector('button[data-route=\"daf-sac\"]')})");
 await evalJS("document.querySelector('button[data-route=\"daf-sac\"]')?.click()");
 await delay(550);
 const ident=await evalJS("({route:location.hash,accept:!!document.getElementById('sac-identity-ok'),phaseText:document.getElementById('sac-identity-sw')?.textContent})");
 await evalJS("document.getElementById('sac-identity-ok')?.click()");
 await delay(4000);
 const final=await evalJS("({route:location.hash,hidden:document.hidden,voltage:document.getElementById('sac-permanent-voltage')?.textContent,pressure1:document.getElementById('sac-pressure-1')?.textContent,pressure2:document.getElementById('sac-pressure-2')?.textContent,pressureUnitHidden:document.getElementById('sac-pressure-1')?.nextElementSibling?.hidden,voltageUnitVisible:!document.getElementById('sac-permanent-voltage')?.nextElementSibling?.hidden,status:document.getElementById('sac-parameters-status')?.textContent})");
 console.log(JSON.stringify({clockStart,clockEnd,clockAdvanced,before,ident,final,counts,exceptions},null,2));
 if(clockAdvanced<2||clockAdvanced>5||
    counts.connect!==1||counts.parameters<2||final?.voltage!=='22.4'||
    final?.pressure1!=='UNAVAILABLE'||final?.pressure2!=='UNAVAILABLE'||
    final?.pressureUnitHidden!==true||final?.voltageUnitVisible!==true||
    exceptions.length)process.exitCode=2;
 else console.log('REAL_CHROMIUM_GUI_MOCKED_E2E=PASS');
}catch(e){console.error('GUI_CHROMIUM_TEST_FAIL',e);process.exitCode=3}
finally{socket?.close();childBrowser.kill('SIGTERM');childServer.kill('SIGTERM')}
})();
