// main/app_audio.h -- 音频引擎: 方波合成曲目播放 + 2s 录音回放 + 麦克风 VU 电平。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AUD_TRK_BEEP = 0,    // 1kHz 短鸣
    AUD_TRK_SWEEP,       // 200Hz-4kHz 扫频
    AUD_TRK_MELODY,      // 上行琶音
    AUD_TRK_COUNT
} app_audio_track_t;

void app_audio_start(void);                 // 初始化(失败则 busy/ok 恒 false)
bool app_audio_ok(void);
void app_audio_play(app_audio_track_t t);   // 异步播放
void app_audio_stop(void);
bool app_audio_busy(void);
int  app_audio_progress(void);              // 0..100, 空闲 -1

void app_audio_vu_start(void);              // 麦克风电平采样
void app_audio_vu_stop(void);
uint8_t app_audio_vu_level(void);           // 0..100 (RMS)

void app_audio_record_play(void);           // 2s 录音 -> 回放(异步)
bool app_audio_recording(void);
