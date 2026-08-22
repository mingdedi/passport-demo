// main/app_audio.c -- ES8311 演示引擎: 单音频任务串行处理 播放/录音/VU 采样。
#include "app_audio.h"
#include "bsp_audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <stdlib.h>

static const char *TAG = "app_audio";

#define RATE_PLAY   16000
#define RATE_REC     8000
#define CHUNK       256

enum { REQ_NONE = 0, REQ_PLAY, REQ_REC, REQ_STOP };

static TaskHandle_t s_task;
static volatile uint8_t s_req;               // 请求
static volatile app_audio_track_t s_track;
static volatile bool s_busy, s_stop;
static volatile int s_progress = -1;
static volatile bool s_vu_on;
static volatile uint8_t s_vu;
static volatile bool s_recording;
static bool s_ok;

// 琶音: (音符频率, 时长 ms); 0 = 休止
typedef struct { uint16_t hz; uint16_t ms; } note_t;
static const note_t MELODY[] = {
    {523, 90}, {659, 90}, {784, 90}, {1047, 160},
    {784, 90}, {1047, 300}, {0, 120},
    {523, 90}, {659, 90}, {784, 90}, {1319, 420},
};
#define MELODY_N (sizeof(MELODY) / sizeof(MELODY[0]))

static void write_square_chunk(int16_t *buf, int n, uint32_t *phase, uint32_t phase_step) {
    for (int i = 0; i < n; i++) {
        buf[i] = (*phase < 0x80000000u) ? 6000 : -6000;
        *phase += phase_step;
    }
}

static void play_track(app_audio_track_t t) {
    if (bsp_audio_set_format(RATE_PLAY, 16, 1) != ESP_OK) return;
    bsp_audio_set_volume(80);

    int16_t *buf = malloc(CHUNK * sizeof(int16_t));
    if (!buf) return;

    // 预计算总时长 -> 进度
    uint32_t total_ms = 0;
    switch (t) {
        case AUD_TRK_BEEP:  total_ms = 350; break;
        case AUD_TRK_SWEEP: total_ms = 2000; break;
        case AUD_TRK_MELODY:
            for (size_t i = 0; i < MELODY_N; i++) total_ms += MELODY[i].ms;
            break;
        default: break;
    }

    uint32_t phase = 0;
    uint32_t done_ms = 0;
    TickType_t t0 = xTaskGetTickCount();

    switch (t) {
    case AUD_TRK_BEEP: {
        uint32_t step = (uint32_t)((float)1000 * 4294967296.0f / RATE_PLAY);
        while (!s_stop) {
            write_square_chunk(buf, CHUNK, &phase, step);
            bsp_audio_write(buf, CHUNK * 2);
            done_ms = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
            s_progress = (done_ms >= total_ms) ? 100 : (int)(done_ms * 100 / total_ms);
            if (done_ms >= total_ms) break;
        }
        break;
    }
    case AUD_TRK_SWEEP: {
        float f = 200.0f;
        while (!s_stop) {
            float step = f * 4294967296.0f / RATE_PLAY;
            write_square_chunk(buf, CHUNK, &phase, (uint32_t)step);
            bsp_audio_write(buf, CHUNK * 2);
            f = 200.0f + (2000.0f - 200.0f) * (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS / (float)total_ms;
            done_ms = (xTaskGetTickCount() - t0) * portTICK_PERIOD_MS;
            s_progress = (done_ms >= total_ms) ? 100 : (int)(done_ms * 100 / total_ms);
            if (f > 4000.0f || done_ms >= total_ms + 100) break;
        }
        break;
    }
    case AUD_TRK_MELODY: {
        for (size_t i = 0; i < MELODY_N && !s_stop; i++) {
            uint32_t step = (uint32_t)((float)MELODY[i].hz * 4294967296.0f / RATE_PLAY);
            if (MELODY[i].hz == 0) {
                memset(buf, 0, CHUNK * 2);
                int ms = 0;
                while (ms < MELODY[i].ms && !s_stop) {
                    bsp_audio_write(buf, CHUNK * 2);
                    vTaskDelay(pdMS_TO_TICKS(CHUNK * 1000 / RATE_PLAY));
                    ms += CHUNK * 1000 / RATE_PLAY;
                }
            } else {
                uint32_t left = (uint32_t)MELODY[i].ms * RATE_PLAY / 1000;
                while (left > 0 && !s_stop) {
                    int n = left < CHUNK ? (int)left : CHUNK;
                    write_square_chunk(buf, n, &phase, step);
                    bsp_audio_write(buf, (size_t)n * 2);
                    left -= n;
                }
            }
            done_ms += MELODY[i].ms;
            s_progress = (int)(done_ms * 100 / total_ms);
        }
        break;
    }
    default: break;
    }

    free(buf);
    s_progress = -1;
}

static void record_and_play(void) {
    if (bsp_audio_set_format(RATE_REC, 16, 1) != ESP_OK) return;

    size_t total = (size_t)RATE_REC * 2;                 // 2s @ 8k = 16KB
    int16_t *rec = malloc(total * sizeof(int16_t));
    if (!rec) { ESP_LOGE(TAG, "rec buf alloc fail"); return; }

    s_recording = true;
    size_t got = 0;
    while (got < total && !s_stop) {
        size_t n = (total - got) < CHUNK ? (total - got) : CHUNK;
        if (bsp_audio_read(rec + got, n * 2) != ESP_OK) break;
        got += n;
        s_progress = (int)(got * 50 / total);            // 0..50 录, 51..100 放
    }
    s_recording = false;

    bsp_audio_set_volume(80);
    for (size_t off = 0; off < got && !s_stop; off += CHUNK) {
        size_t n = (got - off) < CHUNK ? (got - off) : CHUNK;
        bsp_audio_write(rec + off, n * 2);
        s_progress = 50 + (int)(off * 50 / total);
    }
    free(rec);
    s_progress = -1;
}

// VU: 采样麦克风 RMS
static void vu_sample(int16_t *tmp) {
    if (bsp_audio_read(tmp, CHUNK * 2) != ESP_OK) { s_vu = 0; return; }
    uint32_t acc = 0;
    for (int i = 0; i < CHUNK; i++) {
        int32_t v = tmp[i];
        acc += (uint32_t)(v * v);
    }
    float rms = sqrtf((float)acc / CHUNK);
    int lvl = (int)(rms * 100.0f / 9000.0f);             // 经验满量程
    s_vu = lvl > 100 ? 100 : (uint8_t)lvl;
}

static void audio_task(void *arg) {
    (void)arg;
    int16_t *vu_buf = malloc(CHUNK * sizeof(int16_t));
    for (;;) {
        if (s_req == REQ_PLAY)       { s_req = REQ_NONE; s_busy = true; s_stop = false; play_track(s_track); s_busy = false; }
        else if (s_req == REQ_REC)   { s_req = REQ_NONE; s_busy = true; s_stop = false; record_and_play(); s_busy = false; }
        else if (s_req == REQ_STOP)  { s_req = REQ_NONE; s_stop = true; }
        else if (s_vu_on && !s_busy && vu_buf) {
            vu_sample(vu_buf);
        } else {
            vTaskDelay(pdMS_TO_TICKS(40));
        }
    }
}

void app_audio_start(void) {
    s_ok = (bsp_audio_init() == ESP_OK);
    if (s_ok && !s_task)
        xTaskCreate(audio_task, "app_audio", 4096, NULL, 4, &s_task);
}

bool app_audio_ok(void)        { return s_ok; }
void app_audio_play(app_audio_track_t t) { if (s_ok && !s_busy) { s_track = t; s_req = REQ_PLAY; } }
void app_audio_stop(void)      { s_req = REQ_STOP; }
bool app_audio_busy(void)      { return s_busy; }
int  app_audio_progress(void)  { return s_progress; }
void app_audio_vu_start(void)  { s_vu_on = true; }
void app_audio_vu_stop(void)   { s_vu_on = false; s_vu = 0; }
uint8_t app_audio_vu_level(void) { return s_vu; }
void app_audio_record_play(void) { if (s_ok && !s_busy) s_req = REQ_REC; }
bool app_audio_recording(void) { return s_recording; }
