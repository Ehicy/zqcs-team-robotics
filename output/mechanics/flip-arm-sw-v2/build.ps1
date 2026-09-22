$ErrorActionPreference = 'Stop'
$api = 'C:\Program Files\SOLIDWORKS Corp\SOLIDWORKS\api\redist\SolidWorks.Interop.sldworks.dll'
Add-Type -Path $api
Add-Type -ReferencedAssemblies $api -TypeDefinition (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build.cs') -Raw)
[FlipArmV2Builder]::Run($PSScriptRoot)
