@echo off
setlocal
cd /d "%~dp0\..\.."
if not defined NEED_VCVARSALL set "NEED_VCVARSALL=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
call "%NEED_VCVARSALL%" x86 >nul
if errorlevel 1 exit /b 1
python doc\need_equipment_inspection\smoke.py
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W3 /MTd /D_ITERATOR_DEBUG_LEVEL=0 /D_CRT_SECURE_NO_WARNINGS /wd4018 /wd4200 /Isrc /I3rdparty\rapidyaml\src /I3rdparty\rapidyaml\ext\c4core\src /I3rdparty\pcre\include /I3rdparty\mysql\include /I3rdparty\libconfig /Fo.vs\need_equipment_inspection\smoke.obj /Fd.vs\need_equipment_inspection\smoke.pdb /Fe.vs\need_equipment_inspection\smoke.exe .vs\need_equipment_inspection\smoke.cpp
if errorlevel 1 exit /b 1
.vs\need_equipment_inspection\smoke.exe
exit /b %errorlevel%
