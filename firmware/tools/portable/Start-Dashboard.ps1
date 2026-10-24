# Local-only static server. Windows PowerShell 5.1 / .NET, no install or admin.
param([ValidateRange(1024,65535)][int]$Port=8080,[switch]$NoOpen)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'web'))
if(-not(Test-Path -LiteralPath (Join-Path $root 'dashboard.html'))){throw 'Extract the entire ZIP first. Missing web/dashboard.html.'}
$identity=[IO.File]::ReadAllText((Join-Path $root 'portable-id.txt')).Trim()
$url="http://127.0.0.1:$Port/dashboard.html?v=portable-20261024"
function Open-Dashboard {
 if($NoOpen){return}
 $edge=@("${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe","$env:ProgramFiles\Microsoft\Edge\Application\msedge.exe","$env:LOCALAPPDATA\Microsoft\Edge\Application\msedge.exe") | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
 if($edge){Start-Process -FilePath $edge -ArgumentList $url}
 else {Write-Host "Open this address in Edge or Chrome: $url"}
}
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,$Port)
try{$listener.Start(32)}catch{
 try{$existing=(Invoke-WebRequest -UseBasicParsing -Uri "http://127.0.0.1:$Port/portable-id.txt" -TimeoutSec 2).Content.Trim()}catch{$existing=''}
 if($existing -eq $identity){Write-Host 'This Dashboard server is already running.';Open-Dashboard;exit 0}
 Write-Host "Port $Port is occupied by another service. Close that service or ask for help; no process was stopped."
 exit 1
}
Write-Host "Dashboard: $url"
Write-Host "Car: $identity"
Write-Host 'Keep this window open. Close it to stop the server. Bluetooth stays in Edge.'
Write-Host 'On the old PC, disconnect/pause auto-reconnect before using this PC.'
Open-Dashboard
$mime=@{'.html'='text/html; charset=utf-8';'.js'='text/javascript; charset=utf-8';'.css'='text/css; charset=utf-8';'.svg'='image/svg+xml';'.png'='image/png';'.jpg'='image/jpeg';'.json'='application/json; charset=utf-8';'.txt'='text/plain; charset=utf-8';'.ico'='image/x-icon';'.woff2'='font/woff2'}
try{
 while($true){
  $client=$listener.AcceptTcpClient();$stream=$null;$reader=$null
  try{
   $stream=$client.GetStream();$stream.ReadTimeout=2000;$stream.WriteTimeout=5000
   $reader=[IO.StreamReader]::new($stream,[Text.Encoding]::ASCII,$false,1024,$true)
   $line=$reader.ReadLine();if(!$line){continue}
   $parts=$line.Split(' ');$hostHeader='';$size=$line.Length
   do{$header=$reader.ReadLine();if($null -eq $header){throw 'Incomplete request'};$size+=$header.Length;if($size -gt 8192){throw 'Headers too large'};if($header -match '^Host:\s*(.+)$'){$hostHeader=$Matches[1].Trim()}}while($header.Length -gt 0)
   $status='200 OK';$type='text/plain; charset=utf-8';$body=[byte[]]@();$file=$null
   if($parts.Length -ne 3 -or $parts[0] -notin @('GET','HEAD')){$status='405 Method Not Allowed'}
   elseif($hostHeader -notin @("127.0.0.1:$Port","localhost:$Port")){$status='403 Forbidden'}
   else{
    try{
     $requestPath=[Uri]::UnescapeDataString(($parts[1] -split '\?',2)[0])
     if(!$requestPath.StartsWith('/') -or $requestPath.Contains('\') -or $requestPath.Contains(':') -or $requestPath.Contains([char]0) -or ($requestPath.Split('/') -contains '..')){throw 'Invalid path'}
     if($requestPath -eq '/'){$requestPath='/dashboard.html'}
     $file=[IO.Path]::GetFullPath((Join-Path $root $requestPath.TrimStart('/')))
     if(!$file.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Outside web root'}
     $extension=[IO.Path]::GetExtension($file).ToLowerInvariant()
     if(!$mime.ContainsKey($extension) -or !(Test-Path -LiteralPath $file -PathType Leaf)){$status='404 Not Found'}
     else{$type=$mime[$extension];$body=[IO.File]::ReadAllBytes($file)}
    }catch{$status='403 Forbidden'}
   }
   if($status -ne '200 OK'){$body=[Text.Encoding]::UTF8.GetBytes($status)}
   $head="HTTP/1.1 $status`r`nContent-Type: $type`r`nContent-Length: $($body.Length)`r`nCache-Control: no-store`r`nX-Content-Type-Options: nosniff`r`nConnection: close`r`n`r`n"
   $bytes=[Text.Encoding]::ASCII.GetBytes($head);$stream.Write($bytes,0,$bytes.Length)
   if($parts[0] -ne 'HEAD'){$stream.Write($body,0,$body.Length)}
  }catch{Write-Verbose $_}finally{if($reader){$reader.Dispose()};if($stream){$stream.Dispose()};$client.Close()}
 }
}finally{$listener.Stop()}
