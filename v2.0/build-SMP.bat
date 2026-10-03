@echo off
cd /d %~dp0
gcc smp.c -o smp.exe -municode -mwindows -DUNICODE -D_UNICODE ^
    -DMSM_USE_OPUS ^
    -lole32 -lmfplat -lmfreadwrite -lmfuuid -lwinmm -lxaudio2_8 ^
    -lgdi32 -luser32 -luuid -lksuser -lshlwapi -lavrt ^
    -lcomdlg32 -lshell32 ^
    -Wl,-Bstatic -lopus -Wl,-Bdynamic ^
    -std=c11 -O2 -s && echo OK
pause