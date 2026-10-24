"""Exercise the actual Windows PowerShell launcher on loopback, without UI/BLE."""
import subprocess,urllib.request,urllib.error,socket,time,sys
from pathlib import Path
root=Path(sys.argv[1]).resolve();script=root/'Start-Dashboard.ps1';port=18080
args=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(script),'-Port',str(port),'-NoOpen']
p=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
def raw(path='/',method='GET',host=None):
 with socket.create_connection(('127.0.0.1',port),2) as s:
  s.sendall(f'{method} {path} HTTP/1.1\r\nHost: {host or f"127.0.0.1:{port}"}\r\n\r\n'.encode())
  data=b''
  while part:=s.recv(65536):data+=part
  return data
try:
 for _ in range(50):
  if p.poll() is not None:raise AssertionError(p.stdout.read().decode(errors='replace'))
  try:socket.create_connection(('127.0.0.1',port),.1).close();break
  except OSError:time.sleep(.1)
 for file,mime in [('dashboard.html',b'text/html'),('dashboard.js',b'text/javascript'),('car-identity.js',b'text/javascript'),('assets/esp32c3-front-20261022.jpg',b'image/jpeg')]:
  data=raw('/'+file);header,body=data.split(b'\r\n\r\n',1)
  assert b'200 OK' in header,(file,data[:200]);assert mime in header;assert b'Cache-Control: no-store' in header
  assert body==(root/'web'/file).read_bytes()
 for path in ['/../Start-Dashboard.ps1','/%2e%2e/Start-Dashboard.ps1','/%2e%2e%5cStart-Dashboard.ps1','/C:/Windows/win.ini']:
  assert b'403 Forbidden' in raw(path).split(b'\r\n',1)[0],path
 assert b'403 Forbidden' in raw(host='evil.example').split(b'\r\n',1)[0]
 assert b'405 Method Not Allowed' in raw(method='POST').split(b'\r\n',1)[0]
 assert b'404 Not Found' in raw('/not-a-file').split(b'\r\n',1)[0]
 assert raw('/dashboard.js',method='HEAD').endswith(b'\r\n\r\n')
 # Same identity: reuse the existing server rather than stealing its port.
 duplicate=subprocess.run(args,timeout=10,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW)
 assert duplicate.returncode==0,duplicate.stdout
 print('PASS PowerShell 5.1: HTML/JS/JPEG bytes, MIME, no-store, HEAD, traversal/host/method denial, same-port reuse')
finally:
 p.terminate();p.wait(timeout=5)
# Different listener: do not kill or replace it, and report conflict.
import threading
ready=threading.Event()
def other():
 with socket.socket() as s:
  s.bind(('127.0.0.1',port));s.listen();ready.set()
  c,_=s.accept()
  with c:c.recv(4096);c.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nother')
t=threading.Thread(target=other,daemon=True);t.start();ready.wait(3)
conflict=subprocess.run(args,timeout=10,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW);t.join(2)
assert conflict.returncode==1,conflict.stdout
print('PASS unrelated-port conflict exits without stopping another process')
