@echo off
cd /d %~dp0
gcc main.c -o msmcli.exe -lwinmm -O2 -s && echo OK
pause