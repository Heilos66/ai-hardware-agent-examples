/**
 * @file convai_audio_dump.c
 * @brief WAV file dump implementation (desktop only, no-op on embedded).
 *
 * Writes PCM audio data to a WAV file with proper RIFF headers.
 * Used for debugging audio capture on desktop platforms.
 */
#include "convai_audio_dump.h"

#include <stdio.h>
#include <stdint.h>

#ifndef __EMBEDDED__

static FILE *g_dump_file       = NULL;
static long   g_dump_data_bytes = 0;

/**
 * Write a placeholder WAV header; sizes will be patched on close.
 */
static int dump_wav_header(FILE *f, int sample_rate, int channels, int bits)
{
    /* RIFF header */
    fwrite("RIFF", 1, 4, f);
    uint32_t riff_size = 0;  /* placeholder */
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    /* fmt chunk */
    fwrite("fmt ", 1, 4, f);
    uint32_t fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    uint16_t audio_format = 1; /* PCM */
    fwrite(&audio_format, 2, 1, f);
    uint16_t ch = (uint16_t)channels;
    fwrite(&ch, 2, 1, f);
    uint32_t sr = (uint32_t)sample_rate;
    fwrite(&sr, 4, 1, f);
    uint32_t byte_rate = (uint32_t)(sample_rate * channels * bits / 8);
    fwrite(&byte_rate, 4, 1, f);
    uint16_t block_align = (uint16_t)(channels * bits / 8);
    fwrite(&block_align, 2, 1, f);
    uint16_t bps = (uint16_t)bits;
    fwrite(&bps, 2, 1, f);

    /* data chunk header */
    fwrite("data", 1, 4, f);
    uint32_t data_size = 0;  /* placeholder */
    fwrite(&data_size, 4, 1, f);

    fflush(f);
    return 0;
}

/**
 * Patch the WAV header with actual data size.
 */
static void dump_wav_finalize(FILE *f, long total_data_bytes)
{
    if (!f) return;

    /* RIFF chunk size = 4 + 24 + 8 + data_size = 36 + data_size */
    uint32_t riff_size = (uint32_t)(36 + total_data_bytes);
    fseek(f, 4, SEEK_SET);
    fwrite(&riff_size, 4, 1, f);

    /* data chunk size */
    fseek(f, 40, SEEK_SET);
    uint32_t data_size = (uint32_t)total_data_bytes;
    fwrite(&data_size, 4, 1, f);

    fclose(f);
}

int bridge_dump_open(const char *path, int sample_rate, int channels, int bits)
{
    if (g_dump_file) {
        dump_wav_finalize(g_dump_file, g_dump_data_bytes);
        g_dump_file = NULL;
    }

    g_dump_file = fopen(path, "wb");
    if (!g_dump_file) return -1;

    dump_wav_header(g_dump_file, sample_rate, channels, bits);
    g_dump_data_bytes = 0;
    return 0;
}

int bridge_dump_write(const void *data, size_t len)
{
    if (!g_dump_file) return 0;
    size_t written = fwrite(data, 1, len, g_dump_file);
    g_dump_data_bytes += (long)written;
    return (written == len) ? 0 : -1;
}

int bridge_dump_close(void)
{
    if (!g_dump_file) return 0;

    dump_wav_finalize(g_dump_file, g_dump_data_bytes);
    g_dump_file = NULL;
    g_dump_data_bytes = 0;
    return 0;
}

#else /* __EMBEDDED__ */

int bridge_dump_open(const char *path, int sample_rate, int channels, int bits)
{
    (void)path; (void)sample_rate; (void)channels; (void)bits;
    return -1;  /* not supported on embedded — no filesystem */
}

int bridge_dump_write(const void *data, size_t len)
{
    (void)data; (void)len;
    return 0;
}

int bridge_dump_close(void)
{
    return 0;
}

#endif /* __EMBEDDED__ */
