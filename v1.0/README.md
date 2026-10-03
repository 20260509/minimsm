# MSM v1.0

MSM（Mini Sample/Media）是一个轻量级的音频采样媒体容器格式，
基于 **ADPCM** 压缩，使用 MIT 许可证开源。

- 文件头小（30 字节）
- 解码器约 5 KB
- 支持 1 / 2 声道，最高 192 kHz
- 压缩比固定 4:1 / 8:1（不随输入变化）
- **零外部依赖**（只用 C 标准库和 Windows 系统 DLL）
- 单头文件库（`minimsm.h`），可直接嵌入其他项目

**声道说明：** 编码时建议只传 1 或 2；解码时 >2 会被拒绝。

> **提示：** 如果你需要更好的音质、可调的压缩比，请看
> [MSM v2.0](#与-v20-的关系)，它改用 Opus 编解码器。
> v1.0 适合**零依赖、小体积、嵌入式**场景。

---

## 项目结构

| 工具 | 说明 |
|---|---|
| `smp.exe` | 图形化播放器（MP3 / MP4 / MKV / AVI / WAV / MSM） |
| `msmcli.exe` | 命令行工具（MP3 → MSM / MSM → WAV / 播放 MSM） |

**说明：**

- 两个工具都是**单文件可执行程序**，只依赖 Windows 系统 DLL，无需安装。
- `smp.exe` 的 MP4 / MKV / AVI 播放依赖系统 Media Foundation 解码器。
- MKV/AVI 也通过 Media Foundation 播放，能否播放取决于系统是否装了对应解码器。

---

## 源代码文件结构

```
SRC/
├── LICENSE
├── README.md
├── minimp3.h            # MP3 解码器（CC0 1.0）
├── minimsm.h            # MSM v1.0 编解码库（MIT，纯 ADPCM）
├── main.c               # 命令行工具源码（MIT）
├── smp.c                # 图形播放器源码（MIT）
├── build-MSM.bat        # 编译 msmcli.exe
└── build-SMP.bat        # 编译 smp.exe
```

**说明：** v1.0 **不依赖任何第三方库**，也不需要 `libopus.a` 之类的静态库。

---

## 许可证

**MIT License** —— 可自由使用、修改、分发、商用、闭源衍生，
只需保留版权声明。详见 `LICENSE`。

**依赖：**

- minimp3（CC0 1.0，公共领域）
- Media Foundation / XAudio2（Windows 系统组件）

`SRC/LICENSE` 是项目主体（MIT）的许可证文件。

---

## 编译

需要 MinGW-w64 或 MSYS2 + gcc。**无需安装任何第三方库。**

**编译（在 `SRC/` 目录下执行）：**

```cmd
build-MSM.bat    :: 编译命令行工具，产物 msmcli.exe
build-SMP.bat    :: 编译图形播放器，产物 smp.exe
```

**编译产物：**

| 脚本 | 产物 | 大小（参考） |
|---|---|---|
| `build-MSM.bat` | `msmcli.exe` | 约 200 ~ 500 KB |
| `build-SMP.bat` | `smp.exe`    | 约 500 KB ~ 1 MB |

### `build-MSM.bat` 内容

```bat
@echo off
cd /d %~dp0
gcc main.c -o msmcli.exe -lwinmm -O2 -s && echo OK
pause
```

### `build-SMP.bat` 内容

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

**说明：**

- `cd /d %~dp0` 让脚本无论从哪里双击都能切到自身所在目录，
  保证 `minimsm.h` / `minimp3.h` 能被找到。
- `build-MSM.bat` 只需 `-lwinmm`（播放用 Windows `waveOut`）。
- `build-SMP.bat` 额外使用 `-municode -mwindows` 以及
  Media Foundation、XAudio2、GDI 等系统库。

---

## 系统要求

| 项目 | 要求 |
|---|---|
| 操作系统 | Windows 7 SP1 及以上（32/64 位） |
| 运行时依赖 | 无（单文件，只依赖 Windows 系统 DLL） |
| 编译依赖 | gcc、MinGW-w64 或 MSYS2 |

**说明：**

- `msmcli.exe` 播放功能使用 Windows `waveOut` API，
  所有 Windows 版本（含 XP）都支持。
- `smp.exe` 使用 Media Foundation（Vista 起可用）与 XAudio2，
  因此**不支持 Windows XP**。

### 播放器已知限制（`smp.exe`）

- **长文件播放内存增长**：音频缓冲在播放期间保留，
  数小时的文件可能占用数百 MB。
- **MP4 / MKV / AVI 依赖 Media Foundation**：能否播放取决于系统解码器。

### 命令行工具已知限制（`msmcli.exe`）

- `mp3` 和 `wav` 子命令使用**窄字符 `fopen`**，
  Windows 下中文路径可能失败。
  建议使用纯 ASCII 路径，或改用 `play` 子命令（支持中文路径）。
- `play` 子命令使用 Windows `waveOut`，**一次性提交整个 PCM 缓冲**。
  长文件播放期间按 Ctrl+C 可能留下残音。

---

## MSM 格式规范

以下是 `minimsm.h` 中定义的 MSM v1.0 格式核心信息，供实现者参考。

### 文件头（30 字节）

所有多字节字段均为**小端序**。结构体按 `#pragma pack(push, 1)` 紧密排列，
无填充字节。字段合计 30 字节。

| 偏移 | 大小 | 字段 | 类型 | 说明 |
|------|------|------|------|------|
| 0 | 4 | `magic` | `char[4]` | `"MSM1"`（0x4D 0x53 0x4D 0x31） |
| 4 | 2 | `version` | `uint16_t` | 格式版本，`0x0100` |
| 6 | 2 | `flags` | `uint16_t` | 压缩标志，`0x5A53`（ASCII "SZ"） |
| 8 | 2 | `channels` | `uint16_t` | 声道数（1 或 2） |
| 10 | 4 | `sample_rate` | `uint32_t` | 采样率（Hz） |
| 14 | 4 | `frame_count` | `uint32_t` | 帧总数 |
| 18 | 4 | `pcm_samples` | `uint32_t` | 每声道总样本数 |
| 22 | 4 | `data_offset` | `uint32_t` | 帧数据起始偏移（= 30） |
| 26 | 4 | `reserved` | `uint16_t[2]` | 保留，必须为 0 |

**加载校验规则（强制）：**

- 前 4 字节必须等于 `"MSM1"`，否则拒绝
- `flags` 必须等于 `0x5A53`，否则拒绝
- `data_offset` 必须 ≤ 文件大小，否则拒绝

**加载校验规则（非强制）：**

- `version` 当前未强制校验，但应为 `0x0100`
- `channels` 编码时建议只传 1 或 2；解码时 >2 会被拒绝

### 帧布局

`data_offset` 之后是 `frame_count` 个连续的帧块。

**每个帧块：**

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | `frame_samples` | 本帧每声道样本数（必须是 4 的倍数） |
| 4 | 4 | `ch0_length` | 通道 0 压缩数据长度（含模式字节） |
| 8 | `ch0_length` | `ch0_data` | 通道 0 压缩数据 |
| 8+`ch0_length` | 4 | `ch1_length` | 通道 1 压缩数据长度（立体声时存在） |
| 12+`ch0_length` | `ch1_length` | `ch1_data` | 通道 1 压缩数据 |

**帧跳过行为：** 若 `frame_samples == 0`，整个帧块被跳过
（同时跳过 `ch0_length` / `ch0_data` / `ch1_length` / `ch1_data`），
从下一个帧块边界继续解析。**`minimsm.h` 已正确实现此行为。**

**标准帧大小：**

```
MSM_FRAME_SAMPLES = 18432  (= 1152 × 16)
```

该值使得一个 44.1 / 48 kHz 的 MP3 granule（1152 样本）能完整落入
一个 MSM 帧。若 granule 跨帧边界，剩余样本会作为独立小帧写出。

### 通道数据

每个通道的压缩数据以**模式字节**开头：

| 字节 0 | 说明 |
|--------|------|
| `0x00` | 静音，后续 N 个样本全部为零，无 ADPCM 数据 |
| `0x01` | 4-bit ADPCM |
| `0x02` | 2-bit ADPCM |

字节 1 起是 ADPCM nibble 打包数据。

**打包方式：**

| 模式 | 每字节样本数 | 打包顺序 |
|------|--------------|----------|
| `0x02`（2-bit） | 4 | MSB 优先 |
| `0x01`（4-bit） | 2 | MSB 优先 |

**静音模式（`0x00`）：** 仅 1 字节模式，无后续 ADPCM 数据。
`ch_data` 长度为 1，解码器直接填 N 个 0。

### ADPCM 编解码算法

MSM 使用一阶 ADPCM：

```
predictor  = 当前预测值（初始 0）
step_idx   = 步长表索引（初始 0，范围 0..88）
step       = msm_step_table[step_idx]
diff       = sample - predictor
nibble     = 量化后的残差（0..15 或 0..3）
predictor += (step * diff_table[nibble]) / divisor
predictor  = clamp(predictor, -32768, 32767)
step_idx  += index_table[nibble]
step_idx   = clamp(step_idx, 0, 88)
```

**除法因子：**

| 位深 | divisor |
|------|---------|
| 2-bit | 8 |
| 4-bit | 8 |

### 步长表（89 项）

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

### 索引调整表

| 位深 | 表 |
|------|-----|
| 4-bit | `[-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8]` |
| 2-bit | `[-1, 2, 4, 8]` |

### 差值表

| 位深 | 表 |
|------|-----|
| 4-bit | `[0, 1, 2, 3, 4, 5, 6, 7, -1, -2, -3, -4, -5, -6, -7, -8]` |
| 2-bit | `[0, 1, -1, 2]` |

### 模式选择

编码器按每个通道块的平均绝对幅度选择模式：

```
avg = sum(|sample[i]|) / sample_count

if (avg < 512) → 2-bit 模式（0x02）
else           → 4-bit 模式（0x01）
```

**优化提示：** 静音段（全部为 0）会命中 2-bit 模式，但不会自动
切换到静音模式 `0x00`。当前编码器只在从未调用 `msm_sz_compress`
的场景下才可能产出 `0x00`；正常流程下**不会生成静音模式**。

### 2-bit 量化

编码器按以下规则选 2-bit 码：

| 条件 | 码 | 差值 |
|------|-----|------|
| diff == 0 | 0 | 0 |
| diff > 0 且 diff >= step/4 | 3 | +2 |
| diff > 0 且 diff <  step/4 | 1 | +1 |
| diff < 0 | 2 | -1 |

**注意：** `diff < 0` 时不论幅度大小一律编码为码 2（-1），
负方向无法表达更大的差值。这是差值表 `{0,1,-1,2}`
负方向只有 -1 导致的格式层面不对称，也是 v1.0 音质不如 v2.0 的原因之一。

### 数据率

| 模式 | 位/样本 | 相对 16-bit PCM 压缩比 |
|------|---------|------------------------|
| 2-bit | 2 | 8:1 |
| 4-bit | 4 | 4:1 |

立体声 48 kHz 时：

- 4-bit：48,000 × 2 × 4 = **384 kbps**
- 2-bit：48,000 × 2 × 2 = **192 kbps**

### 帧数上限

```
MSM_MAX_FRAME_COUNT = 4,294,967,295  (UINT32_MAX)
```

按 48 kHz / 18432 样本每帧计算，最长约 **52 年**。

### 错误处理

| 情况 | 处理 |
|------|------|
| magic 不匹配 | 拒绝文件 |
| flags 不是 `0x5A53` | 拒绝文件 |
| `data_offset` > 文件大小 | 拒绝文件 |
| `channels == 0` 或 `> 2` | 拒绝文件 |
| `frame_samples` > `MSM_FRAME_SAMPLES` | 截断到上限 |
| `frame_samples == 0` | 跳过整个帧块 |
| 压缩长度 == 0 | 拒绝（无效） |
| 解压后样本数不匹配 | 拒绝（数据损坏） |
| 输出缓冲溢出 | 停止解码，剩余填零 |

> **说明：** `minimsm.h` 的 `msm_decode` **已经正确实现**
> `frame_samples == 0` 跳过整个帧块（含 `ch0_length` / `ch0_data`）。
> 这一点比 v2.0 做得更规范。

---

## 使用示例

```c
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
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

/* 使用 pcm ...
 *
 * samples 是【总样本数】，即所有声道交织在一起的数量：
 *     总样本数      = samples
 *     每声道样本数  = samples / channels
 *     帧数          = 每声道样本数 / 18432（含末尾 padding）
 */

free(pcm);
msm_close(&dec);
```

**平台说明：**

- `msm_open_w` 是 Windows 专用宽字符接口，仅在 `_WIN32` 下提供。
- 非 Windows 平台使用 `msm_open`（窄字符，UTF-8 需自行转换）。
- Windows 上若路径含非 ASCII 字符，请优先使用 `msm_open_w`。

---

## 命令行工具用法

```cmd
msmcli mp3  <input.mp3> <output.msm>   :: MP3 转 MSM
msmcli wav  <input.msm> <output.wav>   :: MSM 转 WAV
msmcli play <input.msm>                :: 播放 MSM（仅 Windows）
```

### 使用示例

```cmd
:: MP3 转 MSM（无需 kbps 参数）
msmcli.exe mp3 E:\MUSIC\001.mp3 test.msm

:: 转 WAV
msmcli.exe wav test.msm test.wav

:: 播放
msmcli.exe play test.msm
```

**说明：** v1.0 的 `mp3` 子命令**不需要比特率参数**，
编码模式由每个通道块的平均幅度自动选择（2-bit 或 4-bit）。

---

## 图形播放器用法

```cmd
smp.exe                     :: 启动空播放器
smp.exe file1.msm file2.mp3 :: 启动并加载多个文件到播放列表
```

也可直接把文件**拖到窗口**，或点击 **Open** 按钮。

### 快捷键

| 按键 | 功能 |
|---|---|
| `空格` | 播放 / 暂停 |
| `←` / `→` | 后退 / 前进 5 秒 |
| `↑` / `↓` | 音量 +5% / -5% |
| `N` / `P` | 下一首 / 上一首 |
| `Esc` | 退出 |

### 配置文件

`smp.exe` 会在**自身所在目录**生成两个文件：

| 文件 | 内容 |
|---|---|
| `smp.ini` | 窗口位置、大小、音量、上次打开的目录 |
| `smp.log` | 运行时日志（调试用） |

删除这两个文件即可恢复默认设置。

---

## 格式局限

- 2-bit 模式负方向只能编码 -1，无法表达 -2（差值表不对称）
- 帧内样本数必须是 4 的倍数
- 不支持 >2 声道
- 无内建元数据（标题、艺术家等）
- 无独立有效样本数字段，解码后末尾可能带 padding 静音
- 采样率跟随输入，与 v2.0 的固定 48000 Hz 不同

---

## 与 v2.0 的关系

| 对比项 | v1.0（本版本） | v2.0 |
|---|---|---|
| 编解码器 | 一阶 ADPCM | Opus |
| Magic | `"MSM1"` | `"MSM2"` |
| 压缩比 | 4:1 / 8:1 固定 | 5:1 ~ 22:1 |
| 音质 | 一般 | 128 kbps 以上接近透明 |
| 采样率 | 跟随输入 | 固定 48000 Hz |
| 外部依赖 | 无 | libopus（BSD 3-Clause） |
| 单文件大小 | 200 ~ 500 KB | 1.5 ~ 3 MB |
| 嵌入场景 | 适合 | 需要评估 libopus 体积 |

**选择建议：**

- 需要**零依赖、小体积**，能接受中等音质 → 选 **v1.0**
- 需要**高音质、灵活压缩比**，可以接受 libopus 体积 → 选 **v2.0**

两个版本的 MSM **文件格式不兼容**，解码器需要按 magic 分派。

---

## 版本历史

### v1.0 (2026)
- 初始版本
- 一阶 ADPCM，2-bit / 4-bit 双模式
- 单头文件库，30 字节文件头
- 压缩比固定 4:1 / 8:1

### v2.0 (2026)
- 改用 Opus 编解码器
- Magic 改为 `"MSM2"`
- 压缩比 5:1 ~ 22:1（由比特率控制）
- 音质显著提升（128 kbps 以上接近透明）
- 支持 44100 Hz 输入（自动重采样）
- 需要 libopus（BSD 3-Clause）

---

## 联系方式

如有问题或建议，请提交 issue 或联系 SimpleToolsStudio。

---

**End of README.md**
```

---