"""Update the user's original wiring editor, retaining its UI and renderer."""
from pathlib import Path
from copy import deepcopy
import json,re,shutil
from html import escape
ROOT=Path(__file__).resolve().parents[3]
source=Path(__file__).with_name('original-editor-template.html')
html=source.read_text(encoding='utf-8')
old=json.loads(re.search(r'<script[^>]*id="seed-data"[^>]*>(.*?)</script>',html,re.S)[1])
models={m['id']:deepcopy(m) for m in old['modules']}
modules=[]; wires=[]
def p(id,label,side,offset,role='signal',voltage=None,unused=False):
    d=dict(id=id,label=label,side=side,offset=offset,role=role)
    if voltage is not None:d['voltage']=voltage
    if unused:d['unused']=True
    return d
def module(id,title,type,x,y,w,h,pins,note=''):
    d=dict(id=id,title=title,type=type,x=x,y=y,w=w,h=h,pins=pins,note=note);modules.append(d);return d
def keep(id,**kw):
    d=models[id];d.update(kw);modules.append(d);return d
def wire(a,b,kind,group='power',label='',optional=False):
    wires.append(dict(id='f'+str(len(wires)+1).zfill(3),**{'from':a,'to':b},kind=kind,group=group,label=label,verified=False,optional=optional))
grounds=[]
def ground(a,name):
    key='G'+str(len(grounds)+1);grounds.append((key,name));wire(a,'ground.'+key,'ground',label=name+' → 公共地')

keep('battery',caption='2S · 标称7.4V / 满电8.4V')
keep('fuse');keep('relay');keep('boost');keep('buck')
wire('battery.POS','fuse.IN','battery');wire('fuse.OUT','relay.COM','battery');wire('fuse.OUT','relay.VPLUS','battery')
wire('relay.NO','boost.IN_POS','battery');wire('relay.NO','buck.IN_POS','battery',label='7.4V分路给降压输入')
for a,name in [('battery.NEG','电池−'),('relay.VMINUS','继电器−'),('boost.IN_NEG','升压IN−'),('boost.OUT_NEG','升压OUT−'),('buck.IN_NEG','降压IN−'),('buck.OUT_NEG','降压OUT−')]:ground(a,name)

bp=[]
pinsets=[['PA0','PA4','PA5'],['PA1','PA6','PA7'],['PA2','PA8','PA9'],['PA3','PA10','PA15']]
for i,ps in enumerate(pinsets):
    for j,pin in enumerate(ps):bp.append(p(pin,pin+' · M'+str(i+1)+' '+['PWM','IN1','IN2'][j],'left',110+150*i+42*j,'pwm' if j==0 else 'signal'))
bp.append(p('PB15','PB15 · 两板STBY','left',750))
bp += [p('PB10','PB10 · UART3 TX','right',110),p('PB11','PB11 · UART3 RX','right',170)]
unused=['PA11','PA12','PB0','PB1','PB3','PB4','PB5','PB6','PB7','PB8','PB9','PB12','PB13','PB14','PC13','PC14','PC15']
bp += [p(q,q+' · 未用','right',245+42*i,unused=True) for i,q in enumerate(unused)]
bp += [p('V5','5V 输入','top',80,'power',5),p('V3','3V3 输出','top',260,'power',3.3),p('GND','GND','top',440,'ground',0),
       p('PA13','PA13 · SWDIO','bottom',80),p('PA14','PA14 · SWCLK','bottom',240),p('SWDGND','SWD GND','bottom',400,'ground',0),
       p('USB','USB · 本版不用下载','bottom',500,'usb',unused=True)]
bp += [p(q,q+' · 未接','left',845+40*i,unused=True) for i,q in enumerate(['NRST','BOOT0','BOOT1','VBAT'])]
bp += [p('GND2','GND · 板内相通','right',945,'ground',0,True),p('GND3','GND · 板内相通','right',985,'ground',0,True),p('V3_2','3V3 · 板内相通','right',1025,'power',3.3,True)]
module('stm','B · STM32 底盘（已直烧）','controller',1200,650,540,1050,bp,'ST-Link直烧APP 0x08000000，无USB bootloader。PA0–7与PA8/9/10/15在实物不同排；此图功能分组，不猜孔序。未用口灰色；BOOT0/BOOT1保持跳帽0，从Flash启动。')

ap=[p('PA2','PA2 · UART2 TX','left',110),p('PA3','PA3 · UART2 RX','left',170),p('V5','5V 输入','left',280,'power',5),p('V3','3V3 不并接B','left',345,'power',3.3),p('GND','GND','left',420,'ground',0)]
for pin,label,offset in [('PB14','PB14 · DAT',80),('PB15','PB15 · CMD',220),('PB12','PB12 · CS',360),('PB13','PB13 · CLK',500)]:ap.append(p(pin,label,'top',offset))
for pin,label,offset in [('PB6','PB6 · 大臂S',100),('PB7','PB7 · 小臂S',300),('PB8','PB8 · 门S',500)]:ap.append(p(pin,label,'bottom',offset))
ua=['PA0','PA1','PA4','PA5','PA6','PA7','PA8','PA9','PA10','PA11','PA12','PA15','PB0','PB1','PB3','PB4','PB5','PB9','PB10','PB11','PC13','PC14','PC15']
ap += [p(q,q+' · 未用','right',100+32*i,unused=True) for i,q in enumerate(ua)]
ap += [p('GND2','GND · 板内相通','right',850,'ground',0,True),p('GND3','GND · 板内相通','right',890,'ground',0,True),p('V3_2','3V3 · 板内相通','right',930,'power',3.3,True),p('USB','USB · 本版不用下载','right',970,'usb',unused=True)]
ap += [p(q,q+' · 未接','left',600+40*i,unused=True) for i,q in enumerate(['NRST','BOOT0','BOOT1','VBAT'])]
ap += [p('PA13','PA13 · SWDIO','left',830),p('PA14','PA14 · SWCLK','left',880)]
module('remote','A · STM32 手柄 / 三舵机','controller',1840,1000,600,1000,ap,'替代损坏ESP32，已ST-Link直烧并校验。PS2+三路舵机，无BLE/Dashboard；上电不输出舵机PWM。两块板3V3不互并；BOOT0/BOOT1保持跳帽0，从Flash启动。')
rx=keep('ps2',title='新 PS2 转接板 · 背面视角',x=850,y=60,w=700,h=350,note='2026-10-19新照片：右侧7针从上往下ACK/CLK/CS/DO/DI/VDD/GND。DI按本板丝印对应DATA→A PB14；DO对应CMD←A PB15。ACK与NC不接。轮趣说明书已确认DI/DAT与DO/CMD方向、ACK不使用、接收器允许3.3–5V；本车继续取A板3V3。接收器上电、手柄ON自动配对，接收器灯常亮表示配对。当前A/B串口已连通，PS2仍未读到有效帧，不能按配对灯代替通信验收。')
rx['pins']=[p(id,label,'right',80+35*i,role,voltage,unused) for i,(id,label,role,voltage,unused) in enumerate([
    ('ACK','ACK · 不接','signal',None,True),('CLK','CLK → PB13','signal',3.3,False),('CS','CS → PB12','signal',3.3,False),('CMD','DO / CMD → PB15','signal',3.3,False),('DAT','DI / DATA → PB14','signal',3.3,False),('VDD','VDD · 接3V3','power',3.3,False),('GND','GND','ground',0,False)])]
for i,label in enumerate(['ACK','NC','CLK','CS','VDD','GND','NC','DO','DI']):
    pin=p('SOCKET'+str(i+1),label,'left',80+28*i,'ground' if label=='GND' else ('power' if label=='VDD' else 'signal'),None,True)
    pin['label']='插座 '+label
    rx['pins'].append(pin)
for a,b in [('DAT','PB14'),('CMD','PB15'),('CS','PB12'),('CLK','PB13')]:wire('ps2.'+a,'remote.'+b,'ps2','ps2')
wire('remote.V3_2','ps2.VDD','power3',label='新转接板VDD接3.3V（厂家范围内）');ground('ps2.GND','PS2')
wire('remote.PA2','stm.PB11','uart','uart','A TX → B RX');wire('stm.PB10','remote.PA3','uart','uart','B TX → A RX')
for id in ['stm','remote']:
    wire('buck.OUT_POS',id+'.V5','power5');ground(id+'.GND','底盘地' if id=='stm' else '手柄板地')
for pin in ap:
    if pin['id']=='PC13':pin.update(label='PC13 · 用户灯（不外接）',unused=False)
for id in ['tb1','tb2']:
    tb=keep(id,note='16个接口完整保留，含3个GND。元件面针序沿用原图，控制口请核对背面丝印；照片目前清楚露出VM/VCC/GND。')
    wire('boost.OUT_POS',id+'.VM','power12');wire('stm.V3',id+'.VCC','power3')
    for j in [1,2,3]:ground(id+'.GND'+str(j),id.upper()+' 地'+str(j))
    wire('stm.PB15',id+'.STBY','stby','control')
    for side,idx in [('A',0 if id=='tb1' else 2),('B',1 if id=='tb1' else 3)]:
        for cpu,dev in zip(pinsets[idx],['PWM'+side,side+'IN1',side+'IN2']):wire('stm.'+cpu,id+'.'+dev,'pwm' if dev.startswith('PWM') else 'direction','control')
for n,xy in [(1,(2050,70)),(2,(50,120)),(3,(1250,2300)),(4,(50,2300))]:
    m=keep('m'+str(n),x=xy[0],y=xy[1]);m['note']='只接两根电机动力线；编码器4根线断开、逐根绝缘。六针孔序沿用历史资料，未重新实物核验。'
    for pin in m['pins']:
        if pin['id'] in ['p2','p3','p4','p5']:pin['label']+=' · 不接';pin['unused']=True
    driver='tb1' if n<=2 else 'tb2';chan='A' if n%2 else 'B'
    wire(driver+'.'+chan+'O1',m['id']+'.p6','motor','M'+str(n));wire(driver+'.'+chan+'O2',m['id']+'.p1','motor','M'+str(n))
for n,(x,y),title,pin in [(1,(1840,2080),'DS3128 大臂','PB6'),(2,(2240,2080),'MG995 小臂','PB7'),(3,(1840,2420),'MG995 开门','PB8')]:
    id='servo'+str(n)
    module(id,title,'reserved',x,y,320,240,[p('S','S 信号','top',160),p('VPLUS','红线 +5V','left',110,'power',5),p('GND','黑/棕 GND','left',180,'ground',0)],'完整三针：S / 5V / GND。供电从分电端独立走线，不经过主控。')
    wire('remote.'+pin,id+'.S','reserved','reserved');wire('buck.OUT_POS',id+'.VPLUS','power5');ground(id+'.GND','舵机'+str(n))
keep('pulldown');wire('stm.PB15','pulldown.A','stby','control');ground('pulldown.B','10k下拉')
keep('stlink',note='ST-Link仅连接当前一块板，不把两块SWD并联。图中当前接底盘B；换接A时仍按PA13/PA14/GND。不并接供电。')
wire('stlink.SWDIO','stm.PA13','usb','tools',optional=True);wire('stlink.SWCLK','stm.PA14','usb','tools',optional=True);wire('stlink.GND','stm.SWDGND','ground','tools',optional=True)
gp=[]
for i,(key,label) in enumerate(grounds):gp.append(p(key,label,'left' if i<12 else 'right',130+45*(i%12),'ground',0))
module('ground','公共 GND · 同一地网','ground',50,1350,350,720,gp,'所有端子同一公共负极，不是多个独立地。电机/舵机粗回流线直接回分电端。')
# Photograph-aligned headers: every row is read top-to-bottom in the supplied photos.
# A USB points down; B USB points up. Keep the already-flashed signal assignment.
A_LEFT=['VBAT','PC13','PC14','PC15','PA0','PA1','PA2','PA3','PA4','PA5','PA6','PA7','PB0','PB1','PB10','PB11','NRST','V3','GND','GND2']
A_RIGHT=['V3_2','GND3','V5','PB9','PB8','PB7','PB6','PB5','PB4','PB3','PA15','PA12','PA11','PA10','PA9','PA8','PB15','PB14','PB13','PB12']
B_LEFT=['PB12','PB13','PB14','PB15','PA8','PA9','PA10','PA11','PA12','PA15','PB3','PB4','PB5','PB6','PB7','PB8','PB9','V5','GND','V3']
B_RIGHT=['GND2','GND3','V3_2','NRST','PB11','PB10','PB1','PB0','PA7','PA6','PA5','PA4','PA3','PA2','PA1','PA0','PC15','PC14','PC13','VBAT']
A_USE={'PA2':'UART TX→B','PA3':'UART RX←B','PB12':'PS2 CS','PB13':'PS2 CLK','PB14':'PS2 DAT','PB15':'PS2 CMD','PB6':'大臂S','PB7':'小臂S','PB8':'开门S','PC13':'板载灯'}
B_USE={'PB10':'UART TX→A','PB11':'UART RX←A','PB15':'两板STBY'}
for n,ps in enumerate(pinsets,1):
    for q,kind in zip(ps,['PWM','IN1','IN2']):B_USE[q]='M'+str(n)+' '+kind

def physical_header(mid,left,right,uses,usb_up):
    m=next(m for m in modules if m['id']==mid)
    pp=[]
    for side,seq in [('left',left),('right',right)]:
        for row,q in enumerate(seq,1):
            role='signal';voltage=None
            if q.startswith('GND'):label='GND';role='ground';voltage=0
            elif q.startswith('V3'):label='3V3';role='power';voltage=3.3
            elif q=='V5':label='5V 输入';role='power';voltage=5
            elif q=='VBAT':label='VB · 后备电池';role='power'
            elif q=='NRST':label='R · 复位'
            else:label=q+' · '+uses.get(q,'未用')
            if uses.get(q,'').endswith('PWM'):role='pwm'
            used=q in uses or q in ['V5','GND'] or (mid=='stm' and q=='V3') or (mid=='remote' and q=='V3_2')
            pin=p(q,label,side,130+(row-1)*48,role,voltage,not used)
            pin.update(physicalRow=row,photoVerified=True)
            pp.append(pin)
    swd_side='bottom' if usb_up else 'top'
    pp += [p('SWDV3','SWD 3V3 不接电源',swd_side,100,'power',3.3,True),p('PA13','PA13 · SWDIO',swd_side,260),p('PA14','PA14 · SWCLK',swd_side,420),p('SWDGND','SWD GND',swd_side,580,'ground',0)]
    pp += [p('USB','USB 朝'+('上' if usb_up else '下'),'top' if usb_up else 'bottom',340,'usb',unused=True)]
    m.update(pins=pp,w=680,h=1160,usbOrientation='top' if usb_up else 'bottom')
    m['note']='按2026-10-19照片：两条长排针各20口，左右均从上往下读。USB朝'+('上' if usb_up else '下')+'；BOOT0/BOOT1跳帽均保持0。SWD四针功能已列出，横排孔序按实物丝印认，不依图猜左右。'+('本板固件已ST-Link直烧；接口分配不变，不需重烧。' if mid=='stm' else '主控A已烧录并校验；上电不输出舵机PWM。')
    return m
physical_header('remote',A_LEFT,A_RIGHT,A_USE,False)
physical_header('stm',B_LEFT,B_RIGHT,B_USE,True)
layout={'remote':(900,470),'stm':(900,1850),'ps2':(850,60),'tb1':(2050,1900),'tb2':(2050,2540),'boost':(2050,580),'buck':(2050,1050),'battery':(100,480),'fuse':(100,830),'relay':(100,1160),'m1':(2570,1900),'m2':(100,2360),'m3':(2570,2540),'m4':(100,3000),'servo1':(2570,300),'servo2':(2570,750),'servo3':(2570,1200),'pulldown':(1730,2450),'stlink':(900,3270)}
for m in modules:
    if m['id'] in layout:m['x'],m['y']=layout[m['id']]
    if m['id']=='remote':m['title']='A 主控 · 上板 / USB朝下'
    if m['id']=='stm':m['title']='B 底盘 · 下板 / USB朝上'
    if m['id']=='ps2':m.update(w=700,h=350)
    if m['id'] in ['tb1','tb2']:
        m['note']='右侧两块TB6612，正面元件朝上、VM在左上。照片可确认左1=VM、左2=VCC、左8/右8=GND；其余孔位名称沿用原图，带?的需翻背面丝印核对。VCC接底盘3V3；VM接12V。每板只画左下GND回升压OUT−，另两个GND口保留，不重复拉线。'
        for pin in m['pins']:
            verified=pin['id'] in ['VM','VCC','GND2','GND3']
            pin['photoVerified']=verified
            if not verified:pin['label']+=' ?'
    if m['id']=='buck':
        for pin in m['pins']:
            if pin['id']=='IN_POS':pin['voltage']=7.4;pin['label']='IN＋7.4V'
    if m['id'] in ['boost','buck']:
        m['note']='普通非隔离模块，断电通断档核对IN−与OUT−内部导通。图示端子按功能排列，正负按板上丝印认。'+('IN+接继电器NO，IN−接电池−；OUT+12V只给两块驱动VM，OUT−接驱动GND。' if m['id']=='boost' else 'IN直接接7.4V电池经总开关/继电器后的分路，与升压器并联输入；OUT调5V给两块主控、接收板和舵机。OUT−作低压负极分线点；大电流舵机回线不经过主控。')
# There is no separate ground distribution board: show real negative-terminal wiring.
modules=[m for m in modules if m['id']!='ground']
wires=[w for w in wires if not (w['from'].startswith('ground.') or w['to'].startswith('ground.'))]
for i,w in enumerate(wires,1):w['id']='f'+str(i).zfill(3)
for a,b,label in [('boost.IN_NEG','battery.NEG','升压输入回电池负极'),('relay.VMINUS','battery.NEG','继电器供电负极'),('buck.IN_NEG','battery.NEG','降压输入回电池负极')]:wire(a,b,'ground','power',label)
for driver in ['tb1','tb2']:
    wire(driver+'.GND2','boost.OUT_NEG','ground','power','每板一根可靠粗负极线，回升压OUT−')
for mid in ['stm','remote','ps2','servo1','servo2','servo3']:wire(mid+'.GND','buck.OUT_NEG','ground','power','低压设备回降压输出负极')
wire('pulldown.B','stm.GND','ground','control','10k下拉回底盘地')
# Internal continuity is a module property, not another external wire.
for mid in ['boost','buck']:
    next(m for m in modules if m['id']==mid)['internalGroundCommon']=True
photo_rows=[]
for title,left,right,uses in [('上板 A 主控（USB朝下）',A_LEFT,A_RIGHT,A_USE),('下板 B 底盘（USB朝上）',B_LEFT,B_RIGHT,B_USE)]:
    def label(q):return {'V3':'3V3','V3_2':'3V3','V5':'5V','GND':'GND','GND2':'GND','GND3':'GND','NRST':'R / NRST','VBAT':'VB / VBAT'}.get(q,q)+(' · '+uses[q] if q in uses else '')
    rows=''.join('<tr><td>'+str(i+1)+'</td><td>'+label(l)+'</td><td>'+label(r)+'</td></tr>' for i,(l,r) in enumerate(zip(left,right)))
    photo_rows.append('<h3>'+title+'</h3><table class="photo-pin-table"><thead><tr><th>从上到下</th><th>照片左排</th><th>照片右排</th></tr></thead><tbody>'+rows+'</tbody></table>')
photo_guide='<section class="section"><details id="photo-pin-guide"><summary>按照片逐排查接口 · 两板各40口</summary><p>方向固定为本次照片；PA写作A、PB写作B、PC写作C。针脚不因板子旋转而改名。</p>'+''.join(photo_rows)+'<p>TB6612 左排从上往下：VM、VCC、GND?、AO1?、AO2?、BO2?、BO1?、GND；右排：PWMA?、AIN2?、AIN1?、STBY?、BIN1?、BIN2?、PWMB?、GND。?沿用原图，照片未拍到背面丝印，不作为实物孔序确认。</p><p>底盘PA0–PA7在右排，PA8/9/10/15在左排；12根电机控制线无法全挤在右排。右排8根就近走中间通道，左排4根从板子上方或下方绕过去，不从板底夹线。维持已烧录引脚，无需重烧底盘。</p></details></section>'
data=dict(version=1,title='双STM32应急接线 · 原版接线台',board=dict(width=3000,height=3650),modules=modules,wires=wires,
          notes=['保留原HTML样式/编辑器/逐接口选择；编码器、PID、ESP32、Dashboard本版不用。','所有在用和未用接口均保留，未用灰色；针序标注与功能图不能替代实物丝印。'],
          warnings=['本次B已ST-Link直烧并确认STBY低，A已烧录；新接线与实车动作待验证。'],revision='fallback-pa-direct',buildId='dual-f103-photo-new-ps2-20261019-v10')
html=re.sub(r'(<script[^>]*id="seed-data"[^>]*>).*?(</script>)',lambda m:m[1]+json.dumps(data,ensure_ascii=False).replace('<','\\u003c')+m[2],html,count=1,flags=re.S)
html=html.replace('data-plan-id="base"','data-plan-id="dual-f103-photo-direct"')
html=html.replace('ESP32 管遥控 / UART / 舵机 · STM32 管四轮底盘 · 2S 电池 + 继电器 + 升降压','A STM32 管 PS2 / 三舵机 · B STM32 管四轮开环 · 无编码器/PID/BLE/Dashboard')
html=html.replace('2S 电池（满电 8.4V）· 升压 12V 给电机 · 降压 5V 给主控；本次新增供电接法尚未通电验收。','7.4V 电池分两路：升压12V给驱动VM；并联另一路降压5V给其余设备。B已ST-Link直烧，A已烧录；不用USB bootloader。')
html=html.replace("['encoder','编码器'],",'').replace("motor:'编码器电机'","motor:'电机（编码器暂停）'")
html=html.replace('霍尔 · 6线','编码器4线不接').replace('编码器／串口','两板串口')
html=html.replace("(selected?.type==='pin'&&selected.id===key?' selected':'')","(selected?.type==='pin'&&selected.id===key?' selected':'')+(p.unused?' unused':'')")
html=html.replace('</style>','.pin.unused{opacity:.42}.pin.unused .pin-label{font-size:19px}\n</style>',1)
html=re.sub(r'<section class="section" id="relay-help">.*?</section>','<section class="section"><details><summary>供电与当前状态</summary><p>7.4V电池经保险/总开关/继电器后分两路：①升压到12V→TB6612 VM；②直接降压到5V→两块主控、三路舵机；新PS2转接板VDD取A板3V3，符合轮趣手册的3.3–5V供电范围。两路输入并联，不串联。两块TB逻辑VCC取B板3V3；两主控3V3不互并。所有GND同网。ST-Link一次只接一块板，不用USB bootloader。</p><p>图中保留各模块全部接口；灰色为本版不接。TB针序沿用原图，需按背面丝印核对，不根据照片未露出的孔位猜线。</p></details></section>',html,flags=re.S)
html=html.replace('已改用本页2S升压方案','已改用本页双STM32应急方案')
html=html.replace('功能布局图：TB6612 保留元件面针序；主控与电机接口按功能分组，不代表实物排针方向。','功能布局图：TB6612 沿用原图针序，背面丝印待核；主控与电机接口按功能分组，不代表实物孔序。')
html=re.sub(r'<footer class="footer">.*?</footer>','<footer class="footer">2026-10-19 · 保留原版接线台样式 · 双STM32 / PA侧电机控制 / ST-Link直烧 · A已烧录，接线与实车动作待验证。</footer>',html,flags=re.S)
html=html.replace('<section class="section"><details><summary>供电与当前状态</summary>',photo_guide+'<section class="section"><details><summary>供电与当前状态</summary>',1)
html=html.replace('所有GND同网。','没有额外公共地板：驱动GND回升压OUT−；低压设备GND回降压OUT−；升压IN−和降压IN−分别接电池负极。普通非隔离模块的IN−/OUT−内部相通，断电先量通断。')
html=html.replace('图中保留各模块全部接口；灰色为本版不接。','按照片：上板A主控USB朝下，下板B底盘USB朝上，TB6612位于底盘右侧。图中保留各模块全部接口；灰色为本版不接。')
html=html.replace('功能布局图：TB6612 沿用原图针序，背面丝印待核；主控与电机接口按功能分组，不代表实物孔序。','两块STM32长排针按照片逐排定位；SWD横排按实物丝印认。TB6612带?接口沿用原图、待核背面。VCC=3.3V，VM=12V；没有额外公共地模块。')
html=html.replace('主控与电机接口按功能分组，不代表实物孔序。','STM32长排针按照片排列；电机六针仍需核对实物线序。')
html=html.replace('</style>','.photo-pin-table{border-collapse:collapse;width:100%;font-size:12px;margin:8px 0 16px}.photo-pin-table td,.photo-pin-table th{border-bottom:1px solid var(--border);padding:5px;text-align:left}.photo-pin-table th{color:var(--accent)}#photo-pin-guide h3{margin-top:12px}\n</style>',1)
html=html.replace("if(m.type==='controller'){", "if(m.type==='controller'){const uy=m.usbOrientation==='top'?84:m.h-95;svg('rect',{x:cx-45,y:uy,width:90,height:48,rx:7,fill:'#aebdb7',stroke:'#e0e9e4','stroke-width':3},g);text(g,cx,uy+32,'USB',{fill:'#20332f','font-size':22,'text-anchor':'middle'});text(g,120,103,'左排 ↓',{fill:'#dbe9e1','font-size':22,'text-anchor':'middle'});text(g,m.w-120,103,'右排 ↓',{fill:'#dbe9e1','font-size':22,'text-anchor':'middle'});")
# Dashed line inside each converter explains the shared return without inventing a module.
html=html.replace("function drawArt(g,m){", "function drawArt(g,m){if(m.internalGroundCommon){const a=m.pins.find(p=>p.id==='IN_NEG'),b=m.pins.find(p=>p.id==='OUT_NEG');svg('path',{d:`M 20 ${a.offset} H ${m.w-20}`,stroke:'#c6d7cb','stroke-width':3,'stroke-dasharray':'8 6',fill:'none'},g);text(g,m.w/2,a.offset-12,'− 内部相通 · 断电核对',{fill:'#e0e8d7','font-size':17,'text-anchor':'middle'});}")
# Keep the detailed operator guide beside the wiring, sourced from the maintained doc.
guide=(ROOT/'docs/14-dual-stm32-fallback.md').read_text(encoding='utf-8')
guide=guide[guide.index('## 手柄操作速查'):guide.index('## 曲线起步和反转急停的实际含义')]
def inline(t):
    t=escape(t)
    t=t.replace('[手柄操作演示](../firmware/web/fallback-controller-guide.html)', '<a href="fallback-controller-guide.html">手柄操作演示</a>')
    t=re.sub(r'`([^`]+)`',r'<code>\1</code>',t)
    return re.sub(r'\*\*([^*]+)\*\*',r'<strong>\1</strong>',t)
parts=[]; table=False; header=False
for line in guide.splitlines():
    if line.startswith('|'):
        cells=[c.strip() for c in line.strip('|').split('|')]
        if all(re.fullmatch(r':?-+:?',c) for c in cells):continue
        if not table:parts.append('<table class="photo-pin-table">');table=True;header=True
        tag='th' if header else 'td'
        parts.append('<tr>'+''.join('<'+tag+'>'+inline(c)+'</'+tag+'>' for c in cells)+'</tr>');header=False
    else:
        if table:parts.append('</table>');table=False
        if line.startswith('## '):parts.append('<h3>'+inline(line[3:])+'</h3>')
        elif line.strip():parts.append('<p>'+inline(line)+'</p>')
if table:parts.append('</table>')
control_guide='<section class="section"><details id="controller-guide"><summary>手柄操作与组合键调参指南 · 13项参数</summary><p><a href="fallback-controller-guide.html">打开大图手柄演示：高亮组合键、逐步讲解、播放灯光</a></p><p><strong>代码已实现并通过主机测试；上板A已烧录L3单闪版并校验，下板B已再次烧录并校验；尚未实车验收。</strong></p>'+''.join(parts)+'</details></section>'
html=html.replace(photo_guide,control_guide+photo_guide,1)
html=html.replace('</h1>','</h1><p><a href="fallback-controller-guide.html">手柄按键演示（单独打开）</a> · <a href="#controller-guide" id="show-controller-guide">组合键速查 ↓</a></p>',1)
# The shortcut opens the real details element; no hardware command is involved.
html=html.replace("const seed=", "const seed=",1)
html=html.replace('</body>','<script>(()=>{const g=document.getElementById("controller-guide");function show(){g.open=true;g.scrollIntoView({block:"start"});}document.getElementById("show-controller-guide").addEventListener("click",show);if(location.hash==="#controller-guide")show();})();</script></body>')
folder=Path(__file__).parent
(folder/'plan-original-style.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
(ROOT/'firmware/web/fallback-wiring.html').write_text(html,encoding='utf-8')
template=(folder/'controller-guide-template.html').read_text(encoding='utf-8')
wiring=ROOT/'firmware/web/fallback-wiring.html'
page=template.replace('%%OPERATOR_GUIDE%%',''.join(parts)).replace('%%WIRING_FILE%%',wiring.name)
(wiring.parent/'fallback-controller-guide.html').write_text(page,encoding='utf-8')
print('Original editor retained:',len(modules),'modules',len(wires),'wires')
