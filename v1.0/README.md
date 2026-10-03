# MSM v1.0

MSM (Mini Sample/Media) is a lightweight audio sample container format,
based on **ADPCM** compression and released under the MIT License.

- Small file header (30 bytes)
- ~5 KB decoder
- Supports 1 / 2 channels, up to 192 kHz
- Fixed compression ratio 4:1 / 8:1 (does not vary with input)
- **Zero external dependencies** (only the C standard library and Windows system DLLs)
- Single-header library (`minimsm.h`), embeddable in other projects

**Channel note:** encoders should pass only 1 or 2 channels; decoders reject >2.

> **Tip:** If you need better quality or an adjustable compression ratio, see
> [MSM v2.0](#relationship-to-v20), which uses the Opus codec.
> v1.0 is suited for **zero-dependency, small-footprint, embedded** scenarios.

---

## Project structure

| Tool | Description |
|---|---|
| `smp.exe` | GUI player (MP3 / MP4 / MKV / AVI / WAV / MSM) |
| `msmcli.exe` | CLI tool (MP3 → MSM / MSM → WAV / play MSM) |

**Notes:**

- Both tools are **single-file executables** that depend only on Windows system DLLs — no installation required.
- `smp.exe` MP4 / MKV / AVI playback relies on the system Media Foundation decoders.
- MKV/AVI also play through Media Foundation; whether they work depends on the decoders installed on the system.

---

## Source file layout

```
SRC/
├── LICENSE
├── README.md
├── minimp3.h            # MP3 decoder (CC0 1.0)
├── minimsm.h            # MSM v1.0 codec library (MIT, pure ADPCM)
├── main.c               # CLI tool source (MIT)
├── smp.c                # GUI player source (MIT)
├── build-MSM.bat        # Builds msmcli.exe
└── build-SMP.bat        # Builds smp.exe
```

**Note:** v1.0 **does not depend on any third-party library** and does not need static libraries such as `libopus.a`.

---

## License

**MIT License** — free to use, modify, distribute, use commercially, and
incorporate into closed-source derivatives, provided the copyright notice
is retained. See `LICENSE`.

**Dependencies:**

- minimp3 (CC0 1.0, public domain)
- Media Foundation / XAudio2 (Windows system components)

`SRC/LICENSE` is the license file for the project proper (MIT).

---

## Building

Requires MinGW-w64 or MSYS2 + gcc. **No third-party library installation needed.**

**Build (run inside `SRC/`):**

```cmd
build-MSM.bat    :: Builds the CLI tool, output msmcli.exe
build-SMP.bat    :: Builds the GUI player, output smp.exe
```

**Outputs:**

| Script | Output | Size (reference) |
|---|---|---|
| `build-MSM.bat` | `msmcli.exe` | ~200 – 500 KB |
| `build-SMP.bat` | `smp.exe`    | ~500 KB – 1 MB |

### `build-MSM.bat` contents

```bat
@echo off
cd /d %~dp0
gcc main.c -o msmcli.exe -lwinmm -O2 -s && echo OK
pause
```

### `build-SMP.bat` contents

```bat
@echo off
cd /d %~dp0
gcc smp.c -o smp.exe -municode -mwindows -DUNICODE -D_UNICODE ^
    -lole32 -lmfplat -lmfreadwrite -lmfuuid -lwinmm -lxaudio2_8 ^
    -lgdi32 -luser32 -luuid -lksuser -lshlwapi -lavrt ^
    -lcomdlg32 -lshell32 ^
    -std=c11 -O2 -s && echo OK
pause
```

**Notes:**

- `cd /d %~dp0` lets the script switch to its own directory regardless of
  where it is double-clicked, so `minimsm.h` / `minimp3.h` are found.
- `build-MSM.bat` only needs `-lwinmm` (playback via Windows `waveOut`).
- `build-SMP.bat` additionally uses `-municode -mwindows` plus
  Media Foundation, XAudio2, GDI, and other system libraries.

---

## System requirements

| Item | Requirement |
|---|---|
| OS | Windows 7 SP1 or later (32/64-bit) |
| Runtime dependencies | None (single file, only Windows system DLLs) |
| Build dependencies | gcc, MinGW-w64 or MSYS2 |

**Notes:**

- `msmcli.exe` uses the Windows `waveOut` API for playback,
  supported by all Windows versions (including XP).
- `smp.exe` uses Media Foundation (available since Vista) and XAudio2,
  so it **does not support Windows XP**.

### Known limitations (player, `smp.exe`)

- **Memory growth on long files:** audio buffers are retained during playback,
  so files several hours long may use hundreds of MB.
- **MP4 / MKV / AVI depend on Media Foundation:** playback depends on system decoders.

### Known limitations (CLI tool, `msmcli.exe`)

- The `mp3` and `wav` subcommands use **narrow-character `fopen`**,
  so non-ASCII paths may fail on Windows.
  Prefer pure-ASCII paths, or use the `play` subcommand (which supports non-ASCII paths).
- The `play` subcommand uses Windows `waveOut` and **submits the entire PCM buffer at once**.
  Pressing Ctrl+C during playback of a long file may leave residual audio.

---

## MSM format specification

The following is the core of the MSM v1.0 format as defined in `minimsm.h`, for implementers.

### File header (30 bytes)

All multi-byte fields are **little-endian**. The struct is packed tightly with
`#pragma pack(push, 1)` and has no padding. Fields total 30 bytes.

| Offset | Size | Field | Type | Description |
|--------|------|-------|------|-------------|
| 0 | 4 | `magic` | `char[4]` | `"MSM1"` (0x4D 0x53 0x4D 0x31) |
| 4 | 2 | `version` | `uint16_t` | Format version, `0x0100` |
| 6 | 2 | `flags` | `uint16_t` | Compression flag, `0x5A53` (ASCII "SZ") |
| 8 | 2 | `channels` | `uint16_t` | Channel count (1 or 2) |
| 10 | 4 | `sample_rate` | `uint32_t` | Sample rate (Hz) |
| 14 | 4 | `frame_count` | `uint32_t` | Total frame count |
| 18 | 4 | `pcm_samples` | `uint32_t` | Total samples per channel |
| 22 | 4 | `data_offset` | `uint32_t` | Offset of frame data (= 30) |
| 26 | 4 | `reserved` | `uint16_t[2]` | Reserved, must be 0 |

**Load validation (mandatory):**

- First 4 bytes must equal `"MSM1"`, otherwise reject
- `flags` must equal `0x5A53`, otherwise reject
- `data_offset` must be ≤ file size, otherwise reject

**Load validation (non-mandatory):**

- `version` is not currently enforced, but should be `0x0100`
- `channels` should be 1 or 2 when encoding; >2 is rejected when decoding

### Frame layout

After `data_offset` come `frame_count` consecutive frame blocks.

**Each frame block:**

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 4 | `frame_samples` | Samples per channel in this frame (must be a multiple of 4) |
| 4 | 4 | `ch0_length` | Compressed data length of channel 0 (includes mode byte) |
| 8 | `ch0_length` | `ch0_data` | Compressed data of channel 0 |
| 8+`ch0_length` | 4 | `ch1_length` | Compressed data length of channel 1 (present for stereo) |
| 12+`ch0_length` | `ch1_length` | `ch1_data` | Compressed data of channel 1 |

**Frame skip behavior:** if `frame_samples == 0`, the entire frame block is skipped
(including `ch0_length` / `ch0_data` / `ch1_length` / `ch1_data`),
and parsing resumes at the next frame block boundary. **`minimsm.h` implements this correctly.**

**Standard frame size:**

```
MSM_FRAME_SAMPLES = 18432  (= 1152 × 16)
```

This value allows a 44.1 / 48 kHz MP3 granule (1152 samples) to fit entirely
within one MSM frame. If a granule crosses a frame boundary, the remaining
samples are written as a separate small frame.

### Channel data

Each channel's compressed data begins with a **mode byte**:

| Byte 0 | Description |
|--------|-------------|
| `0x00` | Silence, the following N samples are all zero, no ADPCM data |
| `0x01` | 4-bit ADPCM |
| `0x02` | 2-bit ADPCM |

Byte 1 onward is packed ADPCM nibble data.

**Packing:**

| Mode | Samples per byte | Packing order |
|------|------------------|---------------|
| `0x02` (2-bit) | 4 | MSB first |
| `0x01` (4-bit) | 2 | MSB first |

**Silence mode (`0x00`):** only the 1-byte mode, no following ADPCM data.
`ch_data` length is 1, and the decoder directly fills N zeros.

### ADPCM codec algorithm

MSM uses first-order ADPCM:

```
predictor  = current predicted value (initial 0)
step_idx   = step table index (initial 0, range 0..88)
step       = msm_step_table[step_idx]
diff       = sample - predictor
nibble     = quantized residual (0..15 or 0..3)
predictor += (step * diff_table[nibble]) / divisor
predictor  = clamp(predictor, -32768, 32767)
step_idx  += index_table[nibble]
step_idx   = clamp(step_idx, 0, 88)
```

**Divisor:**

| Bit depth | divisor |
|-----------|---------|
| 2-bit | 8 |
| 4-bit | 8 |

### Step table (89 entries)

```
7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13900,
15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
```

### Index adjustment table

| Bit depth | Table |
|-----------|-------|
| 4-bit | `[-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]` |
| 2-bit | `[-1, 2, 4, 8]` |

### Difference table

| Bit depth | Table |
|-----------|-------|
| 4-bit | `[0, 1, 2, 3, 4, 5, 6, 7, -1, -2, -3, -4, -5, -6, -7, -8]` |
| 2-bit | `[0, 1, -1, 2]` |

### Mode selection

The encoder selects the mode per channel block based on the mean absolute amplitude:

```
avg = sum(|sample[i]|) / sample_count

if (avg < 512) → 2-bit mode (0x02)
else           → 4-bit mode (0x01)
```

**Optimization note:** silent segments (all zeros) will hit 2-bit mode but
will not automatically switch to silence mode `0x00`. The current encoder
can only produce `0x00` when `msm_sz_compress` is never called; under normal
operation, **silence mode is never generated**.

### 2-bit quantization

The encoder selects the 2-bit code per the following rules:

| Condition | Code | Difference |
|-----------|------|------------|
| diff == 0 | 0 | 0 |
| diff > 0 and diff >= step/4 | 3 | +2 |
| diff > 0 and diff <  step/4 | 1 | +1 |
| diff < 0 | 2 | -1 |

**Note:** when `diff < 0`, regardless of magnitude, it is always encoded as
code 2 (-1); the negative direction cannot express a larger difference.
This is a format-level asymmetry caused by the difference table `{0,1,-1,2}`
having only -1 in the negative direction, and is one reason v1.0's quality
is inferior to v2.0.

### Data rate

| Mode | Bits/sample | Compression vs 16-bit PCM |
|------|-------------|---------------------------|
| 2-bit | 2 | 8:1 |
| 4-bit | 4 | 4:1 |

For stereo at 48 kHz:

- 4-bit: 48,000 × 2 × 4 = **384 kbps**
- 2-bit: 48,000 × 2 × 2 = **192 kbps**

### Frame count limit

```
MSM_MAX_FRAME_COUNT = 4,294,967,295  (UINT32_MAX)
```

At 48 kHz / 18432 samples per frame, this is about **52 years**.

### Error handling

| Condition | Handling |
|-----------|----------|
| magic mismatch | Reject file |
| flags not `0x5A53` | Reject file |
| `data_offset` > file size | Reject file |
| `channels == 0` or `> 2` | Reject file |
| `frame_samples` > `MSM_FRAME_SAMPLES` | Truncate to limit |
| `frame_samples == 0` | Skip the entire frame block |
| compressed length == 0 | Reject (invalid) |
| decompressed sample count mismatch | Reject (corrupt data) |
| output buffer overflow | Stop decoding, zero-fill the rest |

> **Note:** `msm_decode` in `minimsm.h` **correctly implements**
> skipping the entire frame block (including `ch0_length` / `ch0_data`)
> when `frame_samples == 0`. This is more consistent than v2.0.

---

## Usage example

```c
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
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

/* Use pcm ...
 *
 * samples is the [total sample count], i.e. all channels interleaved:
 *     total samples       = samples
 *     samples per channel = samples / channels
 *     frame count         = samples per channel / 18432 (including trailing padding)
 */

free(pcm);
msm_close(&dec);
```

**Platform notes:**

- `msm_open_w` is a Windows-only wide-character interface, available only under `_WIN32`.
- Non-Windows platforms use `msm_open` (narrow character; UTF-8 must be converted by the caller).
- On Windows, prefer `msm_open_w` if paths contain non-ASCII characters.

---

## CLI usage

```cmd
msmcli mp3  <input.mp3> <output.msm>   :: MP3 to MSM
msmcli wav  <input.msm> <output.wav>   :: MSM to WAV
msmcli play <input.msm>                :: Play MSM (Windows only)
```

### Examples

```cmd
:: MP3 to MSM (no kbps parameter needed)
msmcli.exe mp3 E:\MUSIC\001.mp3 test.msm

:: To WAV
msmcli.exe wav test.msm test.wav

:: Play
msmcli.exe play test.msm
```

**Note:** the v1.0 `mp3` subcommand **does not need a bitrate parameter**;
the encoding mode is selected automatically per channel block based on
mean amplitude (2-bit or 4-bit).

---

## GUI player usage

```cmd
smp.exe                     :: Launch empty player
smp.exe file1.msm file2.mp3 :: Launch and load multiple files into the playlist
```

You can also **drag files onto the window**, or click the **Open** button.

### Shortcuts

| Key | Action |
|---|---|
| `Space` | Play / pause |
| `←` / `→` | Seek back / forward 5 seconds |
| `↑` / `↓` | Volume +5% / -5% |
| `N` / `P` | Next / previous track |
| `Esc` | Quit |

### Configuration files

`smp.exe` creates two files in **its own directory**:

| File | Contents |
|---|---|
| `smp.ini` | Window position, size, volume, last opened directory |
| `smp.log` | Runtime log (for debugging) |

Delete these two files to restore default settings.

---

## Format limitations

- 2-bit mode can only encode -1 in the negative direction; it cannot express -2 (asymmetric difference table)
- Frame sample count must be a multiple of 4
- Does not support >2 channels
- No built-in metadata (title, artist, etc.)
- No separate valid-sample-count field; decoded output may contain trailing padding silence
- Sample rate follows the input, unlike v2.0's fixed 48000 Hz

---

## Relationship to v2.0

| Item | v1.0 (this version) | v2.0 |
|---|---|---|
| Codec | First-order ADPCM | Opus |
| Magic | `"MSM1"` | `"MSM2"` |
| Compression ratio | Fixed 4:1 / 8:1 | 5:1 – 22:1 |
| Quality | Fair | Near-transparent at 128 kbps and above |
| Sample rate | Follows input | Fixed 48000 Hz |
| External dependency | None | libopus (BSD 3-Clause) |
| Single-file size | 200 – 500 KB | 1.5 – 3 MB |
| Embedding | Well suited | Must evaluate libopus size |

**Recommendation:**

- Need **zero dependencies, small size**, can accept fair quality → choose **v1.0**
- Need **high quality, adjustable compression ratio**, can accept libopus size → choose **v2.0**

The two MSM versions have **incompatible file formats**; decoders must dispatch by magic.

---

## Version history

### v1.0 (2026)
- Initial release
- First-order ADPCM, 2-bit / 4-bit dual mode
- Single-header library, 30-byte file header
- Fixed compression ratio 4:1 / 8:1

### v2.0 (2026)
- Switched to the Opus codec
- Magic changed to `"MSM2"`
- Compression ratio 5:1 – 22:1 (bitrate-controlled)
- Significantly improved quality (near-transparent at 128 kbps and above)
- Supports 44100 Hz input (automatic resampling)
- Requires libopus (BSD 3-Clause)

---

## Contact

For questions or suggestions, open an issue or contact SimpleToolsStudio.

---

**End of README.md**