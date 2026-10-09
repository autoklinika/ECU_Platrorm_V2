import test from "node:test";
import assert from "node:assert/strict";
import { SacParameterMonitor } from "../src/sac-parameter-monitor.mjs";
import { SacConnectionFlow } from "../src/sac-connect-flow.mjs";

let now=1791545568000;
const identity=(overrides={})=>({
  vin:null,vin_status:"UNPROGRAMMED_FF17",software:"2027746",hardware:"K127968",
  bitrate:500000,profile_id:0xDAF00050,parameters_published:true,
  parameters_status:"completed",parameter_capture_floor_ms:now,
  parameter_captured_at_unix_ms:now,parameter_completed_generation:1,...overrides
});
const parameters=(stamp=now,overrides={})=>({
  source:"completed_application_operation", live:false,
  captured_at_unix_ms:stamp,profile_id:0xDAF00050,completed_generation:1,
  parameters:{permanent_voltage_v:22.4,ignition_voltage_v:22.4,
    pgn_feae_observed:true,pressure1_bar:null,pressure2_bar:null},...overrides
});
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function clockwork(read, onUpdate) {
  let nextId=0;
  const tasks=new Map();
  const monitor=new SacParameterMonitor({
    read,onUpdate,clock:()=>now,intervalMs:1200,retryMs:4000,
    schedule:(fn,ms)=>{const id=++nextId; tasks.set(id,{fn,ms});return id;},
    cancel:(id)=>tasks.delete(id)
  });
  return {
    monitor,tasks,
    async tick() {
      const [id,job]=tasks.entries().next().value??[];
      assert.ok(job,"expected one bounded scheduled poll");
      tasks.delete(id);
      job.fn();
      await flush();
    }
  };
}

test("continuous screen polling: two distinct native read cycles and null pressures",async()=>{
  const updates=[];
  let calls=0;
  const start=identity({parameter_captured_at_unix_ms:now-1000});
  const w=clockwork(async()=>{
    calls++;
    now+=2000;
    return {operation:identity({parameter_captured_at_unix_ms:now}),
      parameters:parameters(now)};
  }, x=>updates.push(x));
  w.monitor.start(start);
  await w.tick();
  assert.equal(calls,1);
  assert.equal(updates.at(-1).status,"updated");
  assert.equal(updates.at(-1).parameters.parameters.pressure1_bar,null);
  assert.equal([...w.tasks.values()][0].ms,1200);
  await w.tick();
  assert.equal(calls,2);
  assert.equal(updates.at(-1).status,"updated");
  assert.equal(w.tasks.size,1);
  w.monitor.stop();
  assert.equal(w.tasks.size,0);
});

test("no overlapping hardware requests; leaving page ignores an in-flight reply",async()=>{
  const updates=[];
  let resolve;
  let calls=0;
  const w=clockwork(async()=>{calls++;return new Promise(r=>resolve=r);},
    x=>updates.push(x));
  w.monitor.start(identity());
  await w.tick();
  assert.equal(w.monitor.busy,true);
  assert.equal(w.monitor.refreshNow(),false);
  w.monitor.stop();
  resolve({operation:identity(),parameters:parameters()});
  await flush();
  assert.equal(calls,1);
  assert.equal(w.monitor.active,false);
  assert.equal(w.tasks.size,0);
  assert.deepEqual(updates.map(x=>x.status),["reading"]);
});

test("lost communication and corrupt snapshot both fail closed and retry later",async()=>{
  const events=[];
  let attempt=0;
  const w=clockwork(async()=>{
    attempt++;
    if(attempt===1) throw Error("ECU disconnect");
    now+=2000;
    return {operation:identity({parameter_captured_at_unix_ms:now}),
      parameters:parameters(now+1)};
  },e=>events.push(e.status));
  w.monitor.start(identity({parameter_captured_at_unix_ms:now-1000}));
  await w.tick();
  assert.equal(events.at(-1),"error");
  assert.equal([...w.tasks.values()][0].ms,4000);
  await w.tick();
  assert.equal(events.at(-1),"invalid_readout");
  assert.equal([...w.tasks.values()][0].ms,4000);
  w.monitor.stop();
});

test("profile or CAN speed mismatch stops acquisition without re-identification",async()=>{
  for (const change of [
    {bitrate:250000,profile_id:0xDAF00025},
    {profile_id:0xDAF00025}
  ]) {
    let calls=0;
    const events=[];
    const w=clockwork(async()=>{
      calls++; now+=2000;
      return {operation:identity({...change,parameter_captured_at_unix_ms:now}),
        parameters:parameters(now)};
    },e=>events.push(e.status));
    w.monitor.start(identity({parameter_captured_at_unix_ms:now-1000}));
    await w.tick();
    assert.equal(events.at(-1),"profile_mismatch");
    assert.equal(w.monitor.active,false);
    assert.equal(w.tasks.size,0);
    assert.equal(calls,1);
  }
});

test("new identity with unavailable pressure service clears measured values",async()=>{
  const events=[];
  const w=clockwork(async()=>({
    operation:identity({parameters_published:false,parameters_status:"timeout",
      parameter_captured_at_unix_ms:null,parameter_completed_generation:0}),
    parameters:null
  }),e=>events.push(e));
  w.monitor.start(identity());
  await w.tick();
  assert.equal(events.at(-1).status,"timeout");
  assert.equal(events.at(-1).operation.parameters_published,false);
  w.monitor.stop();
});

test("completed connection identity remains unchanged across parameter screen",async()=>{
  const flow=new SacConnectionFlow();
  await flow.begin(async()=>identity());
  assert.equal(flow.accept(),true);
  assert.equal(flow.phase,"accepted");
  assert.equal(flow.identity.hardware,"K127968");
  flow.reset();
  assert.equal(flow.identity,null);
});

test("expired backend session stops polling and requires explicit reconnect",async()=>{
  const updates=[];
  const w=clockwork(async()=>{const e=Error("expired");e.code="session_expired";throw e;},
    e=>updates.push(e.status));
  w.monitor.start(identity());
  await w.tick();
  assert.equal(updates.at(-1),"session_expired");
  assert.equal(w.monitor.active,false);
  assert.equal(w.tasks.size,0);
});
