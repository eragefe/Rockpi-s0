#include <alsa/asoundlib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define RATE        44100
#define CHANNELS    2
#define PERIOD      1024
#define RING_FRAMES 32768

static int16_t ring_buffer[RING_FRAMES * CHANNELS];
static unsigned int ring_read_pos = 0;
static unsigned int ring_write_pos = 0;
static unsigned int ring_count = 0;

static pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;

static snd_pcm_t *capture;
static snd_pcm_t *playback;

static void *capture_thread(void *arg)
{
    int16_t data[PERIOD * CHANNELS];

    while (1) {
        snd_pcm_sframes_t n = snd_pcm_readi(capture, data, PERIOD);

        if (n == -EAGAIN) {
            usleep(1000);
            continue;
        }

        if (n < 0) {
            fprintf(stderr, "Capture: %s\n", snd_strerror(n));

            if (n == -EIO) {
                usleep(100000);
                snd_pcm_prepare(capture);
            } else {
                snd_pcm_recover(capture, (int)n, 1);
            }
            continue;
        }

        pthread_mutex_lock(&ring_mutex);

        for (snd_pcm_sframes_t i = 0; i < n; i++) {
            if (ring_count == RING_FRAMES) {
                ring_read_pos = (ring_read_pos + 1) % RING_FRAMES;
                ring_count--;
            }

            unsigned int p = ring_write_pos * CHANNELS;
            ring_buffer[p]     = data[i * CHANNELS];
            ring_buffer[p + 1] = data[i * CHANNELS + 1];

            ring_write_pos = (ring_write_pos + 1) % RING_FRAMES;
            ring_count++;
        }

        pthread_mutex_unlock(&ring_mutex);
    }

    return NULL;
}

static void *playback_thread(void *arg)
{
    int16_t data[PERIOD * CHANNELS];

    while (1) {
        memset(data, 0, sizeof(data));

        pthread_mutex_lock(&ring_mutex);

        for (unsigned int i = 0; i < PERIOD && ring_count > 0; i++) {
            unsigned int p = ring_read_pos * CHANNELS;

            data[i * CHANNELS]     = ring_buffer[p];
            data[i * CHANNELS + 1] = ring_buffer[p + 1];

            ring_read_pos = (ring_read_pos + 1) % RING_FRAMES;
            ring_count--;
        }

        pthread_mutex_unlock(&ring_mutex);

        snd_pcm_sframes_t n = snd_pcm_writei(playback, data, PERIOD);

        if (n < 0) {
            fprintf(stderr, "Playback: %s\n", snd_strerror(n));
            snd_pcm_recover(playback, (int)n, 1);
        }
    }

    return NULL;
}

int main(void)
{
    pthread_t cap_thread, play_thread;
    int err;

    err = snd_pcm_open(&capture, "hw:0,0", SND_PCM_STREAM_CAPTURE,
                       SND_PCM_NONBLOCK);
    if (err < 0) {
        fprintf(stderr, "Cannot open capture: %s\n", snd_strerror(err));
        return 1;
    }

    err = snd_pcm_set_params(capture, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             CHANNELS, RATE, 0, 100000);
    if (err < 0) {
        fprintf(stderr, "Capture setup: %s\n", snd_strerror(err));
        return 1;
    }

    snd_pcm_sw_params_t *sw;
    snd_pcm_sw_params_alloca(&sw);

    err = snd_pcm_sw_params_current(capture, sw);
    if (err < 0) {
        fprintf(stderr, "Capture sw params: %s\n", snd_strerror(err));
        return 1;
    }

    err = snd_pcm_sw_params_set_start_threshold(capture, sw, 1);
    if (err < 0) {
        fprintf(stderr, "Capture start threshold: %s\n", snd_strerror(err));
        return 1;
    }

    err = snd_pcm_sw_params(capture, sw);
    if (err < 0) {
        fprintf(stderr, "Capture sw params apply: %s\n", snd_strerror(err));
        return 1;
    }

    err = snd_pcm_open(&playback, "hw:1,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        fprintf(stderr, "Cannot open playback: %s\n", snd_strerror(err));
        return 1;
    }

    err = snd_pcm_set_params(playback, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED,
                             CHANNELS, RATE, 0, 100000);
    if (err < 0) {
        fprintf(stderr, "Playback setup: %s\n", snd_strerror(err));
        return 1;
    }

    printf("SPDIF capture: hw:0,0 — S16_LE, stereo, 44100 Hz\n");
    printf("I2S playback:  hw:1,0 — S16_LE, stereo, 44100 Hz\n");
    printf("RAM ring buffer: %d frames (%.2f seconds)\n",
           RING_FRAMES, (double)RING_FRAMES / RATE);
    printf("No resampling. Playback remains active.\n");
    fflush(stdout);

    pthread_create(&cap_thread, NULL, capture_thread, NULL);
    pthread_create(&play_thread, NULL, playback_thread, NULL);

    pthread_join(cap_thread, NULL);
    pthread_join(play_thread, NULL);

    return 0;
}
