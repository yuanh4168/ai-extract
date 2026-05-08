@echo off
if not exist output mkdir output
g++ -std=c++17 -o output\ai-extract.exe src/main.cpp src/config.cpp src/utils.cpp src/clipboard.cpp src/pathutil.cpp src/parser.cpp src/directiveproc.cpp src/backup.cpp src/promptchain.cpp src/markdown_render.cpp src/task_manager.cpp -luser32 -lshell32
pause
