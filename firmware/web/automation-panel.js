import {MotionLibrary,pulseFromAngle,angleFromPulse,validateProgram} from './motion-library.js';
import {ServoActions} from './servo-actions.js';
import {TeachRecorder,TraceReplay} from './teach-replay.js';
import {RobustOdometry} from './robust-odometry.js';
import {OP} from './protocol.js';
import {mixWheels} from './dashboard-input.js';

export function installAutomation({root,controller,telemetry,calibration,intent,takeover=()=>{},now=()=>controller.now()}){
  if(!root)return null;
  const c=controller,lib=new MotionLibrary(),recorder=new TeachRecorder();
  const names=['大臂','小臂','门'];let odometer=null,odom=null,odomKey='',lastSession=null,draftTrace=null,blocks=[],lastDrive=-Infinity,autoPwm=[0,0];
  const $=id=>document.getElementById(id);
  root.innerHTML=`<h2>动作工坊 · 位置 / 组合状态 / 轨迹复刻</h2>
    <p class="hint">编码器持续参与相对里程估计，坏数据按轮剔除，无需切换编码器模式。记录不需要地图位置；所有内容保存在本浏览器，可导出备份。</p>
    <div class="automation-grid">
    <fieldset><legend>① 给舵机位置命名</legend><label>舵机<select id="positionChannel"><option value="0">大臂 · IO7</option><option value="1">小臂 · IO6</option><option value="2">门 · IO10</option></select></label>
    <label>参考角度 °<input id="positionAngle" type="number" step="0.5" value="135"></label><button id="capturePosition" type="button">读取该路滑块目标</button>
    <label>位置名称<input id="positionName" maxlength="64" placeholder="例如：大臂取物低位"></label><button id="savePosition" type="button">保存 / 覆盖同名位置</button>
    <label>已存位置<select id="positionList"></select></label><div class="row"><button id="editPosition" type="button">载入编辑</button><button id="callPosition" type="button">转到此位置</button><button id="positionToBlock" type="button">加入积木</button><button id="deletePosition" type="button">删除</button></div></fieldset>
    <fieldset><legend>② 三路组合成一个机械臂状态</legend>${names.map((n,i)=>`<label>${n}参考角度 °<input id="poseAngle${i}" type="number" step="0.5" value="${i===0?135:i===1?90:45}"></label>`).join('')}
    <button id="capturePose" type="button">读取三路滑块目标</button><label>状态名称<input id="poseName" maxlength="64" placeholder="例如：收拢 / 取物 / 倾倒"></label><button id="savePose" type="button">保存 / 覆盖状态</button>
    <label>已存状态<select id="poseList"></select></label><div class="row"><button id="editPose" type="button">载入数值编辑</button><button id="callPose" type="button">一键到此状态</button><button id="deletePose" type="button">删除</button></div>
    <p class="hint">一键状态默认三路并行。需先后顺序时，点下方“由此状态生成积木”。</p><button id="poseToBlocks" type="button">由此状态生成积木</button></fieldset></div>
    <fieldset><legend>③ 动作积木 · 顺序 / 并行 / 延时 / 位置触发</legend><p class="hint">选择前置积木与触发条件。依赖“任务开始”即可并行；延时从所选事件开始计时。到位和阈值只看PWM指令回读，不是轴角传感器。</p>
    <div id="actionBlocks" class="action-blocks"></div><button id="addAction" type="button">＋ 舵机积木</button>
    <div class="row"><label>动作名称<input id="programName" maxlength="64" placeholder="例如：先抬臂，再开门"></label><button id="saveProgram" type="button">保存动作</button></div>
    <label>已存动作<select id="programList"></select></label><div class="row"><button id="loadProgram" type="button">载入编辑</button><button id="runSavedProgram" type="button">一键运行已存动作</button><button id="runProgram" type="button">运行当前积木</button><button id="deleteProgram" type="button">删除已存动作</button><button id="stopActions" type="button">停止动作</button></div><p id="actionStatus" role="status"></p></fieldset>
    <fieldset><legend>④ 示教录制与编码器相对路径复刻</legend><p class="hint">开始录制 → WASD手动开车、操作舵机 → 结束并命名。回放跟踪左右轮累计路程，自动纠偏并复现舵机目标；不要求地图坐标。整侧编码器丢失时继续录制的驱动前馈，并明确标为估计。</p>
    <div class="row"><button id="teachStart" type="button">开始录制</button><button id="teachFinish" type="button">结束录制</button><label>路径名称<input id="routeName" maxlength="64" placeholder="例如：起点到取物点"></label><button id="saveRoute" type="button">保存本次记录</button></div>
    <label>已存路径<select id="routeList"></select></label><div class="row"><label>回放速度比例<input id="replayScale" type="number" min="0.1" max="1" step="0.1" value="1"></label><label>里程纠偏 Kp<input id="replayKp" type="number" min="0" max="20" step="0.1" value="1.5"></label><label>终点容差 mm<input id="replayTolerance" type="number" min="1" max="100" value="8"></label></div>
    <div class="row"><button id="replayStart" type="button">复刻选中路径</button><button id="replayStop" type="button">停止复刻</button><button id="deleteRoute" type="button">删除路径</button></div><p id="teachStatus" role="status"></p><p id="odomStatus" class="hint"></p></fieldset>
    <div class="row"><button id="exportMotion" type="button">导出全部记录 JSON</button><label>导入记录<input id="importMotion" type="file" accept=".json,application/json"></label></div>
    <p id="automationMessage" role="status" class="hint"></p>
    <details><summary>精度与使用说明</summary><p>这些定位值都是指令参考。270°大臂完整0–270°需新版C3固件（IO7/IO6/IO10）；旧固件仅108–162°。小臂仍72–108°、门0–90°参考行程。首次启用会从上方“起点参考”建立PWM，不知道实际轴角。上电、加载记录或导入不会自动动作。</p>
    <p>路径保存原始四轮计数、左右里程、控制输入、时间与舵机目标。每次复刻以当前地点为相对起点；请把车放成与录制起点相同的朝向。轮径、CPR、方向和有效轮距沿用机械参数表及下方轮距值。未标定会影响实际距离；轮胎打滑、同侧两路失效不能保证复刻精度。修改机械参数后应重录。</p>
    <p>参考Road Runner的协作式Action、顺序/并行思想，并非移植FTC Java运行库。当前工坊运行在浏览器：页面要保持前台和蓝牙连接；断开或STOP不自动续跑。机械臂动作期间底盘等待，结束后继续路径。到位条件不是堵转检测或真实到位开关。</p></details>`;
  function message(text){$('automationMessage').textContent=text;}
  const run=fn=>async()=>{try{await fn();message('');refresh();}catch(e){message(e.message);}};
  function list(id,kind){const value=$(id).value;$(id).replaceChildren();for(const r of lib.data[kind]){const o=document.createElement('option');o.value=r.name;o.textContent=r.name;$(id).append(o);}if(lib.data[kind].some(r=>r.name===value))$(id).value=value;}
  function selected(id,kind){const r=lib.data[kind].find(r=>r.name===$(id).value);if(!r)throw Error('请先保存或选择一条记录');return r;}
  function refresh(){for(const [id,kind]of [['positionList','positions'],['poseList','poses'],['programList','programs'],['routeList','routes']])list(id,kind);}
  const references=()=>[0,1,2].map(ch=>pulseFromAngle(ch,Number($('servoReference'+ch).value)));
  const currentPose=()=>[0,1,2].map(ch=>pulseFromAngle(ch,Number($('servoAngle'+ch).value)));
  const actions=new ServoActions(c,{onTarget:e=>recorder.servo(e,now())});
  function startActions(steps){if(replay.active)replay.cancel('手动动作接管复刻');takeover();actions.start(steps,references());void actions.tick();}
  function drive(left,right){
    if(now()-lastDrive<45)return;lastDrive=now();
    if(!c.state.connected||!c.state.uart)return;
    autoPwm=[left,right];
    c.queue.offer(c.packet(OP.PWM,{throttle:Math.round((left+right)/2),steer:Math.round((left-right)/2),limit:1000,ttl:200}).bytes,{kind:'drive',expiresAt:now()+100}).catch(()=>{});
  }
  const replay=new TraceReplay({now,drive,stop:()=>{c.stop('复刻停止').catch(()=>{});},servoBusy:()=>actions.active,
    canServo:()=>!c.state.active&&!c.state.armed&&!c.state.configBusy,
    servo:events=>actions.retarget(events,references())});
  function poseSteps(pulses,parallel=false){return pulses.map((pulseUs,channel)=>({channel,pulseUs,settleMs:300,trigger:{after:parallel||!channel?null:channel-1,event:'done',delayMs:0,thresholdUs:1500}}));}
  $('positionChannel').onchange=()=>{$('positionAngle').value=String(angleFromPulse(Number($('positionChannel').value),1500));};
  $('capturePosition').onclick=run(()=>{$('positionAngle').value=$('servoAngle'+$('positionChannel').value).value;});
  $('savePosition').onclick=run(()=>{const channel=Number($('positionChannel').value);lib.save('positions',$('positionName').value,{channel,pulseUs:pulseFromAngle(channel,Number($('positionAngle').value))});});
  $('editPosition').onclick=run(()=>{const r=selected('positionList','positions');$('positionName').value=r.name;$('positionChannel').value=String(r.channel);$('positionAngle').value=String(angleFromPulse(r.channel,r.pulseUs));});
  $('positionToBlock').onclick=run(()=>{const r=selected('positionList','positions');if(blocks.length)blocks=readBlocks();blocks.push({channel:r.channel,pulseUs:r.pulseUs,settleMs:300,trigger:{after:blocks.length?blocks.length-1:null,event:'done',delayMs:0,thresholdUs:1500}});renderBlocks();});
  $('callPosition').onclick=run(()=>{const r=selected('positionList','positions');startActions([{channel:r.channel,pulseUs:r.pulseUs,trigger:{after:null,event:'start',delayMs:0}}]);});
  $('capturePose').onclick=run(()=>{currentPose().forEach((p,ch)=>{$('poseAngle'+ch).value=String(angleFromPulse(ch,p));});});
  $('savePose').onclick=run(()=>lib.save('poses',$('poseName').value,{pulses:[0,1,2].map(ch=>pulseFromAngle(ch,Number($('poseAngle'+ch).value)))}));
  $('editPose').onclick=run(()=>{const r=selected('poseList','poses');$('poseName').value=r.name;r.pulses.forEach((p,ch)=>{$('poseAngle'+ch).value=String(angleFromPulse(ch,p));});});
  $('callPose').onclick=run(()=>startActions(poseSteps(selected('poseList','poses').pulses,true)));
  $('poseToBlocks').onclick=run(()=>{blocks=poseSteps(selected('poseList','poses').pulses);renderBlocks();});
  for(const [button,id,kind]of [['deletePosition','positionList','positions'],['deletePose','poseList','poses'],['deleteProgram','programList','programs'],['deleteRoute','routeList','routes']])$(button).onclick=run(()=>lib.remove(kind,selected(id,kind).name));
  function renderBlocks(){
    $('actionBlocks').innerHTML=blocks.map((s,i)=>`<div class="action-block" data-block="${i}"><strong>积木 ${i+1}</strong><label>舵机<select data-field="channel">${names.map((n,ch)=>`<option value="${ch}" ${ch===s.channel?'selected':''}>${n}</option>`).join('')}</select></label><label>目标角度 °<input data-field="angle" type="number" step="0.5" value="${angleFromPulse(s.channel,s.pulseUs)}"></label><label>触发参照<select data-field="after"><option value="-1">任务开始</option>${Array.from({length:i},(_,j)=>`<option value="${j}" ${j===s.trigger.after?'selected':''}>积木 ${j+1}</option>`).join('')}</select></label><label>条件<select data-field="event"><option value="done" ${s.trigger.event==='done'?'selected':''}>PWM到位后</option><option value="start" ${s.trigger.event==='start'?'selected':''}>启动后</option><option value="position" ${s.trigger.event==='position'?'selected':''}>经过指定角度后</option></select></label><label>延时 秒<input data-field="delay" type="number" min="0" step="0.1" value="${s.trigger.delayMs/1000}"></label><label>触发角度 °<input data-field="threshold" type="number" step="0.5" value="${angleFromPulse(s.trigger.after===null?s.channel:blocks[s.trigger.after].channel,s.trigger.thresholdUs||1500)}"></label><label>到位稳定等待 秒<input data-field="settle" type="number" min="0" max="10" step="0.1" value="${(s.settleMs??300)/1000}"></label><button type="button" data-remove="${i}">删除积木</button></div>`).join('');
    for(const row of $('actionBlocks').children)row.querySelector('[data-field="after"]').value=String(blocks[Number(row.dataset.block)].trigger.after??-1);
  }
  function readBlocks(){return validateProgram([...$('actionBlocks').children].map(row=>{const f=key=>row.querySelector(`[data-field="${key}"]`).value,channel=Number(f('channel')),after=Number(f('after')),parent=after<0?channel:Number($('actionBlocks').children[after].querySelector('[data-field="channel"]').value);return {channel,pulseUs:pulseFromAngle(channel,Number(f('angle'))),settleMs:Number(f('settle'))*1000,trigger:{after:after<0?null:after,event:f('event'),delayMs:Number(f('delay'))*1000,thresholdUs:f('event')==='position'&&after>=0?pulseFromAngle(parent,Number(f('threshold'))):1500}};}));}
  $('actionBlocks').onclick=run(()=>{});
  $('actionBlocks').addEventListener('click',e=>{const b=e.target.closest('[data-remove]');if(!b)return;try{blocks=readBlocks();const i=Number(b.dataset.remove);blocks.splice(i,1);blocks.forEach(s=>{if(s.trigger.after===i)s.trigger.after=null;else if(s.trigger.after>i)s.trigger.after--;});renderBlocks();}catch(e){message(e.message);}});
  $('actionBlocks').addEventListener('change',e=>{const row=e.target.closest('[data-block]');if(!row)return;if(e.target.dataset.field==='channel')row.querySelector('[data-field="angle"]').value=String(angleFromPulse(Number(e.target.value),1500));if(['channel','after'].includes(e.target.dataset.field)){for(const r of $('actionBlocks').children){const index=Number(r.querySelector('[data-field="after"]').value),ch=Number((index<0?r:$('actionBlocks').children[index]).querySelector('[data-field="channel"]').value);r.querySelector('[data-field="threshold"]').value=String(angleFromPulse(ch,1500));}}});
  $('addAction').onclick=run(()=>{if(blocks.length)blocks=readBlocks();const i=blocks.length;blocks.push({channel:i%3,pulseUs:1500,settleMs:300,trigger:{after:i?i-1:null,event:'done',delayMs:0,thresholdUs:1500}});renderBlocks();});
  $('saveProgram').onclick=run(()=>lib.save('programs',$('programName').value,{steps:readBlocks()}));
  $('loadProgram').onclick=run(()=>{const r=selected('programList','programs');blocks=structuredClone(r.steps);$('programName').value=r.name;renderBlocks();});
  $('runProgram').onclick=run(()=>startActions(readBlocks()));
  $('runSavedProgram').onclick=run(()=>startActions(selected('programList','programs').steps));
  $('stopActions').onclick=()=>{actions.cancel();c.stop('用户停止机械臂动作').catch(()=>{});};
  function params(){const wheelCalibration=calibration().map(v=>({cpr:v.countsPerRev,diameterMm:v.diameterTenthMm/10,direction:v.encoderNegative?-1:1,motorDirection:v.motorNegative?-1:1,maxSpeedMmS:v.maxMmS}));return {wheelCalibration,trackWidthMm:Number($('mapTrack')?.value||165)};}
  function ensureOdom(){const p=params(),key=JSON.stringify(p);if(key!==odomKey){if(recorder.active||replay.active)throw Error('录制/复刻期间请保持机械参数不变');odometer=new RobustOdometry(p);odom=odometer.last;odomKey=key;}return p;}
  function primeOdom(){odometer.reset();const t=telemetry();odom=odometer.last;if(t.encoder&&now()-t.encoderAt<500)odom=odometer.update({counts:t.encoder.counts,validMask:t.encoder.validMask,timestampMs:t.encoderAt});}
  function freshOdom(){const t=telemetry(),fresh=now()-t.encoderAt<500;
    if(!fresh)return {...odom,sideTravelMm:odom?.sideTravelMm||[0,0],observable:false,estimated:true,missingSideMask:3};
    if(odom?.reason==='baseline'&&t.encoder){const mask=t.encoder.validMask,missing=(mask&10?0:1)|(mask&5?0:2);return {...odom,observable:!missing,estimated:!!missing,missingSideMask:missing};}
    return odom;
  }
  const sample=()=>({now:now(),odometry:freshOdom(),counts:telemetry().encoder?.counts,intent:intent(),limit:c.state.limit});
  $('teachStart').onclick=run(()=>{if(!c.state.connected||!c.state.uart)throw Error('请先连接底盘');if(replay.active)replay.cancel();const p=ensureOdom();primeOdom();recorder.start({now:now(),calibration:p.wheelCalibration,trackWidthMm:p.trackWidthMm});draftTrace=null;recorder.sample(sample());});
  $('teachFinish').onclick=run(()=>{if(!recorder.active)throw Error('当前没有录制');draftTrace=recorder.finish(sample());});
  $('saveRoute').onclick=run(()=>{if(recorder.active)draftTrace=recorder.finish(sample());if(!draftTrace)throw Error('请先录制并结束');lib.save('routes',$('routeName').value,{trace:draftTrace});});
  $('replayStart').onclick=run(()=>{
    if(recorder.active)throw Error('请先结束录制');if(!c.state.connected||!c.state.uart||!c.state.pwmSupported)throw Error('请连接C3直控固件');
    const trace=selected('routeList','routes').trace,p=ensureOdom();if(JSON.stringify(p.wheelCalibration)!==JSON.stringify(trace.calibration)||p.trackWidthMm!==trace.trackWidthMm)throw Error('机械参数已改变，请恢复录制参数或重录');
    takeover();actions.cancel('轨迹复刻接管');primeOdom();lastDrive=-Infinity;
    replay.start(trace,{odometry:odom,session:c.state.session,generation:c.generation,kp:Number($('replayKp').value),toleranceMm:Number($('replayTolerance').value),scale:Number($('replayScale').value)});
  });
  $('replayStop').onclick=()=>{replay.cancel();actions.cancel();};
  $('exportMotion').onclick=run(()=>{const url=URL.createObjectURL(new Blob([lib.export()],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download='crtc-motion-records.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
  $('importMotion').onchange=run(async()=>{const file=$('importMotion').files[0];if(!file)return;if(file.size>4000000)throw Error('文件超过4MB');lib.import(await file.text());$('importMotion').value='';});
  refresh();message(lib.error);
  return {library:lib,actions,recorder,replay,
    isRunning:()=>actions.active||replay.active,
    target:e=>recorder.servo(e,now()),
    cancel(reason='用户停止'){if(replay.active)replay.cancel(reason,false);if(actions.active)actions.cancel(reason);},
    encoderReset(){
      this.cancel('编码器清零');
      if(recorder.active){try{draftTrace=recorder.finish(sample());}catch{}recorder.active=false;}
      odometer?.reset();odom=odometer?.last;message('编码器已清零；当前录制结束，保存的动作保留');
    },
    receive(packet,at){try{ensureOdom();const manual=mixWheels(intent(),c.state.limit),m=replay.active?(now()-lastDrive<200?{left:autoPwm[0],right:autoPwm[1]}:{left:0,right:0}):manual,max=calibration().map(v=>v.maxMmS),commandedWheelMmS=[m.right*max[0]/1000,m.left*max[1]/1000,m.right*max[2]/1000,m.left*max[3]/1000];odom=odometer.update({counts:packet.counts,validMask:packet.validMask,timestampMs:at,commandedWheelMmS});}catch(e){message(e.message);}},
    tick(){
      if(lastSession!==null&&lastSession!==c.state.session){this.encoderReset();}lastSession=c.state.session;
      if(!c.state.connected&&recorder.active){try{draftTrace=recorder.finish(sample());}catch{}recorder.active=false;}
      if(recorder.active||replay.active){let changed=false;try{changed=JSON.stringify(params())!==odomKey;}catch{changed=true;}if(changed){const moving=replay.active;this.cancel('机械参数改变，请重录');if(moving)c.stop('机械参数改变').catch(()=>{});if(recorder.active){try{draftTrace=recorder.finish(sample());}catch{}recorder.active=false;}message('机械参数已改变，当前任务结束，请重录。');}}
      if(recorder.active)recorder.sample(sample());
      void actions.tick();void replay.tick({odometry:freshOdom(),session:c.state.session,generation:c.generation,connected:c.state.connected&&c.state.uart});
      $('actionStatus').textContent=actions.message;
      $('teachStatus').textContent=recorder.active?`录制中 · ${recorder.trace.samples.length}帧 / ${recorder.trace.servos.length}舵机目标`:replay.trace?replay.message:draftTrace?`已结束 · ${draftTrace.samples.length}帧，可命名保存`:replay.message;
      $('odomStatus').textContent=odom?`相对里程 左/右 ${odom.sideTravelMm.map(v=>v.toFixed(0)).join('/')}mm · 置信度 ${odom.confidence.map(v=>v.toFixed(2)).join('/')} · ${odom.observable?(odom.estimated?'降级估计':'反馈可用'):'整侧缺测，位姿不可观测'} · 不影响手动行驶`:'等待编码器样本';
    },status:()=>({recording:recorder.active,replaying:replay.active,actions:actions.active,odometry:odom,libraryCounts:Object.fromEntries(['positions','poses','programs','routes'].map(k=>[k,lib.data[k].length]))})};
}
