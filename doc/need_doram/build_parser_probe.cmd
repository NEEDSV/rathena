@echo off
setlocal
cd /d "%~dp0\..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
set "PROBE=docs\reports\need-doram-release-gate-20261118\parser"
cl /nologo /c /W3 /EHsc /MTd /std:c++17 /bigobj /DWIN32 /DFD_SETSIZE=4096 /DPCRE_SUPPORT /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_WINSOCK_DEPRECATED_NO_WARNINGS /DLIBCONFIG_STATIC /DYY_USE_CONST /D_DEBUG /D_CONSOLE /D_ITERATOR_DEBUG_LEVEL=0 /I"src" /I"src\map" /I"3rdparty\rapidyaml\src" /I"3rdparty\rapidyaml\ext\c4core\src" /I"3rdparty\pcre\include" /I"3rdparty\libconfig" /I"3rdparty\mysql\include" /Fo%PROBE%\ %PROBE%\map_probe.cpp %PROBE%\script_probe.cpp
if errorlevel 1 exit /b 1
link @%PROBE%\link.rsp
if errorlevel 1 exit /b 1
copy /y 3rdparty\mysql\lib\Win32\libmysql.dll %PROBE%\ >nul
copy /y 3rdparty\pcre\lib\Win32\pcre8.dll %PROBE%\ >nul
copy /y 3rdparty\zlib\lib\Win32\zlib.dll %PROBE%\ >nul
exit /b %errorlevel%
