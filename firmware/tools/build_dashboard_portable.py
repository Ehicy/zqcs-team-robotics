"""Build a portable Windows dashboard; only the selected car identity is baked in."""
from pathlib import Path
import argparse,re,shutil,zipfile
p=argparse.ArgumentParser();p.add_argument('--output',required=True);a=p.parse_args()
repo=Path(__file__).resolve().parents[2];web=repo/'firmware/web'
identity=re.search(r"CAR_NAME\s*=\s*['\"](CRTC-Lower-Car-[0-9A-F]{12})['\"]",(web/'car-identity.js').read_text(encoding='utf-8'))
if not identity:raise SystemExit('Read IDENTITY from flashed C3 and set car-identity.js first.')
name=identity[1];out=Path(a.output).resolve()
# Never erase or overwrite a pre-existing release folder.
out.mkdir(parents=True,exist_ok=False)
shutil.copytree(web,out/'web')
for f in (repo/'firmware/tools/portable').iterdir():
 if f.is_file():shutil.copy2(f,out/f.name)
(out/'web/portable-id.txt').write_text('CRTC-Dashboard-portable-20261024-'+name,encoding='utf-8')
(out/'使用说明.txt').write_text(f'''CRTC Dashboard 便携版 · {name}

1. 在 Windows 10/11 解压整个 ZIP，打开电脑蓝牙。
2. 双击 Start-Dashboard.cmd，Edge 自动打开 http://127.0.0.1:8080/dashboard.html 。保留服务窗口。
3. 点击“连接蓝牙”，首次手动选择 {name} 并连接。
无需 Python、Node、管理员权限或开发环境；使用时无需联网。需要支持蓝牙的电脑及 Edge/Chrome。

换电脑前，在旧电脑点“暂停重连 / 交给另一台电脑”，关闭旧 Dashboard，避免争抢连接。
只自动重连本车已授权设备；首次浏览器授权必须在新电脑上完成。
连接后读取 C3 Flash 的轮参数、PID、舵机范围，不会把网页初值自动写回硬件。
这只是网页交付，不烧录固件；打开网页、恢复默认参数均不执行动作。

默认前进/后退 19.1%，转向独立为 21.8%，两滑块均可调到 100%。
F / 功能1：大臂232度（0秒），小臂150度（0.25秒）。
G / 功能2：大臂115度、小臂86度（均0秒）。
H / 功能3：仅小臂147度（0秒）。
每个时间均相对本次按键时刻；不同功能键保持2秒间隔。
新浏览器自动使用以上默认动作；已有自定义动作保留。
需要更新已有配置时，点“恢复本套 F/G/H 默认参数（只保存）”。
原有路线与自定义动作可在旧电脑导出JSON，在新电脑导入JSON。

当前车的 C3 power-switch-v14 固件要求底盘停稳后操作舵机；
同时行驶与操作舵机需要支持并行能力的 C3 固件，单独更新网页不能解除固件互斥。

请固定使用 127.0.0.1:8080；不同地址或端口的蓝牙授权、浏览器记录互相独立。
本地服务器仅监听127.0.0.1，不对局域网开放。关闭服务窗口后网页不能刷新。
端口8080被其他服务占用时会提示退出，不会停止其他程序。
''',encoding='utf-8-sig')
archive=out.with_suffix('.zip')
if archive.exists():raise SystemExit('ZIP already exists; choose a new output name.')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for f in out.rglob('*'):
  if f.is_file():z.write(f,f.relative_to(out.parent))
print(archive)
