#ifndef SDL_config_og_xbox_h_
#define SDL_config_og_xbox_h_

#include <windows.h>
#include <stdlib.h>
#include "SDL_platform.h"

// Prevent inclusion of unwanted Windows calls
#undef __WIN32__
#undef _MSC_VER
#undef 

#ifndef NXDK_FORCE_MMX
#undef __MMX__
#endif

#undef __SSE2__
#undef __SSE3__

#define HAVE_STDARG_H 1
#define HAVE_STDDEF_H 1
#define HAVE_LIBC 1
#define HAVE_MALLOC 1
#define HAVE_CALLOC 1
#define HAVE_REALLOC 1
#define HAVE_FREE 1
#define HAVE_GCC_SYNC_LOCK_TEST_AND_SET 1
#define HAVE__EXIT 1

#define LACKS_SYS_MMAN_H 1
#define LACKS_FCNTL_H 1
#define LACKS_SYS_PARAM_H 1
#define DYNAPI_NEEDS_DLOPEN 1

#ifndef TEXT
#define TEXT(x) x
#endif
#ifndef MINGW32_FORCEALIGN
#define MINGW32_FORCEALIGN
#endif
#ifndef WIN_UTF8ToStringW
#define WIN_UTF8ToStringW(S) (WCHAR *)SDL_iconv_string("UTF-16LE", "UTF-8", (const char *)(S), SDL_strlen(S) + 1)
#endif
int WIN_SetError(const char *prefix);
HMODULE GetModuleHandle(LPCSTR lpModuleName);

/* Enable the dummy audio driver (src/audio/dummy/\*.c) */
//#define SDL_AUDIO_DRIVER_DUMMY 1
#define SDL_AUDIO_DRIVER_XBOX 1

/* Enable the stub joystick driver (src/joystick/dummy/\*.c) */
#define SDL_JOYSTICK_DISABLED 1

/* Enable the stub haptic driver (src/haptic/dummy/\*.c) */
#define SDL_HAPTIC_DISABLED 1

/* Enable the stub HIDAPI */
#define SDL_HIDAPI_DISABLED 1

/* Enable the stub sensor driver (src/sensor/dummy/\*.c) */
#define SDL_SENSOR_DISABLED 1

/* Enable the stub shared object loader (src/loadso/dummy/\*.c) */
#define SDL_LOADSO_DISABLED 1

/* Enable the stub thread support (src/thread/generic/\*.c) */
// #define SDL_THREADS_DISABLED    1
#define SDL_THREAD_GENERIC_COND_SUFFIX 1
#define SDL_THREAD_WINDOWS 1

/* Enable the stub timer support (src/timer/dummy/\*.c) */
// #define SDL_TIMERS_DISABLED 1
#define SDL_TIMER_WINDOWS 1

/* Enable the dummy video driver (src/video/dummy/\*.c) */
#define SDL_VIDEO_DRIVER_DUMMY 1
#define SDL_VIDEO_DRIVER_XBOX 1

/* Enable the dummy filesystem driver (src/filesystem/dummy/\*.c) */
#define SDL_FILESYSTEM_DUMMY 1

#endif /* SDL_config_h_ */
