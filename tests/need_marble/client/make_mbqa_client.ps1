# NEED marble real-client playtest: makes a NEW client folder that connects only to the isolated test stack
# (login 127.0.0.1:26901). Never changes the base client, NEEDResoruce or any live / other test client.
#
# usage: make_mbqa_client.ps1 -Base <existing KR client folder> -Out <new folder> [-Res <NEEDResoruce>]
#   -Base : a full NEED KR client (NEEDClient.exe + DATA.INI + need_data.grf + data.grf), read only
#   -Out  : must not exist yet (e.g. E:\ttt\NEED_MBQA_CLIENT); about 5-6 GB are copied
#   -Res  : resource repo for need_marbl.gat / .gnd / .rsw and data\texture\needmarble (default E:\tools\Need\NEEDResoruce)
param([Parameter(Mandatory)] [string]$Base, [Parameter(Mandatory)] [string]$Out, [string]$Res = 'E:\tools\Need\NEEDResoruce')
$ErrorActionPreference = 'Stop'
if (Test-Path $Out) { throw "$Out exists - refusing (never overwrite a client folder)" }
foreach ($f in 'NEEDClient.exe', 'DATA.INI', 'need_data.grf', 'data.grf') {
    if (-not (Test-Path (Join-Path $Base $f))) { throw "$Base has no $f - not a full KR client" }
}
foreach ($f in 'data\need_marbl.gat', 'data\need_marbl.gnd', 'data\need_marbl.rsw', 'data\texture\needmarble') {
    if (-not (Test-Path (Join-Path $Res $f))) { throw "$Res has no $f" }
}
robocopy $Base $Out /E /NFL /NDL /NJH /NJS /XD Replay ScreenShot | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }
New-Item -ItemType Directory -Force (Join-Path $Out 'data\texture') | Out-Null
Copy-Item (Join-Path $Res 'data\need_marbl.*') (Join-Path $Out 'data') -Force
robocopy (Join-Path $Res 'data\texture\needmarble') (Join-Path $Out 'data\texture\needmarble') /E /NFL /NDL /NJH /NJS | Out-Null
# connection: the isolated login-server only (packet version / langtype as the KR client)
$ci = @'
<?xml version="1.0" encoding="euc-kr" ?>
<clientinfo>
	<desc>Ragnarok Client Information</desc>
	<servicetype>korea</servicetype>
	<servertype>primary</servertype>
	<connection>
		<display>NEED MBQA (isolated test)</display>
		<address>127.0.0.1</address>
		<port>26901</port>
		<version>55</version>
		<langtype>0</langtype>
		<registrationweb>www.ragnarok.com</registrationweb>
	</connection>
</clientinfo>
'@
[IO.File]::WriteAllText((Join-Path $Out 'data\clientinfo.xml'), $ci, [Text.Encoding]::ASCII)
foreach ($f in 'data\sclientinfo.xml', 'System\clientinfo.xml') {
    $p = Join-Path $Out $f
    if (Test-Path $p) { Rename-Item $p ($p + '.disabled_by_mbqa') }   # only the isolated address may remain
}
"client ready: $Out (login 127.0.0.1:26901). Start the isolated stack first, then $Out\NEEDClient.exe."
