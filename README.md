# MSM

- **v1.0/** — ADPCM, zero external dependencies, ~200–500 KB
- **v2.0/** — Opus-based, requires libopus.a (MSYS2: `pacman -S mingw-w64-x86_64-opus`), ~1.5–2.5 MB
- **v3.0/** — Opus-based, Opus-native sample rates pass through, ~1.5–2.5 MB

The three versions are **format-incompatible** (v1.0 magic = `"MSM1"`, v2.0 magic = `"MSM2"`, v3.0 magic = `"MSM3"`).
Decoders must dispatch by magic; they cannot be mixed.

Build inside each version directory:

    build-MSM.bat  -> msmcli.exe  (CLI: MP3->MSM / MSM->WAV / play MSM)
    build-SMP.bat  -> smp.exe     (GUI player)

See the README.md inside each version directory for the full format spec and usage.

---

MSM is developed by **SimpleToolsStudio** (GitHub: [@20260509](https://github.com/20260509)).