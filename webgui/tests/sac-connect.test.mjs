import test from "node:test";
import assert from "node:assert/strict";
import { SacConnectionFlow } from "../src/sac-connect-flow.mjs";
import { requestSacIdentity, ApiError, SAC_CONNECT_ROOT, SAC_CONNECT_PATH } from "../src/api-client.mjs";
const token = "a".repeat(64);
const ok = () => { const at = Date.now(); return ({
  profile_id:0xDAF00050,bitrate:500000,vin:null,vin_status:"UNPROGRAMMED_FF17",
  software:"2027746",hardware:"K127968",parameters_published:true,
  parameters_status:"completed",parameter_capture_floor_ms:at,
  parameter_captured_at_unix_ms:at,parameter_completed_generation:1
}); };
const reply = (data,status=200) => ({status,ok:status===200,redirected:false,headers:{get:()=> "application/json"},text:async()=>JSON.stringify({schema_version:1,[status===200?"data":"error"]:data})});
test("fixed POST and validated FF17 identification", async()=>{
 let called=0;
 const result=await requestSacIdentity(token,async(url,options)=>{
   called++;
   assert.equal(url,SAC_CONNECT_ROOT+SAC_CONNECT_PATH);
   assert.equal(options.method,"POST");
   assert.equal(options.credentials,"omit");
   return reply(ok());
 });
 assert.equal(called,1); assert.equal(result.vin,null);
 assert.equal(result.software,"2027746");
});
test("invalid identity and communication failure are rejected",async()=>{
 for(const invalid of [{...ok(),hardware:""},{...ok(),profile_id:0xDAF00025}])
   await assert.rejects(requestSacIdentity(token,async()=>reply(invalid)),e=>e.code==="invalid_response");
 await assert.rejects(requestSacIdentity(token,async()=>reply({code:"communication_failed"},503)),e=>e.code==="communication_failed");
});
test("completed capture must bind to exact generation, profile and time",async()=>{
  const fresh=ok();
  for (const corrupt of [
    {...fresh,parameter_captured_at_unix_ms:fresh.parameter_capture_floor_ms-1},
    {...fresh,parameter_completed_generation:0},
    {...fresh,parameter_captured_at_unix_ms:Date.now()+5000},
    {...fresh,parameters_published:false},
    {...fresh,parameters_status:"timeout"},
  ]) await assert.rejects(requestSacIdentity(token,async()=>reply(corrupt)),
      e=>e.code==="invalid_response");
  const unavailable={...fresh,parameters_published:false,parameters_status:"timeout",
    parameter_captured_at_unix_ms:null,parameter_completed_generation:0};
  assert.equal((await requestSacIdentity(token,async()=>reply(unavailable))).parameters_status,"timeout");
});
test("identification must be confirmed, cancelled requests never reopen",async()=>{
 const flow=new SacConnectionFlow();
 await flow.begin(async()=>ok());
 assert.equal(flow.phase,"identified");
 assert.equal(flow.accept(),true);
 assert.equal(flow.phase,"accepted");
 flow.reset();
 let resolve;
 const pending=flow.begin(()=>new Promise(r=>resolve=r));
 flow.reset();
 resolve(ok()); await pending;
 assert.equal(flow.identity,null); assert.equal(flow.phase,"idle");
 await flow.begin(async()=>{throw new ApiError("communication_failed");});
 assert.equal(flow.phase,"failed"); assert.equal(flow.accept(),false);
});
