param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build/power-traces'
[void][IO.Directory]::CreateDirectory($build)
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vcvars=@(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find 'VC/Auxiliary/Build/vcvars64.bat') | Select-Object -First 1
if(-not $vcvars -or -not (Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 tools missing'}
foreach($path in @($root,$vcvars)){if($path -match '["&|<>%^!\r\n]'){throw 'Unsupported shell path characters'}}
$lines=@('@echo off',('call "'+$vcvars+'" >nul'),'if errorlevel 1 exit /b 1',('cd /d "'+$root+'"'))
foreach($name in @('read_msi_selectors','snapshot_msi_display','read_msi_etl')){
 $lines+=('cl /nologo /std:c++17 /EHsc /O2 /W4 /WX tools\'+$name+'.cpp /Febuild\power-traces\'+$name+'.exe /Fobuild\power-traces\'+$name+'.obj')
 $lines+='if errorlevel 1 exit /b 1'
}
$lines+='exit /b 0'
$command=Join-Path $build 'build-windows-tools.cmd'
Set-Content -LiteralPath $command -Value $lines -Encoding ascii
& $env:ComSpec /d /c $command
if($LASTEXITCODE -ne 0){throw 'Windows reader build failed'}
# Compilation only: no service installation, GPU access or policy changes.
