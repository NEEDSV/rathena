@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul
if errorlevel 1 exit /b 1
cl /nologo /c /W3 /EHsc /MTd /std:c++17 /bigobj /DWIN32 /DFD_SETSIZE=4096 /DPCRE_SUPPORT /D_CRT_SECURE_NO_DEPRECATE /D_CRT_NONSTDC_NO_DEPRECATE /D_WINSOCK_DEPRECATED_NO_WARNINGS /DLIBCONFIG_STATIC /DYY_USE_CONST /D_DEBUG /D_CONSOLE /D_ITERATOR_DEBUG_LEVEL=0 /I"F:\rathena\src" /I"F:\rathena\3rdparty\rapidyaml\src" /I"F:\rathena\3rdparty\rapidyaml\ext\c4core\src" /I"F:\rathena\3rdparty\pcre\include" /I"F:\rathena\3rdparty\libconfig" /I"F:\rathena\3rdparty\mysql\include" /FoF:\rathena\tools\need_pvp_dummy\obj\ "F:\rathena\src\map\battle.cpp" "F:\rathena\src\map\mob.cpp"
exit /b %errorlevel%
