import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { KioskSession, ApiError } from "../src/api-client.mjs";

const payload = (data, status=200) => ({
  ok:status===200,status,redirected:false,
  headers:{get:()=>"application/json"},
  async text() {return JSON.stringify({schema_version:1,
    [status===200?"data":"error"]:data});}
});

test("kiosk reads API status via same-origin proxy without a browser credential",async()=>{
  let count=0;
  const kiosk = new KioskSession(async(url, init)=>{
    count++;
    assert.equal(url,"/kiosk/v1/about");
    assert.equal(init.method,"GET");
    assert.equal(init.headers["X-ECU-Kiosk"],"v1");
    assert.equal(init.headers.Authorization,undefined);
    assert.equal(init.credentials,"omit");
    return payload({api_version:"v1",read_only:true,build_revision:"sample"});
  });
  const about=await kiosk.read("/api/v1/about");
  assert.equal(about.read_only,true);
  assert.equal(count,1);
  await assert.rejects(kiosk.read("/api/v1/control"),e=>e.code==="invalid_client_request");
});

test("SAC connection uses exact same-origin path and explicit POST",async()=>{
  const identity={
    vin:null,vin_status:"UNPROGRAMMED_FF17",
    software:"2027746",hardware:"K127968",
    profile_id:0xDAF00050,bitrate:500000,parameters_published:false,
    parameters_status:"unavailable", parameter_capture_floor_ms:Date.now(),
    parameter_captured_at_unix_ms:null,parameter_completed_generation:0
  };
  const kiosk = new KioskSession(async(url, init)=>{
    assert.equal(url,"/kiosk/v1/bench/daf-sac/connect");
    assert.equal(init.method,"POST");
    assert.equal(init.headers["X-ECU-Kiosk"],"v1");
    assert.equal(init.headers.Authorization,undefined);
    return payload(identity);
  });
  assert.equal((await kiosk.identifySac()).hardware,"K127968");
});

test("prototype UI displays no token field and preserves fail closed errors",async()=>{
  const html=readFileSync(new URL("../index.html",import.meta.url),"utf8");
  const app=readFileSync(new URL("../src/app.mjs",import.meta.url),"utf8");
  assert.doesNotMatch(html,/id="api-token"/);
  assert.match(html,/id="prototype-api-state"/);
  assert.match(app,/new KioskSession\(\)/);
  assert.match(html,/connect-src 'self';/);
  const kiosk=new KioskSession(async()=>payload({code:"communication_failed"},503));
  await assert.rejects(kiosk.identifySac(),e=>e instanceof ApiError && e.code==="communication_failed");
});


test("both SAC CAN profiles are accepted only with matching bitrate",async()=>{
  for (const [speed,profile] of [[250000,0xDAF00025],[500000,0xDAF00050]]) {
    const now=Date.now();
    const identity={
      vin:null,vin_status:"UNPROGRAMMED_FF17",software:"2027746",
      hardware:"K127968",bitrate:speed,profile_id:profile,
      parameters_published:true,parameters_status:"completed",
      parameter_capture_floor_ms:now,parameter_captured_at_unix_ms:now,
      parameter_completed_generation:5
    };
    const kiosk=new KioskSession(async()=>payload(identity));
    const selected=await kiosk.identifySac();
    assert.equal(selected.bitrate,speed);
    assert.equal(selected.profile_id,profile);
    const wrong=new KioskSession(async()=>payload({...identity,profile_id:
      profile===0xDAF00025 ? 0xDAF00050 : 0xDAF00025}));
    await assert.rejects(wrong.identifySac(),e=>e.code==="invalid_response");
  }
});
