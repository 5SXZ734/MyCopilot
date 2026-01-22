@echo off
cd OverlayText
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.7.2\msvc2019_64"
cmake --build build --config Release
rem build\Release\OverlayText.exe
