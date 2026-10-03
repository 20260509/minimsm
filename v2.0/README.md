# MSM v2.0

MSM（Mini Sample/Media）是一个轻量级的音频采样媒体容器格式，
使用 **Opus** 编解码器，支持灵活的压缩比和出色的音质。

- 文件头小（30 字节）
- 压缩比灵活（约 5:1 ~ 22:1，由比特率控制）
- 音质出色（128 kbps 以上接近透明，Opus 是业界公认的高质量有损编解码器）
- 采样率 48000 Hz
- 支持 1 / 2 声道
- 单头文件库（`minimsm.h`），可直接嵌入其他项目
- **免费商用**（Opus 采用 BSD 3-Clause 许可证）

---

## 项目结构

| 工具 | 说明 |
|---|---|
| `msmcli.exe` | 命令行工具（MP3 → MSM / MSM → WAV / 播放 MSM） |
| `smp.exe` | 图形化媒体播放器（MP3 / MP4 / WAV / MSM） |

**说明：** `msmcli.exe` 是单文件可执行程序，只依赖 Windows 系统 DLL，无需安装。

---

## 源代码文件结构

```
SRC/
├── LICENSE
├── README.md
├── minimp3.h            # MP3 解码器（CC0 1.0）
├── minimsm.h            # MSM 编解码库（MIT）
├── main.c               # 命令行工具源码（MIT）
├── build-MSM.bat        # 编译脚本
│
├── opus.h               # libopus 公开头文件（BSD 3-Clause，随源码分发）
├── opus_defines.h       # 同上
├── opus_types.h         # 同上
├── opus_multistream.h   # 同上
├── opus_projection.h    # 同上
└── opus_custom.h        # 同上
```

**说明：** `opus*.h` 是从 libopus 项目随源码分发的公开头文件，仅用于编译。
链接时仍然需要 `libopus` 静态库（`libopus.a`）。

---

## 许可证

**MIT License** —— 可自由使用、修改、分发、商用、闭源衍生，
只需保留版权声明。详见 `LICENSE`。

**依赖：**

- minimp3（CC0 1.0，公共领域）
- libopus（BSD 3-Clause，可闭源商用，主要专利持有者已承诺免版税）

本仓库 `SRC/` 下随附 libopus 的公开头文件（`opus.h`、`opus_defines.h`、
`opus_types.h`、`opus_multistream.h`、`opus_projection.h`、`opus_custom.h`），
版权归 Xiph.Org Foundation、Skype Limited 等所有，采用 BSD 3-Clause 许可。
这些头文件仅用于编译，链接时仍需要 libopus 静态库（`libopus.a`）。

---

## 编译

需要 MinGW-w64 或 MSYS2 + gcc，以及 `libopus` 静态库（`libopus.a`）。

**头文件已随源码提供**（`SRC/opus*.h`），无需单独安装。
MSYS2 下安装 libopus 静态库：

```bash
pacman -S mingw-w64-x86_64-opus
```

**注意：** 随源码分发的 `opus*.h` 与所链接的 `libopus.a` 应尽量保持同一版本。

**编译（在 `SRC/` 目录下执行）：**

```cmd
build-MSM.bat
```

**编译产物：** `msmcli.exe`（单文件，约 1.5~2.5 MB）

**编译脚本 `build-MSM.bat` 内容：**

```bat
@echo off
cd /d %~dp0
gcc main.c -o msmcli.exe -lwinmm -lm -DMSM_USE_OPUS ^
    -Wl,-Bstatic -lopus -Wl,-Bdynamic ^
    -O2 -s && echo OK
pause
```

**说明：**

- `cd /d %~dp0` 让脚本无论从哪里双击都能切到自身所在目录，保证 `opus*.h` 能被找到。
- `-Wl,-Bstatic -lopus -Wl,-Bdynamic` 让 libopus 静态链接，
  生成不依赖 `libopus-0.dll` 的单文件。

---

## 系统要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 7 SP1 及以上（32/64 位） |
| 运行时依赖 | 无（单文件，只依赖 Windows 系统 DLL） |
| 编译依赖 | gcc、MinGW-w64 或 MSYS2、libopus 静态库 |

**说明：** 播放功能使用 Windows `waveOut` API，所有 Windows 版本都支持。

---

## 命令行工具用法

```cmd
msmcli mp3  <input.mp3> <output.msm> <kbps>   :: MP3 转 MSM（Opus）
msmcli wav  <input.msm> <output.wav>          :: MSM 转 WAV
msmcli play <input.msm>                       :: 播放 MSM（仅 Windows）
```

### `kbps` 参数

**必填。** 比特率控制压缩比和音质：

| kbps | 压缩比 | 音质 | 3 分钟文件大小 |
|---|---|---|---|
| 64 | ~22:1 | 中等（语音良好，音乐可接受） | ~1.4 MB |
| 96 | ~15:1 | 好 | ~2.1 MB |
| **128** | **~11:1** | **很好（推荐）** | **~2.8 MB** |
| 192 | ~7:1 | 极好 | ~4.2 MB |
| 256 | ~5.5:1 | 接近透明 | ~5.6 MB |

### 使用示例

```cmd
:: 128 kbps（推荐）
msmcli.exe mp3 E:\MUSIC\001.mp3 test.msm 128

:: 64 kbps（小文件）
msmcli.exe mp3 E:\MUSIC\001.mp3 small.msm 64

:: 192 kbps（高音质）
msmcli.exe mp3 E:\MUSIC\001.mp3 hq.msm 192

:: 转 WAV
msmcli.exe wav test.msm test.wav

:: 播放
msmcli.exe play test.msm
```

**44100 Hz 的 MP3 会自动重采样到 48000 Hz。**

---

## MSM 格式规范

以下是 `minimsm.h` 中定义的 MSM v2.0 格式核心信息，供实现者参考。

### 文件头（30 字节）

所有多字节字段均为**小端序**。结构体按 `#pragma pack(push, 1)` 紧密排列，
无填充字节。字段合计 30 字节。

| 偏移 | 大小 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 4 | `magic` | `char[4]` | `"MSM2"`（0x4D 0x53 0x4D 0x32） |
| 4 | 2 | `version` | `uint16_t` | 格式版本，`0x0200` |
| 6 | 2 | `flags` | `uint16_t` | 标志，值 `0x5A53`，小端存储为字节 `53 5A`（ASCII "SZ"） |
| 8 | 2 | `channels` | `uint16_t` | 声道数（1 或 2） |
| 10 | 4 | `sample_rate` | `uint32_t` | 采样率，固定 48000 |
| 14 | 4 | `frame_count` | `uint32_t` | 帧总数 |
| 18 | 4 | `pcm_samples` | `uint32_t` | 每声道总样本数，= `frame_count × 18432`（含末尾 padding） |
| 22 | 4 | `data_offset` | `uint32_t` | 帧数据起始偏移（= 30） |
| 26 | 4 | `reserved` | `uint16_t[2]` | 4 字节保留区，必须为 0 |

**加载校验规则：**

- 前 4 字节必须等于 `"MSM2"`，否则拒绝
- `flags` 必须等于 `0x5A53`，否则拒绝
- `version` 必须等于 `0x0200`，否则拒绝
- `data_offset` 必须 ≤ 文件大小，否则拒绝

### 帧布局

`data_offset` 之后是 `frame_count` 个连续的帧块。

**每个帧块：**

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | `frame_samples` | 本帧每声道样本数，固定 18432 |
| 4 | 4 | `comp_len` | 通道块压缩数据长度（含模式字节） |
| 8 | `comp_len` | `ch_data` | 通道块压缩数据 |

**标准帧大小：**

```
MSM_FRAME_SAMPLES = 18432
```

**固定 18432**（不是 960 的整数倍，最后一帧用 0 padding）。

> **重要：** `pcm_samples = frame_count × 18432`，**包含最后一帧的 padding 样本**。
> 解码后 PCM 末尾可能有最多 18432 个静音样本，由应用自行裁剪或忽略。
> 当前格式没有单独的有效样本数字段。

### 通道块

每个通道块以**模式字节**开头：

| 字节 0 | 说明 |
|--------|------|
| `0x20` | Opus 交织数据（立体声共用） |

字节 1 起是 Opus 数据：

```
[2 bytes num_blocks]
[2 bytes packet_len_0][packet_0]
[2 bytes packet_len_1][packet_1]
...
```

- `num_blocks = 20`（18432 / 960 向上取整）
- 每块 960 样本（20ms at 48kHz）

### Opus 编码

- **采样率**：48000 Hz（Opus 原生支持）
- **帧大小**：960 样本（20ms）
- **应用模式**：`OPUS_APPLICATION_AUDIO`
- **信号类型**：`OPUS_SIGNAL_MUSIC`
- **复杂度**：10（最高）

### 重采样

输入 MP3 采样率不是 48000 Hz 时（如 44100 Hz），
自动线性插值重采样到 48000 Hz。

### 数据率

| kbps | 立体声文件大小（3 分钟） |
|---|---|
| 64 | ~1.4 MB |
| 128 | ~2.8 MB |
| 192 | ~4.2 MB |
| 256 | ~5.6 MB |

### 错误处理

按下表顺序依次判断：

| 情况 | 处理 |
|------|------|
| magic 不是 `"MSM2"` | 拒绝文件 |
| flags 不是 `0x5A53` | 拒绝文件 |
| version 不是 `0x0200` | 拒绝文件 |
| `data_offset` > 文件大小 | 拒绝文件 |
| `frame_samples` > `MSM_FRAME_SAMPLES` | 截断到上限 |
| `frame_samples == 0` | 跳过整个帧块（见下注） |
| 压缩长度 == 0 | 拒绝（无效） |
| 解压后样本数不匹配 | 拒绝（数据损坏） |
| 输出缓冲溢出 | 停止解码，剩余填零 |

> **注：** 「跳过整个帧块」指跳过该帧的 `frame_samples`、`comp_len` 和
> `ch_data` 三个部分，从下一个帧块边界继续解析。当前 `minimsm.h` 实现为
> `continue`，仅跳过 `frame_samples` 字段；实现者解析时应注意此差异。
>
> **编码侧补充：** `frame_samples` 固定 18432；非满帧调用 `msm_encode_frame`
> 会返回 -5。编码器在最后一帧先 padding 到 18432 再写入。

---

## 使用示例

```c
#define MSM_USE_OPUS
#include "opus.h"
#include "minimsm.h"

/* 打开 MSM 文件（Windows 宽字符路径） */
msm_decoder_t dec;
if (msm_open_w(&dec, L"audio.msm") != 0) {
    /* 失败 */
}

/* 解码为 16-bit PCM */
int16_t *pcm = NULL;
size_t samples = 0;
if (msm_decode(&dec, &pcm, &samples) != 0) {
    /* 失败 */
}

/* 使用 pcm ...（samples 是每声道样本数，含末尾 padding） */

free(pcm);
msm_close(&dec);
```

**平台说明：**

- `msm_open_w` 是 Windows 专用宽字符接口，仅在 `_WIN32` 下提供。
- 非 Windows 平台使用 `msm_open`（窄字符，UTF-8 需自行转换）。
- Windows 上若路径含非 ASCII 字符，请优先使用 `msm_open_w`。

---

## 格式局限

- 采样率固定 48000 Hz（Opus 的限制）
- 帧内样本数固定 18432
- 仅支持 1 / 2 声道（>2 在打开阶段被拒绝）
- 无内建元数据（标题、艺术家等）
- 无独立有效样本数字段，解码后末尾可能带 padding 静音

---

## 版本历史

### v2.0 (2026)
- 改用 Opus 编解码器
- magic 改为 "MSM2"（0x4D 0x53 0x4D 0x32）
- 压缩比 5:1 ~ 22:1（由比特率控制）
- 音质显著提升（128 kbps 以上接近透明）
- 支持 44100 Hz 输入（自动重采样）
- 需要 libopus（BSD 3-Clause）

### v1.0 (2026)
- 初始版本
- 一阶 ADPCM，2-bit / 4-bit 双模式
- 单头文件库，30 字节文件头
- 压缩比固定 4:1 / 8:1

---

## 联系方式

如有问题或建议，请提交 issue 或联系 SimpleToolsStudio。

---

**End of README.md**