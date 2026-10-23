"""Three independent local servo channels, one serialized ST-Link owner.

No automatic centering, flash writes, or feedback-angle claims. Watchdog is
host-side only: a broken SWD link can leave the last PWM active.
"""
import collections
import msvcrt
import re
import struct
import subprocess
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = r'D:\programe\stm32\bin\STM32_Programmer_CLI.exe'
NM = r'D:\DEVELOP\CQU_AI\tools\arm-gnu-14.3\bin\arm-none-eabi-nm.exe'
ELF = ROOT / 'sample-mecanum-car-study/build/fallback-remote/fallback-remote.elf'
CHANNELS = (
    dict(id=0,name='大臂',pin='PB6',address=0x40000834,minimum=1470,maximum=1530,delta=20),
    dict(id=1,name='小臂',pin='PB7',address=0x40000838,minimum=1450,maximum=1550,delta=50),
    dict(id=2,name='门',pin='PB8',address=0x4000083C,minimum=1450,maximum=1550,delta=50),
)


class PositionBridge:
    def __init__(self, start_worker=True):
        self.guard = threading.RLock()
        self.connected = False
        self.connect_pending = False
        self.connecting = False
        self.pending = {}
        self.steps = [[],[],[]]
        self.revision = [0,0,0]
        self.actual = [None]*3
        self.target = [None]*3
        self.owned = set()
        self.updated = 0
        self.last_client = 0
        self.request_id = -1
        self.owner = None
        self.error = ''
        self.note = '连接不回中；每路独立控制。'
        self.notes = ['尚未读取指令。']*3
        self.events = [collections.deque(maxlen=6) for _ in CHANNELS]
        self.alive = True
        self.worker = None
        if start_worker:
            self.worker = threading.Thread(target=self.loop, daemon=True)
            self.worker.start()

    def cli(self, *args):
        r = subprocess.run([CLI,'-c','port=SWD',
            'sn=37FF71064E57343655231443','mode=HOTPLUG','freq=1000',*map(str,args)],
            capture_output=True,text=True,errors='replace',timeout=3,
            creationflags=subprocess.CREATE_NO_WINDOW)
        if r.returncode or 'Error:' in r.stdout:
            raise RuntimeError((r.stdout+r.stderr)[-650:])
        return r.stdout

    def read(self, addr, size):
        output = self.cli('-r8',hex(addr),size)
        data = bytes.fromhex(' '.join(re.findall(
            r'^0x[0-9a-fA-F]+\s*:\s*([0-9a-fA-F ]+)',output,re.M)))
        if len(data)!=size:
            raise RuntimeError('ST-Link 读取不完整')
        return data

    def hold_state(self):
        return struct.unpack('<2I4h',self.read(self.mailbox,16))

    def check_hold(self):
        command,flags,*outputs = self.hold_state()
        if command!=0xDEAD0000 or flags&512 or any(outputs):
            raise RuntimeError('调试暂停状态已改变，未继续发送动作')

    def open(self):
        path = ROOT.parent/'work/fallback-20261019/main-a-stlink/bench-stop.lock'
        path.parent.mkdir(parents=True,exist_ok=True)
        self.owner = path.open('a+b')
        self.owner.seek(0)
        msvcrt.locking(self.owner.fileno(),msvcrt.LK_NBLCK,1)
        symbols = subprocess.check_output([NM,str(ELF)],text=True,
                                         creationflags=subprocess.CREATE_NO_WINDOW)
        def symbol(name):
            match = re.search(r'^([0-9a-fA-F]+)\s+\w\s+'+name+r'$',symbols,re.M)
            if not match:
                raise RuntimeError('未找到 A 固件符号')
            return int(match[1],16)
        self.mailbox = symbol('fallback_stlink_test')
        address = symbol('fb_bench_poll')&~1
        offset = address-0x08000000
        signature = ELF.with_suffix('.bin').read_bytes()[offset:offset+32]
        if len(signature)!=32 or self.read(address,32)!=signature:
            raise RuntimeError('连接板不是当前 A 固件，未发送动作')
        regs = struct.unpack('<16I',self.read(0x40000800,64))
        if (regs[10]!=71 or regs[11]!=19999 or not regs[0]&1 or
            regs[6]&0x7878!=0x6868 or regs[7]&0x78!=0x68 or regs[8]&0x111!=0x111):
            raise RuntimeError('PB6/PB7/PB8 定时器配置不符合预期')
        command,flags,*outputs = self.hold_state()
        if command not in (0,0xDEAD0000) or flags&512 or any(outputs):
            raise RuntimeError('底盘正在输出或被其他调试程序占用，未接管')
        if command==0:
            # Explicit Connect pauses normal input; no servo reference is set.
            self.cli('-w32',hex(self.mailbox),'0xDEAD0000')
        self.check_hold()
        pulses = list(struct.unpack('<3I',self.read(0x40000834,12)))
        with self.guard:
            self.actual = pulses
            self.target = pulses.copy()
            self.owned.clear()
            self.connected = True
            self.updated = self.last_client = time.monotonic()
            self.error = ''
            self.note = '三路已连接，手柄暂停；未发送定位指令。'
            self.notes = ['已读取，尚未操作此路。']*3

    def write_pulse(self, channel, pulse):
        config = CHANNELS[channel]
        if type(pulse) is not int or (pulse!=0 and not config['minimum']<=pulse<=config['maximum']):
            raise ValueError('指令超出该路试验范围')
        if pulse:
            self.check_hold()
        # Track ownership before issuing the write: failure can be ambiguous.
        self.owned.add(channel)
        self.cli('-w32',hex(config['address']),hex(pulse))
        actual = struct.unpack('<I',self.read(config['address'],4))[0]
        if actual!=pulse:
            raise RuntimeError(config['pin']+' 指令读回不一致')
        with self.guard:
            self.target[channel] = self.actual[channel] = actual
            self.events[channel].append(dict(time=time.strftime('%H:%M:%S'),pulse=actual))
            if pulse==0:
                self.owned.discard(channel)
        # Other channels' freshness is not renewed by a single-channel write.

    def cancel(self, channel, action):
        self.revision[channel] += 1
        self.steps[channel] = []
        self.pending[channel] = (action,None,self.revision[channel])

    def submit(self, data):
        action = data.get('action')
        number = data.get('id')
        if type(number) is not int:
            raise ValueError('缺少请求序号')
        with self.guard:
            if action=='keepalive':
                self.last_client = time.monotonic()
                return
            if action not in ('connect','reference','pulse','test','hold','off','off_all','off_owned','stop'):
                raise ValueError('未知操作')
            channel = data.get('channel')
            if action in ('reference','pulse','test','hold','off') and (type(channel) is not int or not 0<=channel<3):
                raise ValueError('请选择明确的舵机通道 0/1/2')
            if number<=self.request_id and action not in ('off','off_all','off_owned','stop','hold'):
                return
            self.request_id = max(number,self.request_id)
            self.last_client = time.monotonic()
            if action=='connect':
                if not self.connected and not self.connecting:
                    self.connect_pending = True
                return
            if not self.connected:
                raise ValueError('请先连接 ST-Link')
            if action in ('off_all','stop','off_owned'):
                # Cancel all queued starts too; a queued reference may not yet
                # appear in owned. Only actual owned channels need cutoff.
                for ch in range(3):
                    self.revision[ch] += 1
                    self.steps[ch] = []
                self.pending.clear()
                targets = range(3) if action!='off_owned' else list(self.owned)
                for ch in targets:
                    self.pending[ch] = ('off',None,self.revision[ch])
                return
            if action in ('off','hold'):
                self.cancel(channel,action)
                return
            config = CHANNELS[channel]
            if self.steps[channel] or channel in self.pending:
                raise ValueError('该路动作处理中，请等待或取消')
            pulse = data.get('pulse')
            if action=='pulse':
                if type(pulse) is not int or not config['minimum']<=pulse<=config['maximum']:
                    raise ValueError('超出该路小范围试验限制')
                if not self.actual[channel]:
                    raise ValueError('该路尚无参考；先明确建立位置指令')
                if abs(pulse-self.actual[channel])>10:
                    raise ValueError('每次最多变化 10μs')
            if action=='test' and self.actual[channel] and not config['minimum']<=self.actual[channel]<=config['maximum']:
                raise ValueError('当前指令在试验范围外，不能直接往返')
            self.revision[channel] += 1
            self.pending[channel] = (action,pulse,self.revision[channel])

    def readback(self):
        self.check_hold()
        pulses = list(struct.unpack('<3I',self.read(0x40000834,12)))
        if any(pulses[ch]!=self.actual[ch] for ch in self.owned):
            raise RuntimeError('已接管的舵机输出被其他来源修改')
        with self.guard:
            self.actual = pulses
            self.updated = time.monotonic()

    def tick(self):
        with self.guard:
            now = time.monotonic()
            if self.connect_pending:
                self.connect_pending = False
                self.connecting = True
                action = ('connect',None,None,None)
            else:
                if self.connected and now-self.last_client>3:
                    for ch in range(3):
                        if self.steps[ch] or (ch in self.pending and self.pending[ch][0]!='off'):
                            self.revision[ch] += 1
                        self.steps[ch] = []
                    self.pending = {ch:('off',None,self.revision[ch]) for ch in self.owned}
                action = None
                if self.pending:
                    # Any cutoff/cancel outranks pending motion on other channels.
                    ch = min(self.pending,key=lambda c:(self.pending[c][0] not in ('off','hold'),c))
                    kind,pulse,revision = self.pending.pop(ch)
                    action = (kind,ch,pulse,revision)
                else:
                    due = [(seq[0][0],ch) for ch,seq in enumerate(self.steps) if seq and seq[0][0]<=now]
                    if due:
                        _,ch = min(due)
                        _,pulse,revision = self.steps[ch].pop(0)
                        action = ('step',ch,pulse,revision)
            if action and action[0] not in ('connect','hold','off'):
                # Include an in-flight start in off_owned, even before the SWD
                # write finishes; page exit must not miss this channel.
                self.owned.add(action[1])
        if action:
            kind,ch,pulse,revision = action
            if ch is not None and self.revision[ch]!=revision:
                return
            if kind=='connect':
                try:self.open()
                finally:self.connecting=False
            elif kind=='hold':
                self.notes[ch] = '已取消后续动作；保持最后指令。'
            else:
                if kind=='off':pulse=0
                if kind=='reference':pulse=1500
                if kind=='test':pulse=self.actual[ch] or 1500
                self.write_pulse(ch,pulse)
                with self.guard:
                    if self.revision[ch]!=revision:
                        # A cutoff/cancel arrived during SWD. Its queued action
                        # remains authoritative; never resurrect a test sequence.
                        return
                    if kind=='test':
                        cfg=CHANNELS[ch]
                        other=pulse+cfg['delta'] if pulse+cfg['delta']<=cfg['maximum'] else pulse-cfg['delta']
                        now=time.monotonic()
                        self.steps[ch]=[(now+.65,other,revision),(now+1.3,pulse,revision),(now+1.95,0,revision)]
                        self.notes[ch]='短测中：变化 → 返回参考 → 关闭信号。'
                    elif pulse==0:
                        self.notes[ch]='已读回 0：此路信号关闭。'
                    elif kind=='reference':
                        self.notes[ch]='已建立 1500μs 指令参考，不是实测角度。'
                    elif kind=='pulse':
                        self.notes[ch]='已更新；松手保持，不自动回中。'
            if self.connected and time.monotonic()-self.updated>.6:
                self.readback()
        elif self.connected and now-self.updated>.6:
            self.readback()

    def status(self):
        with self.guard:
            age=round(time.monotonic()-self.updated,2) if self.updated else None
            channels=[]
            for cfg in CHANNELS:
                ch=cfg['id']
                channels.append(dict(id=ch,name=cfg['name'],pin=cfg['pin'],minimum=cfg['minimum'],
                    maximum=cfg['maximum'],delta=cfg['delta'],actual=self.actual[ch],target=self.target[ch],
                    pending=self.pending[ch][0] if ch in self.pending else None,testing=bool(self.steps[ch]),
                    note=self.notes[ch],events=list(self.events[ch]),age=age))
            return dict(schemaVersion=2,connected=self.connected,pending='connect' if self.connecting or self.connect_pending else None,
                        error=self.error,note=self.note,channels=channels)

    def handle_error(self, exc):
        failed=[]
        if self.connected:
            for ch in list(self.owned):
                try:self.write_pulse(ch,0)
                except Exception:failed.append(CHANNELS[ch]['pin'])
        with self.guard:
            self.error=str(exc)[-650:]
            if failed:self.error+='；未确认关闭：'+', '.join(failed)+'。若仍转动，请断开舵机电源。'
            self.connected=self.connect_pending=self.connecting=False
            self.pending.clear()
            self.steps=[[],[],[]]
            self.actual=self.target=[None]*3
            self.updated=0
            self.note='连接中断，旧读数不代表当前输出。'
        if self.owner:
            self.owner.close()
            self.owner=None

    def loop(self):
        while self.alive:
            try:self.tick()
            except Exception as exc:self.handle_error(exc)
            time.sleep(.05)

    def close(self):
        with self.guard:
            for ch in range(3):self.revision[ch]+=1
            self.steps=[[],[],[]]
            self.pending={ch:('off',None,self.revision[ch]) for ch in self.owned}
        deadline=time.monotonic()+12
        while self.connected and (self.owned or self.pending) and time.monotonic()<deadline:
            time.sleep(.05)
        self.alive=False
        if self.worker:self.worker.join(timeout=7)
        if self.owner:self.owner.close()
