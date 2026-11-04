@echo off
rem Сборка Romchik.exe компилятором MinGW-w64 (g++). Нужен g++ в PATH (MSYS2 / w64devkit / WinLibs).
if not exist build mkdir build
windres romchik.rc -O coff -o build\res.o || goto :err
g++ -std=c++17 -O2 -mwindows -static -static-libgcc -static-libstdc++ -Isrc ^
  src\game.cpp src\fishing.cpp src\overlay.cpp src\assets.cpp src\mixer.cpp src\win_main.cpp build\res.o ^
  -o Romchik.exe -ld2d1 -ldwrite -lwinmm -luser32 -lgdi32 || goto :err
echo.
echo Готово: Romchik.exe
goto :eof
:err
echo Ошибка сборки. Скопируйте текст ошибки и отправьте разработчику.
exit /b 1
