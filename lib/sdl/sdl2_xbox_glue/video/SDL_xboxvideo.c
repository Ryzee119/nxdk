#include "../../SDL2/src/SDL_internal.h"

#if SDL_VIDEO_DRIVER_XBOX

#include "SDL_xboxvideo.h"

#include <SDL.h>
#include <SDL_video.h>
#include <hal/video.h>
#include <assert.h>

#define XBOXVID_DRIVER_NAME "xbox"
#define XBOX_SURFACE "_SDL_XboxSurface"
#define XBOX_bootstrap DUMMY_bootstrap

/* Currently only one window */
static SDL_Window *xbox_window = NULL;

static int XBOX_CreateWindow(_THIS, SDL_Window *window)
{
    if (xbox_window)
    {
        return SDL_SetError("Xbox only supports one window");
    }

    /* Adjust the window data to match the screen */
    VIDEO_MODE vm = XVideoGetMode();
    window->x = 0;
    window->y = 0;
    window->w = vm.width;
    window->h = vm.height;

    window->flags &= ~SDL_WINDOW_RESIZABLE; /* window is NEVER resizeable */
    window->flags &= ~SDL_WINDOW_HIDDEN;
    window->flags |= SDL_WINDOW_SHOWN; /* only one window on Xbox */
    window->flags |= SDL_WINDOW_FULLSCREEN;

    /* One window, it always has focus */
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);

    xbox_window = window;

    return 0;
}

/* XBOX driver bootstrap functions */

static int XBOX_Available(void)
{
    return 1;
    const char *envr = SDL_getenv("SDL_VIDEODRIVER");
    if ((envr) && (SDL_strcmp(envr, XBOXVID_DRIVER_NAME) == 0))
    {
        return (1);
    }

    return (0);
}

static void XBOX_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static void XBOX_PumpEvents(SDL_VideoDevice *device)
{
    // Do nothing
}

static inline Uint32 pixelFormatSelector(int bpp)
{
    Uint32 ret_val = 0;
    switch (bpp)
    {
    case 15:
        ret_val = SDL_PIXELFORMAT_RGB555;
        break;
    case 16:
        ret_val = SDL_PIXELFORMAT_RGB565;
        break;
    case 32:
        ret_val = SDL_PIXELFORMAT_RGB888;
        break;
    default:
        assert(0);
        break;
    }
    return ret_val;
}

static int SDL_XBOX_CreateWindowFramebuffer(_THIS, SDL_Window *window, Uint32 *format, void **pixels, int *pitch)
{
    SDL_Surface *surface;
    VIDEO_MODE vm = XVideoGetMode();
    const Uint32 surface_format = pixelFormatSelector(vm.bpp);
    int w, h;
    int bpp;
    Uint32 Rmask, Gmask, Bmask, Amask;

    /* Free the old framebuffer surface */
    surface = (SDL_Surface *)SDL_GetWindowData(window, XBOX_SURFACE);
    SDL_FreeSurface(surface);

    /* Create a new one */
    SDL_PixelFormatEnumToMasks(surface_format, &bpp, &Rmask, &Gmask, &Bmask, &Amask);
    SDL_GetWindowSize(window, &w, &h);
    surface = SDL_CreateRGBSurface(0, w, h, bpp, Rmask, Gmask, Bmask, Amask);
    if (!surface)
    {
        return -1;
    }

    /* Save the info and return! */
    SDL_SetWindowData(window, XBOX_SURFACE, surface);
    *format = surface_format;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return 0;
}

static int SDL_XBOX_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_Surface *surface;

    surface = (SDL_Surface *)SDL_GetWindowData(window, XBOX_SURFACE);
    if (!surface)
    {
        return SDL_SetError("Couldn't find Xbox surface for window");
    }

    VIDEO_MODE vm = XVideoGetMode();

    // Get information about SDL window surface
    const void *src = surface->pixels;
    Uint32 src_format = surface->format->format;
    int src_pitch = surface->pitch;

    // Get information about GPU framebuffer
    void *dst = XVideoGetFB();
    Uint32 dst_format = pixelFormatSelector(vm.bpp);
    int dst_bytes_per_pixel = SDL_BYTESPERPIXEL(dst_format);
    int dst_pitch = vm.width * dst_bytes_per_pixel;

    // Check if the SDL window fits into GPU framebuffer
    int width = surface->w;
    int height = surface->h;
    assert(width <= vm.width);
    assert(height <= vm.height);

    // Copy SDL window surface to GPU framebuffer
    SDL_ConvertPixels(width, height, src_format, src, src_pitch, dst_format, dst, dst_pitch);

    // Writeback WC buffers
    XVideoFlushFB();

    return 0;
}

static void SDL_XBOX_DestroyWindowFramebuffer(_THIS, SDL_Window *window)
{
    SDL_Surface *surface;

    surface = (SDL_Surface *)SDL_SetWindowData(window, XBOX_SURFACE, NULL);
    SDL_FreeSurface(surface);
}

static int XBOX_VideoInit(_THIS)
{
    SDL_DisplayMode mode;
    VIDEO_MODE vm = XVideoGetMode();

    /* Select display mode based on Xbox video mode */
    mode.format = pixelFormatSelector(vm.bpp);
    mode.w = vm.width;
    mode.h = vm.height;
    mode.refresh_rate = vm.refresh;
    mode.driverdata = NULL;
    if (SDL_AddBasicVideoDisplay(&mode) < 0)
    {
        return -1;
    }

    SDL_zero(mode);
    SDL_AddDisplayMode(&_this->displays[0], &mode);

    /* We're done! */
    return 0;
}

static int XBOX_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    return 0;
}

static void XBOX_VideoQuit(_THIS)
{
}

static SDL_VideoDevice *XBOX_CreateDevice()
{
    SDL_VideoDevice *device;

    /* Initialize all variables that we clean on shutdown */
    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device)
    {
        SDL_OutOfMemory();
        return (0);
    }

    /* Set the function pointers */
    device->CreateSDLWindow = XBOX_CreateWindow;
    device->VideoInit = XBOX_VideoInit;
    device->VideoQuit = XBOX_VideoQuit;
    device->SetDisplayMode = XBOX_SetDisplayMode;
    device->PumpEvents = XBOX_PumpEvents;
    device->CreateWindowFramebuffer = SDL_XBOX_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = SDL_XBOX_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = SDL_XBOX_DestroyWindowFramebuffer;

    device->free = XBOX_DeleteDevice;

    return device;
}

VideoBootStrap XBOX_bootstrap = {
    XBOXVID_DRIVER_NAME, "SDL XBOX video driver",
    XBOX_CreateDevice, NULL};

#endif
