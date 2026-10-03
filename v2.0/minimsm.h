/*
 * minimsm.h - MSM v2.0 (Opus only)
 *
 * Copyright (c) 2026 SimpleToolsStudio
 * SPDX-License-Identifier: MIT
 *
 * File format:
 *   header (30 bytes, little-endian):
 *     magic[4] = "MSM2"
 *     version  = 0x0200
 *     flags    = 0x5A53
 *     channels
 *     sample_rate (always 48000)
 *     frame_count
 *     pcm_samples (per channel)
 *     data_offset = 30
 *     reserved[2]
 *
 *   frames: for each frame
 *     frame_samples: uint32
 *     channel block:
 *       comp_len: uint32 (includes 1 mode byte)
 *       mode byte: 0x20
 *       opus data:
 *         num_blocks: uint16
 *         for each block: [plen: uint16][opus packet]
 *
 * Opus requires libopus (BSD 3-Clause, free for commercial use).
 * Link with -lopus -DMSM_USE_OPUS.
 * Input at rates other than 8000/12000/16000/24000/48000 Hz is
 * resampled to 48000 Hz.
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

#ifdef MSM_USE_OPUS
#include "opus.h"
#else
#error "minimsm.h requires MSM_USE_OPUS. Define MSM_USE_OPUS and link with -lopus."
#endif

/* ==================== Configuration ==================== */

#define MSM_FRAME_SAMPLES 18432

#define MSM_MAX_FRAME_COUNT UINT32_MAX

/* ==================== File Header ==================== */

#pragma pack(push, 1)
typedef struct {
    char     magic[4];      /* "MSM2" */
    uint16_t version;       /* 0x0200 */
    uint16_t flags;         /* 0x5A53 */
    uint16_t channels;
    uint32_t sample_rate;   /* 48000 */
    uint32_t frame_count;
    uint32_t pcm_samples;   /* samples per channel */
    uint32_t data_offset;
    uint16_t reserved[2];
} msm_header_t;
#pragma pack(pop)

#define MSM_FLAG_SZ  0x5A53
#define MSM_VERSION  0x0200

/* ==================== Opus configuration ==================== */

#define MSM_OPUS_FRAME_SAMPLES  960
#define MSM_OPUS_MAX_PACKET     1500
#define MSM_OPUS_NUM_BLOCKS  ((MSM_FRAME_SAMPLES + MSM_OPUS_FRAME_SAMPLES - 1) / MSM_OPUS_FRAME_SAMPLES)

/* ==================== Resampling ==================== */

static size_t msm_resample(const int16_t *src, size_t src_samples,
                           int channels, int src_rate, int dst_rate,
                           int16_t *dst, size_t dst_cap)
{
    if (src_rate == dst_rate) {
        size_t n = src_samples;
        if (n > dst_cap) n = dst_cap;
        memcpy(dst, src, n * channels * sizeof(int16_t));
        return n;
    }

    if (src_samples == 0) return 0;

    double ratio = (double)dst_rate / (double)src_rate;
    size_t dst_samples = (size_t)((double)src_samples * ratio);
    if (dst_samples > dst_cap) dst_samples = dst_cap;

    for (size_t i = 0; i < dst_samples; i++) {
        double src_pos = (double)i / ratio;
        size_t s0 = (size_t)src_pos;
        size_t s1 = s0 + 1;
        if (s1 >= src_samples) s1 = src_samples - 1;
        double frac = src_pos - (double)s0;

        for (int ch = 0; ch < channels; ch++) {
            double a = (double)src[s0 * channels + ch];
            double b = (double)src[s1 * channels + ch];
            double v = a + (b - a) * frac;
            if (v >  32767.0) v =  32767.0;
            if (v < -32768.0) v = -32768.0;
            dst[i * channels + ch] = (int16_t)v;
        }
    }

    return dst_samples;
}

/* ==================== Opus encode / decode ==================== */

static size_t msm_opus_encode(const int16_t *pcm, size_t n,
                              uint8_t *out, size_t out_cap,
                              int channels, void *enc_state)
{
    OpusEncoder *enc = (OpusEncoder *)enc_state;
    if (!enc) return 0;
    if (n != MSM_FRAME_SAMPLES) return 0;

    int num_blocks = MSM_OPUS_NUM_BLOCKS;
    if (out_cap < 2) return 0;

    out[0] = (uint8_t)(num_blocks & 0xFF);
    out[1] = (uint8_t)((num_blocks >> 8) & 0xFF);
    size_t pos = 2;

    int16_t block[MSM_OPUS_FRAME_SAMPLES * 2];
    uint8_t packet[MSM_OPUS_MAX_PACKET];

    for (int b = 0; b < num_blocks; b++) {
        size_t offset = (size_t)b * MSM_OPUS_FRAME_SAMPLES;
        size_t samples = MSM_FRAME_SAMPLES - offset;
        if (samples > MSM_OPUS_FRAME_SAMPLES) samples = MSM_OPUS_FRAME_SAMPLES;

        if (channels == 1) {
            for (size_t i = 0; i < samples; i++)
                block[i] = pcm[offset + i];
            if (samples < MSM_OPUS_FRAME_SAMPLES)
                memset(block + samples, 0,
                       (MSM_OPUS_FRAME_SAMPLES - samples) * sizeof(int16_t));
        } else {
            for (size_t i = 0; i < samples; i++) {
                block[i * 2 + 0] = pcm[(offset + i) * 2 + 0];
                block[i * 2 + 1] = pcm[(offset + i) * 2 + 1];
            }
            if (samples < MSM_OPUS_FRAME_SAMPLES) {
                memset(block + samples * 2, 0,
                       (MSM_OPUS_FRAME_SAMPLES - samples) * 2 * sizeof(int16_t));
            }
        }

        int len = opus_encode(enc, block, MSM_OPUS_FRAME_SAMPLES,
                              packet, MSM_OPUS_MAX_PACKET);
        if (len < 0) return 0;

        if (pos + 2 + (size_t)len > out_cap) return 0;
        out[pos + 0] = (uint8_t)(len & 0xFF);
        out[pos + 1] = (uint8_t)((len >> 8) & 0xFF);
        memcpy(out + pos + 2, packet, len);
        pos += 2 + len;
    }

    return pos;
}

static size_t msm_opus_decode(const uint8_t *in, size_t in_len,
                              int16_t *pcm, size_t n,
                              int channels, void *dec_state)
{
    OpusDecoder *dec = (OpusDecoder *)dec_state;
    if (!dec) return 0;
    if (in_len < 2) return 0;

    int num_blocks = (int)in[0] | ((int)in[1] << 8);
    size_t pos = 2;
    size_t out_pos = 0;

    int16_t block[MSM_OPUS_FRAME_SAMPLES * 2];

    for (int b = 0; b < num_blocks && out_pos < n; b++) {
        if (pos + 2 > in_len) break;
        int plen = (int)in[pos + 0] | ((int)in[pos + 1] << 8);
        pos += 2;
        if (pos + (size_t)plen > in_len) break;

        int samples = opus_decode(dec, in + pos, plen,
                                  block, MSM_OPUS_FRAME_SAMPLES, 0);
        pos += plen;
        if (samples < 0) break;

        size_t remaining = n - out_pos;
        size_t take = (size_t)samples < remaining ? (size_t)samples : remaining;

        for (size_t i = 0; i < take; i++) {
            for (int c = 0; c < channels; c++) {
                pcm[(out_pos + i) * channels + c] = block[i * channels + c];
            }
        }
        out_pos += take;
    }

    return out_pos;
}

/* ==================== Encoder ==================== */

typedef struct {
    msm_header_t header;
    uint8_t *data;
    size_t   data_size;
    size_t   data_cap;
    int      bitrate_kbps;
    void    *opus_enc;
} msm_encoder_t;

static int msm_encode_init(msm_encoder_t *enc, uint16_t channels,
                           uint32_t sample_rate, uint32_t pcm_samples,
                           int bitrate_kbps)
{
    memset(enc, 0, sizeof(*enc));

    memcpy(enc->header.magic, "MSM2", 4);
    enc->header.version     = MSM_VERSION;
    enc->header.flags       = MSM_FLAG_SZ;
    enc->header.channels    = channels;
    enc->header.sample_rate = sample_rate;
    enc->header.frame_count = 0;
    enc->header.pcm_samples = pcm_samples;
    enc->header.data_offset = sizeof(msm_header_t);
    enc->header.reserved[0] = 0;
    enc->header.reserved[1] = 0;

    enc->bitrate_kbps = bitrate_kbps;

    size_t est = (size_t)pcm_samples * channels / 4 + 4 * 1024 * 1024;
    if (est < 1024 * 1024) est = 1024 * 1024;
    enc->data_cap = est;

    enc->data = (uint8_t *)malloc(enc->data_cap);
    if (!enc->data) return -1;

    int err = OPUS_OK;
    OpusEncoder *op = opus_encoder_create((int)sample_rate, (int)channels,
                                          OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || !op) {
        free(enc->data);
        enc->data = NULL;
        return -1;
    }

    opus_encoder_ctl(op, OPUS_SET_BITRATE(bitrate_kbps * 1000));
    opus_encoder_ctl(op, OPUS_SET_COMPLEXITY(10));
    opus_encoder_ctl(op, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));

    enc->opus_enc = op;
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

    size_t worst_case = 4 + (size_t)channels * (4 + MSM_FRAME_SAMPLES * 2);
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

    if (frame_samples != MSM_FRAME_SAMPLES) return -5;

    enc->data[enc->data_size + 4] = 0x20;

    size_t written = msm_opus_encode(pcm, frame_samples,
                                     enc->data + enc->data_size + 5,
                                     enc->data_cap - enc->data_size - 5,
                                     channels, enc->opus_enc);
    if (written == 0) return -4;

    uint32_t total = (uint32_t)(1 + written);
    enc->data[enc->data_size]     = (uint8_t)( total        & 0xFF);
    enc->data[enc->data_size + 1] = (uint8_t)((total >>  8) & 0xFF);
    enc->data[enc->data_size + 2] = (uint8_t)((total >> 16) & 0xFF);
    enc->data[enc->data_size + 3] = (uint8_t)((total >> 24) & 0xFF);
    enc->data_size += 4 + total;

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
    if (enc->opus_enc) {
        opus_encoder_destroy((OpusEncoder *)enc->opus_enc);
        enc->opus_enc = NULL;
    }
    if (enc->data) {
        free(enc->data);
        enc->data = NULL;
    }
}

/* ==================== Decoder ==================== */

typedef struct {
    msm_header_t     header;
    uint8_t         *data;
    size_t           data_size;
    void            *opus_dec;
} msm_decoder_t;

static int msm_open(msm_decoder_t *dec, const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    if (fread(&dec->header, 1, sizeof(msm_header_t), fp) != sizeof(msm_header_t)) {
        fclose(fp);
        return -2;
    }

    if (memcmp(dec->header.magic, "MSM2", 4) != 0) { fclose(fp); return -3; }
    if (dec->header.flags != MSM_FLAG_SZ)          { fclose(fp); return -6; }
    if (dec->header.version != MSM_VERSION)        { fclose(fp); return -9; }

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

    int err = OPUS_OK;
    OpusDecoder *op = opus_decoder_create((int)dec->header.sample_rate,
                                          (int)dec->header.channels, &err);
    if (err != OPUS_OK || !op) {
        free(dec->data);
        dec->data = NULL;
        return -8;
    }
    dec->opus_dec = op;

    return 0;
}

#if defined(_WIN32)
static int msm_open_w(msm_decoder_t *dec, const wchar_t *path)
{
    FILE *fp = _wfopen(path, L"rb");
    if (!fp) return -1;

    if (fread(&dec->header, 1, sizeof(msm_header_t), fp) != sizeof(msm_header_t)) {
        fclose(fp);
        return -2;
    }

    if (memcmp(dec->header.magic, "MSM2", 4) != 0) { fclose(fp); return -3; }
    if (dec->header.flags != MSM_FLAG_SZ)          { fclose(fp); return -6; }
    if (dec->header.version != MSM_VERSION)        { fclose(fp); return -9; }

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

    int err = OPUS_OK;
    OpusDecoder *op = opus_decoder_create((int)dec->header.sample_rate,
                                          (int)dec->header.channels, &err);
    if (err != OPUS_OK || !op) {
        free(dec->data);
        dec->data = NULL;
        return -8;
    }
    dec->opus_dec = op;

    return 0;
}
#endif

static int msm_decode(msm_decoder_t *dec, int16_t **out_pcm, size_t *out_samples)
{
    size_t channels = dec->header.channels;
    if (channels == 0 || channels > 2) return -3;

    if (dec->header.pcm_samples == 0) {
        *out_pcm = NULL;
        *out_samples = 0;
        return 0;
    }

    uint64_t ts = (uint64_t)dec->header.pcm_samples * channels;
    if (ts > (uint64_t)SIZE_MAX / sizeof(int16_t)) return -4;

    size_t total_samples = (size_t)ts;

    int16_t *pcm = (int16_t *)malloc(total_samples * sizeof(int16_t));
    if (!pcm) return -1;

    int16_t *interleaved = (int16_t *)malloc(
        MSM_FRAME_SAMPLES * channels * sizeof(int16_t));
    if (!interleaved) {
        free(pcm);
        return -1;
    }

    size_t data_pos = 0;
    size_t out_pos  = 0;
    int    err      = 0;

    for (uint32_t f = 0; f < dec->header.frame_count && !err; f++) {
        if (data_pos + 4 > dec->data_size) break;

        uint32_t frame_samples =
            (uint32_t)dec->data[data_pos] |
            ((uint32_t)dec->data[data_pos + 1] << 8) |
            ((uint32_t)dec->data[data_pos + 2] << 16) |
            ((uint32_t)dec->data[data_pos + 3] << 24);
        data_pos += 4;

        if (frame_samples > MSM_FRAME_SAMPLES) frame_samples = MSM_FRAME_SAMPLES;
        frame_samples = (frame_samples / 4) * 4;

        if (frame_samples == 0) continue;

        if (out_pos + (size_t)frame_samples * channels > total_samples) break;

        if (data_pos + 4 > dec->data_size) { err = 1; break; }
        uint32_t comp_len =
            (uint32_t)dec->data[data_pos] |
            ((uint32_t)dec->data[data_pos + 1] << 8) |
            ((uint32_t)dec->data[data_pos + 2] << 16) |
            ((uint32_t)dec->data[data_pos + 3] << 24);
        if (data_pos + 4 + comp_len > dec->data_size) { err = 1; break; }
        if (comp_len == 0) { err = 1; break; }

        uint8_t mode = dec->data[data_pos + 4];
        if (mode != 0x20) { err = 1; break; }

        size_t n = (frame_samples / 4) * 4;
        size_t produced = msm_opus_decode(dec->data + data_pos + 5,
                                          comp_len - 1,
                                          interleaved, n,
                                          (int)channels,
                                          dec->opus_dec);

        if (produced != n) { err = 1; break; }

        for (size_t j = 0; j < n; j++) {
            for (size_t c = 0; c < channels; c++) {
                size_t idx = out_pos + j * channels + c;
                if (idx < total_samples)
                    pcm[idx] = interleaved[j * channels + c];
            }
        }

        data_pos += 4 + comp_len;
        out_pos += (size_t)frame_samples * channels;
    }

    free(interleaved);

    if (err) {
        free(pcm);
        return -2;
    }

    if (out_pos < total_samples) {
        memset(pcm + out_pos, 0, (total_samples - out_pos) * sizeof(int16_t));
    }

    *out_pcm = pcm;
    *out_samples = out_pos;
    return 0;
}

static void msm_close(msm_decoder_t *dec)
{
    if (dec->opus_dec) {
        opus_decoder_destroy((OpusDecoder *)dec->opus_dec);
        dec->opus_dec = NULL;
    }
    if (dec->data) {
        free(dec->data);
        dec->data = NULL;
    }
}

/* ==================== MP3 -> MSM ==================== */

#include "minimp3.h"

static int msm_mp3_to_msm(const char *mp3_path, const char *msm_path,
                          int bitrate_kbps)
{
    if (bitrate_kbps <= 0) return -10;

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

    int need_resample = (sample_rate != 48000);
    uint32_t output_rate = need_resample ? 48000 : sample_rate;

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

    uint64_t total_samples_out = need_resample
        ? (total_samples * output_rate / sample_rate)
        : total_samples;
    if (total_samples_out > UINT32_MAX) total_samples_out = UINT32_MAX;

    msm_encoder_t enc;
    if (msm_encode_init(&enc, channels, output_rate,
                        (uint32_t)total_samples_out, bitrate_kbps) != 0) {
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

    size_t resample_cap = MINIMP3_MAX_SAMPLES_PER_FRAME * 2 + 1024;
    int16_t *resample_buf = (int16_t *)malloc(resample_cap * channels * sizeof(int16_t));
    if (!resample_buf) {
        free(acc); free(mp3_data);
        msm_encode_free(&enc);
        return -9;
    }

    while (offset < mp3_size) {
        mp3dec_frame_info_t info3;
        int16_t granule[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int n = mp3dec_decode_frame(&mp3d3, mp3_data + offset,
                                    (int)(mp3_size - offset),
                                    granule, &info3);
        if (n <= 0) break;
        offset += info3.frame_bytes;

        const int16_t *src = granule;
        size_t per_ch = (size_t)n;

        if (need_resample) {
            size_t out_n = msm_resample(granule, per_ch, channels,
                                        (int)sample_rate, (int)output_rate,
                                        resample_buf, resample_cap);
            src = resample_buf;
            per_ch = out_n;
        }

        size_t consumed = 0;
        while (consumed < per_ch) {
            size_t space = MSM_FRAME_SAMPLES - acc_samples;
            size_t take  = per_ch - consumed;
            if (take > space) take = space;

            for (size_t j = 0; j < take; j++) {
                for (int ch = 0; ch < channels; ch++) {
                    acc[(acc_samples + j) * channels + ch] =
                        src[(consumed + j) * channels + ch];
                }
            }
            acc_samples += take;
            consumed += take;

            if (acc_samples >= MSM_FRAME_SAMPLES) {
                if (frame_count >= MSM_MAX_FRAME_COUNT) {
                    free(resample_buf); free(acc); free(mp3_data);
                    msm_encode_free(&enc);
                    return -6;
                }
                size_t aligned = (MSM_FRAME_SAMPLES / 4) * 4;
                if (msm_encode_frame(&enc, acc, aligned, frame_count) != 0) {
                    free(resample_buf); free(acc); free(mp3_data);
                    msm_encode_free(&enc);
                    return -6;
                }
                frame_count++;
                acc_samples = 0;
            }
        }
    }

    if (acc_samples > 0) {
        /* Pad the last frame with zeros to MSM_FRAME_SAMPLES */
        size_t total = acc_samples * channels;
        size_t target = MSM_FRAME_SAMPLES * channels;
        if (total < target) {
            memset(acc + total, 0, (target - total) * sizeof(int16_t));
        }
        if (msm_encode_frame(&enc, acc, MSM_FRAME_SAMPLES, frame_count) != 0) {
            free(resample_buf); free(acc); free(mp3_data);
            msm_encode_free(&enc);
            return -6;
        }
        frame_count++;
    }

    free(resample_buf);
    free(acc);

    enc.header.frame_count = frame_count;
    enc.header.pcm_samples = frame_count * MSM_FRAME_SAMPLES;

    if (msm_encode_save(&enc, msm_path) != 0) {
        free(mp3_data);
        msm_encode_free(&enc);
        return -7;
    }

    printf("MP3 -> MSM: %u ch, %u Hz -> %u Hz, %u frames, %u samples (target %d kbps)\n",
           channels, sample_rate, output_rate, frame_count,
           (uint32_t)total_samples_out, bitrate_kbps);

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
#endif

#ifdef __cplusplus
}
#endif

#endif /* MINIMSM_H */