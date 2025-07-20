#include "SDL_xboxaudio.h"
#include "SDL_internal.h"

#include <hal/audio.h>
#include <xboxkrnl/xboxkrnl.h>

#ifndef SDL_XBOXAUDIO_BUFFER_COUNT
#define SDL_XBOXAUDIO_BUFFER_COUNT 2
#endif

#if (SDL_XBOXAUDIO_BUFFER_COUNT < 2)
#error "SDL_XBOXAUDIO_BUFFER_COUNT must be at least 2"
#endif

typedef struct SDL_PrivateAudioData
{
    void *buffers[SDL_XBOXAUDIO_BUFFER_COUNT];
    int next_buffer;
    KSEMAPHORE playsem; // Use KSEMAPHORE because we need to post from DPC
} SDL_PrivateAudioData;

static void xbox_audio_callback(void *pac97device, void *data)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *)data;
    KeReleaseSemaphore(&audiodata->playsem, IO_SOUND_INCREMENT, 1, FALSE);
    return;
}

static bool XBOXAUDIO_WaitDevice(SDL_AudioDevice *device)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *)device->hidden;
    KeWaitForSingleObject(&audiodata->playsem, Executive, KernelMode, FALSE, NULL);
    return true;
}

static bool XBOXAUDIO_OpenDevice(SDL_AudioDevice *device)
{
    SDL_PrivateAudioData *audio_data = (SDL_PrivateAudioData *)SDL_calloc(1, sizeof(SDL_PrivateAudioData));
    if (audio_data == NULL) {
        SDL_SetError("Failed to allocate audio private data");
        return false;
    }

    device->hidden = (struct SDL_PrivateAudioData *)audio_data;
    device->spec.freq = 48000;
    device->spec.format = SDL_AUDIO_S16;
    device->spec.channels = 2;

    XAudioInit(16, 2, xbox_audio_callback, (void *)device->hidden);

    for (int i = 0; i < SDL_XBOXAUDIO_BUFFER_COUNT; i++) {
        audio_data->buffers[i] = MmAllocateContiguousMemoryEx(SDL_AUDIO_FRAMESIZE(device->spec), 0, 0xFFFFFFFF,
                                                              0, PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (audio_data->buffers[i] == NULL) {
            SDL_SetError("Failed to allocate audio buffer");
            for (int j = 0; j < i; j++) {
                if (audio_data->buffers[j]) {
                    MmFreeContiguousMemory(audio_data->buffers[j]);
                }
            }
            SDL_free(audio_data);
            return false;
        }
        SDL_memset(audio_data->buffers[i], 0, SDL_AUDIO_FRAMESIZE(device->spec));
    }

    KeInitializeSemaphore(&audio_data->playsem, 1, SDL_XBOXAUDIO_BUFFER_COUNT);
    XAudioProvideSamples(audio_data->buffers[0], SDL_AUDIO_FRAMESIZE(device->spec), 0);
    audio_data->next_buffer = 1;

    XAudioPlay();
    return true;
}

static void XBOXAUDIO_CloseDevice(SDL_AudioDevice *device)
{
    SDL_PrivateAudioData *audio_data = (SDL_PrivateAudioData *)device->hidden;
    XAudioInit(16, 2, NULL, NULL);

    for (int i = 0; i < SDL_XBOXAUDIO_BUFFER_COUNT; i++) {
        if (audio_data->buffers[i]) {
            MmFreeContiguousMemory(audio_data->buffers[i]);
        }
    }

    SDL_free(audio_data);
}

static Uint8 *XBOXAUDIO_GetDeviceBuf(SDL_AudioDevice *device, int *buffer_size)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *)device->hidden;
    return (Uint8 *)audiodata->buffers[audiodata->next_buffer];
}

static bool XBOXAUDIO_PlayDevice(SDL_AudioDevice *device, const Uint8 *buffer, int buflen)
{
    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *)device->hidden;
    XAudioProvideSamples((unsigned char *)buffer, buflen, 0);
    audiodata->next_buffer = (audiodata->next_buffer + 1) % SDL_XBOXAUDIO_BUFFER_COUNT;
    return true;
}

static int XBOXAUDIO_RecordDevice(SDL_AudioDevice *device, void *buffer, int buflen)
{
    return -1;
}

static bool XBOXAUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = XBOXAUDIO_OpenDevice;
    impl->CloseDevice = XBOXAUDIO_CloseDevice;
    impl->WaitDevice = XBOXAUDIO_WaitDevice;
    impl->GetDeviceBuf = XBOXAUDIO_GetDeviceBuf;
    impl->WaitRecordingDevice = XBOXAUDIO_WaitDevice;
    impl->PlayDevice = XBOXAUDIO_PlayDevice;
    impl->RecordDevice = XBOXAUDIO_RecordDevice;

    impl->OnlyHasDefaultPlaybackDevice = true;
    impl->HasRecordingSupport = false;

    return true;
}

AudioBootStrap XBOXAUDIO_bootstrap = {
    .name = "nxdk_audio",
    .desc = "SDL nxdk audio driver",
    .init = XBOXAUDIO_Init,
    .demand_only = false,
    .is_preferred = true
};
