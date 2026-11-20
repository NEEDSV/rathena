@echo off
setlocal
cd /d "%~dp0\..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
cl /nologo /EHsc /W3 /std:c++17 /Fe:docs\reports\need-doram-ep162-20261118\native_smoke.exe /Fo:docs\reports\need-doram-ep162-20261118\native_smoke.obj docs\reports\need-doram-ep162-20261118\native_smoke.cpp
if errorlevel 1 exit /b 1
docs\reports\need-doram-ep162-20261118\native_smoke.exe
exit /b %errorlevel%
