/*
  Original Xbox Audio Driver for Simple DirectMedia Layer
  (based on SDL_naclaudio.c)
  Copyright (C) 1997-2019 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2020 Jannik Vogel

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL2/src/SDL_internal.h"

#if SDL_AUDIO_DRIVER_XBOX

#include "SDL_xboxaudio.h"

#include <xboxkrnl/xboxkrnl.h>
#include <hal/audio.h>

/* The tag name used by Original Xbox audio */
#define XBOXAUDIO_DRIVER_NAME         "xbox"

#define SAMPLE_FRAME_COUNT 1024

static void
xbox_audio_callback(void *pac97device, void *data)
{
    /* This runs from a DPC, so it can't use the FPU without restoring it */

    struct SDL_PrivateAudioData *audiodata = (struct SDL_PrivateAudioData *) data;
    SDL_SemPost(audiodata->playsem);
    return;
}

static void
XBOXAUDIO_CloseDevice(_THIS)
{
    /* Reset hardware and disable callback */
    XAudioInit(16, 2, NULL, NULL);

    /* Free buffers */
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        if (_this->hidden->buffers[i] != NULL) {
            MmFreeContiguousMemory(_this->hidden->buffers[i]);
        }
    }

    /* Destroy the audio buffer semaphore */
    if (_this->hidden->playsem) {
        SDL_DestroySemaphore(_this->hidden->playsem);
    }

    SDL_free(_this->hidden);

    return;
}

static int
XBOXAUDIO_OpenDevice(_THIS, const char *devname)
{
    _this->hidden = (SDL_PrivateAudioData *) SDL_calloc(1, (sizeof *_this->hidden));
    if (_this->hidden == NULL) {
        return SDL_OutOfMemory();
    }

    _this->spec.freq = 48000;
    _this->spec.format = AUDIO_S16LSB;
    _this->spec.channels = 2;
    _this->spec.samples = SAMPLE_FRAME_COUNT;

    /* Calculate the final parameters for this audio specification */
    SDL_CalculateAudioSpec(&_this->spec);

    /* Create the audio buffer semaphore; we have no buffers ready for queue */
    _this->hidden->playsem = SDL_CreateSemaphore(0);
    if (!_this->hidden->playsem) {
        return SDL_SetError("Open device failed!");
    }

    XAudioInit(16, 2, xbox_audio_callback, (void *)_this->hidden);

    /* Allocate buffers */
    for (int i = 0; i < BUFFER_COUNT; ++i) {
        _this->hidden->buffers[i] = MmAllocateContiguousMemoryEx(_this->spec.size, 0, 0xFFFFFFFF, 0, PAGE_READWRITE | PAGE_WRITECOMBINE);
        if (_this->hidden->buffers[i] == NULL) {
            return SDL_OutOfMemory();
        }

        /* Do not queue the first buffer */
        if (i == 0) {
            continue;
        }

        /* Fill buffer with silence */
        memset(_this->hidden->buffers[i], _this->spec.silence, _this->spec.size);

        /* Send samples to XAudio */
        XAudioProvideSamples(_this->hidden->buffers[i], _this->spec.size, FALSE);
    }

    _this->hidden->next_buffer = 0;

    /* Start audio playback */
    XAudioPlay();

    return 0;
}

static void
XBOXAUDIO_WaitDevice(_THIS)
{
    /* Wait for an audio buffer to be free */
    SDL_SemWait(_this->hidden->playsem);

    return;
}

static Uint8 *
XBOXAUDIO_GetDeviceBuf(_THIS)
{
    return _this->hidden->buffers[_this->hidden->next_buffer];
}

static void
XBOXAUDIO_PlayDevice(_THIS)
{
    /* Send samples to XAudio */
    XAudioProvideSamples(_this->hidden->buffers[_this->hidden->next_buffer], _this->spec.size, FALSE);

    /* Advance to next buffer */
    _this->hidden->next_buffer = (_this->hidden->next_buffer + 1) % BUFFER_COUNT;

    return;
}

static int
XBOXAUDIO_Init(SDL_AudioDriverImpl * impl)
{
    /* Set the function pointers */
    impl->OpenDevice = XBOXAUDIO_OpenDevice;
    impl->CloseDevice = XBOXAUDIO_CloseDevice;
    impl->WaitDevice = XBOXAUDIO_WaitDevice;
    impl->GetDeviceBuf = XBOXAUDIO_GetDeviceBuf;
    impl->PlayDevice = XBOXAUDIO_PlayDevice;
    /*
     *    impl->Deinitialize = XBOXAUDIO_Deinitialize;
     */

    impl->HasCaptureSupport             = 0;        /* TODO */
    impl->OnlyHasDefaultOutputDevice    = 1;

    return 1;
}

AudioBootStrap XBOXAUDIO_bootstrap = {
    XBOXAUDIO_DRIVER_NAME, "Original Xbox audio driver", XBOXAUDIO_Init, 0
};

#endif /* SDL_AUDIO_DRIVER_XBOX */

/* vi: set ts=4 sw=4 expandtab: */
