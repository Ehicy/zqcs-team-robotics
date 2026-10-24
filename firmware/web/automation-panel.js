import {WorkshopStore,KeyTimeline,NodeRecorder,encoderPoint,functionKey,validateBlocks,relativeEncoderTargets} from './keyboard-workshop.js?v=portable-20261024';
import {pulseFromAngle} from './motion-library.js';
import {ServoActions} from './servo-actions.js?v=portable-20261024';
import {TraceReplay} from './teach-replay.js?v=portable-20261024';
import {RobustOdometry} from './robust-odometry.js';
import {OP} from './protocol.js?v=portable-20261024';
import {mixWheels,isDriveKey,normalizeKey} from './dashboard-input.js';
const copy=x=>JSON.parse(JSON.stringify(x));
const esc=x=>String(x).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const names=['大臂 IO7 · 270°','小臂 IO6 · 180°','门 IO10 · 180°'];
const timing=b=>b.trigger?`方块${b.trigger.after+1}${b.trigger.event==='done'?'PWM到位':'启动'}后 ${(b.trigger.delayMs/1000).toFixed(3)}s`:`按键后 ${(b.atMs/1000).toFixed(3)}s`;
export function installAutomation({root,controller,telemetry,calibration,intent,takeover=()=>{},driveLimit=()=>controller.state.limit,now=()=>controller.now()}){
  if(!root)return null;
  const c=controller,lib=new WorkshopStore(),recorder=new NodeRecorder();
  let odometer=null,odom=null,odomKey='',recordingId=null,replayingId=null,sequence=null,playEvents=[],eventIndex=0;
  let targets=null,lastDrive=-Infinity,autoPwm=[0,0],lastSession=null,context=null,lastTick=now(),internalStopping=false,editing=null,serial=0,waitingServo=false,recoveringArm=false;
  const $=id=>root.querySelector('#'+id),field=id=>document.getElementById(id);
  const uid=prefix=>prefix+Date.now().toString(36)+'-'+(++serial);
  root.innerHTML=`<h2>按键动作与节点复刻</h2>
    <fieldset><legend>① 一个按键，一个功能</legend>
    <p class="hint">点编号方块可编辑角度与先后关系。可按本次按键计时，也可等前一方块启动或PWM到位；另一个功能键需间隔2秒，长按不连发。</p>
    <label for="shortcutSpeed">快捷动作速度 <output id="shortcutSpeedValue">80 μs/s</output></label>
    <input id="shortcutSpeed" type="range" min="10" max="500" step="10" value="80">
    <p class="hint" id="shortcutSpeedHint">大臂、小臂、门共用；拖动立即调速，仅影响快捷动作，手动角度滑块不变。</p>
    <p class="hint">一键动作持续尝试到设定PWM：短暂掉包/同车断线恢复后继续未完成的目标。停止、关闭继电器、手动接管会取消；主控重启或PWM释放后需重新确认起点。完成表示PWM指令到位，并非测得机械轴角。</p>
    <button id="installArmPresets" type="button">恢复本套 F/G/H 默认参数（只保存）</button>
    <p class="hint">F：大臂232°（按键后0秒），小臂150°（0.25秒）。G：大臂115°、小臂86°（均0秒）。H：小臂147°（0秒）。均不操作门；时间是网页下发目标的时刻，固件与机械动作可能有延迟。</p>
    <div id="keyRows" class="key-rows"></div><button id="addKeyRow" type="button">＋ 功能键</button><button id="stopActions" type="button">停止当前动作</button><p id="actionStatus" role="status"></p></fieldset>
    <fieldset><legend>② 一行一个节点 · 录制键盘操作</legend>
    <p class="hint">录制 → 在本页按 WASD、功能键等操作键 → 结束录制。四个数据是结束时的原始编码器计数：M1右前、M2左前、M3右后、M4左后。重置只清本行，不清实机编码器。</p>
    <div id="nodeRows" class="node-rows"></div><button id="addNode" type="button">＋ 节点</button><button id="replayAll" type="button">顺序复刻全部节点</button><button id="replayStop" type="button">停止复刻</button>
    <p id="teachStatus" role="status"></p><p id="nodeTargetStatus" class="hint"></p><p id="odomStatus" class="hint"></p>
    <details><summary>复刻参数</summary><div class="row"><label>回放速度比例<input id="replayScale" type="number" min="0.1" max="1" step="0.1" value="1"></label><label>里程纠偏 Kp<input id="replayKp" type="number" min="0" max="20" step="0.1" value="1.5"></label><label>终点容差 mm<input id="replayTolerance" type="number" min="1" max="100" value="8"></label></div></details></fieldset>
    <div class="row"><button id="exportMotion" type="button">导出按键与节点 JSON</button><label>导入备份<input id="importMotion" type="file" accept=".json,application/json"></label></div>
    <p id="automationMessage" role="status"></p>
    <details><summary>操作、计时与复刻说明</summary>
    <p>功能键可自定义为单个字母/数字，WASD留给底盘。例如F的方块1设为0.000秒、方块2设为0.500秒，二者都从按F时开始计时，方块2不等待方块1完成。同路舵机有新目标时以较晚方块为准。2秒后可执行另一个功能；带关联关系的功能会接管上一个舵机动作，防止旧动作续跑。普通独立计时方块保持原本时序。时间输入精确到0.001秒；网页、蓝牙、PWM及机械运动不能保证毫秒级真实动作精度。</p>
    <p>当前 C3 v14 需停车调舵机。支持并行动作的固件回传能力后，WASD与舵机可同时执行，松开WASD只停底盘，空格/STOP停全部动作；手动行驶仍会接管路径复刻。首次从释放状态启用使用上方该路起点参考，不自动回中；没有轴角反馈，到位仅指PWM指令到达，不代表实测机械角度。</p>
    <p>录制范围为Dashboard页面的操作按键按下/松开，不录输入框文字、组合快捷键或其他窗口。结束会补齐尚未松开的按键并停止输出。记录包含按键时间、当次功能方块快照、行驶输入和四轮编码器；以后改功能键不会改旧节点。每次录制结束自动保存，无需另起名称再点保存。</p>
    <p>复刻采用录制前馈加编码器相对里程纠偏，不需要地图位置；从当前计数建立新起点。末端四轮目标是当前起点加原记录计数差。四胶轮差速只能独立控制左右两侧，不能保证四个原始计数同时完全一致；轮胎打滑及缺测段会降低精度，估计区段不宣称定位完成。复刻时采用原功能快照，页面需保持前台。停止、断联、会话或编码器原点改变均不自动续跑。</p>
    <p>新浏览器首次打开自动载入本套F/G/H；已有按键与节点继续保留，点击“恢复本套F/G/H默认参数”仅更新这三个功能。按键/节点在本浏览器自动保存，并可导出备份；设备PID、轮参数、舵机限位仍以C3 Flash为准。原位置库的记录留在旧存储键中，不读取、不删除，不会因打开本页执行动作。</p></details>
    <dialog id="blockDialog" aria-labelledby="blockDialogTitle"><form id="blockForm"><h3 id="blockDialogTitle">编辑动作方块</h3>
    <label>舵机<select id="blockChannel">${names.map((n,i)=>`<option value="${i}">${n}</option>`).join('')}</select></label>
    <label>开始条件<select id="blockTrigger"><option value="absolute">按键后计时</option><option value="start">前方块启动后</option><option value="done">前方块PWM到位后</option></select></label>
    <label>依赖方块编号<input id="blockAfter" type="number" min="1" step="1" value="1"></label>
    <label>延时秒数<input id="blockSeconds" type="number" min="0" max="600" step="0.001" value="0.000" required></label>
    <p class="hint">“启动后”从前方块目标被C3确认开始计时；“PWM到位”还需回传目标到达并稳定300ms。延迟回传不会让先后动作同时补发。</p>
    <label>目标角度 <output id="blockAngleValue"></output><input id="blockAngle" type="range" min="0" max="270" step="0.1" value="135"></label>
    <label>精确角度 °<input id="blockAngleExact" type="number" min="0" max="270" step="0.1" value="135" required></label>
    <p id="blockError" role="alert"></p><div class="row"><button id="saveBlock" type="submit">应用方块</button><button id="removeBlock" type="button">删除方块</button><button id="closeBlock" type="button">取消</button></div></form></dialog>`;
  const message=text=>{$('automationMessage').textContent=text;};
  function run(fn){return async()=>{try{await fn();renderRows();message('');}catch(e){message(e.message);}};}
  function params(){return {wheelCalibration:calibration().map(v=>({cpr:v.countsPerRev,diameterMm:v.diameterTenthMm/10,direction:v.encoderNegative?-1:1,motorDirection:v.motorNegative?-1:1,maxSpeedMmS:v.maxMmS})),trackWidthMm:Number(field('mapTrack')?.value||165)};}
  function ensureOdom(){const p=params(),key=JSON.stringify(p);if(key!==odomKey){if(recorder.active||replay.active)throw Error('录制/复刻期间机械参数改变，请结束并重录');odometer=new RobustOdometry(p);odom=odometer.last;odomKey=key;}return p;}
  function primeOdom(){odometer.reset();odom=odometer.last;const t=telemetry();if(t.encoder&&now()-t.encoderAt<500)odom=odometer.update({counts:t.encoder.counts,validMask:t.encoder.validMask,timestampMs:t.encoderAt});}
  function freshOdom(){const t=telemetry();if(now()-t.encoderAt>=500)return {...odom,sideTravelMm:odom?.sideTravelMm||[0,0],observable:false,estimated:true,missingSideMask:3};if(odom?.reason==='baseline'&&t.encoder){const mask=t.encoder.validMask,missing=(mask&10?0:1)|(mask&5?0:2);return {...odom,observable:!missing,estimated:!!missing,missingSideMask:missing};}return odom;}
  const point=()=>{const t=telemetry();return encoderPoint(t.encoder,t.encoderAt,now());};
  const sample=()=>({at:now(),odometry:freshOdom(),counts:telemetry().encoder?.counts,intent:intent(),limit:driveLimit(intent())});
  const references=()=>[0,1,2].map(ch=>pulseFromAngle(ch,Number(field('servoReference'+ch)?.value??(ch?90:135))));
  const actions=new ServoActions(c,{speedUsS:80});
  // Runtime-only action pacing: do not write Flash or start motion when adjusting.
  $('shortcutSpeed').addEventListener('input',()=>{
    const speed=Number($('shortcutSpeed').value);
    if(!Number.isFinite(speed)||speed<10||speed>500)return;
    actions.speedUsS=speed;$('shortcutSpeedValue').textContent=`${speed} μs/s`;
  });
  const timeline=new KeyTimeline({now,ready:b=>!c.state.configBusy&&(!b.program||!actions.busy)&&c.servoAvailable(b.channel),
    send:events=>{actions.retarget(events,references());},sendProgram:steps=>{actions.cancel('新功能接管');actions.start(steps,references());}});
  function rememberContext(){context={session:c.state.session,generation:c.generation,stopGeneration:c.stopGeneration};lastTick=now();}
  function stopAuto(){timeline.cancel();actions.cancel();internalStopping=true;try{c.stop('节点复刻停止').catch(e=>message(e.message));if(context)context.stopGeneration=c.stopGeneration;}finally{internalStopping=false;}}
  function drive(left,right){if(now()-lastDrive<45)return;lastDrive=now();if(!c.state.connected||!c.state.uart)return;autoPwm=[left,right];c.queue.offer(c.packet(OP.PWM,{throttle:Math.round((left+right)/2),steer:Math.round((left-right)/2),limit:1000,ttl:200}).bytes,{kind:'drive',expiresAt:now()+100}).catch(()=>{});}
  const replay=new TraceReplay({now,drive,stop:stopAuto,servo:()=>{},concurrentMotion:()=>!!c.concurrentMotion?.(),servoBusy:()=>actions.active||waitingServo||timeline.pending.some(b=>b.due<=now()&&!timeline.ready(b)),canServo:()=>true});
  function renderRows(){
    $('keyRows').innerHTML=lib.data.bindings.map(row=>`<div class="key-action-row" data-key-row="${esc(row.id)}"><label>按键<input data-key="${esc(row.id)}" aria-label="功能按键" maxlength="1" value="${esc(row.key)}"></label><input data-key-name="${esc(row.id)}" aria-label="功能名称" maxlength="64" value="${esc(row.name)}"><div class="number-blocks">${row.blocks.map((b,i)=>`<button class="number-block" type="button" data-block-row="${esc(row.id)}" data-block-index="${i}" title="${esc(names[b.channel])} · ${esc(timing(b))} → ${b.angle}°"><b>${i+1}</b><small>${esc(timing(b))} · ${b.angle}°</small></button>`).join('')}<button type="button" data-add-block="${esc(row.id)}">＋</button></div><button type="button" data-run-key="${esc(row.id)}">执行 ${esc(row.key.toUpperCase())}</button><button type="button" data-delete-key="${esc(row.id)}">删除行</button></div>`).join('');
    $('nodeRows').innerHTML=lib.data.nodes.map((node,i)=>{const end=node.trace?.endEncoder;return `<div class="node-row" data-node-row="${esc(node.id)}"><strong>${i+1}</strong><input aria-label="节点名称" data-node-name="${esc(node.id)}" maxlength="64" value="${esc(node.name)}"><div class="node-counts">${[0,1,2,3].map(w=>`<div><small>M${w+1} ${['右前','左前','右后','左后'][w]}</small><output title="${end?.validMask&(1<<w)?'录制结束时采样有效':'未录制或结束采样无效/过期'}">${end?.counts[w]??'—'}${end&&!(end.validMask&(1<<w))?' ⚠':''}</output></div>`).join('')}</div><div class="node-buttons"><button type="button" data-reset-node="${esc(node.id)}">重置</button><button type="button" data-record-node="${esc(node.id)}" ${recordingId?'disabled':''}>录制</button><button type="button" data-finish-node="${esc(node.id)}" ${recordingId===node.id?'':'disabled'}>结束录制</button><button type="button" data-replay-node="${esc(node.id)}" ${node.trace&&!recordingId?'':'disabled'}>复刻</button><button type="button" data-delete-node="${esc(node.id)}">删除</button></div><span class="node-meta">${recordingId===node.id?'● 录制中':node.trace?`${(node.trace.samples.at(-1).t/1000).toFixed(2)}秒 · ${node.trace.keyEvents.length}键盘事件`:'未录制'}</span></div>`;}).join('');
  }
  function openBlock(id,index){const row=lib.data.bindings.find(r=>r.id===id),b=row.blocks[index]||{channel:0,atMs:0,angle:135};editing={id,index};$('blockDialogTitle').textContent=`${row.key.toUpperCase()} · 方块 ${index+1}`;$('blockChannel').value=String(b.channel);$('blockTrigger').value=b.trigger?.event||'absolute';$('blockAfter').value=String((b.trigger?.after??0)+1);$('blockAfter').max=String(Math.max(1,index));$('blockAfter').disabled=!b.trigger;$('blockSeconds').value=((b.trigger?.delayMs??b.atMs)/1000).toFixed(3);$('blockAngle').max=$('blockAngleExact').max=String([270,180,180][b.channel]);$('blockAngle').value=$('blockAngleExact').value=String(b.angle);$('blockAngleValue').textContent=b.angle+'°';$('blockError').textContent='';$('removeBlock').hidden=index>=row.blocks.length;if($('blockDialog').showModal)$('blockDialog').showModal();else $('blockDialog').setAttribute('open','');}
  // Absolute timing has no predecessor. Disable the unused field so browser
  // form validation cannot reject block 1 with min=1 / max=0.
  $('blockTrigger').onchange=()=>{$('blockAfter').disabled=$('blockTrigger').value==='absolute';};
  function closeBlock(){if($('blockDialog').close)$('blockDialog').close();else $('blockDialog').removeAttribute('open');editing=null;}
  $('blockForm').onsubmit=e=>{e.preventDefault();try{const seconds=Number($('blockSeconds').value),atMs=Math.round(seconds*1000),angle=Number($('blockAngleExact').value),channel=Number($('blockChannel').value);if($('blockSeconds').value.trim()===''||$('blockAngleExact').value.trim()===''||Math.abs(seconds*1000-atMs)>0.00001)throw Error('请输入秒数，最多三位小数');const b={channel,atMs,angle},mode=$('blockTrigger').value;if(mode!=='absolute')b.trigger={after:Number($('blockAfter').value)-1,event:mode,delayMs:atMs};lib.edit(d=>{d.bindings.find(r=>r.id===editing.id).blocks[editing.index]=b;});closeBlock();renderRows();}catch(error){$('blockError').textContent=error.message;}};
  $('blockChannel').onchange=()=>{const ch=Number($('blockChannel').value),max=[270,180,180][ch];$('blockAngle').max=$('blockAngleExact').max=String(max);const angle=Math.min(max,Number($('blockAngleExact').value));$('blockAngle').value=$('blockAngleExact').value=String(angle);$('blockAngleValue').textContent=angle+'°';};
  $('blockAngle').oninput=()=>{$('blockAngleExact').value=$('blockAngle').value;$('blockAngleValue').textContent=$('blockAngle').value+'°';};
  $('blockAngleExact').oninput=()=>{$('blockAngle').value=$('blockAngleExact').value;$('blockAngleValue').textContent=$('blockAngleExact').value+'°';};
  $('closeBlock').onclick=closeBlock;
  $('removeBlock').onclick=run(()=>{lib.edit(d=>{const row=d.bindings.find(r=>r.id===editing.id);if(row.blocks.length===1)throw Error('一行至少保留一个方块');row.blocks.splice(editing.index,1);});closeBlock();});
  $('addKeyRow').onclick=run(()=>{const key=[...'fghjklqertyuio pzxcvbnm0123456789'.replaceAll(' ','')].find(k=>!lib.data.bindings.some(r=>r.key===k));if(!key)throw Error('没有可用功能键');lib.edit(d=>d.bindings.push({id:uid('key'),key,name:'新功能',blocks:[{channel:1,atMs:0,angle:90},{channel:0,atMs:500,angle:135,trigger:{after:0,event:'start',delayMs:500}}]}));});
  $('installArmPresets').onclick=run(()=>{if(recorder.active||replay.active||timeline.active||actions.active)throw Error('请先结束当前动作再更改功能键');lib.installArmPresets();});
  $('addNode').onclick=run(()=>lib.edit(d=>d.nodes.push({id:uid('node'),name:'节点'+(d.nodes.length+1),trace:null})));
  function cancel(reason='用户停止') {recoveringArm=false;sequence=null;if(replay.active)replay.cancel(reason,false);timeline.cancel(reason);actions.cancel(reason);replayingId=null;targets=null;context=null;}
  function trigger(row,at=now(),record=true){
    if(!c.state.connected||!c.state.uart){timeline.message='请先连接底盘，再执行功能键';return false;}
    if(!timeline.trigger(row,at))return false;
    const wasDriving=replay.active||!!sequence;
    if(wasDriving){cancel('功能键接管复刻');}
    const moving=c.state.active||c.state.armed||c.state.armPending||c.state.remoteArmed||intent().throttle||intent().steer;
    if(!c.concurrentMotion?.()||wasDriving){
      takeover();if(moving)c.stop('功能键接管，先停止自动底盘').catch(e=>message(e.message));
    }
    timeline.trigger(row,at);rememberContext();
    if(record&&recorder.active)recorder.key('down',row.key,at,{accepted:true,action:row.blocks});
    return true;
  }
  function startRecording(id){if(recorder.active)throw Error('请先结束当前节点录制');if(!c.state.connected||!c.state.uart)throw Error('请先连接底盘');if(replay.active||sequence){cancel();stopAuto();}const p=ensureOdom();primeOdom();recorder.start({at:now(),calibration:p.wheelCalibration,trackWidthMm:p.trackWidthMm,startEncoder:point()});recordingId=id;recorder.sample(sample(),true);renderRows();}
  function finishRecording(){if(!recorder.active)throw Error('没有正在录制的节点');const id=recordingId,trace=recorder.finish(sample(),point());recordingId=null;lib.edit(d=>{d.nodes.find(n=>n.id===id).trace=trace;});cancel('录制结束，输出已停止');takeover();stopAuto();renderRows();return trace;}
  function startNode(id){
    const node=lib.data.nodes.find(n=>n.id===id);if(!node?.trace)throw Error('节点尚未录制');
    if(!c.state.connected||!c.state.uart||!c.state.pwmSupported)throw Error('请连接C3直控固件');
    const p=ensureOdom(),trace=node.trace;if(JSON.stringify(p.wheelCalibration)!==JSON.stringify(trace.calibration)||p.trackWidthMm!==trace.trackWidthMm)throw Error('机械参数与录制时不同，请恢复参数或重录');
    takeover();timeline.cancel();actions.cancel();primeOdom();lastDrive=-Infinity;targets=relativeEncoderTargets(trace,point());
    playEvents=[...trace.keyEvents.map(e=>({...e,kind:'key'})),...trace.directServos.map(e=>({...e,kind:'servo'}))].sort((a,b)=>a.t-b.t);eventIndex=0;replayingId=id;rememberContext();
    replay.start({...trace,servos:[]},{odometry:odom,session:c.state.session,generation:c.generation,kp:Number($('replayKp').value),toleranceMm:Number($('replayTolerance').value),scale:Number($('replayScale').value)});
  }
  $('nodeRows').addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;void run(()=>{const d=b.dataset;if(d.recordNode)startRecording(d.recordNode);if(d.finishNode)finishRecording();if(d.resetNode||d.deleteNode){const id=d.resetNode||d.deleteNode;if(recordingId===id)throw Error('请先结束本行录制');if(replayingId===id||sequence){cancel('节点记录改变');stopAuto();}lib.edit(data=>{if(d.deleteNode)data.nodes=data.nodes.filter(n=>n.id!==id);else data.nodes.find(n=>n.id===id).trace=null;});}if(d.replayNode){if(recorder.active)throw Error('请先结束录制');cancel();startNode(d.replayNode);}})();});
  $('keyRows').addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;void run(()=>{const d=b.dataset;if(d.blockRow)openBlock(d.blockRow,Number(d.blockIndex));if(d.addBlock){const r=lib.data.bindings.find(r=>r.id===d.addBlock);openBlock(r.id,r.blocks.length);}if(d.deleteKey)lib.edit(data=>{data.bindings=data.bindings.filter(r=>r.id!==d.deleteKey);});if(d.runKey)trigger(lib.data.bindings.find(r=>r.id===d.runKey));})().then(()=>{});});
  for(const id of ['keyRows','nodeRows'])$(id).addEventListener('change',e=>{const d=e.target.dataset;try{lib.edit(data=>{if(d.key)data.bindings.find(r=>r.id===d.key).key=functionKey(e.target.value);if(d.keyName)data.bindings.find(r=>r.id===d.keyName).name=e.target.value;if(d.nodeName)data.nodes.find(r=>r.id===d.nodeName).name=e.target.value;});message('已自动保存');}catch(error){message(error.message);}renderRows();});
  $('stopActions').onclick=()=>{cancel('用户停止动作');stopAuto();};$('replayStop').onclick=()=>{cancel('用户停止复刻');stopAuto();};
  $('replayAll').onclick=run(()=>{if(recorder.active)throw Error('请先结束录制');const ids=lib.data.nodes.filter(n=>n.trace).map(n=>n.id);if(!ids.length)throw Error('请先录制节点');cancel();sequence={ids,index:0,waiting:false};startNode(ids[0]);});
  $('exportMotion').onclick=run(()=>{const url=URL.createObjectURL(new Blob([lib.export()],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download='crtc-keyboard-nodes.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);});
  $('importMotion').onchange=run(async()=>{if(recorder.active||replay.active||timeline.active||actions.active)throw Error('先结束当前任务再导入');const file=$('importMotion').files[0];if(!file)return;if(file.size>4000000)throw Error('文件超过4MB');lib.import(await file.text());$('importMotion').value='';});
  renderRows();message(lib.error);
  return {library:lib,actions,recorder,replay,timeline,
    isRunning:()=>replay.active||!!sequence||timeline.active||actions.active,
    isDriving:()=>replay.active||!!sequence,
    target:e=>{if(e.pulseUs)recorder.target(e,now());},
    cancel,
    onControllerLock(event={}){
      if(internalStopping)return;
      if(event.recoverable&&!replay.active&&!sequence&&!recorder.active&&(actions.active||timeline.active)){
        recoveringArm=true;context=null;actions.pause(event.reason);
        timeline.message='一键目标已保留，等待同车通信恢复';return;
      }
      cancel(event.reason||'控制已停止');
    },
    clearInput(){if(recorder.active){for(const key of [...recorder.held])if(isDriveKey(key))recorder.key('up',key,now(),{synthetic:true});recorder.sample({...sample(),intent:{throttle:0,steer:0}},true);}},
    keyEvent(type,event){if(event.repeat)return false;const key=normalizeKey(event.key),at=now(),row=lib.data.bindings.find(r=>r.key===key);let accepted=false;
      if(type==='down'&&row){accepted=trigger(row,at,false);}
      if(recorder.active){recorder.key(type,key,at,{accepted,action:accepted?row.blocks:null});recorder.sample(sample(),true);}
      return !!row;
    },
    encoderReset(){cancel('编码器原点改变');if(recorder.active){try{finishRecording();}catch(e){recorder.active=false;recordingId=null;message(e.message);}}odometer?.reset();odom=odometer?.last;},
    receive(packet,at){try{ensureOdom();const m=replay.active?{left:autoPwm[0],right:autoPwm[1]}:mixWheels(intent(),driveLimit(intent())),max=calibration().map(v=>v.maxMmS);odom=odometer.update({counts:packet.counts,validMask:packet.validMask,timestampMs:at,commandedWheelMmS:[m.right*max[0]/1000,m.left*max[1]/1000,m.right*max[2]/1000,m.left*max[3]/1000]});}catch(e){message(e.message);}},
    tick(){
      if(lastSession!==null&&lastSession!==c.state.session){
        if(recoveringArm){odometer?.reset();odom=odometer?.last;}
        else this.encoderReset();
      }lastSession=c.state.session;
      if(recorder.active&&(!c.state.connected||!c.state.uart)){try{finishRecording();message('连接中断，节点已结束并保存；终点质量见标记');}catch(e){recorder.active=false;recordingId=null;message(e.message);}}
      if(context&&(context.session!==c.state.session||context.generation!==c.generation||context.stopGeneration!==c.stopGeneration||!c.state.connected||now()-lastTick>400&&(replay.active||sequence))){cancel('会话、连接或页面调度改变');}lastTick=now();
      if(recorder.active||replay.active){try{if(JSON.stringify(params())!==odomKey)throw Error('机械参数改变，请重录');if(recorder.active)recorder.sample(sample());}catch(e){if(recorder.active){try{finishRecording();}catch{recorder.active=false;recordingId=null;}}cancel(e.message);stopAuto();message(e.message);}}
      waitingServo=false;
      try{if(replay.active){while(eventIndex<playEvents.length&&playEvents[eventIndex].t<=replay.clock){const e=playEvents[eventIndex++];if(e.kind==='servo'){if(!c.servoAvailable(e.channel)){waitingServo=true;eventIndex--;break;}actions.retarget([e],references());}else if(e.type==='down'&&e.accepted&&e.action){timeline.trigger({key:e.key,blocks:e.action},now(),{recorded:true});}else if(e.type==='down'&&isDriveKey(e.key)&&!c.concurrentMotion?.()){timeline.cancel('录制的手动行驶接管');actions.cancel();}else if(e.type==='down'&&[' ','escape','esc','spacebar'].includes(e.key)){stopAuto();rememberContext();}}}}catch(e){cancel(e.message);stopAuto();message(e.message);}
      try{timeline.tick();}catch(e){cancel(e.message);message(e.message);}void actions.tick();
      if(recoveringArm&&c.state.connected&&c.state.session&&c.servoStatus?.session===c.state.session&&actions.pausedAt===null)recoveringArm=false;
      const was=replay.active;void replay.tick({odometry:freshOdom(),session:c.state.session,generation:c.generation,connected:c.state.connected&&c.state.uart});
      if(was&&!replay.active&&sequence){if(replay.message.startsWith('复刻完成')||replay.message.startsWith('复刻时序结束')){sequence.index++;sequence.waiting=true;}else sequence=null;}
      if(sequence?.waiting&&!c.state.active&&!c.state.armed&&!c.state.remoteArmed&&!c.state.configBusy){if(sequence.index>=sequence.ids.length){sequence=null;replay.message='全部节点复刻结束';}else{try{sequence.waiting=false;startNode(sequence.ids[sequence.index]);}catch(e){cancel(e.message);message(e.message);}}}
      $('actionStatus').textContent=timeline.message+(actions.steps.length?' · '+actions.message:'');
      $('teachStatus').textContent=recorder.active?`录制中 · ${recorder.trace.keyEvents.length}键盘事件 / ${recorder.trace.samples.length}编码器采样`:replay.message;
      $('nodeTargetStatus').textContent=targets?`本次相对终点目标 M1/M2/M3/M4：${targets.map(v=>v??'—').join(' / ')}（原始计数，缺测不伪造）`:'';
      $('odomStatus').textContent=odom?`相对里程 左/右 ${odom.sideTravelMm.map(v=>v.toFixed(0)).join('/')}mm · ${odom.observable?(odom.estimated?'部分估计':'反馈可用'):'缺测，不能确认位置'} · 不影响手动行驶`:'等待编码器样本';
    },status:()=>({recording:recorder.active,replaying:replay.active,actions:actions.active||timeline.active,recordingNode:recordingId,replayingNode:replayingId,odometry:odom,libraryCounts:{bindings:lib.data.bindings.length,nodes:lib.data.nodes.length}})};
}
