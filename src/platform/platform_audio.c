#include "platform_audio.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

struct platform_audio {
    SDL_AudioDeviceID device_id;
    int actual_rate;
    int actual_channels;
    SDL_AudioFormat actual_format;
    int callback_samples;   /* frames per callback */
    audio_buffer *buf;      /* not owned; lives in the application layer */
    float *mono_tmp;        /* callback-private scratch, capacity = callback_samples + guard */
    bool is_open;
};

static void platform_audio_callback(void *userdata, Uint8 *stream, int len) {
    platform_audio *audio = (platform_audio *)userdata;
    int channels = audio->actual_channels;
    int frames = len / (channels * (int)sizeof(float));
    float *out = (float *)(void *)stream;
    size_t read_count;
    int i;
    int ch;

    if (frames <= 0 || frames > audio->callback_samples) {
        memset(stream, 0, (size_t)len);
        return;
    }

    read_count = audio_buffer_read(audio->buf, audio->mono_tmp, (size_t)frames);

    /* Silence for any unread frames (underrun already counted in buffer). */
    for (i = (int)read_count; i < frames; i++) {
        audio->mono_tmp[i] = 0.0f;
    }

    /* Expand mono to output channels. */
    for (i = 0; i < frames; i++) {
        float s = audio->mono_tmp[i];
        for (ch = 0; ch < channels; ch++) {
            out[i * channels + ch] = s;
        }
    }
}

platform_audio *platform_audio_create(const platform_audio_desc *desc) {
    platform_audio *audio;
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    int cb_samples;

    if (desc == NULL || desc->buffer == NULL) {
        return NULL;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_Log("platform_audio: SDL_InitSubSystem(AUDIO) failed: %s", SDL_GetError());
        return NULL;
    }

    audio = calloc(1, sizeof(*audio));
    if (audio == NULL) {
        return NULL;
    }

    audio->buf = desc->buffer;

    cb_samples = desc->requested_callback_samples > 0 ? desc->requested_callback_samples : 512;

    memset(&desired, 0, sizeof(desired));
    desired.freq     = desc->requested_rate > 0 ? desc->requested_rate : 48000;
    desired.format   = AUDIO_F32SYS;
    desired.channels = (Uint8)(desc->requested_channels > 0 ? desc->requested_channels : 2);
    desired.samples  = (Uint16)cb_samples;
    desired.callback = platform_audio_callback;
    desired.userdata = audio;

    audio->device_id = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    if (audio->device_id == 0) {
        SDL_Log("platform_audio: SDL_OpenAudioDevice failed: %s", SDL_GetError());
        free(audio);
        return NULL;
    }

    audio->actual_rate     = obtained.freq;
    audio->actual_channels = obtained.channels;
    audio->actual_format   = obtained.format;
    audio->callback_samples = obtained.samples;

    /* Allocate mono scratch buffer (add a guard to handle minor SDL overruns). */
    audio->mono_tmp = calloc((size_t)(obtained.samples + 64), sizeof(float));
    if (audio->mono_tmp == NULL) {
        SDL_CloseAudioDevice(audio->device_id);
        free(audio);
        return NULL;
    }

    audio->is_open = true;

    SDL_Log("platform_audio: opened device id=%u rate=%d channels=%d samples=%d",
        (unsigned)audio->device_id,
        audio->actual_rate,
        audio->actual_channels,
        audio->callback_samples);

    return audio;
}

void platform_audio_destroy(platform_audio *audio) {
    if (audio == NULL) {
        return;
    }

    if (audio->device_id != 0) {
        SDL_PauseAudioDevice(audio->device_id, 1);
        SDL_CloseAudioDevice(audio->device_id);
        audio->device_id = 0;
    }

    free(audio->mono_tmp);
    free(audio);
}

void platform_audio_start(platform_audio *audio) {
    if (audio != NULL && audio->device_id != 0) {
        SDL_PauseAudioDevice(audio->device_id, 0);
    }
}

void platform_audio_stop(platform_audio *audio) {
    if (audio != NULL && audio->device_id != 0) {
        SDL_PauseAudioDevice(audio->device_id, 1);
    }
}

int platform_audio_actual_rate(const platform_audio *audio) {
    return audio != NULL ? audio->actual_rate : 0;
}

int platform_audio_actual_channels(const platform_audio *audio) {
    return audio != NULL ? audio->actual_channels : 0;
}

bool platform_audio_is_open(const platform_audio *audio) {
    return audio != NULL && audio->is_open;
}
