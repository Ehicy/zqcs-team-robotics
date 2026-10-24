import test from 'node:test';
import assert from 'node:assert/strict';
import {DeviceSettings,RemoteController,PA_CHASSIS_CALIBRATION,decodeStatus,encodeCommand,OP} from '../web/protocol.js';
const pid=(revision=1)=>({kind:8,session:7,revision,loaded:true,synced:true,servoFullRange:false,concurrentDrive:false,kpMilli:1200,kiMilli:2300,kdMilli:40});
const wheel=(i,revision=1)=>({kind:7,session:7,revision,...PA_CHASSIS_CALIBRATION[i],maxMmS:333,accelMmS2:500});
const servo=(channel=0,revision=1)=>({kind:9,session:7,revision,channel,minUs:1300,maxUs:1600,referenceUs:1422,calibrated:true,reversed:false,doorClosed:1000,doorOpen:2000});
test('complete settings must share session and revision, including both rear signs',()=>{
 const s=new DeviceSettings();s.accept(pid(),7);for(let i=0;i<3;i++)s.accept(wheel(i),7);assert.equal(s.calibration(),null);
 assert.equal(s.accept(wheel(3),99),false);s.accept(wheel(3),7);
 assert.deepEqual(s.calibration().map(v=>v.motorNegative),[false,true,false,false]);
 s.accept(pid(2),7);assert.equal(s.calibration(),null);assert.equal(s.accept(wheel(0,1),7),false);
 for(let i=0;i<4;i++)s.accept(wheel(i,2),7);assert.equal(s.calibration()[2].kpMilli,1200);
 s.reset();s.accept(pid(65535),7);s.accept(pid(1),7);assert.equal(s.revision,1);
});
test('settings notifications never send targets or restore a saved servo reference',()=>{
 const writes=[],c=new RemoteController({now:()=>0});const g=c.attach(b=>{writes.push(b);return Promise.resolve();});
 c.ingest({kind:0,session:7,uart:false,armed:false,ready:false,pidReady:false,calibratedMask:0},g);
 c.ingest(pid(),g);for(let i=0;i<4;i++)c.ingest(wheel(i),g);c.ingest(servo(),g);
 assert.equal(c.appliedCalibration[3].motorNegative,false);assert.equal(c.deviceSettings.servos[0].referenceUs,1422);
 assert.equal(c.servoStatus,null);assert.equal(writes.length,0);
 c.disconnect();assert.equal(c.deviceSettings.calibration(),null);
});
test('servo settings/encoder-reset wire packets and capabilities round-trip without motion',()=>{
 const p=encodeCommand(OP.SERVO_SETTINGS,{session:7,sequence:1,channel:0,minUs:1300,maxUs:1600,referenceUs:1422,calibrated:true});
 const v=new DataView(p.buffer);assert.equal(p[1],13);assert.equal(p[13],1);assert.equal(v.getUint16(18,true),1422);
 assert.throws(()=>encodeCommand(OP.SERVO_SETTINGS,{session:7,sequence:1,channel:0,minUs:1600,maxUs:1300,referenceUs:1422}));
 const reset=encodeCommand(OP.ENCODER_RESET,{session:7,sequence:2});assert(reset.slice(12).every(b=>!b));
 const ack=new Uint8Array(20);ack[0]=1;ack[1]=3;ack[2]=252;assert.equal(decodeStatus(ack).wheel,252);
 const read=new Uint8Array(20),r=new DataView(read.buffer);read[0]=1;read[1]=8;read[2]=3;r.setUint32(4,7,true);
 for(const[o,n]of[[8,1200],[10,2300],[12,40],[14,200],[16,50],[18,2]])r.setUint16(o,n,true);
 assert.deepEqual(decodeStatus(read),{kind:8,session:7,revision:2,loaded:true,synced:true,servoFullRange:false,concurrentDrive:false,kpMilli:1200,kiMilli:2300,kdMilli:40});
});
test('mechanical servo range applies to controller targets, not only manual sliders',async()=>{
 const c=new RemoteController({now:()=>0});c.attach(()=>Promise.resolve());
 Object.assign(c.state,{session:7,uart:true});c.mainStatusAt=0;
 c.servoStatus={available:true,session:7,pulses:[0,0,0],slew:true,positionMask:7,armFullRange:true};c.servoStatusAt=0;
 c.servoMotion={movingMask:0,errorMask:0};c.servoMotionAt=0;c.deviceSettings.accept(servo(),7);
 await assert.rejects(c.servo(0,1700,null,1422));assert.equal(c.queue.flight,null);
});

test('C3 servo release/settings are independent of chassis parameter readback',async()=>{
 const writes=[],c=new RemoteController({now:()=>0,deviceStorage:true});let g;
 g=c.attach(async b=>{writes.push(b.slice());c.ingest({kind:3,wheel:254,result:0,session:7,
   sourceSequence:new DataView(b.buffer).getUint32(8,true)},g);});
 c.ingest({kind:0,session:7,uart:true,armed:false,active:false,ready:true,pidReady:true,calibratedMask:0},g);
 c.ingest({kind:5,session:7,pulses:[1500,1500,0],available:true,slew:true,positionMask:7,armFullRange:true},g);
 c.ingest({kind:6,session:7,targets:[1500,1500,0],movingMask:0,errorMask:0},g);
 assert.equal(c.servoAvailable(1),false,'missing own settings is visibly unavailable');
 assert.equal(c.servoAvailable(1,true),true,'release does not need a saved angle/limit');
 await c.servo(1,0);assert.equal(writes.length,1);assert.equal(new DataView(writes[0].buffer).getUint16(14,true),0);
 await assert.rejects(c.servo(1,1550),/本路舵机参数/);
 c.ingest(servo(1),g);assert.equal(c.servoAvailable(1),true);
 assert.equal(c.deviceSettings.calibration(),null);
 await assert.rejects(c.servo(1,1700),/范围/,'own C3 limits remain authoritative');
 await c.configureServo(1,{minUs:1300,maxUs:1600,referenceUs:1422,calibrated:true});
 assert.equal(writes.at(-1)[1],OP.SERVO_SETTINGS);
 await assert.rejects(c.configurePid({kpMilli:1000,kiMilli:2000,kdMilli:0}),/完整参数回读/,
   'unrelated chassis parameter-write behavior remains unchanged');
 assert.equal(writes.length,2);
});
