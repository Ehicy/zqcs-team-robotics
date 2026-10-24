// Windows BLE may deliver a valid GATT response after 600ms. Warn at that
// point, but keep the SAME write in flight: no parallel write or blind retry.
export const BLE_WRITE_WARN_MS=600;
export const BLE_WRITE_TIMEOUT_MS=1500;
export const BLE_COMMAND_ACK_MS=2500; // 500ms queue + 1500ms GATT + ACK margin.
export function createBleWriter({write,onSlow=()=>{},timeoutMessage=()=>`写入响应超时（${BLE_WRITE_TIMEOUT_MS}ms）`,
  setTimer=(fn,ms)=>setTimeout(fn,ms),clearTimer=id=>clearTimeout(id)}){
  return bytes=>new Promise((resolve,reject)=>{
    let finished=false,warning,deadline;
    const finish=(handler,value)=>{
      if(finished)return;finished=true;clearTimer(warning);clearTimer(deadline);handler(value);
    };
    warning=setTimer(()=>{if(!finished){try{onSlow();}catch{}}},BLE_WRITE_WARN_MS);
    deadline=setTimer(()=>finish(reject,new Error(timeoutMessage())),BLE_WRITE_TIMEOUT_MS);
    // Hand off synchronously while this connection is current. Deferring the
    // call a microtask could start an old queued write after disconnect.
    try{Promise.resolve(write(bytes)).then(value=>finish(resolve,value),error=>finish(reject,error));}
    catch(error){finish(reject,error);}
  });
}
