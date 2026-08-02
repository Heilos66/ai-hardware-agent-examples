/**
 * @file convai_audio_uplink.c
 * @brief Uplink audio pipeline: capture → stereo-to-planar → G.711A encode → send.
 *
 * Owns the recording thread, audio hardware handle, and uplink statistics.
 * Extracted from convai_bridge.c — behavior is bit-for-bit identical.
 */
#include "convai_audio_internal.h"
#include "convai_audio_dump.h"
#include "convai_codec_g711a.h"
#include "audio_service.h"
#include "goldie_osal.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---- Audio source state ---- */
typedef struct {
    void         *audio_service;       /* AudioService* */
    int           sample_rate;
    int           channels;
    int           bits_per_sample;
    int           running;             /* flag to stop recording thread */
    void         *thread_handle;       /* goldie thread handle */
    goldie_sem    exit_sem;            /* semaphore for graceful exit */
    unsigned int  frames_sent;         /* mic frames successfully enqueued */
    unsigned int  frames_dropped;      /* mic frames dropped (send failed) */
} audio_source_t;

static audio_source_t g_audio_src = {0};

#define AUDIO_DUMP_PATH     "audio_dump.wav"
#define AUDIO_RECORD_BUF_SIZE  640   /* 40ms @ 8kHz mono 16bit = 640 bytes (double-buffered) */

/* ---- Shared audio HW accessor (downlink module reads this) ---- */
const audio_hw_info_t *bridge_get_audio_hw(void)
{
    /* audio_hw_info_t has identical layout to the fields we care about.
     * We cast the first 4 fields which are guaranteed to match. */
    return (const audio_hw_info_t *)&g_audio_src;
}

static int audio_record_thread(void *arg)
{
    (void)arg;
    audio_source_t *s = &g_audio_src;
    AudioService *audio = (AudioService *)s->audio_service;

    if (!audio || !audio->audio_read) {
        printf("[convai_bridge] ERROR: no audio_read available\n");
        return -1;
    }

    /* Static capture buffers — the record thread is a single instance (guarded by
     * g_audio_src.running), so static is safe and avoids malloc/free churn on
     * every start/stop (3×640B), which fragments the small WS63 heap. */
    static uint8_t buf[AUDIO_RECORD_BUF_SIZE];
    static uint8_t planar_buf[AUDIO_RECORD_BUF_SIZE];
    static uint8_t g711_buf[AUDIO_RECORD_BUF_SIZE];

    /* Start audio capture */
    if (audio->record_start) audio->record_start();
    printf("[convai_bridge] audio recording started (sr=%d)\n", s->sample_rate);

    while (s->running) {
        int len = audio->audio_read(buf, AUDIO_RECORD_BUF_SIZE);
        if (len > 0) {
            /* Write mono PCM to debug dump file (desktop only) */
            bridge_dump_write(buf, (size_t)len);
            /*
             * GoldieOS audio hardware provides stereo interleaved PCM (L/R).
             * We need planar format [L0, L1, ...] [R0, R1, ...] for cloud AEC.
             * Both channels are silent frames.
             */
            int sample_count = len / (int)sizeof(short);   /* total 16-bit samples */
            int frame_count = sample_count / 2;            /* stereo frames (L/R pairs) */
            int16_t *samples = (int16_t *)buf;

            /* Rearrange to planar format: [L0, L1, ... R0, R1, ...] */
            int16_t *planar_samples = (int16_t *)planar_buf;
            for (int i = 0; i < frame_count; i++) {
                /* Left channel = mic data */
                planar_samples[i] = samples[i * 2];
                /* Right channel = silent (zero) for cloud AEC */
#ifdef PLATFORM_TYPE_WS63
                planar_samples[frame_count + i] = samples[i * 2 + 1];
#else
                planar_samples[frame_count + i] = 0;
#endif
            }

            /* Encode planar stereo PCM → G.711A before sending to SDK */
            size_t  g711_len = 0;
            int enc_ret = convai_g711a_encode(planar_buf, (size_t)len, 2,
                                              g711_buf, AUDIO_RECORD_BUF_SIZE,
                                              &g711_len);
            if (enc_ret != 0 || g711_len == 0) {
                printf("[convai_bridge] WARNING: g711 encode failed\n");
            } else {
                convai_audio_frame_info_t info;
                memset(&info, 0, sizeof(info));
                info.data_type = CONVAI_AUDIO_DATA_TYPE_G711A;

                int send_ret = bridge_uplink_send(g711_buf, g711_len, &info);
                if (send_ret == 0) {
                    s->frames_sent++;
                } else {
                    s->frames_dropped++;
                }
            }
        } else {
            goldie_msleep(10);
        }
    }

    printf("[convai_bridge] audio recording thread stopped\n");
    goldie_sem_post(&s->exit_sem);

    return 0;
}

/* ---- Module entry points ---- */

void bridge_uplink_set_audio_source(void *src, int sr, int ch, int bits)
{
    if (!src) {
        printf("[convai_bridge] audio source cleared\n");
        memset(&g_audio_src, 0, sizeof(g_audio_src));
        return;
    }
    g_audio_src.audio_service   = src;
    g_audio_src.sample_rate     = sr;
    g_audio_src.channels        = ch;
    g_audio_src.bits_per_sample = bits;
    printf("[convai_bridge] audio source set: sr=%d ch=%d bits=%d\n", sr, ch, bits);
}

void bridge_uplink_start(void)
{
    if (!g_audio_src.audio_service) {
        printf("[convai_bridge] no audio source set, skipping recording\n");
        return;
    }
    if (g_audio_src.running) return;

    /* Open debug dump file (desktop only, no-op on embedded) */
    int dump_ret = bridge_dump_open(AUDIO_DUMP_PATH,
                                     g_audio_src.sample_rate ? g_audio_src.sample_rate : 8000,
                                     g_audio_src.channels ? g_audio_src.channels : 1,
                                     g_audio_src.bits_per_sample ? g_audio_src.bits_per_sample : 16);
    if (dump_ret == 0) {
        printf("[convai_bridge] audio dump file opened: %s\n", AUDIO_DUMP_PATH);
    } else {
        printf("[convai_bridge] WARNING: cannot open dump file %s\n", AUDIO_DUMP_PATH);
    }

    /* Init exit semaphore */
    goldie_sem_init(&g_audio_src.exit_sem);

    /* Set running=1 BEFORE creating the thread — the thread checks s->running
     * at loop entry, so if we set it after thread_create the thread could run
     * with running==0 and exit immediately (silent mic failure). */
    g_audio_src.running = 1;

    goldie_thread_lock();
    g_audio_src.thread_handle = goldie_thread_create(
        audio_record_thread, NULL, "convai_audio", 0x2000);
    if (g_audio_src.thread_handle) {
        goldie_thread_set_priority(g_audio_src.thread_handle, 22);
        printf("[convai_bridge] AUTO mode: recording started\n");
    } else {
        g_audio_src.running = 0;
        goldie_sem_destroy(&g_audio_src.exit_sem);
        printf("[convai_bridge] ERROR: audio record thread create failed — mic input disabled\n");
    }
    goldie_thread_unlock();
}

void bridge_uplink_stop(void)
{
    if (!g_audio_src.running) return;
    g_audio_src.running = 0;
    if (g_audio_src.thread_handle) {
        goldie_sem_wait(&g_audio_src.exit_sem);
        goldie_thread_destroy(g_audio_src.thread_handle);
        g_audio_src.thread_handle = NULL;
        goldie_sem_destroy(&g_audio_src.exit_sem);
    }
    /* Stop audio capture */
    AudioService *audio = (AudioService *)g_audio_src.audio_service;
    if (audio && audio->record_stop) audio->record_stop();

    /* Finalize debug dump file (desktop only, no-op on embedded) */
    bridge_dump_close();

    printf("[convai_bridge] audio recording stopped\n");
}

int bridge_uplink_send(const uint8_t *data, size_t len,
                       const convai_audio_frame_info_t *info)
{
    if (!bridge_get_engine() || !bridge_is_started()) return -1;
    return convai_send_audio(bridge_get_engine(), data, len, info);
}

int bridge_uplink_get_stats(unsigned int *sent, unsigned int *dropped)
{
    *sent    = g_audio_src.frames_sent;
    *dropped = g_audio_src.frames_dropped;
    return 0;
}
