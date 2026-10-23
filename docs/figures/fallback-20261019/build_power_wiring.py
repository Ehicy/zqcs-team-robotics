"""All power terminals, including every TB6612 ground and each servo supply."""
from pathlib import Path
from html import escape
import re
ROOT=Path(__file__).resolve().parents[3]
out=[]
def add(s):out.append(s)
def txt(x,y,s,cls='',anchor='start'):add(f'<text x="{x}" y="{y}" class="{cls}" text-anchor="{anchor}">{escape(s)}</text>')
def box(x,y,w,h,title,sub='',fill='#f6f8fb'):
    add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="{fill}" stroke="#35465b" stroke-width="2.5"/>')
    txt(x+w/2,y+32,title,'hd','middle')
    if sub:txt(x+w/2,y+57,sub,'sm','middle')
def wire(p,c='#be6538'):
    p=' '.join(f'{x},{y}' for x,y in p)
    add(f'<polyline points="{p}" stroke="white" stroke-width="9" fill="none"/><polyline points="{p}" stroke="{c}" stroke-width="3" fill="none"/>')
def pin(x,y,s,left=True):
    add(f'<circle cx="{x}" cy="{y}" r="6" fill="white" stroke="#596a7c" stroke-width="2.5"/>')
    txt(x+15 if left else x-15,y+8,s,'','start' if left else 'end')
def ground(x,y):add(f'<path d="M{x} {y}v12m-16 0h32m-11 7h22m-15 7h8" stroke="#596a7c" stroke-width="3" fill="none"/>')
def gndport(x,y,s,left=True):
    end=x-40 if left else x+40;wire([(x,y),(end,y),(end,y+15)],'#596a7c');pin(x,y,s,left);ground(end,y+15)
def dot(x,y,c):add(f'<circle cx="{x}" cy="{y}" r="5" fill="{c}"/>')

add('<svg class="full-power" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 2100 1240" role="img" aria-label="完整电源端子逐线图">')
add('<style>.full-power text{font:24px Microsoft YaHei,system-ui,sans-serif;fill:#243248}.full-power .hd{font-size:27px;font-weight:700}.full-power .sm{font-size:19px;fill:#526278}.full-power .net{font-size:25px;font-weight:700}</style><rect width="2100" height="1240" fill="white"/>')
txt(25,32,'所有接地符号都接同一个公共负极；端子按功能排列，不代表未知模块的实物孔序。','sm')
box(30,60,210,160,'2S 电池','标称 7.4V / 满电 8.4V')
box(340,60,220,160,'保险 / 总开关','动力正极经过触点')
box(680,60,260,170,'升压器','输出调到 12V','#fff3df')
box(1240,60,260,170,'降压器','输入 12V → 输出 5V','#eaf6ee')
wire([(240,115),(340,115)]);pin(240,115,'＋',False);pin(340,115,'IN＋')
wire([(560,115),(680,115)]);pin(560,115,'OUT＋',False);pin(680,115,'VIN＋')
gndport(240,180,'−',False);gndport(680,180,'VIN− / GND')
wire([(940,115),(1030,115),(1240,115)]);pin(940,115,'VOUT＋',False);pin(1240,115,'IN＋')
gndport(940,180,'VOUT−',False);gndport(1240,180,'IN− / GND')
wire([(1500,115),(1660,115),(1660,1070)],'#c24e47');pin(1500,115,'OUT＋',False);gndport(1500,180,'OUT−',False)
txt(1675,255,'5V 母线','net')
wire([(1030,115),(1030,790)]);dot(1030,115,'#be6538');txt(1045,285,'12V 母线','net')
box(1240,320,260,200,'B · 底盘 STM32','3V3 只供两驱动逻辑','#edf5fc')
wire([(1660,420),(1500,420)],'#c24e47');dot(1660,420,'#c24e47');pin(1500,420,'5V',False)
gndport(1500,480,'GND',False)
wire([(1240,420),(1110,420),(1110,840)],'#b59727');pin(1240,420,'3V3 输出');txt(1125,560,'3.3V 母线','net')
for title,y in [('TB6612 ①',420),('TB6612 ②',740)]:
    box(680,y,260,220,title,'每个 GND 端子单独画出','#fff8e2')
    wire([(1030,y+50),(940,y+50)]);dot(1030,y+50,'#be6538');pin(940,y+50,'VM',False)
    wire([(1110,y+100),(940,y+100)],'#b59727');dot(1110,y+100,'#b59727');pin(940,y+100,'VCC',False)
    wire([(620,y+60),(620,y+180),(680,y+180)],'#596a7c')
    for off,num in [(60,1),(120,2),(180,3)]:
        wire([(620,y+off),(680,y+off)],'#596a7c');pin(680,y+off,f'GND {num}');dot(620,y+off,'#596a7c')
    wire([(620,y+180),(620,y+200)],'#596a7c');ground(620,y+200)
for y,title,sub in [(290,'A · 手柄 STM32','PS2 / 三路舵机'),(480,'PS2 接收转接板','仅原带稳压转接板接 5V')]:
    box(1780,y,270,160,title,sub,'#edf5fc')
    wire([(1660,y+75),(1780,y+75)],'#c24e47');dot(1660,y+75,'#c24e47');pin(1780,y+75,'5V')
    gndport(1780,y+130,'GND')
for y,title,signal in [(680,'舵机1 · DS3128','PB6'),(850,'舵机2 · MG995','PB7'),(1020,'舵机3 · MG995','PB8')]:
    box(1780,y,270,135,title,'S 信号 → A '+signal,'#f5effa')
    wire([(1660,y+50),(1780,y+50)],'#c24e47');dot(1660,y+50,'#c24e47');pin(1780,y+50,'红线 ＋5V')
    gndport(1780,y+100,'黑 / 棕 GND')
txt(30,1080,'TB6612：VM 是电机电源；VCC 是逻辑电源，不能把 12V 接入 VCC。','net')
txt(30,1125,'电源负极用粗线接驱动电源侧 GND；主控地线不承担电机、舵机的大电流。','net')
txt(30,1170,'三个 GND 通常板内相通，断电两两测通。舵机三针完整定义：S / ＋5V / GND。','sm')
txt(30,1210,'交叉无圆点＝不相连；有实心圆点＝汇接。若另用无线继电器，其供电/触点端子须按实物型号另行核对。','sm')
add('</svg>');svg='\n'.join(out)
(Path(__file__).parent/'power-terminals.svg').write_text(svg,encoding='utf-8')
page=ROOT/'firmware/web/fallback-wiring.html';html=page.read_text(encoding='utf-8')
section='<section class="sheet" id="power-sheet"><div class="sheet-title"><h2>② 电源接口全图 · 升压、降压、三路舵机和每个 GND 端子</h2></div><div class="diagram">'+svg+'</div></section>'
if 'id="power-sheet"' in html:html=re.sub(r'<section class="sheet" id="power-sheet">.*?</section>',lambda _:section,html,flags=re.S)
else:html=html.replace('<div class="cols">',section+'\n<div class="cols">')
page.write_text(html,encoding='utf-8');print('Power terminals updated')
