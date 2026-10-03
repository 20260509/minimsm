# MSM v2.0

MSM (Mini Sample/Media) is a lightweight audio sample container format
using the **Opus** codec, offering a flexible compression ratio and excellent quality.

- Small file header (30 bytes)
- Flexible compression ratio (~5:1 – 22:1, bitrate-controlled)
- Excellent quality (near-transparent at 128 kbps and above; Opus is widely recognized as a high-quality lossy codec)
- Sample rate 48000 Hz
- Supports 1 / 2 channels
- Single-header library (`minimsm.h`), embeddable in other projects
- **Free for commercial use** (Opus is BSD 3-Clause licensed)

---

## Project structure

| Tool | Description |
|---|---|
| `msmcli.exe` | CLI tool (MP3 → MSM / MSM → WAV / play MSM) |
| `smp.exe` | GUI media player (MP3 / MP4 / WAV / MSM) |

**Note:** `msmcli.exe` is a single-file executable that depends only on Windows system DLLs — no installation required.

---

## Source file layout

```
SRC/
├── LICENSE
├── README.md
├── minimp3.h            # MP3 decoder (CC0 1.0)
├── minimsm.h            # MSM codec library (MIT)
├── main.c               # CLI tool source (MIT)
├── build-MSM.bat        # Build script
│
├── opus.h               # libopus public headers (BSD 3-Clause, distributed with source)
├── opus_defines.h       # same as above
├── opus_types.h         # same as above
├── opus_multistream.h   # same as above
├── opus_projection.h    # same as above
└── opus_custom.h        # same as above
```

**Note:** `opus*.h` are public headers distributed with the libopus project; they are only needed for compilation.
Linking still requires the `libopus` static library (`libopus.a`).

---

## License

**MIT License** — free to use, modify, distribute, use commercially, and
incorporate into closed-source derivatives, provided the copyright notice is
retained. See `LICENSE`.

**Dependencies:**

- minimp3 (CC0 1.0, public domain)
- libopus (BSD 3-Clause; closed-source commercial use permitted; major patent holders have pledged royalty-free terms)

This repository's `SRC/` ships libopus public headers (`opus.h`, `opus_defines.h`,
`opus_types.h`, `opus_multistream.h`, `opus_projection.h`, `opus_custom.h`),
copyright Xiph.Org Foundation, Skype Limited, and others, under BSD 3-Clause.
These headers are only used for compilation; linking still requires the libopus
static library (`libopus.a`).

---

## Building

Requires MinGW-w64 or MSYS2 + gcc, plus the `libopus` static library (`libopus.a`).

**The headers are shipped with the source** (`SRC/opus*.h`) and need no separate installation.
Install the libopus static library under MSYS2:

```bash
pacman -S mingw-w64-x86_64-opus
```

**Note:** the shipped `opus*.h` and the linked `libopus.a` should be from the same version if possible.

**Build (run inside `SRC/`):**

```cmd
build-MSM.bat
```

**Output:** `msmcli.exe` (single file, ~1.5 – 2.5 MB)

**Contents of `build-MSM.bat`:**

```bat
@echo off
cd /d %~dp0
gcc main.c -o msmcli.exe -lwinmm -lm -DMSM_USE_OPUS ^
    -Wl,-Bstatic -lopus -Wl,-Bdynamic ^
    -O2 -s && echo OK
pause
```

**Notes:**

- `cd /d %~dp0` lets the script switch to its own directory regardless of
  where it is double-clicked, so `opus*.h` are found.
- `-Wl,-Bstatic -lopus -Wl,-Bdynamic` links libopus statically,
  producing a single file that does not depend on `libopus-0.dll`.

---

## System requirements

| Item | Requirement |
|---|---|
| OS | Windows 7 SP1 or later (32/64-bit) |
| Runtime dependencies | None (single file, only Windows system DLLs) |
| Build dependencies | gcc, MinGW-w64 or MSYS2, libopus static library |

**Note:** playback uses the Windows `waveOut` API, supported by all Windows versions.

---

## CLI usage

```cmd
msmcli mp3  <input.mp3> <output.msm> <kbps>   :: MP3 to MSM (Opus)
msmcli wav  <input.msm> <output.wav>          :: MSM to WAV
msmcli play <input.msm>                       :: Play MSM (Windows only)
```

### The `kbps` parameter

**Required.** Bitrate controls compression ratio and quality:

| kbps | Compression | Quality | 3-minute file size |
|---|---|---|---|
| 64 | ~22:1 | Fair (good for speech, acceptable for music) | ~1.4 MB |
| 96 | ~15:1 | Good | ~2.1 MB |
| **128** | **~11:1** | **Very good (recommended)** | **~2.8 MB** |
| 192 | ~7:1 | Excellent | ~4.2 MB |
| 256 | ~5.5:1 | Near-transparent | ~5.6 MB |

### Examples

```cmd
:: 128 kbps (recommended)
msmcli.exe mp3 E:\MUSIC\001.mp3 test.msm 128

:: 64 kbps (smaller file)
msmcli.exe mp3 E:\MUSIC\001.mp3 small.msm 64

:: 192 kbps (high quality)
msmcli.exe mp3 E:\MUSIC\001.mp3 hq.msm 192

:: To WAV
msmcli.exe wav test.msm test.wav

:: Play
msmcli.exe play test.msm
```

**44100 Hz MP3 input is automatically resampled to 48000 Hz.**

---

## MSM format specification

The following is the core of the MSM v2.0 format as defined in `minimsm.h`, for implementers.

### File header (30 bytes)

All multi-byte fields are **little-endian**. The struct is packed tightly with
`#pragma pack(push, 1)` and has no padding. Fields total 30 bytes.

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 4 | `magic` | `char[4]` | `"MSM2"` (0x4D 0x53 0x4D 0x32) |
| 4 | 2 | `version` | `uint16_t` | Format version, `0x0200` |
| 6 | 2 | `flags` | `uint16_t` | Flag, value `0x5A53`, stored little-endian as bytes `53 5A` (ASCII "SZ") |
| 8 | 2 | `channels` | `uint16_t` | Channel count (1 or 2) |
| 10 | 4 | `sample_rate` | `uint32_t` | Sample rate, fixed 48000 |
| 14 | 4 | `frame_count` | `uint32_t` | Total frame count |
| 18 | 4 | `pcm_samples` | `uint32_t` | Total samples per channel, = `frame_count × 18432` (including trailing padding) |
| 22 | 4 | `data_offset` | `uint32_t` | Offset of frame data (= 30) |
| 26 | 4 | `reserved` | `uint16_t[2]` | 4-byte reserved area, must be 0 |

**Load validation:**

- First 4 bytes must equal `"MSM2"`, otherwise reject
- `flags` must equal `0x5A53`, otherwise reject
- `version` must equal `0x0200`, otherwise reject
- `data_offset` must be ≤ file size, otherwise reject

### Frame layout

After `data_offset` come `frame_count` consecutive frame blocks.

**Each frame block:**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | `frame_samples` | Samples per channel in this frame, fixed 18432 |
| 4 | 4 | `comp_len` | Compressed data length of the channel block (includes mode byte) |
| 8 | `comp_len` | `ch_data` | Compressed data of the channel block |

**Standard frame size:**

```
MSM_FRAME_SAMPLES = 18432
```

**Fixed 18432** (not a multiple of 960; the last frame is zero-padded).

> **Important:** `pcm_samples = frame_count × 18432`, **including the padding samples of the last frame**.
> After decoding, the PCM tail may contain up to 18432 silent samples; the application
> must trim or ignore them. The current format has no separate valid-sample-count field.

### Channel block

Each channel block begins with a **mode byte**:

| Byte 0 | Description |
|--------|-------------|
| `0x20` | Opus interleaved data (shared for stereo) |

Byte 1 onward is Opus data:

```
[2 bytes num_blocks]
[2 bytes packet_len_0][packet_0]
[2 bytes packet_len_1][packet_1]
...
```

- `num_blocks = 20` (18432 / 960, rounded up)
- Each block is 960 samples (20 ms at 48 kHz)

### Opus encoding

- **Sample rate:** 48000 Hz (natively supported by Opus)
- **Frame size:** 960 samples (20 ms)
- **Application mode:** `OPUS_APPLICATION_AUDIO`
- **Signal type:** `OPUS_SIGNAL_MUSIC`
- **Complexity:** 10 (maximum)

### Resampling

When the input MP3 sample rate is not 48000 Hz (e.g. 44100 Hz),
it is automatically resampled to 48000 Hz using linear interpolation.

### Data rate

| kbps | Stereo file size (3 minutes) |
|---|---|
| 64 | ~1.4 MB |
| 128 | ~2.8 MB |
| 192 | ~4.2 MB |
| 256 | ~5.6 MB |

### Error handling

Evaluated in order, top to bottom:

| Condition | Handling |
|-----------|----------|
| magic not `"MSM2"` | Reject file |
| flags not `0x5A53` | Reject file |
| version not `0x0200` | Reject file |
| `data_offset` > file size | Reject file |
| `frame_samples` > `MSM_FRAME_SAMPLES` | Truncate to limit |
| `frame_samples == 0` | Skip the entire frame block (see note below) |
| compressed length == 0 | Reject (invalid) |
| decompressed sample count mismatch | Reject (corrupt data) |
| output buffer overflow | Stop decoding, zero-fill the rest |

> **Note:** "skip the entire frame block" means skipping the frame's `frame_samples`,
> `comp_len`, and `ch_data` fields, and resuming parsing at the next frame block
> boundary. The current `minimsm.h` implementation uses `continue`, which only
> skips the `frame_samples` field; implementers should be aware of this difference.
>
> **Encoder-side note:** `frame_samples` is fixed at 18432; calling `msm_encode_frame`
> with a non-full frame returns -5. The encoder pads the last frame to 18432 before writing.

---

## Usage example

```c
#define MSM_USE_OPUS
#include "opus.h"
#include "minimsm.h"

/* Open an MSM file (Windows wide-character path) */
msm_decoder_t dec;
if (msm_open_w(&dec, L"audio.msm") != 0) {
    /* failure */
}

/* Decode to 16-bit PCM */
int16_t *pcm = NULL;
size_t samples = 0;
if (msm_decode(&dec, &pcm, &samples) != 0) {
    /* failure */
}

/* Use pcm ... (samples is the per-channel sample count, including trailing padding) */

free(pcm);
msm_close(&dec);
```

**Platform notes:**

- `msm_open_w` is a Windows-only wide-character interface, available only under `_WIN32`.
- Non-Windows platforms use `msm_open` (narrow character; UTF-8 must be converted by the caller).
- On Windows, prefer `msm_open_w` if paths contain non-ASCII characters.

---

## Format limitations

- Fixed 48000 Hz sample rate (an Opus constraint)
- Fixed 18432 samples per frame
- Supports only 1 / 2 channels (>2 is rejected at open time)
- No built-in metadata (title, artist, etc.)
- No separate valid-sample-count field; decoded output may contain trailing padding silence

---

## Version history

### v2.0 (2026)
- Switched to the Opus codec
- Magic changed to "MSM2" (0x4D 0x53 0x4D 0x32)
- Compression ratio 5:1 – 22:1 (bitrate-controlled)
- Significantly improved quality (near-transparent at 128 kbps and above)
- Supports 44100 Hz input (automatic resampling)
- Requires libopus (BSD 3-Clause)

### v1.0 (2026)
- Initial release
- First-order ADPCM, 2-bit / 4-bit dual mode
- Single-header library, 30-byte file header
- Fixed compression ratio 4:1 / 8:1

---

## Contact

For questions or suggestions, open an issue or contact SimpleToolsStudio.

---

**End of README.md**