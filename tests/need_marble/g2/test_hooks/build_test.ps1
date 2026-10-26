# G-1(b) T2: builds the TEST map-server from a scratch source copy. Never builds inside the repository.
# usage: build_test.ps1 -Src <scratch>\mbqa_testsrc -Out <scratch>\mbqa_testbuild [-Real]
# -Real (V20-REAL, sources patched with apply_test_hooks.py --v20real): the map-server also gets
# NEED_MARBLE_CP_CHAR_WRITER=1 (through the CL environment variable: a second define in DefineConstants does not
# survive the MSBuild command line) and the TEST char-server is built too. Both are rebuilt from scratch.
param([Parameter(Mandatory)] [string]$Src, [Parameter(Mandatory)] [string]$Out, [switch]$Real)
$ErrorActionPreference = 'Stop'
if (Test-Path (Join-Path $Src '.git')) { throw "$Src is a git checkout - refusing" }
$MSB = 'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe'
New-Item -ItemType Directory -Force $Out, "$Out\logs" | Out-Null
$sd = (Resolve-Path $Src).Path.TrimEnd('\') + '\'
$libs = '3rdparty\rapidyaml\ryml.vcxproj', '3rdparty\libconfig\libconfig.vcxproj', '3rdparty\yaml-cpp\yaml-cpp.vcxproj', 'src\common\common.vcxproj'
foreach ($p in $libs) {
    $n = [IO.Path]::GetFileNameWithoutExtension($p)
    & $MSB (Join-Path $Src $p) /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$sd" /m /v:minimal /nologo | Out-File -Encoding utf8 "$Out\logs\$n.txt"
    "{0} exit={1}" -f $n, $LASTEXITCODE
    if ($LASTEXITCODE -ne 0) { exit 1 }
}
$od = (Resolve-Path $Out).Path.TrimEnd('\') + '\'
$target = 'Build'
if ($Real) { $env:CL = '/DNEED_MARBLE_CP_CHAR_WRITER=1'; $target = 'Rebuild' }
& $MSB (Join-Path $Src 'src\map\map-server.vcxproj') "/t:$target" /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$sd" `
    /p:DefineConstants=NEED_MARBLE_TEST_HOOKS "/p:OutDir=$od" "/p:IntDir=${od}obj\" /m /v:minimal /nologo | Out-File -Encoding utf8 "$Out\logs\map-server.txt"
$mapExit = $LASTEXITCODE
$env:CL = ''
"map-server(test) exit={0}" -f $mapExit
if ($Real -and $mapExit -eq 0) {
    & $MSB (Join-Path $Src 'src\char\char-server.vcxproj') /t:Rebuild /p:Configuration=Release /p:Platform=x64 "/p:SolutionDir=$sd" `
        /p:DefineConstants=NEED_MARBLE_TEST_HOOKS "/p:OutDir=$od" "/p:IntDir=${od}objc\" /m /v:minimal /nologo | Out-File -Encoding utf8 "$Out\logs\char-server.txt"
    "char-server(test) exit={0}" -f $LASTEXITCODE
}
