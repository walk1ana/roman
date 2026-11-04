@echo off
rem Сборка Romchik.exe компилятором Visual Studio. Запускайте из "x64 Native Tools Command Prompt for VS".
if not exist build mkdir build
rc /nologo /fo build\romchik.res romchik.rc || goto :err
cl /nologo /std:c++17 /O2 /EHsc /utf-8 /DNDEBUG /Isrc /Fo:build\ /Fe:Romchik.exe ^
  src\game.cpp src\fishing.cpp src\overlay.cpp src\assets.cpp src\mixer.cpp src\win_main.cpp build\romchik.res ^
  /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib d2d1.lib dwrite.lib winmm.lib || goto :err
echo.
echo Готово: Romchik.exe
goto :eof
:err
echo Ошибка сборки.
exit /b 1
