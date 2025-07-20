/*
  Original Xbox Audio Driver for Simple DirectMedia Layer
  (based on SDL_naclaudio.h)
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

#ifndef SDL_xboxaudio_h_
#define SDL_xboxaudio_h_

#include <SDL_audio.h>
#include "../../SDL2/src/audio/SDL_sysaudio.h"

#define _THIS  SDL_AudioDevice *_this

#define BUFFER_COUNT 2

typedef struct SDL_PrivateAudioData {
    void* buffers[BUFFER_COUNT];
    int next_buffer;
    SDL_sem *playsem;
} SDL_PrivateAudioData;

#endif /* SDL_xboxaudio_h_ */

/* vi: set ts=4 sw=4 expandtab: */
