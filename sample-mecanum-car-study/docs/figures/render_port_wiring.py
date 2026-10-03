"""Exact functional-port wiring drawings; positions are not physical pin order."""
from pathlib import Path
from html import escape
from PIL import Image, ImageDraw, ImageFont
from math import hypot
import json

OUT=Path(__file__).resolve().parent
BG='#f7f9fc'; INK='#182338'; BLUE='#245cbd'; TEAL='#087b7c'; RED='#bb3540'; GOLD='#a47313'; PURPLE='#7552a5'; GND='#596374'
font_cache={}
def font(n,b=False):
    key=(n,b)
    if key not in font_cache:
        font_cache[key]=ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc' if b else 'C:/Windows/Fonts/msyh.ttc',n)
    return font_cache[key]

class Drawing:
    def __init__(self,w,h):
        self.w=w;self.h=h;self.im=Image.new('RGB',(w,h),BG);self.d=ImageDraw.Draw(self.im);self.ports={};self.links=[]
        self.svg=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',f'<rect width="{w}" height="{h}" fill="{BG}"/>']
    def text(self,x,y,s,n=24,c=INK,b=False,anchor='start'):
        self.d.text((x,y),s,font=font(n,b),fill=c,anchor={'start':'ls','end':'rs','middle':'ms'}[anchor])
        self.svg.append(f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-family="Microsoft YaHei,Noto Sans CJK SC,sans-serif" font-size="{n}" font-weight="{700 if b else 400}" fill="{c}">{escape(s)}</text>')
    def rect(self,x,y,w,h,c='#ffffff',edge='#ccd7e6',r=12):
        self.d.rounded_rectangle((x,y,x+w,y+h),r,c,edge,2)
        self.svg.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" fill="{c}" stroke="{edge}" stroke-width="2"/>')
    def line(self,pts,c=BLUE,width=3,dashed=False):
        if dashed:
            for (x1,y1),(x2,y2) in zip(pts,pts[1:]):
                length=hypot(x2-x1,y2-y1)
                for n in range(0,int(length),18):
                    e=min(n+10,length)
                    self.d.line((x1+(x2-x1)*n/length,y1+(y2-y1)*n/length,x1+(x2-x1)*e/length,y1+(y2-y1)*e/length),c,width)
        else:self.d.line(pts,c,width,joint='curve')
        dash=' stroke-dasharray="10 8"' if dashed else ''
        self.svg.append(f'<polyline points="{" ".join(f"{x},{y}" for x,y in pts)}" fill="none" stroke="{c}" stroke-width="{width}" stroke-linejoin="round"{dash}/>')
    def dot(self,x,y,c=BLUE,r=6,hollow=False):
        self.d.ellipse((x-r,y-r,x+r,y+r),fill=BG if hollow else c,outline=c,width=2)
        self.svg.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{BG if hollow else c}" stroke="{c}" stroke-width="2"/>')
    def port(self,key,x,y,label,side='left',c=BLUE,n=24):
        assert key not in self.ports,key
        self.ports[key]=(x,y)
        if side=='left':self.text(x+16,y+8,label,n,c)
        elif side=='right':self.text(x-16,y+8,label,n,c,anchor='end')
        elif side=='top':self.text(x,y+33,label,n,c,anchor='middle')
        else:self.text(x,y-17,label,n,c,anchor='middle')
        self.dot(x,y,c,6,True)
    def connect(self,a,b,via=(),c=BLUE,dashed=False):
        assert a in self.ports and b in self.ports,(a,b)
        self.line([self.ports[a],*via,self.ports[b]],c,3,dashed)
        self.links.append({'from':a,'to':b})
        for name in (a,b):self.dot(*self.ports[name],c,6,True)
    def net(self,key,x,y,label,name,side='right',c=GND,n=22):
        if side=='top':
            assert key not in self.ports,key
            self.ports[key]=(x,y)
            self.text(x+12,y-12,label,n,c)
            self.dot(x,y,c,6,True)
        else:self.port(key,x,y,label,side,c,n)
        if side=='right':
            self.line([(x,y),(x+38,y)],c);self.text(x+46,y+7,name,n,c)
        elif side=='left':
            self.line([(x,y),(x-38,y)],c);self.text(x-46,y+7,name,n,c,anchor='end')
        elif side=='bottom':
            self.line([(x,y),(x,y+35)],c);self.text(x,y+63,name,n,c,anchor='middle')
        else:
            self.line([(x,y),(x,y-35)],c);self.text(x,y-48,name,n,c,anchor='middle')
        self.dot(x,y,c,6,True)
    def save(self,name):
        p=OUT/name
        p.with_suffix('.svg').write_text('\n'.join(self.svg+['</svg>']),encoding='utf-8')
        self.im.save(p.with_suffix('.png'),optimize=True)
        return {'ports':list(self.ports),'connections':self.links}

# SIGNALS: exactly one main controller and one chassis controller.
s=Drawing(2500,3400)
s.text(60,72,'信号接线图：每个接口逐一连接',46,b=True)
s.text(60,118,'矩形是模块；端口按功能排列，不能据此推断实物排针／PH2.0 六针顺序。',27,GND)
s.rect(60,180,550,330,'#f0f5ff')
s.text(88,224,'PS2 接收器／转接板',30,BLUE,True)
s.rect(990,180,480,725,'#f0f5ff')
s.text(1017,224,'A：主控 F103',33,BLUE,True)
s.text(1017,495,'PS2 → 四轮目标 → USART2',22,GND)
for pin,ps2,y in [('PB14','DAT',270),('PB15','CMD',330),('PB12','CS/ATT',390),('PB13','CLK',450)]:
    s.port('PS2.'+ps2,610,y,ps2,'right')
    s.port('A.'+pin,990,y,pin+' · PS2','left')
    s.connect('PS2.'+ps2,'A.'+pin)
s.net('PS2.VCC',170,510,'VCC','5V_CTRL','bottom',GOLD)
s.net('PS2.GND',410,510,'GND','GND','bottom',GND)

s.text(1740,159,'可选／预留四路舵机：S 信号来自 A',26,BLUE,True)
for i,y in enumerate([270,445,620,795],1):
    pin='PB'+str(5+i)
    s.port('A.'+pin,1470,y,pin+' · TIM4_CH'+str(i),'right')
    s.rect(1740,y-66,430,142,'#f6f2fb')
    s.text(1764,y-26,'舵机 '+str(i)+'（可选）',27,PURPLE,True)
    s.port('SERVO'+str(i)+'.S',1740,y,'S','left')
    s.connect('A.'+pin,'SERVO'+str(i)+'.S')
    s.net('SERVO'+str(i)+'.V+',2170,y-18,'V+','5V_SERVO','right',GOLD)
    s.net('SERVO'+str(i)+'.GND',2170,y+42,'GND','GND','right',GND)
s.text(1017,540,'后续 I2C2：PB10 / PB11',23,GND)
s.text(1017,574,'PB6–PB9 已给四舵机 PWM',22,GND)
s.port('A.PB10',990,600,'PB10 · I2C2_SCL','left')
s.port('A.PB11',990,650,'PB11 · I2C2_SDA','left')
s.line([(990,600),(900,600)],TEAL,dashed=True);s.text(884,608,'传感器 SCL（预留）',22,TEAL,anchor='end')
s.line([(990,650),(900,650)],TEAL,dashed=True);s.text(884,658,'传感器 SDA（预留）',22,TEAL,anchor='end')
s.port('A.PA2',990,715,'PA2 · USART2_TX','left')
s.port('A.PA3',990,770,'PA3 · USART2_RX','left')
s.net('A.PA13',1130,905,'PA13','SWDIO_A','bottom',BLUE)
s.net('A.PA14',1340,905,'PA14','SWCLK_A','bottom',BLUE)
s.text(90,1020,'以下唯一大外框 B，是同一块底盘 F103；后轴／前轴仅为它的接口分区。',30,BLUE,True)

s.rect(70,1100,550,2160,'#edf4ff','#9db8e3')
s.text(95,1150,'B：唯一一块底盘 F103',31,BLUE,True)
s.port('B.PA3',70,1190,'PA3 · RX','left')
s.port('B.PA2',70,1250,'PA2 · TX','left')
s.connect('A.PA2','B.PA3',[(750,715),(750,1050),(25,1050),(25,1190)])
s.connect('B.PA2','A.PA3',[(45,1250),(45,1075),(805,1075),(805,770)])
s.text(105,1570,'后轴两个编码器反馈',25,TEAL,True)
s.text(105,2525,'前轴两个编码器反馈',25,TEAL,True)

axes=[(0,1,2,{'PWMA':'PB6','AIN1':'PB0','AIN2':'PB1','PWMB':'PB7','BIN1':'PB3','BIN2':'PB4'},[('PA0','PA1'),('PA6','PA7')]),
      (950,3,4,{'PWMA':'PB8','AIN1':'PB5','AIN2':'PB12','PWMB':'PB9','BIN1':'PB13','BIN2':'PB14'},[('PA8','PA9'),('PB10','PB11')])]
for offset,ml,mr,mapping,encs in axes:
    di=1 if offset==0 else 2;prefix='D'+str(di)
    s.rect(990,1100+offset,480,520,'#fff7e9','#dec08b')
    s.text(1016,1146+offset,'TB6612 '+str(di)+('：后轴' if di==1 else '：前轴'),29,GOLD,True)
    for j,(port,pin) in enumerate(mapping.items()):
        y=1195+offset+j*60
        s.port('B.'+pin,620,y,pin,'right')
        s.port(prefix+'.'+port,990,y,port,'left')
        s.connect('B.'+pin,prefix+'.'+port)
    sty=1555+offset
    s.port(prefix+'.STBY',990,sty,'STBY','left')
    if di==1:
        s.port('B.PB15',620,sty,'PB15 · 共控 STBY','right')
        s.connect('B.PB15',prefix+'.STBY')
        s.dot(850,sty)
    else:
        s.line([(850,1555),(850,sty),(990,sty)],BLUE)
        s.dot(850,1555);s.dot(990,sty,BLUE,6,True)
        s.links.append({'from':'B.PB15','to':prefix+'.STBY'})
        s.text(868,sty-15,'同一 PB15',20,BLUE)
    for x,name,net,c in [(1070,'VM','12V_MOTOR',RED),(1230,'VCC','3V3_B',PURPLE),(1400,'GND','GND',GND)]:
        s.net(prefix+'.'+name,x,1100+offset,name,net,'top',c,20)
    for name,y in [('AO1',1195),('AO2',1255),('BO1',1435),('BO2',1495)]:
        s.port(prefix+'.'+name,1470,y+offset,name,'right',INK)
    for mi,my,ab,yenc in [(ml,1100+offset,encs[0],(1675+offset,1735+offset)),(mr,1530+offset,encs[1],(1810+offset,1870+offset))]:
        mp='M'+str(mi)
        names={1:'左后',2:'右后',3:'左前',4:'右前'}
        s.rect(1750,my,420,385,'#edf8f7','#9dc6c3')
        s.text(1776,my+45,mp+' '+names[mi]+' · MG370',29,TEAL,True)
        for name,dy in [('M+',95),('M−',155),('A',215),('B',275)]:
            s.port(mp+'.'+name,1750,my+dy,name,'left',INK if name.startswith('M') else TEAL)
        s.net(mp+'.VCC',2170,my+215,'VCC','ENC_VCC*','right',PURPLE,22)
        s.net(mp+'.GND',2170,my+275,'GND','GND','right',GND,22)
        s.text(1776,my+349,'六功能端口，非六针实际顺序',21,GND)
        if mi==ml:
            s.connect(prefix+'.AO1',mp+'.M+',c=INK)
            s.connect(prefix+'.AO2',mp+'.M−',c=INK)
            for k,(sig,pin,by) in enumerate(zip(['A','B'],ab,yenc)):
                s.port('B.'+pin,620,by,pin+' · '+mp+' '+sig,'right',TEAL)
                lane=1650+k*55
                s.connect(mp+'.'+sig,'B.'+pin,[(lane,my+215+k*60),(lane,by)],TEAL)
        else:
            s.connect(prefix+'.BO1',mp+'.M+',[(1530,1435+offset),(1530,my+95)],INK)
            s.connect(prefix+'.BO2',mp+'.M−',[(1575,1495+offset),(1575,my+155)],INK)
            for k,(sig,pin,by) in enumerate(zip(['A','B'],ab,yenc)):
                s.port('B.'+pin,620,by,pin+' · '+mp+' '+sig,'right',TEAL)
                lane=1610+k*40
                s.connect(mp+'.'+sig,'B.'+pin,[(lane,my+215+k*60),(lane,by)],TEAL)
    if di==1:s.text(104,1957,'PB3 / PB4：关闭 JTAG，保留 SWD',23,RED,True)

s.text(95,2914,'硬件计数：M1/TIM2、M2/TIM3',23,TEAL)
s.text(95,2950,'M3/TIM1；M4/EXTI10 双边沿',23,TEAL)
s.text(95,2986,'前三路 4x；第四路 2x，分别换算',23,TEAL)
s.port('B.PA13',620,3075,'PA13 · SWDIO','right')
s.port('B.PA14',620,3135,'PA14 · SWCLK','right')
s.rect(990,3005,480,215,'#f0f5ff')
s.text(1016,3049,'ST-Link：每次接一块板',27,BLUE,True)
s.port('ST.SWDIO',990,3075,'SWDIO','left');s.port('ST.SWCLK',990,3135,'SWCLK','left')
s.connect('B.PA13','ST.SWDIO');s.connect('B.PA14','ST.SWCLK')
s.net('ST.GND',1340,3220,'GND','GND','bottom',GND)
s.text(1510,3050,'A 下载时：SWDIO→A.PA13',24,BLUE)
s.text(1510,3092,'SWCLK→A.PA14，GND共地',24,BLUE)
s.text(1510,3134,'3V3 / 5V 供电端子见供电图',23,GND)
s.text(90,3318,'* ENC_VCC 按霍尔版本核实；各模块同名电源网接在一起。线交叉无实心点＝不相连。',25,GND)
s.text(90,3364,'STBY可加10kΩ下拉；当前固件尚未改为新接口，不能按旧程序直接运行。',25,RED)
signal_manifest=s.save('dual-f103-signal-ports-20261003')

# POWER: same-name nets are exact electrical connections, keeping GND readable.
p=Drawing(2500,2780)
p.text(60,75,'供电接线图：正负极与每个电源接口',46,b=True)
p.text(60,123,'同名彩色网标＝接在同一条电源线上；GND全部共地。12V只接驱动VM。',27,GND)
p.rect(60,200,440,265)
p.text(87,247,'2S 18650 电池',31,b=True)
p.text(87,292,'7.4V 标称／8.4V满电',27)
p.text(87,339,'原两节保留；四节2S2P可选',23,GND)
p.port('BAT.+',500,382,'+（经保险丝／开关）','right',GOLD,23)
p.port('BAT.-',500,430,'−','right',GND)
p.rect(850,200,540,265,'#fff4f4','#ddb3b6')
p.text(876,247,'12V升压：选型待核',32,RED,True)
p.text(876,291,'单个30W MAX板不足四机',24,RED)
p.text(876,328,'四机额定工作点52.8W / 4.4A',24)
p.port('BOOST.IN+',850,382,'IN+','left',GOLD)
p.port('BOOST.IN-',850,430,'IN−','left',GND)
p.connect('BAT.+','BOOST.IN+',c=GOLD);p.connect('BAT.-','BOOST.IN-',c=GND)
p.net('BOOST.OUT+',1390,382,'OUT+','12V_MOTOR','right',RED)
p.net('BOOST.OUT-',1390,430,'OUT−','GND','right',GND)
p.rect(1800,200,520,265,'#fffbed','#dfcea1')
p.text(1826,247,'控制用5V降压',31,GOLD,True)
p.net('CTRL.IN+',1800,310,'IN+','BAT+','left',GOLD)
p.net('CTRL.IN-',1800,370,'IN−','GND','left',GND)
p.net('CTRL.OUT+',2320,310,'OUT+','5V_CTRL','right',GOLD)
p.net('CTRL.OUT-',2320,370,'OUT−','GND','right',GND)
p.text(538,351,'BAT+',23,GOLD)
p.text(533,457,'GND',23,GND)

for x,title,tag in [(250,'A主控 F103','A'),(1060,'B底盘 F103','B'),(1950,'PS2转接板','PS2')]:
    core_width=500 if tag=='A' else 540
    p.rect(x,620,core_width,260,'#f0f5ff')
    p.text(x+27,668,title,31,BLUE,True)
    p.net(tag+'.5V',x,725,'VCC' if tag=='PS2' else '5V输入','5V_CTRL','left',GOLD)
    p.net(tag+'.GND',x,800,'GND','GND','left',GND)
    if tag!='PS2':
        p.net(tag+'.3V3',x+core_width,725,'3V3输出',('3V3_B' if tag=='B' else '3V3_A'),'right',PURPLE)
    p.text(x+26,849,'两核心板3V3输出不互接' if tag!='PS2' else '现有宽输入转接板接5V',23,GND)

for x,di in [(250,1),(1320,2)]:
    p.rect(x,1060,710,310,'#fff7e9','#dec08b')
    p.text(x+30,1110,'TB6612 '+str(di)+('：后轴' if di==1 else '：前轴'),33,GOLD,True)
    p.net('D'+str(di)+'.VM',x,1185,'VM','12V_MOTOR','left',RED)
    p.net('D'+str(di)+'.VCC',x,1255,'VCC','3V3_B','left',PURPLE)
    p.net('D'+str(di)+'.GND',x,1325,'GND','GND','left',GND)
    p.text(x+30,1160,'控制端与AO/BO动力输出见信号图',24,GND)
    p.text(x+290,1262,'每个电机独立H桥',24)
    p.text(x+290,1323,'电流／散热仍需升级验证',23,RED)

for i,x in enumerate([60,645,1230,1815],1):
    p.rect(x,1520,540,245,'#edf8f7','#9dc6c3')
    p.text(x+24,1567,'M'+str(i)+' 霍尔编码器供电',28,TEAL,True)
    p.net('M'+str(i)+'.VCC',x+140,1765,'VCC','ENC_VCC*','bottom',PURPLE)
    p.net('M'+str(i)+'.GND',x+390,1765,'GND','GND','bottom',GND)
    p.text(x+24,1621,'A/B → 底盘B计数引脚',24,TEAL)
    p.text(x+24,1668,'M+/M− → 对应驱动AO/BO',24)
    p.text(x+24,1709,'不把12V接编码器VCC',23,RED)

p.rect(850,1890,700,450,'#f6f2fb')
p.text(876,1934,'可选舵机供电：另设合适5V支路',27,PURPLE,True)
p.net('SERVOPOWER.IN+',850,1992,'IN+','BAT+','left',GOLD)
p.net('SERVOPOWER.IN-',850,2048,'IN−','GND','left',GND)
p.net('SERVOPOWER.OUT+',1550,1992,'OUT+','5V_SERVO','right',GOLD)
p.net('SERVOPOWER.OUT-',1550,2048,'OUT−','GND','right',GND)
p.text(876,2102,'四舵机各自：V+→5V_SERVO',24,GOLD)
p.text(876,2146,'GND→GND；S→A.PB6/7/8/9',24,BLUE)
p.text(876,2192,'电源能力／电压按实际型号确认',24,GND)
p.text(876,2238,'舵机动力不经主控供电排针',24,RED)

p.rect(60,2440,2320,235,'#fff1da','#debd83')
p.text(86,2490,'* ENC_VCC：若厂家确认3.3V供电且B稳压器余量足够，接3V3_B；否则核实供电及A/B电平转换。',27,PURPLE,True)
p.text(86,2538,'新候选单升压30W MAX不足四机；两块各供一驱动板仍需验证26.4W/组持续、低电压、起步和散热。',26,RED)
p.text(86,2586,'双升压输出不得直接并联；原1.5A小板也不足。MG370堵转6.2A超过TB6612瞬时能力。',26,RED)
p.text(86,2634,'2S2P仅备选，电压仍7.4/8.4V；电池／电池盒能力待核实。动力接线不经面包板或核心板。',26,GND)
p.text(60,2720,'模块按功能端子连接，非实物排针顺序；所有GND，包括主控、驱动、电机编码器和舵机，必须共地。',26,GND)
power_manifest=p.save('dual-f103-power-ports-20261003')
OUT.joinpath('dual-f103-port-connections-20261003.json').write_text(json.dumps({'signals':signal_manifest,'power':power_manifest},ensure_ascii=False,indent=2),encoding='utf-8')
print('Generated signal and power PNG/SVG files.')
