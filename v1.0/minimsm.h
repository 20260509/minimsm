/*
 * minimsm.h - MSM v1.0 base format library
 *
 * Copyright (c) 2026 SimpleToolsStudio
 * SPDX-License-Identifier: MIT
 */

#ifndef MINIMSM_H
#define MINIMSM_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== Configuration ==================== */

#define MSM_SAMPLE_RATE   48000
#define MSM_CHANNELS      2

/*
 * Samples per channel per MSM frame.
 * Must be a multiple of 4 for ADPCM alignment.
 * 18432 = 1152 * 16, so a single 44.1 kHz stereo MP3 frame (1152 samples)
 * expands to a fraction of one MSM frame.
 */
#define MSM_FRAME_SAMPLES 18432

/*
 * Frame count is stored as uint32_t in the header.
 * The value below is the format-level hard cap; in practice the real
 * limit is available memory (see msm_encode_init).
 */
#define MSM_MAX_FRAME_COUNT UINT32_MAX

/* ==================== File Header ==================== */

#pragma pack(push, 1)
typedef struct {
    char     magic[4];      /* "MSM1" */
    uint16_t version;       /* 0x0100 */
    uint16_t flags;         /* 0x5A53 = "SZ" magic */
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t frame_count;
    uint32_t pcm_samples;   /* samples per channel */
    uint32_t data_offset;
    uint16_t reserved[2];
} msm_header_t;
#pragma pack(pop)

#define MSM_FLAG_SZ  0x5A53

/* ==================== ADPCM Tables ==================== */

static const int16_t msm_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13900,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t msm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static const int8_t msm_index_table_2bit[4] = { -1, 2, 4, 8 };

static const int16_t msm_diff_table_4bit[16] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    -1, -2, -3, -4, -5, -6, -7, -8
};

static const int16_t msm_diff_table_2bit[4] = { 0, 1, -1, 2 };

/* ==================== ADPCM 4-bit Encoder ==================== */

static size_t msm_adpcm_4bit_encode(const int16_t *pcm, size_t n, uint8_t *out)
{
    size_t out_pos = 0;
    size_t in_pos = 0;
    int predictor = 0;
    int step_idx = 0;

    while (in_pos < n) {
        uint8_t byte = 0;

        for (int pass = 0; pass < 2; pass++) {
            int nibble = 0;
            if (in_pos < n) {
                int step = msm_step_table[step_idx];
                int diff = (int)pcm[in_pos++] - predictor;

                if (diff < 0) { nibble = 8; diff = -diff; }
                int tempstep = step;
                if (diff >= tempstep) { nibble |= 4; diff -= tempstep; }
                tempstep >>= 1;
                if (diff >= tempstep) { nibble |= 2; diff -= tempstep; }
                tempstep >>= 1;
                if (diff >= tempstep) { nibble |= 1; }

                int delta = (step * msm_diff_table_4bit[nibble]) / 8;
                predictor += delta;
                if (predictor >  32767) predictor =  32767;
                if (predictor < -32768) predictor = -32768;

                step_idx += msm_index_table[nibble];
                if (step_idx < 0)  step_idx = 0;
                if (step_idx > 88) step_idx = 88;
            }

            byte |= (uint8_t)(nibble << (pass == 0 ? 4 : 0));
        }

        out[out_pos++] = byte;
    }
    return out_pos;
}

/* ==================== ADPCM 2-bit Encoder ==================== */

static size_t msm_adpcm_2bit_encode(const int16_t *pcm, size_t n, uint8_t *out)
{
    size_t out_pos = 0;
    size_t in_pos = 0;
    int predictor = 0;
    int step_idx = 0;

    while (in_pos < n) {
        uint8_t byte = 0;

        for (int shift = 6; shift >= 0; shift -= 2) {
            int nibble = 0;
            if (in_pos < n) {
                int step = msm_step_table[step_idx];
                int diff = (int)pcm[in_pos++] - predictor;

                int q;
                if (diff == 0) {
                    q = 0;
                } else if (diff > 0) {
                    q = (diff >= step / 4) ? 3 : 1;
                } else {
                    q = 2;
                }
                nibble = q;

                int delta = (step * msm_diff_table_2bit[nibble]) / 8;
                predictor += delta;
                if (predictor >  32767) predictor =  32767;
                if (predictor < -32768) predictor = -32768;

                step_idx += msm_index_table_2bit[nibble];
                if (step_idx < 0)  step_idx = 0;
                if (step_idx > 88) step_idx = 88;
            }

            byte |= (uint8_t)(nibble << shift);
        }

        out[out_pos++] = byte;
    }
    return out_pos;
}

/* ==================== ADPCM 4-bit Decoder ==================== */

static size_t msm_adpcm_4bit_decode(const uint8_t *in, size_t in_len,
                                    int16_t *pcm, size_t n)
{
    size_t out_pos = 0;
    size_t in_pos = 0;
    int predictor = 0;
    int step_idx = 0;

    while (out_pos < n && in_pos < in_len) {
        uint8_t byte = in[in_pos++];

        for (int pass = 0; pass < 2; pass++) {
            if (out_pos >= n) break;

            int nibble = (pass == 0) ? ((byte >> 4) & 0x0F) : (byte & 0x0F);
            int step = msm_step_table[step_idx];

            predictor += (step * msm_diff_table_4bit[nibble]) / 8;
            if (predictor >  32767) predictor =  32767;
            if (predictor < -32768) predictor = -32768;
            pcm[out_pos++] = (int16_t)predictor;

            step_idx += msm_index_table[nibble];
            if (step_idx < 0)  step_idx = 0;
            if (step_idx > 88) step_idx = 88;
        }
    }
    return out_pos;
}

/* ==================== ADPCM 2-bit Decoder ==================== */

static size_t msm_adpcm_2bit_decode(const uint8_t *in, size_t in_len,
                                    int16_t *pcm, size_t n)
{
    size_t out_pos = 0;
    size_t in_pos = 0;
    int predictor = 0;
    int step_idx = 0;

    while (out_pos < n && in_pos < in_len) {
        uint8_t byte = in[in_pos++];

        for (int shift = 6; shift >= 0; shift -= 2) {
            if (out_pos >= n) break;

            int nibble = (byte >> shift) & 0x03;
            int step = msm_step_table[step_idx];

            predictor += (step * msm_diff_table_2bit[nibble]) / 8;
            if (predictor >  32767) predictor =  32767;
            if (predictor < -32768) predictor = -32768;
            pcm[out_pos++] = (int16_t)predictor;

            step_idx += msm_index_table_2bit[nibble];
            if (step_idx < 0)  step_idx = 0;
            if (step_idx > 88) step_idx = 88;
        }
    }
    return out_pos;
}

/* ==================== Compression ==================== */
/*
 * 'sample_count' MUST be a multiple of 4. Callers guarantee this.
 * Output layout: [1 byte mode][compressed data...]
 *   mode 0x00 = silence
 *   mode 0x01 = 4-bit ADPCM
 *   mode 0x02 = 2-bit ADPCM
 */
static size_t msm_sz_compress(const int16_t *pcm, size_t sample_count, uint8_t *out)
{
    size_t n = (sample_count / 4) * 4;
    if (n == 0) return 0;

    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        int16_t v = pcm[i];
        if (v < 0) v = (int16_t)-v;
        sum += v;
    }
    int avg = (int)(sum / (int64_t)n);

    if (avg < 512) {
        size_t comp = msm_adpcm_2bit_encode(pcm, n, out + 1);
        out[0] = 0x02;
        return 1 + comp;
    }

    size_t comp = msm_adpcm_4bit_encode(pcm, n, out + 1);
    out[0] = 0x01;
    return 1 + comp;
}

/* ==================== Encoder ==================== */

typedef struct {
    msm_header_t header;
    uint8_t *data;
    size_t   data_size;
    size_t   data_cap;
    int16_t *scratch;
} msm_encoder_t;

static int msm_encode_init(msm_encoder_t *enc, uint16_t channels,
                           uint32_t sample_rate, uint32_t pcm_samples)
{
    memset(enc, 0, sizeof(*enc));

    memcpy(enc->header.magic, "MSM1", 4);
    enc->header.version     = 0x0100;
    enc->header.flags       = MSM_FLAG_SZ;
    enc->header.channels    = channels;
    enc->header.sample_rate = sample_rate;
    enc->header.frame_count = 0;
    enc->header.pcm_samples = pcm_samples;
    enc->header.data_offset = sizeof(msm_header_t);
    enc->header.reserved[0] = 0;
    enc->header.reserved[1] = 0;

    size_t est = (size_t)pcm_samples / 2 * channels + 4 * 1024 * 1024;
    if (est < 1 * 1024 * 1024) est = 1 * 1024 * 1024;
    enc->data_cap = est;

    enc->data = (uint8_t *)malloc(enc->data_cap);
    if (!enc->data) return -1;

    enc->scratch = (int16_t *)malloc(MSM_FRAME_SAMPLES * sizeof(int16_t));
    if (!enc->scratch) {
        free(enc->data);
        enc->data = NULL;
        return -1;
    }

    enc->data_size = 0;
    return 0;
}

static int msm_encode_frame(msm_encoder_t *enc, const int16_t *pcm,
                            size_t frame_samples, uint32_t frame_count)
{
    uint16_t channels = enc->header.channels;

    if (frame_count >= MSM_MAX_FRAME_COUNT) return -1;
    if (frame_samples > MSM_FRAME_SAMPLES) return -5;

    frame_samples = (frame_samples / 4) * 4;
    if (frame_samples == 0) return 0;

    size_t worst_case = 4 + (size_t)channels * (4 + 1 + frame_samples / 2);
    if (enc->data_size + worst_case > enc->data_cap) {
        size_t needed  = enc->data_size + worst_case;
        size_t new_cap = enc->data_cap * 2;
        if (new_cap < needed) new_cap = needed;
        uint8_t *new_data = (uint8_t *)realloc(enc->data, new_cap);
        if (!new_data) return -2;
        enc->data = new_data;
        enc->data_cap = new_cap;
    }

    uint8_t *p = enc->data + enc->data_size;
    p[0] = (uint8_t)( frame_samples        & 0xFF);
    p[1] = (uint8_t)((frame_samples >>  8) & 0xFF);
    p[2] = (uint8_t)((frame_samples >> 16) & 0xFF);
    p[3] = (uint8_t)((frame_samples >> 24) & 0xFF);
    enc->data_size += 4;

    for (int ch = 0; ch < channels; ch++) {
        for (size_t j = 0; j < frame_samples; j++) {
            enc->scratch[j] = pcm[j * channels + ch];
        }

        size_t written = msm_sz_compress(enc->scratch, frame_samples,
                                         enc->data + enc->data_size + 4);
        if (written == 0 && frame_samples > 0) {
            return -4;
        }

        uint32_t total = (uint32_t)written;
        enc->data[enc->data_size]     = (uint8_t)( total        & 0xFF);
        enc->data[enc->data_size + 1] = (uint8_t)((total >>  8) & 0xFF);
        enc->data[enc->data_size + 2] = (uint8_t)((total >> 16) & 0xFF);
        enc->data[enc->data_size + 3] = (uint8_t)((total >> 24) & 0xFF);
        enc->data_size += 4 + written;
    }

    return 0;
}

static int msm_encode_save(msm_encoder_t *enc, const char *path)
{
    enc->header.data_offset = sizeof(msm_header_t);

    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    if (fwrite(&enc->header, 1, sizeof(msm_header_t), fp) != sizeof(msm_header_t)) {
        fclose(fp);
        return -2;
    }
    if (enc->data_size > 0 &&
        fwrite(enc->data, 1, enc->data_size, fp) != enc->data_size) {
        fclose(fp);
        return -2;
    }
    fclose(fp);
    return 0;
}

static void msm_encode_free(msm_encoder_t *enc)
{
    if (enc->data) {
        free(enc->data);
        enc->data = NULL;
    }
    if (enc->scratch) {
        free(enc->scratch);
        enc->scratch = NULL;
    }
}

/* ==================== Decoder ==================== */

typedef struct {
    msm_header_t header;
    uint8_t *data;
    size_t   data_size;
} msm_decoder_t;

/*
 * Narrow-path open (uses fopen). Use msm_open_w for wide-character paths.
 */
static int msm_open(msm_decoder_t *dec, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    if (fread(&dec->header, 1, sizeof(msm_header_t), fp) != sizeof(msm_header_t)) {
        fclose(fp);
        return -2;
    }

    if (memcmp(dec->header.magic, "MSM1", 4) != 0) { fclose(fp); return -3; }
    if (dec->header.flags != MSM_FLAG_SZ)          { fclose(fp); return -6; }

#if defined(_WIN32) && !defined(__MINGW32__)
    _fseeki64(fp, 0, SEEK_END);
    long long size = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_END);
    off_t size = ftello(fp);
    fseeko(fp, 0, SEEK_SET);
#endif

    if (size < 0 || (uint64_t)size < dec->header.data_offset) { fclose(fp); return -7; }

    dec->data_size = (size_t)(size - dec->header.data_offset);
    dec->data = (uint8_t *)malloc(dec->data_size ? dec->data_size : 1);
    if (!dec->data) { fclose(fp); return -4; }

#if defined(_WIN32) && !defined(__MINGW32__)
    _fseeki64(fp, dec->header.data_offset, SEEK_SET);
#else
    fseeko(fp, dec->header.data_offset, SEEK_SET);
#endif

    if (dec->data_size > 0 &&
        fread(dec->data, 1, dec->data_size, fp) != dec->data_size) {
        free(dec->data);
        dec->data = NULL;
        fclose(fp);
        return -5;
    }

    fclose(fp);
    return 0;
}

/*
 * Wide-path open (uses _wfopen). Handles Unicode/Chinese paths correctly.
 * Only available on Windows (MinGW / MSVC both provide _wfopen).
 */
#if defined(_WIN32)
static int msm_open_w(msm_decoder_t *dec, const wchar_t *path)
{
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return -1;

    if (fread(&dec->header, 1, sizeof(msm_header_t), fp) != sizeof(msm_header_t)) {
        fclose(fp);
        return -2;
    }

    if (memcmp(dec->header.magic, "MSM1", 4) != 0) { fclose(fp); return -3; }
    if (dec->header.flags != MSM_FLAG_SZ)          { fclose(fp); return -6; }

    _fseeki64(fp, 0, SEEK_END);
    long long size = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);

    if (size < 0 || (uint64_t)size < dec->header.data_offset) { fclose(fp); return -7; }

    dec->data_size = (size_t)(size - dec->header.data_offset);
    dec->data = (uint8_t *)malloc(dec->data_size ? dec->data_size : 1);
    if (!dec->data) { fclose(fp); return -4; }

    _fseeki64(fp, dec->header.data_offset, SEEK_SET);
    if (dec->data_size > 0 &&
        fread(dec->data, 1, dec->data_size, fp) != dec->data_size) {
        free(dec->data);
        dec->data = NULL;
        fclose(fp);
        return -5;
    }

    fclose(fp);
    return 0;
}
#endif /* _WIN32 */

static int msm_decode(msm_decoder_t *dec, int16_t **out_pcm, size_t *out_samples)
{
    /* Sanity check: reject invalid channel counts */
    if (dec->header.channels == 0 || dec->header.channels > 2) return -3;

    if (dec->header.pcm_samples == 0) {
        *out_pcm = NULL;
        *out_samples = 0;
        return 0;
    }

    /* Use 64-bit arithmetic to avoid overflow on 32-bit hosts:
     *   total_samples = pcm_samples * channels
     * then check that total_samples * sizeof(int16_t) fits in size_t. */
    uint64_t ts = (uint64_t)dec->header.pcm_samples * dec->header.channels;
    if (ts > (uint64_t)SIZE_MAX / sizeof(int16_t)) return -4;

    size_t total_samples = (size_t)ts;

    int16_t *pcm = (int16_t *)malloc(total_samples * sizeof(int16_t));
    if (!pcm) return -1;

    int16_t *frame_pcm = (int16_t *)malloc(MSM_FRAME_SAMPLES * sizeof(int16_t));
    if (!frame_pcm) {
        free(pcm);
        return -1;
    }

    size_t data_pos = 0;
    size_t out_pos  = 0;

    for (uint32_t f = 0; f < dec->header.frame_count; f++) {
        if (data_pos + 4 > dec->data_size) break;

        uint32_t frame_samples =
            (uint32_t)dec->data[data_pos] |
            ((uint32_t)dec->data[data_pos + 1] << 8) |
            ((uint32_t)dec->data[data_pos + 2] << 16) |
            ((uint32_t)dec->data[data_pos + 3] << 24);
        data_pos += 4;

        if (frame_samples > MSM_FRAME_SAMPLES) frame_samples = MSM_FRAME_SAMPLES;
        frame_samples = (frame_samples / 4) * 4;

        if (frame_samples == 0) {
            for (int ch = 0; ch < dec->header.channels; ch++) {
                if (data_pos + 4 > dec->data_size) break;
                uint32_t comp_len =
                    (uint32_t)dec->data[data_pos] |
                    ((uint32_t)dec->data[data_pos + 1] << 8) |
                    ((uint32_t)dec->data[data_pos + 2] << 16) |
                    ((uint32_t)dec->data[data_pos + 3] << 24);
                data_pos += 4 + comp_len;
            }
            continue;
        }

        if (out_pos + (size_t)frame_samples * dec->header.channels > total_samples) break;

        for (int ch = 0; ch < dec->header.channels; ch++) {
            if (data_pos + 4 > dec->data_size) break;

            uint32_t comp_len =
                (uint32_t)dec->data[data_pos] |
                ((uint32_t)dec->data[data_pos + 1] << 8) |
                ((uint32_t)dec->data[data_pos + 2] << 16) |
                ((uint32_t)dec->data[data_pos + 3] << 24);
            data_pos += 4;

            if (data_pos + comp_len > dec->data_size) break;
            if (comp_len == 0) { free(frame_pcm); free(pcm); return -2; }

            size_t n = (frame_samples / 4) * 4;
            size_t decompressed = 0;

            uint8_t mode = dec->data[data_pos];
            switch (mode) {
            case 0x00:
                memset(frame_pcm, 0, n * sizeof(int16_t));
                decompressed = n;
                break;
            case 0x02:
                decompressed = msm_adpcm_2bit_decode(dec->data + data_pos + 1,
                                                     comp_len - 1, frame_pcm, n);
                break;
            case 0x01:
                decompressed = msm_adpcm_4bit_decode(dec->data + data_pos + 1,
                                                     comp_len - 1, frame_pcm, n);
                break;
            default:
                break;
            }

            if (decompressed != n) { free(frame_pcm); free(pcm); return -2; }

            for (size_t j = 0; j < n; j++) {
                size_t idx = out_pos + j * dec->header.channels + ch;
                if (idx < total_samples) pcm[idx] = frame_pcm[j];
            }

            data_pos += comp_len;
        }

        out_pos += (size_t)frame_samples * dec->header.channels;
    }

    if (out_pos < total_samples) {
        memset(pcm + out_pos, 0, (total_samples - out_pos) * sizeof(int16_t));
    }

    free(frame_pcm);
    *out_pcm = pcm;
    *out_samples = out_pos;
    return 0;
}

static void msm_close(msm_decoder_t *dec)
{
    if (dec->data) {
        free(dec->data);
        dec->data = NULL;
    }
}

/* ==================== MP3 -> MSM ==================== */
/*
 * Note: caller must #define MINIMP3_IMPLEMENTATION and #include "minimp3.h"
 * BEFORE including this header.
 */
#include "minimp3.h"

static int msm_mp3_to_msm(const char *mp3_path, const char *msm_path)
{
    FILE *fp = fopen(mp3_path, "rb");
    if (!fp) return -1;

#if defined(_WIN32) && !defined(__MINGW32__)
    _fseeki64(fp, 0, SEEK_END);
    long long mp3_size_ll = _ftelli64(fp);
    _fseeki64(fp, 0, SEEK_SET);
#else
    fseeko(fp, 0, SEEK_END);
    off_t mp3_size_ll = ftello(fp);
    fseeko(fp, 0, SEEK_SET);
#endif

    if (mp3_size_ll <= 0) { fclose(fp); return -8; }

    size_t mp3_size = (size_t)mp3_size_ll;
    uint8_t *mp3_data = (uint8_t *)malloc(mp3_size);
    if (!mp3_data) { fclose(fp); return -2; }

    if (fread(mp3_data, 1, mp3_size, fp) != mp3_size) {
        free(mp3_data);
        fclose(fp);
        return -3;
    }
    fclose(fp);

    mp3dec_t mp3d_probe;
    mp3dec_init(&mp3d_probe);
    mp3dec_frame_info_t info;
    int16_t probe_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    int samples = mp3dec_decode_frame(&mp3d_probe, mp3_data,
                                      (int)mp3_size, probe_pcm, &info);
    if (samples <= 0) {
        free(mp3_data);
        return -4;
    }

    uint16_t channels    = (uint16_t)info.channels;
    uint32_t sample_rate = info.hz;

    uint64_t total_samples = 0;
    size_t offset = 0;
    mp3dec_t mp3d_count;
    mp3dec_init(&mp3d_count);
    int16_t count_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    while (offset < mp3_size) {
        mp3dec_frame_info_t info2;
        int n = mp3dec_decode_frame(&mp3d_count, mp3_data + offset,
                                    (int)(mp3_size - offset),
                                    count_pcm, &info2);
        if (n <= 0) break;
        total_samples += (uint64_t)((n / 4) * 4);
        offset += info2.frame_bytes;
    }

    if (total_samples > UINT32_MAX) total_samples = UINT32_MAX;

    msm_encoder_t enc;
    if (msm_encode_init(&enc, channels, sample_rate, (uint32_t)total_samples) != 0) {
        free(mp3_data);
        return -5;
    }

    mp3dec_t mp3d3;
    mp3dec_init(&mp3d3);
    offset = 0;
    uint32_t frame_count = 0;

    int16_t *acc = (int16_t *)malloc(MSM_FRAME_SAMPLES * channels * sizeof(int16_t));
    if (!acc) {
        free(mp3_data);
        msm_encode_free(&enc);
        return -9;
    }
    size_t acc_samples = 0;

    while (offset < mp3_size) {
        mp3dec_frame_info_t info3;
        int16_t granule[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int n = mp3dec_decode_frame(&mp3d3, mp3_data + offset,
                                    (int)(mp3_size - offset),
                                    granule, &info3);
        if (n <= 0) break;
        offset += info3.frame_bytes;

        size_t per_ch = (size_t)n;
        size_t space = MSM_FRAME_SAMPLES - acc_samples;
        size_t take  = per_ch < space ? per_ch : space;

        for (size_t j = 0; j < take; j++) {
            for (int ch = 0; ch < channels; ch++) {
                acc[(acc_samples + j) * channels + ch] =
                    granule[j * channels + ch];
            }
        }
        acc_samples += take;

        size_t rem = per_ch - take;

        if (acc_samples >= MSM_FRAME_SAMPLES) {
            if (frame_count >= MSM_MAX_FRAME_COUNT) {
                fprintf(stderr, "Error: too many frames\n");
                free(acc); free(mp3_data);
                msm_encode_free(&enc);
                return -6;
            }
            size_t aligned = (MSM_FRAME_SAMPLES / 4) * 4;
            if (msm_encode_frame(&enc, acc, aligned, frame_count) != 0) {
                free(acc); free(mp3_data);
                msm_encode_free(&enc);
                return -6;
            }
            frame_count++;
            acc_samples = 0;
        }

        if (rem > 0) {
            if (frame_count >= MSM_MAX_FRAME_COUNT) {
                free(acc); free(mp3_data);
                msm_encode_free(&enc);
                return -6;
            }
            size_t aligned = (rem / 4) * 4;
            if (aligned > 0) {
                if (msm_encode_frame(&enc, granule + take * channels,
                                     aligned, frame_count) != 0) {
                    free(acc); free(mp3_data);
                    msm_encode_free(&enc);
                    return -6;
                }
                frame_count++;
            }
        }
    }

    if (acc_samples > 0) {
        size_t aligned = (acc_samples / 4) * 4;
        if (aligned > 0) {
            if (msm_encode_frame(&enc, acc, aligned, frame_count) != 0) {
                free(acc); free(mp3_data);
                msm_encode_free(&enc);
                return -6;
            }
            frame_count++;
        }
    }

    free(acc);

    enc.header.frame_count = frame_count;

    if (msm_encode_save(&enc, msm_path) != 0) {
        free(mp3_data);
        msm_encode_free(&enc);
        return -7;
    }

    printf("MP3 -> MSM: %u ch, %u Hz, %u frames, %u samples\n",
           channels, sample_rate, frame_count, (uint32_t)total_samples);

    free(mp3_data);
    msm_encode_free(&enc);
    return 0;
}

/* ==================== MSM -> WAV ==================== */

static int msm_to_wav(const char *msm_path, const char *wav_path)
{
    msm_decoder_t dec;
    memset(&dec, 0, sizeof(dec));

    if (msm_open(&dec, msm_path) != 0) {
        fprintf(stderr, "Failed to open %s\n", msm_path);
        return -1;
    }

    int16_t *pcm = NULL;
    size_t samples = 0;
    if (msm_decode(&dec, &pcm, &samples) != 0) {
        fprintf(stderr, "Decode failed\n");
        msm_close(&dec);
        return -2;
    }

    FILE *fp = fopen(wav_path, "wb");
    if (!fp) {
        fprintf(stderr, "Failed to create %s\n", wav_path);
        free(pcm);
        msm_close(&dec);
        return -3;
    }

    size_t data_bytes = samples * sizeof(int16_t);
    if (data_bytes > UINT32_MAX - 36) {
        fprintf(stderr, "Error: audio too large for standard WAV\n");
        fclose(fp);
        free(pcm);
        msm_close(&dec);
        return -4;
    }

    uint32_t byte_rate   = dec.header.sample_rate * dec.header.channels * 2;
    uint16_t block_align = (uint16_t)(dec.header.channels * 2);

    fwrite("RIFF", 1, 4, fp);
    uint32_t riff_size = 36 + (uint32_t)data_bytes;
    fwrite(&riff_size, 1, 4, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 1, 4, fp);
    uint16_t audio_format = 1;
    fwrite(&audio_format, 1, 2, fp);
    fwrite(&dec.header.channels, 1, 2, fp);
    fwrite(&dec.header.sample_rate, 1, 4, fp);
    fwrite(&byte_rate, 1, 4, fp);
    fwrite(&block_align, 1, 2, fp);
    uint16_t bits = 16;
    fwrite(&bits, 1, 2, fp);
    fwrite("data", 1, 4, fp);
    uint32_t data_bytes32 = (uint32_t)data_bytes;
    fwrite(&data_bytes32, 1, 4, fp);
    fwrite(pcm, 1, data_bytes, fp);

    fclose(fp);
    printf("MSM -> WAV: %zu samples, %u Hz, %u ch\n",
           samples, dec.header.sample_rate, dec.header.channels);

    free(pcm);
    msm_close(&dec);
    return 0;
}

/* ==================== MSM Direct Playback (Windows) ==================== */

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

typedef struct {
    HWAVEOUT  hWaveOut;
    WAVEHDR   waveHdr;
    int16_t  *pcm;
    size_t    samples;
    volatile LONG is_playing;
} msm_player_t;

static void CALLBACK msm_wave_out_cb(HWAVEOUT hwo, UINT uMsg, DWORD_PTR dwInst,
                                     DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    (void)hwo; (void)dwParam1; (void)dwParam2;
    if (uMsg == WOM_DONE) {
        msm_player_t *player = (msm_player_t *)dwInst;
        InterlockedExchange(&player->is_playing, 0);
    }
}

static int msm_play(const char *msm_path)
{
    msm_decoder_t dec;
    memset(&dec, 0, sizeof(dec));

    if (msm_open(&dec, msm_path) != 0) {
        fprintf(stderr, "Failed to open %s\n", msm_path);
        return -1;
    }

    int16_t *pcm = NULL;
    size_t samples = 0;
    if (msm_decode(&dec, &pcm, &samples) != 0) {
        fprintf(stderr, "Decode failed\n");
        msm_close(&dec);
        return -2;
    }

    printf("Playing: %s\n", msm_path);
    printf("Format: %u Hz, %u ch, %zu samples\n",
           dec.header.sample_rate, dec.header.channels, samples);

    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag      = WAVE_FORMAT_PCM;
    wfx.nChannels       = dec.header.channels;
    wfx.nSamplesPerSec  = dec.header.sample_rate;
    wfx.wBitsPerSample  = 16;
    wfx.nBlockAlign     = (WORD)(wfx.nChannels * wfx.wBitsPerSample / 8);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    msm_player_t player;
    memset(&player, 0, sizeof(player));
    player.pcm = pcm;
    player.samples = samples;

    if (waveOutOpen(&player.hWaveOut, WAVE_MAPPER, &wfx,
                    (DWORD_PTR)msm_wave_out_cb, (DWORD_PTR)&player,
                    CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to open wave device\n");
        free(pcm);
        msm_close(&dec);
        return -3;
    }

    player.waveHdr.lpData         = (LPSTR)pcm;
    player.waveHdr.dwBufferLength = (DWORD)(samples * sizeof(int16_t));
    player.waveHdr.dwFlags        = 0;

    if (waveOutPrepareHeader(player.hWaveOut, &player.waveHdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to prepare header\n");
        waveOutClose(player.hWaveOut);
        free(pcm);
        msm_close(&dec);
        return -4;
    }

    InterlockedExchange(&player.is_playing, 1);
    if (waveOutWrite(player.hWaveOut, &player.waveHdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
        fprintf(stderr, "Failed to start playback\n");
        waveOutUnprepareHeader(player.hWaveOut, &player.waveHdr, sizeof(WAVEHDR));
        waveOutClose(player.hWaveOut);
        free(pcm);
        msm_close(&dec);
        return -5;
    }

    printf("Playing... (Ctrl+C to stop)\n");
    while (InterlockedCompareExchange(&player.is_playing, 1, 1)) {
        Sleep(10);
    }

    waveOutUnprepareHeader(player.hWaveOut, &player.waveHdr, sizeof(WAVEHDR));
    waveOutClose(player.hWaveOut);

    printf("Playback finished\n");
    free(pcm);
    msm_close(&dec);
    return 0;
}
#endif /* _WIN32 */

#ifdef __cplusplus
}
#endif

#endif /* MINIMSM_H */