"""Classic rectangular, port-to-port wiring diagram; no hardware access."""
from pathlib import Path
from html import escape
import re

ROOT = Path(__file__).resolve().parents[3]
parts = []
def put(s): parts.append(s)
def text(x,y,s,cls="",anchor="start"):
    put(f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}">{escape(s)}</text>')
def box(x,y,w,h,title,subtitle,fill="#f7f9fc"):
    put(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="{fill}" stroke="#35465b" stroke-width="2.5"/>')
    put(f'<path d="M{x} {y+65}H{x+w}" stroke="#9aa9bb"/>')
    text(x+w/2,y+33,title,"hd","middle"); text(x+w/2,y+56,subtitle,"sm","middle")
def pin(x,y,label,side="left",color="#426995"):
    put(f'<circle cx="{x}" cy="{y}" r="6" fill="white" stroke="{color}" stroke-width="2.5"/>')
    text(x+16 if side=="left" else x-16,y+8,label,"", "start" if side=="left" else "end")
def wire(points,color="#426995",dash=False):
    points=" ".join(f"{x},{y}" for x,y in points)
    put(f'<polyline points="{points}" fill="none" stroke="white" stroke-width="9"/>')
    put(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="3"'+(' stroke-dasharray="10 6"' if dash else '')+'/>')
def ground(x,y):
    put(f'<path d="M{x} {y}v12m-16 0h32m-11 7h22m-15 7h8" stroke="#57616e" stroke-width="3" fill="none"/>')

put('<svg class="rect-svg" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2000 1210" role="img" aria-labelledby="rect-title rect-desc">')
put('<title id="rect-title">双 STM32 常规矩形逐线接线图</title><desc id="rect-desc">左侧接收器和三舵机逐线连接手柄板，串口交叉连接底盘板；十二根电机控制线由底盘板 PA 逐根连接两块 TB6612，STBY共用PB15，电源在底部集中标注。</desc>')
put('<style>.rect-svg text{font-family:Microsoft YaHei,system-ui,sans-serif;font-size:24px;fill:#243248}.rect-svg .hd{font-size:26px;font-weight:700}.rect-svg .sm{font-size:19px;fill:#526278}.rect-svg .net{font-size:21px;font-weight:700}.rect-svg .port{font-size:23px;font-family:Consolas,monospace}</style>')
put('<rect width="2000" height="1210" fill="white"/>')
text(30,35,"按丝印接线；引脚在图上为走线排序，不代表实物排针孔序。", "sm")
box(30,70,250,320,"PS2 接收转接板","原带稳压转接板 · 5V")
box(400,70,260,800,"A · 手柄 STM32","PS2 + 三路舵机", "#edf5fc")
box(880,70,260,860,"B · 底盘 STM32","四轮开环 · ST-Link直烧", "#edf5fc")
box(1370,70,240,420,"TB6612 ①","M1 右前 + M2 左前", "#fff8e2")
box(1370,520,240,420,"TB6612 ②","M3 右后 + M4 左后", "#fff8e2")

# Draw the shared enable trunk before the point-to-point signal wires.
wire([(1140,460),(1240,460),(1370,460)],"#d79527")
wire([(1240,460),(1240,910),(1370,910)],"#d79527")
for y in (460,910): put(f'<circle cx="1240" cy="{y}" r="5" fill="#d79527"/>')
text(1255,555,"STBY", "net")
wire([(1240,910),(1240,965)],"#d79527")
put('<path d="M1240 965l-9 7 18 10-18 10 18 10-9 7v14" fill="none" stroke="#57616e" stroke-width="2.5"/>')
text(1260,993,"10kΩ 下拉", "sm"); ground(1240,1023)
text(1240,1080,"公共 GND", "sm","middle")

for y,label,pa in [(160,"CS / SEL","PB12"),(210,"CLK","PB13"),(260,"DAT / DI","PB14"),(310,"CMD / DO","PB15")]:
    wire([(280,y),(400,y)]); pin(280,y,label,"right"); pin(400,y,pa)
for i,y,model,p in [(1,465,"DS3128 大臂","PB6"),(2,595,"MG995 小臂","PB7"),(3,725,"MG995 开门","PB8")]:
    put(f'<rect x="30" y="{y-45}" width="250" height="90" fill="#f5effa" stroke="#35465b" stroke-width="2.5"/>')
    text(155,y-10,f"{i} · {model}","","middle"); text(155,y+28,"红 5V / 黑棕 GND","sm","middle")
    wire([(280,y),(400,y)],"#9565ab"); pin(280,y,"","right","#9565ab"); text(325,y-10,"S","sm"); pin(400,y,p,"left","#9565ab")
    for px,label in [(80,"5V"),(230,"GND")]:
        put(f'<circle cx="{px}" cy="{y+45}" r="5" fill="white" stroke="#687583" stroke-width="2"/><path d="M{px} {y+45}v12" stroke="#687583" stroke-width="2"/>')
        text(px,y+70,label,"sm","middle")
for y,ap,bp in [(160,"PA2 TX","PB11 RX"),(210,"PA3 RX","PB10 TX")]:
    wire([(660,y),(880,y)],"#327d79"); pin(660,y,ap,"right","#327d79"); pin(880,y,bp,"left","#327d79")
text(770,260,"TX 接对方 RX", "sm","middle"); text(770,290,"115200 · 3.3V", "sm","middle")
text(530,365,"STM32F103", "hd","middle"); text(530,398,"50Hz 舵机 PWM", "sm","middle")
text(1010,520,"STM32F103", "hd","middle"); text(1010,550,"TIM2 · 20kHz PWM", "sm","middle")
pin(1140,460,"PB15","right","#d79527"); pin(1370,460,"STBY","left","#d79527"); pin(1370,910,"STBY","left","#d79527")

groups=[(0,["PA0","PA4","PA5","PA1","PA6","PA7"],[160,210,260,310,360,410]),
        (1,["PA2","PA8","PA9","PA3","PA10","PA15"],[610,660,710,760,810,860])]
labels=["PWMA","AIN1","AIN2","PWMB","BIN1","BIN2"]
colors=["#b86539"]*3+["#426995"]*3
for _,pins,ys in groups:
    for p,label,y,c in zip(pins,labels,ys,colors):
        wire([(1140,y),(1370,y)],c); pin(1140,y,p,"right",c); pin(1370,y,label,"left",c)
for i,y,label,outs in [(1,190,"M1 右前",("AO1","AO2")),(2,350,"M2 左前",("BO1","BO2")),
                       (3,640,"M3 右后",("AO1","AO2")),(4,800,"M4 左后",("BO1","BO2"))]:
    box(1770,y-50,190,100,label,"只接两根动力线", "#f1f6f0")
    for dy,lab in zip((-15,15),outs):
        wire([(1610,y+dy),(1770,y+dy)],"#6f7854"); pin(1610,y+dy,lab,"right","#6f7854")
        put(f'<circle cx="1770" cy="{y+dy}" r="5" fill="white" stroke="#6f7854" stroke-width="2.5"/>')

for x,y,label in [(80,390,"5V"),(230,390,"GND"),(455,870,"5V"),(605,870,"GND"),
                  (930,930,"5V"),(1010,930,"3V3"),(1090,930,"GND")]:
    put(f'<circle cx="{x}" cy="{y}" r="5" fill="white" stroke="#687583" stroke-width="2"/><path d="M{x} {y}v18" stroke="#687583" stroke-width="2"/>')
    text(x,y-16 if y==390 else y+43,label,"net","middle")
for start in (420,870):
    for offset,label,net in [(0,"VCC","3V3"),(30,"VM","12V"),(60,"GND","公共 GND")]:
        y=start+offset; wire([(1610,y),(1650,y)],"#687583"); pin(1610,y,label,"right","#687583"); text(1665,y+8,net,"net")
text(30,835,"三路舵机供电直接接 5V 分电端", "net")
text(30,865,"不要让舵机电流穿过主控板。", "sm")
text(30,960,"保留：PA11/PA12（USB）及 PA13/PA14（SWD）", "sm")
text(30,996,"只用 ST-Link 下载；本版不使用 USB bootloader。", "sm")
text(30,1032,"交叉无圆点＝不连接；实心圆点＝汇接。", "sm")
put('<rect x="30" y="1100" width="1930" height="85" fill="#f1f4f7" stroke="#b7c2ce" stroke-width="2"/>')
text(50,1134,"电源：7.4V 电池 → 保险 / 总开关 / 继电器 → 升压 12V → 两块 TB6612 的 VM", "net")
text(50,1170,"12V → 降压 5V → A/B 主控、接收转接板、舵机分电；所有 GND 共地。TB6612 VCC 取 B 板 3V3。", "net")
put('</svg>')
svg="\n".join(parts)
(Path(__file__).parent/'rectangular-wiring.svg').write_text(svg,encoding='utf-8')
page=ROOT/'firmware/web/fallback-wiring.html'
html=page.read_text(encoding='utf-8')
html,n=re.subn(r'<svg\b.*?</svg>',lambda _:svg,html,count=1,flags=re.S)
assert n==1
html=html.replace('信号从左到右走，电源和公共地走下方','常规矩形接线图 · 接口逐根对应')
page.write_text(html,encoding='utf-8')
print(page)
