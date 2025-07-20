#include <SDL3/SDL.h>
#include <windows.h>
#include "../SDL_build_config.h"

#ifdef SDL_TIMER_NXDK

Uint64 SDL_GetPerformanceFrequency(void)
{
    LARGE_INTEGER frequency;
    const BOOL rc = QueryPerformanceFrequency(&frequency);
    SDL_assert(rc != 0); // this should _never_ fail if you're on XP or later.
    return (Uint64)frequency.QuadPart;
}

Uint64 SDL_GetPerformanceCounter(void)
{
    LARGE_INTEGER counter;
    const BOOL rc = QueryPerformanceCounter(&counter);
    SDL_assert(rc != 0); // this should _never_ fail if you're on XP or later.
    return (Uint64)counter.QuadPart;
}

void SDL_SYS_DelayNS(Uint64 ns)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -(int64_t)ns / 100;
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

#endif