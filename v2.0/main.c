#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "minimsm.h"

static void usage(const char *prog)
{
    printf("Usage:\n");
    printf("  %s mp3  <input.mp3> <output.msm> <kbps>  - MP3 to MSM (Opus)\n", prog);
    printf("  %s play <input.msm>                      - Play MSM (Windows)\n", prog);
    printf("  %s wav  <input.msm> <output.wav>         - MSM to WAV\n", prog);
    printf("\n");
    printf("  kbps: bitrate in kbps (required for 'mp3' command)\n");
    printf("        64  (small, ~22:1)\n");
    printf("        96  (balanced, ~15:1)\n");
    printf("        128 (recommended, ~11:1)\n");
    printf("        192 (high quality, ~7:1)\n");
    printf("        256 (near transparent, ~5.5:1)\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) { usage(argv[0]); return 1; }

    if (strcmp(argv[1], "mp3") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s mp3 <input.mp3> <output.msm> <kbps>\n", argv[0]);
            return 1;
        }
        int kbps = atoi(argv[4]);
        if (kbps <= 0) {
            fprintf(stderr, "Invalid bitrate: %s\n", argv[4]);
            return 1;
        }
        int ret = msm_mp3_to_msm(argv[2], argv[3], kbps);
        if (ret != 0) {
            fprintf(stderr, "MP3 -> MSM failed: %d\n", ret);
            return 1;
        }
        printf("Done!\n");
        return 0;
    }

    if (strcmp(argv[1], "play") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Usage: %s play <input.msm>\n", argv[0]);
            return 1;
        }
#ifdef _WIN32
        int ret = msm_play(argv[2]);
        if (ret != 0) {
            fprintf(stderr, "Play failed: %d\n", ret);
            return 1;
        }
#else
        fprintf(stderr, "Direct playback only supported on Windows\n");
        return 1;
#endif
        return 0;
    }

    if (strcmp(argv[1], "wav") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s wav <input.msm> <output.wav>\n", argv[0]);
            return 1;
        }
        int ret = msm_to_wav(argv[2], argv[3]);
        if (ret != 0) {
            fprintf(stderr, "MSM -> WAV failed: %d\n", ret);
            return 1;
        }
        printf("Done!\n");
        return 0;
    }

    usage(argv[0]);
    return 1;
}