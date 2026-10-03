# MSM

- **v1.0/** — ADPCM 版，零外部依赖，约 200–500 KB
- **v2.0/** — Opus 版，需要 libopus.a（MSYS2: `pacman -S mingw-w64-x86_64-opus`），约 1.5–2.5 MB

两版文件格式**不兼容**（v1.0 magic = `"MSM1"`，v2.0 magic = `"MSM2"`），
解码器需按 magic 分派，不能混用。

进入对应目录编译：

    build-MSM.bat  → msmcli.exe（命令行：MP3→MSM / MSM→WAV / 播放）
    build-SMP.bat  → smp.exe（图形播放器）

各自目录下的 README.md 有完整格式规范与用法。