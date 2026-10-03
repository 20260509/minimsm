@echo off
cd /d %~dp0
gcc main.c -o msmcli.exe -lwinmm -lm -DMSM_USE_OPUS ^
    -Wl,-Bstatic -lopus -Wl,-Bdynamic ^
    -O2 -s && echo OK
pause