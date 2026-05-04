@echo off
if not exist output mkdir output
g++ -std=c++17 -o output\ai-extract.exe src\ai-extract.cpp -luser32 -lshell32
echo Build complete: output\ai-extract.exe
pause