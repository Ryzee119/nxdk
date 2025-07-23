// 10-geometry // Something about triangle strip
// 14-viewport

#include <../src/SDL_internal.h>

#ifdef SDL_VIDEO_RENDER_XGU

#include "xgu/xgux.h"
#include <../src/render/SDL_sysrender.h>
#include <SDL3/SDL_pixels.h>
#include <hal/video.h>
#include <stdint.h>

#define nxdk_RenderDriver GPU_RenderDriver

// Note: To avoid stalls this vertex buffer must be sufficient to hold three frames worth of vertices.
#ifndef VERTEX_BUFFER_SIZE
#define VERTEX_BUFFER_SIZE (256 * 1024)
#endif

#ifndef XGU_VERTEX_ALIGNMENT
#define XGU_VERTEX_ALIGNMENT 32
#endif

#define XGU_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define XGU_MAYBE_UNUSED __attribute__((unused))

typedef struct xgu_texture
{
    int data_width;
    int data_height;
    int tex_width;
    int tex_height;
    int bytes_per_pixel;
    XguTexFormatColor format;
    uint8_t *data;
    uint8_t *data_physical_address;
} xgu_texture_t;

typedef struct xgu_point
{
    float pos[2];
} xgu_point_t;

typedef struct xgu_vertex
{
    float pos[2];
    float color[4];
} xgu_vertex_t;

typedef struct xgu_vertex_texture
{
    float pos[2];
    float color[4];
    float tex[2];
} xgu_vertex_textured_t;

typedef struct xgu_render_data
{
    int texture_shader_active;
    int vsync_enabled;
    const xgu_texture_t *active_texture;
    const xgu_texture_t *active_render_target;
    SDL_Rect viewport;
    SDL_Rect clip_rect;
    SDL_BlendMode active_blend_mode;
    size_t vertex_data_used;
} xgu_render_data_t;

// FIXME, should be in xgu
typedef enum
{
    XGU_TEXTURE_FILTER_NEAREST = NV_PGRAPH_TEXFILTER0_MIN_BOX_LOD0,
    XGU_TEXTURE_FILTER_LINEAR = NV_PGRAPH_TEXFILTER0_MIN_TENT_LOD0,
} XguTexFilter;

// pushbuffer pointer
static uint32_t *p = NULL;

static inline uint32_t npot2pot(uint32_t num)
{
    uint32_t msb;
    __asm__("bsr %1, %0" : "=r"(msb) : "r"(num));

    if ((1 << msb) == num) {
        return num;
    }

    return 1 << (msb + 1);
}

static bool sdl_to_xgu_texture_format(SDL_PixelFormat fmt, int *xgu_format, int *bytes_per_pixel)
{
    switch (fmt) {
    case SDL_PIXELFORMAT_ARGB1555:
        *xgu_format = XGU_TEXTURE_FORMAT_A1R5G5B5;
        *bytes_per_pixel = 2;
        return true;
    case SDL_PIXELFORMAT_RGB565:
        *xgu_format = XGU_TEXTURE_FORMAT_R5G6B5;
        *bytes_per_pixel = 2;
        return true;
    case SDL_PIXELFORMAT_ARGB8888:
        *xgu_format = XGU_TEXTURE_FORMAT_A8R8G8B8;
        *bytes_per_pixel = 4;
        return true;
    case SDL_PIXELFORMAT_XRGB8888:
        *xgu_format = XGU_TEXTURE_FORMAT_X8R8G8B8;
        *bytes_per_pixel = 4;
        return true;
    case SDL_PIXELFORMAT_RGBA8888:
        *xgu_format = XGU_TEXTURE_FORMAT_R8G8B8A8;
        *bytes_per_pixel = 4;
        return true;
    case SDL_PIXELFORMAT_ABGR8888:
        *xgu_format = XGU_TEXTURE_FORMAT_A8B8G8R8;
        *bytes_per_pixel = 4;
        return true;
    case SDL_PIXELFORMAT_BGRA8888:
        *xgu_format = XGU_TEXTURE_FORMAT_B8G8R8A8;
        *bytes_per_pixel = 4;
        return true;
    case SDL_PIXELFORMAT_ARGB4444:
        *xgu_format = XGU_TEXTURE_FORMAT_A4R4G4B4;
        *bytes_per_pixel = 2;
        return true;
    case SDL_PIXELFORMAT_XRGB1555:
        *xgu_format = XGU_TEXTURE_FORMAT_X1R5G5B5;
        *bytes_per_pixel = 2;
        return true;
    default:
        return false;
    }
}

// Although SDL provides SDL_AllocateRenderVertices, this allocates on the heap,
// which we don't want. Instead we provide our own that uses contiguous memory allocation
static bool arena_init(SDL_Renderer *renderer)
{
    renderer->vertex_data = MmAllocateContiguousMemoryEx(VERTEX_BUFFER_SIZE, 0, 0xFFFFFFFF, 0,
                                                         PAGE_WRITECOMBINE | PAGE_READWRITE);
    if (renderer->vertex_data == NULL) {
        SDL_SetError("Failed to allocate XGU arena");
        return false;
    }

    return true;
}

static void *arena_allocate(SDL_Renderer *renderer, size_t size, size_t *offset)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;

    // Ensure alignment. If every allocation is aligned, we know every pointer will be aligned.
    size = (size + XGU_VERTEX_ALIGNMENT - 1) & ~(XGU_VERTEX_ALIGNMENT - 1);

    // Round robin back to the start of the arena if we run out of space
    if (render_data->vertex_data_used + size > VERTEX_BUFFER_SIZE) {
        render_data->vertex_data_used = 0;
    }

    void *ptr = (void *)((intptr_t)renderer->vertex_data + render_data->vertex_data_used);

    *offset = render_data->vertex_data_used;
    render_data->vertex_data_used += size;
    return ptr;
}

static bool xgu_texture_format_to_pb_surface_format(int xgu_format, uint32_t *pb_surface_format)
{
    switch (xgu_format) {
    case XGU_TEXTURE_FORMAT_X1R5G5B5:
    case XGU_TEXTURE_FORMAT_A1R5G5B5:
        *pb_surface_format = NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5;
        return true;
    case XGU_TEXTURE_FORMAT_R5G6B5:
        *pb_surface_format = NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5;
        return true;
    case XGU_TEXTURE_FORMAT_X8R8G8B8:
    case XGU_TEXTURE_FORMAT_A8R8G8B8:
        *pb_surface_format = NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8;
        return true;
    default:
        return false;
    }
}

static inline void clear_attribute_pointers(void)
{
    for (int i = 0; i < XGU_ATTRIBUTE_COUNT; i++) {
        xgux_set_attrib_pointer(i, XGU_FLOAT, 0, 0, NULL);
    }
}

// clang-format off

static inline void shader_init(void)
{
   pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT,
    XGU_MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE1, 0)
    | XGU_MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE2, 0)
    | XGU_MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE3, 0));
    p += 2;
    pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM,
        XGU_MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_PROGRAM_NONE)
        | XGU_MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE1, NV097_SET_SHADER_STAGE_PROGRAM_STAGE1_PROGRAM_NONE)
        | XGU_MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE2, NV097_SET_SHADER_STAGE_PROGRAM_STAGE2_PROGRAM_NONE)
        | XGU_MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE3, NV097_SET_SHADER_STAGE_PROGRAM_STAGE3_PROGRAM_NONE));
    p += 2;

    pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x1)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_COLOR_OCW + 0 * 4,
        XGU_MASK(NV097_SET_COMBINER_COLOR_OCW_AB_DST, 0x4)
        | XGU_MASK(NV097_SET_COMBINER_COLOR_OCW_CD_DST, 0x0)
        | XGU_MASK(NV097_SET_COMBINER_COLOR_OCW_SUM_DST, 0x0)
        | XGU_MASK(NV097_SET_COMBINER_COLOR_OCW_MUX_ENABLE, 0)
        | XGU_MASK(NV097_SET_COMBINER_COLOR_OCW_AB_DOT_ENABLE, 0)
        | XGU_MASK(NV097_SET_COMBINER_COLOR_OCW_CD_DOT_ENABLE, 0)
        | XGU_MASK(NV097_SET_COMBINER_COLOR_OCW_OP, NV097_SET_COMBINER_COLOR_OCW_OP_NOSHIFT));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x1)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_ALPHA_OCW + 0 * 4,
        XGU_MASK(NV097_SET_COMBINER_ALPHA_OCW_AB_DST, 0x4)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_OCW_CD_DST, 0x0)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_OCW_SUM_DST, 0x0)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_OCW_MUX_ENABLE, 0)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_OCW_OP, NV097_SET_COMBINER_ALPHA_OCW_OP_NOSHIFT));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_CONTROL,
        XGU_MASK(NV097_SET_COMBINER_CONTROL_FACTOR0, NV097_SET_COMBINER_CONTROL_FACTOR0_SAME_FACTOR_ALL)
        | XGU_MASK(NV097_SET_COMBINER_CONTROL_FACTOR1, NV097_SET_COMBINER_CONTROL_FACTOR1_SAME_FACTOR_ALL)
        | XGU_MASK(NV097_SET_COMBINER_CONTROL_ITERATION_COUNT, 1));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW0,
        XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_INVERSE, 0)
        | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_INVERSE, 0)
        | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_INVERSE, 0)
        | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_INVERSE, 0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW1,
        XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_INVERSE, 0)
        | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_INVERSE, 0)
        | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_INVERSE, 0)
        | XGU_MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_CLAMP, 0));
    p += 2;
}

static inline void unlit_shader_apply (void)
{
    p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, 0);

    p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x1)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));

    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x1)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
}

static inline void texture_shader_apply (void)
{
    p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, XGU_MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_2D_PROJECTIVE));

    p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x8) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x6)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | XGU_MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));
  
    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x8) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x4) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x6)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | XGU_MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
}
// clang-format on

static void XBOX_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
    (void)renderer;
    (void)event;
}

static bool XBOX_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture, SDL_PropertiesID create_props)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    xgu_texture_t *xgu_texture = (xgu_texture_t *)SDL_calloc(1, sizeof(xgu_texture_t));
    if (xgu_texture == NULL) {
        return SDL_OutOfMemory();
    }

    xgu_texture->tex_height = texture->h;
    xgu_texture->tex_width = texture->w;
    xgu_texture->data_height = npot2pot(texture->h);
    xgu_texture->data_width = npot2pot(texture->w);

    // Ensure the texture format is supported
    if (sdl_to_xgu_texture_format(texture->format, &xgu_texture->format, &xgu_texture->bytes_per_pixel) == false) {
        SDL_free(xgu_texture);
        return SDL_SetError("[nxdk renderer] Unsupported texture format (%s)", SDL_GetPixelFormatName(texture->format));
    }

    // If this is a render target, we need to check if the render target format is supported
    if (SDL_GetNumberProperty(create_props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, 0) == SDL_TEXTUREACCESS_TARGET) {
        uint32_t surface_format;
        if (xgu_texture_format_to_pb_surface_format(xgu_texture->format, &surface_format) == false) {
            SDL_free(xgu_texture);
            return SDL_SetError("[nxdk renderer] Unsupported render target format (%s)", SDL_GetPixelFormatName(texture->format));
        }
    }

    size_t allocation_size = xgu_texture->data_height * xgu_texture->data_width * xgu_texture->bytes_per_pixel;
    xgu_texture->data = MmAllocateContiguousMemoryEx(allocation_size, 0, 0xFFFFFFFF, 0, PAGE_WRITECOMBINE | PAGE_READWRITE);
    if (xgu_texture->data == NULL) {
        SDL_free(xgu_texture);
        return SDL_OutOfMemory();
    }
    xgu_texture->data_physical_address = (uint8_t *)MmGetPhysicalAddress(xgu_texture->data);

    texture->internal = xgu_texture;
    return true;
}

static bool XBOX_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *rect, void **pixels, int *pitch)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    xgu_texture_t *xgu_texture = (xgu_texture_t *)texture->internal;
    uint8_t *pixels8 = (uint8_t *)xgu_texture->data;
    *pixels = &pixels8[rect->y * xgu_texture->data_width * xgu_texture->bytes_per_pixel +
                       rect->x * xgu_texture->bytes_per_pixel];

    *pitch = xgu_texture->data_width * xgu_texture->bytes_per_pixel;
    return true;
}

static void XBOX_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void)renderer;
    (void)texture;
    return;
}

static bool XBOX_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                               const SDL_Rect *rect, const void *pixels, int pitch)
{
    xgu_texture_t *xgu_texture = (xgu_texture_t *)texture->internal;
    const Uint8 *src;
    Uint8 *dst;
    int row, length, dpitch;
    src = pixels;

    XBOX_LockTexture(renderer, texture, rect, (void **)&dst, &dpitch);
    length = rect->w * xgu_texture->bytes_per_pixel;
    if (length == pitch && length == dpitch) {
        SDL_memcpy(dst, src, length * rect->h);
    } else {
        for (row = 0; row < rect->h; ++row) {
            SDL_memcpy(dst, src, length);
            src += pitch;
            dst += dpitch;
        }
    }

    XBOX_UnlockTexture(renderer, texture);

    return true;
}

static bool XBOX_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    Uint32 address, pitch, zpitch, width, height, clip_width, clip_height, format, dma_channel;
    xgu_texture_t *xgu_texture;
    extern unsigned int pb_ColorFmt; // From pbkit.c

    if (texture == NULL) {
        xgu_texture = NULL;

        dma_channel = DMA_CHANNEL_PIXEL_RENDERER;
        address = 0;
        pitch = pb_back_buffer_pitch();
        width = pb_back_buffer_width();
        height = pb_back_buffer_height();
        clip_width = width;
        clip_height = height;
        format = XGU_MASK(NV097_SET_SURFACE_FORMAT_COLOR, pb_ColorFmt);
    } else {
        xgu_texture = (xgu_texture_t *)texture->internal;
        assert(xgu_texture);

        uint32_t surface_format;
        bool status = xgu_texture_format_to_pb_surface_format(xgu_texture->format, &surface_format);
        assert(status);

        dma_channel = DMA_CHANNEL_3D_3;
        address = (Uint32)xgu_texture->data_physical_address;
        pitch = xgu_texture->data_width * xgu_texture->bytes_per_pixel; // The full NPOT width
        width = xgu_texture->data_width;
        height = xgu_texture->data_height;
        clip_width = xgu_texture->tex_width;
        clip_height = xgu_texture->tex_height;
        format = XGU_MASK(NV097_SET_SURFACE_FORMAT_COLOR, surface_format);
    }

    format |= XGU_MASK(NV097_SET_SURFACE_FORMAT_ZETA, NV097_SET_SURFACE_FORMAT_ZETA_Z24S8) |
              XGU_MASK(NV097_SET_SURFACE_FORMAT_TYPE, NV097_SET_SURFACE_FORMAT_TYPE_PITCH) |
              XGU_MASK(NV097_SET_SURFACE_FORMAT_WIDTH, width) |
              XGU_MASK(NV097_SET_SURFACE_FORMAT_HEIGHT, height);

    // We are rendering to the depth buffer still so use back buffer width
    // Z24S8 format has 4 bytes per pixel for the zeta buffer
    zpitch = pb_back_buffer_width() * 4;

    p = pb_begin();

    p = pb_push1(p, NV097_SET_CONTEXT_DMA_COLOR, dma_channel);
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_HORIZONTAL, (clip_width << 16));
    p = pb_push1(p, NV097_SET_SURFACE_CLIP_VERTICAL, (clip_height << 16));
    p = pb_push1(p, NV097_SET_SURFACE_FORMAT, format);
    p = pb_push1(p, NV097_SET_SURFACE_COLOR_OFFSET, address);
    p = pb_push1(p, NV097_SET_SURFACE_PITCH, XGU_MASK(NV097_SET_SURFACE_PITCH_COLOR, pitch) | XGU_MASK(NV097_SET_SURFACE_PITCH_ZETA, zpitch));
    p = pb_push1(p, NV097_NO_OPERATION, 0);
    p = pb_push1(p, NV097_WAIT_FOR_IDLE, 0); //fix me, do we need this?
    pb_end(p);

    render_data->active_render_target = xgu_texture;
    return true;
}

static bool XBOX_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;

    float *vertices = (float *)arena_allocate(renderer, count * 2 * sizeof(float), &cmd->data.draw.first);
    if (!vertices) {
        return SDL_OutOfMemory();
    }

    cmd->data.draw.count = count;
    for (int i = 0; i < count; i++) {
        *(vertices++) = 0.5f + points[i].x;
        *(vertices++) = 0.5f + points[i].y;
    }
    return true;
}

static bool XBOX_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                               const float *xy, int xy_stride, const SDL_FColor *color, int color_stride, const float *uv, int uv_stride,
                               int num_vertices, const void *indices, int num_indices, int size_indices,
                               float scale_x, float scale_y)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const int count = indices ? num_indices : num_vertices;
    const size_t sz = (texture) ? sizeof(xgu_vertex_textured_t) : sizeof(xgu_vertex_t);
    const float color_scale = cmd->data.draw.color_scale;

    float *vertices = (float *)arena_allocate(renderer, count * sz, &cmd->data.draw.first);
    if (vertices == NULL) {
        return SDL_OutOfMemory();
    }

    cmd->data.draw.count = count;
    size_indices = indices ? size_indices : 0;

    for (int i = 0; i < count; i++) {
        int j;
        if (size_indices == 4) {
            j = ((const Uint32 *)indices)[i];
        } else if (size_indices == 2) {
            j = ((const Uint16 *)indices)[i];
        } else if (size_indices == 1) {
            j = ((const Uint8 *)indices)[i];
        } else {
            j = i;
        }

        const float *vertex_pos = (float *)((char *)xy + j * xy_stride);
        *(vertices++) = vertex_pos[0] * scale_x;
        *(vertices++) = vertex_pos[1] * scale_y;

        const SDL_FColor *vertex_color = (SDL_FColor *)((intptr_t)color + j * color_stride);
        *(vertices++) = vertex_color->r * color_scale;
        *(vertices++) = vertex_color->g * color_scale;
        *(vertices++) = vertex_color->b * color_scale;
        *(vertices++) = vertex_color->a;

        if (texture) {
            const xgu_texture_t *xgu_texture = (xgu_texture_t *)texture->internal;
            const float *vertex_uv = (float *)((char *)uv + j * uv_stride);
            *(vertices++) = vertex_uv[0] * (xgu_texture->tex_width - 1);
            *(vertices++) = vertex_uv[1] * (xgu_texture->tex_height - 1);
        }
    }
    return true;
}

static bool XBOX_QueueNoOp(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    (void)renderer;
    (void)cmd;
    return true;
}

static bool XBOX_RenderSetViewPort(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const SDL_Rect *viewport = &cmd->data.viewport.rect;

    // If the new viewport is the same as the current one, no need to update
    if (SDL_memcmp(viewport, &render_data->viewport, sizeof(SDL_Rect)) == 0) {
        return true;
    }

    // Create a new rect that is the intersection of the new clip rect and the current viewport
    // This is becaused SDL expects rendering to be clipped to the viewport and the clip rect
    // but we can only set one scissor at a time.
    SDL_Rect scissor_clipped_rect;
    SDL_GetRectIntersection(&render_data->clip_rect, viewport, &scissor_clipped_rect);

    p = pb_begin();
    p = xgu_set_viewport_offset(p, viewport->x, viewport->y, 0.0f, 0.0f);
    p = xgu_set_scissor_rect(p, false, scissor_clipped_rect.x, scissor_clipped_rect.y,
                             scissor_clipped_rect.w - 1, scissor_clipped_rect.h - 1);
    pb_end(p);

    // Store the viewport in the render data
    render_data->viewport = *viewport;
    return true;
}

static bool XBOX_RenderSetClipRect(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    SDL_Rect *clip_rect = &cmd->data.cliprect.rect;

    // If clipping is disabled, reset the clip rect to the entire back buffer
    if (cmd->data.cliprect.enabled == false) {
        const SDL_Rect no_clip = { 0, 0, pb_back_buffer_width() - 1, pb_back_buffer_height() - 1 };
        *clip_rect = no_clip;
        return true;
    }

    // If the new clip rect is the same as the current one, no need to update
    if (SDL_memcmp(clip_rect, &render_data->clip_rect, sizeof(SDL_Rect)) == 0) {
        return true;
    }

    // Create a new rect that is the intersection of the new clip rect and the current viewport
    // This is becaused SDL expects rendering to be clipped to the viewport and the clip rect
    // but we can only set one scissor at a time.
    SDL_Rect scissor_clipped_rect;
    SDL_GetRectIntersection(&render_data->viewport, clip_rect, &scissor_clipped_rect);

    p = pb_begin();
    p = xgu_set_scissor_rect(p, false, scissor_clipped_rect.x, scissor_clipped_rect.y,
                             scissor_clipped_rect.w - 1, scissor_clipped_rect.h - 1);
    pb_end(p);

    // Update the clip rect in the render data
    render_data->clip_rect = *clip_rect;
    return true;
}

static bool XBOX_RenderSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const SDL_FColor *color = &cmd->data.color.color;
    p = pb_begin();
    p = xgux_set_color4f(p, color->r, color->g, color->b, color->a);
    pb_end(p);

    return true;
}

static bool XBOX_RenderClear(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const SDL_FColor color = cmd->data.color.color;

    const uint32_t color32 = ((uint32_t)(color.r * 255.0f) << 16) |
                             ((uint32_t)(color.g * 255.0f) << 8) |
                             ((uint32_t)(color.b * 255.0f) << 0) |
                             ((uint32_t)(color.a * 255.0f) << 24);

    if (render_data->active_render_target) {
        pb_fill(0, 0, render_data->active_render_target->tex_width, render_data->active_render_target->tex_height, color32);
    } else {
        p = pb_begin();
        p = xgu_set_color_clear_value(p, color32);
        p = xgu_clear_surface(p, XGU_CLEAR_Z | XGU_CLEAR_STENCIL | XGU_CLEAR_COLOR);
        pb_end(p);
    }

    return true;
}

static void XBOX_SetBlendMode(SDL_Renderer *renderer, int blendMode)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;

    if (blendMode == render_data->active_blend_mode) {
        return;
    }

    XguBlendFactor sfactor;
    XguBlendFactor dfactor;

    switch (blendMode) {
    case SDL_BLENDMODE_NONE:
        sfactor = XGU_FACTOR_ONE;
        dfactor = XGU_FACTOR_ZERO;
        break;
    case SDL_BLENDMODE_BLEND:
        sfactor = XGU_FACTOR_SRC_ALPHA;
        dfactor = XGU_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case SDL_BLENDMODE_BLEND_PREMULTIPLIED:
        sfactor = XGU_FACTOR_ONE;
        dfactor = XGU_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case SDL_BLENDMODE_ADD:
        sfactor = XGU_FACTOR_SRC_ALPHA;
        dfactor = XGU_FACTOR_ONE;
        break;
    case SDL_BLENDMODE_ADD_PREMULTIPLIED:
        sfactor = XGU_FACTOR_ONE;
        dfactor = XGU_FACTOR_ONE;
        break;
    case SDL_BLENDMODE_MUL:
        sfactor = XGU_FACTOR_DST_COLOR;
        dfactor = XGU_FACTOR_ONE_MINUS_SRC_ALPHA;
        break;
    case SDL_BLENDMODE_MOD:
        sfactor = XGU_FACTOR_ZERO;
        dfactor = XGU_FACTOR_SRC_COLOR;
        break;
    default:
        SDL_Log("Unsupported blend mode %d, defaulting to SDL_BLENDMODE_BLEND", blendMode);
        sfactor = XGU_FACTOR_SRC_ALPHA;
        dfactor = XGU_FACTOR_ONE_MINUS_SRC_ALPHA;
    }

    p = pb_begin();
    p = xgu_set_blend_func_sfactor(p, sfactor);
    p = xgu_set_blend_func_dfactor(p, dfactor);
    p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
    pb_end(p);

    render_data->active_blend_mode = blendMode;
}

static bool XBOX_RenderGeometry(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const size_t count = cmd->data.draw.count;

    XBOX_SetBlendMode(renderer, cmd->data.draw.blend);

    if (cmd->data.draw.texture) {
        xgu_texture_t *xgu_texture = (xgu_texture_t *)cmd->data.draw.texture->internal;

        p = pb_begin();

        if (render_data->texture_shader_active == 0) {
            texture_shader_apply();
            render_data->texture_shader_active = 1;
        }

        const XguTexFilter texture_filter =
            (cmd->data.draw.texture_scale_mode == SDL_SCALEMODE_NEAREST) ? XGU_TEXTURE_FILTER_NEAREST : XGU_TEXTURE_FILTER_LINEAR;

        if (render_data->active_texture != xgu_texture) {
            p = xgu_set_texture_offset(p, 0, xgu_texture->data_physical_address);
            p = xgu_set_texture_format(p, 0, 2, false, XGU_SOURCE_COLOR, 2, xgu_texture->format, 1,
                                       __builtin_ctz(xgu_texture->data_width), __builtin_ctz(xgu_texture->data_height), 0);
            p = xgu_set_texture_address(p, 0, XGU_CLAMP_TO_EDGE, false, XGU_CLAMP_TO_EDGE, false, XGU_CLAMP_TO_EDGE, false, false);
            p = xgu_set_texture_control0(p, 0, true, 0, 0);
            p = xgu_set_texture_control1(p, 0, xgu_texture->data_width * xgu_texture->bytes_per_pixel);
            p = xgu_set_texture_image_rect(p, 0, xgu_texture->data_width, xgu_texture->data_height);
            p = xgu_set_texture_filter(p, 0, 0, XGU_TEXTURE_CONVOLUTION_GAUSSIAN, texture_filter, texture_filter, false, false, false, false);
            render_data->active_texture = xgu_texture;
        }

        pb_end(p);

        xgu_vertex_textured_t *xgu_verts = (xgu_vertex_textured_t *)vertices;
        clear_attribute_pointers();
        xgux_set_attrib_pointer(XGU_VERTEX_ARRAY, XGU_FLOAT,
                                XGU_ARRAY_SIZE(xgu_verts->pos), sizeof(xgu_vertex_textured_t), xgu_verts->pos);
        xgux_set_attrib_pointer(XGU_COLOR_ARRAY, XGU_FLOAT,
                                XGU_ARRAY_SIZE(xgu_verts->color), sizeof(xgu_vertex_textured_t), xgu_verts->color);
        xgux_set_attrib_pointer(XGU_TEXCOORD0_ARRAY, XGU_FLOAT,
                                XGU_ARRAY_SIZE(xgu_verts->tex), sizeof(xgu_vertex_textured_t), xgu_verts->tex);
        xgux_draw_arrays(XGU_TRIANGLES, 0, count);

    } else {
        p = pb_begin();

        if (render_data->texture_shader_active == 1) {
            unlit_shader_apply();
            render_data->texture_shader_active = 0;
        }

        pb_end(p);

        xgu_vertex_t *xgu_verts = (xgu_vertex_t *)vertices;
        clear_attribute_pointers();
        xgux_set_attrib_pointer(XGU_VERTEX_ARRAY, XGU_FLOAT,
                                XGU_ARRAY_SIZE(xgu_verts->pos), sizeof(xgu_vertex_t), xgu_verts->pos);
        xgux_set_attrib_pointer(XGU_COLOR_ARRAY, XGU_FLOAT,
                                XGU_ARRAY_SIZE(xgu_verts->color), sizeof(xgu_vertex_t), xgu_verts->color);
        xgux_draw_arrays(XGU_TRIANGLES, 0, count);
    }

    return true;
}

static bool XBOX_RenderPoints(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const size_t count = cmd->data.draw.count;

    XBOX_SetBlendMode(renderer, cmd->data.draw.blend);

    xgu_point_t *xgu_verts = (xgu_point_t *)vertices;
    clear_attribute_pointers();
    xgux_set_attrib_pointer(XGU_VERTEX_ARRAY, XGU_FLOAT,
                            XGU_ARRAY_SIZE(xgu_verts->pos), sizeof(xgu_point_t), xgu_verts->pos);
    xgux_draw_arrays(XGU_POINTS, 0, count);

    return true;
}

static void XBOX_InvalidateCachedState(SDL_Renderer *renderer)
{
    (void)renderer;
}

static bool XBOX_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    (void)vertsize;
    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_SETVIEWPORT:
        {
            XBOX_RenderSetViewPort(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_SETCLIPRECT:
        {
            XBOX_RenderSetClipRect(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_SETDRAWCOLOR:
        {
            XBOX_RenderSetDrawColor(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_CLEAR:
        {
            XBOX_RenderClear(renderer, cmd);
            break;
        }
        case SDL_RENDERCMD_DRAW_POINTS:
        {
            XBOX_RenderPoints(renderer, (uint8_t *)vertices + cmd->data.draw.first, cmd);
            break;
        }
        case SDL_RENDERCMD_GEOMETRY:
        {
            XBOX_RenderGeometry(renderer, (uint8_t *)vertices + cmd->data.draw.first, cmd);
            break;
        }
        // SDL should use XBOX_QueueGeometry instead of these commands.
        case SDL_RENDERCMD_DRAW_LINES:
        case SDL_RENDERCMD_FILL_RECTS:
        case SDL_RENDERCMD_COPY:
        case SDL_RENDERCMD_COPY_EX:
            break;
        case SDL_RENDERCMD_NO_OP:
            break;
        }
        cmd = cmd->next;
    }

    return true;
}

static SDL_Surface *XBOX_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    SDL_PixelFormat format = renderer->target ? renderer->target->format : SDL_PIXELFORMAT_ARGB8888;

    SDL_Surface *surface = SDL_CreateSurface(rect->w, rect->h, format);
    if (surface == NULL) {
        return NULL;
    }
    const SDL_PixelFormat dst_format = surface->format;
    const int dst_bytes_per_pixel = SDL_BYTESPERPIXEL(dst_format);
    const int dst_pitch = surface->pitch;
    uint8_t *dst8 = surface->pixels;

    // Ensure the back buffer is fully renderered before reading pixels
    p = pb_begin();
    p = pb_push1(p, NV097_NO_OPERATION, 0);
    p = pb_push1(p, NV097_WAIT_FOR_IDLE, 0);
    pb_end(p);

    while (pb_busy()) {
        Sleep(0);
    }

    XVideoFlushFB();

    VIDEO_MODE vm = XVideoGetMode();
    const SDL_PixelFormat src_format = (vm.bpp == 32) ? SDL_PIXELFORMAT_ARGB8888 :
                                       (vm.bpp == 16) ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_XRGB1555;
    const int src_pitch = vm.width * ((vm.bpp + 7) / 8);
    const uint8_t *src8 = (const uint8_t *)pb_back_buffer();

    // Now copy the back buffer pixels to the surface
    SDL_ConvertPixels(rect->w, rect->h,
                      src_format, &src8[rect->y * src_pitch + rect->x * dst_bytes_per_pixel], src_pitch,
                      dst_format, &dst8[rect->y * dst_pitch + rect->x * dst_bytes_per_pixel], dst_pitch);

    return surface;
}

static bool XBOX_RenderPresent(SDL_Renderer *renderer)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;

    while (pb_busy()) {
        Sleep(0);
    }

    while (pb_finished()) {
        Sleep(0);
    }

    if (render_data->vsync_enabled) {
        pb_wait_for_vbl();
    }

    // Prepare for the next frame
    pb_reset();
    pb_target_back_buffer();
    return true;
}

static void XBOX_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    (void)renderer;
    xgu_texture_t *xgu_texture = (xgu_texture_t *)texture->internal;

    MmFreeContiguousMemory(xgu_texture->data);
    SDL_free(xgu_texture);
    texture->internal = NULL;
}

static void XBOX_DestroyRenderer(SDL_Renderer *renderer)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    SDL_free(render_data);
    renderer->internal = NULL;

    MmFreeContiguousMemory(renderer->vertex_data);
    renderer->vertex_data = NULL;
}

static bool XBOX_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    XGU_MAYBE_UNUSED xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;

    if (vsync == 1) {
        render_data->vsync_enabled = 1;
        return true;
    } else if (vsync == 0) {
        render_data->vsync_enabled = 0;
        return true;
    }
    return SDL_Unsupported();
}

static bool XBOX_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, SDL_PropertiesID create_props)
{
    (void)create_props;
    xgu_render_data_t *render_data = (xgu_render_data_t *)SDL_calloc(1, sizeof(xgu_render_data_t));
    if (render_data == NULL) {
        return SDL_OutOfMemory();
    }

    const float m_identity[4 * 4] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

    // Change the framebuffer surface format based on the video mode
    VIDEO_MODE vm = XVideoGetMode();
    if (vm.bpp == 16) {
        pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_R5G6B5, false);
    } else if (vm.bpp == 15) {
        pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_X1R5G5B5_Z1R5G5B5, false);
    } else {
        pb_set_color_format(NV097_SET_SURFACE_FORMAT_COLOR_LE_A8R8G8B8, false);
    }

    pb_init();
    pb_show_front_screen();

    p = pb_begin();
    shader_init();
    unlit_shader_apply();

    p = xgu_set_blend_enable(p, true);
    p = xgu_set_depth_test_enable(p, false);
    p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_SRC_ALPHA);
    p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ONE_MINUS_SRC_ALPHA);
    p = xgu_set_depth_func(p, XGU_FUNC_LESS_OR_EQUAL);

    p = xgu_set_skin_mode(p, XGU_SKIN_MODE_OFF);
    p = xgu_set_normalization_enable(p, false);
    p = xgu_set_lighting_enable(p, false);
    p = xgu_set_clear_rect_vertical(p, 0, pb_back_buffer_height());
    p = xgu_set_clear_rect_horizontal(p, 0, pb_back_buffer_width());

    pb_end(p);

    for (int i = 0; i < XGU_TEXTURE_COUNT; i++) {
        p = pb_begin();
        p = xgu_set_texgen_s(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_t(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_r(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_q(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texture_matrix_enable(p, i, false);
        p = xgu_set_texture_matrix(p, i, m_identity);
        pb_end(p);
    }

    for (int i = 0; i < XGU_WEIGHT_COUNT; i++) {
        p = pb_begin();
        p = xgu_set_model_view_matrix(p, i, m_identity);
        p = xgu_set_inverse_model_view_matrix(p, i, m_identity);
        pb_end(p);
    }

    p = pb_begin();
    p = xgu_set_transform_execution_mode(p, XGU_FIXED, XGU_RANGE_MODE_PRIVATE);
    p = xgu_set_projection_matrix(p, m_identity);
    p = xgu_set_composite_matrix(p, m_identity);
    p = xgu_set_viewport_offset(p, 0.0f, 0.0f, 0.0f, 0.0f);
    p = xgu_set_viewport_scale(p, 1.0f, 1.0f, 1.0f, 1.0f);
    p = xgu_set_scissor_rect(p, false, 0, 0, pb_back_buffer_width() - 1, pb_back_buffer_height() - 1);
    pb_end(p);

    renderer->WindowEvent = XBOX_WindowEvent;
    renderer->CreateTexture = XBOX_CreateTexture;
    renderer->UpdateTexture = XBOX_UpdateTexture;
    renderer->LockTexture = XBOX_LockTexture;
    renderer->UnlockTexture = XBOX_UnlockTexture;
    renderer->SetRenderTarget = XBOX_SetRenderTarget;
    renderer->QueueSetViewport = XBOX_QueueNoOp;
    renderer->QueueSetDrawColor = XBOX_QueueNoOp;
    renderer->QueueDrawPoints = XBOX_QueueDrawPoints;
    renderer->QueueDrawLines = XBOX_QueueDrawPoints;
    renderer->QueueGeometry = XBOX_QueueGeometry;
    renderer->InvalidateCachedState = XBOX_InvalidateCachedState;
    renderer->RunCommandQueue = XBOX_RunCommandQueue;
    renderer->RenderPresent = XBOX_RenderPresent;
    renderer->DestroyTexture = XBOX_DestroyTexture;
    renderer->DestroyRenderer = XBOX_DestroyRenderer;
    renderer->RenderReadPixels = XBOX_RenderReadPixels;
    renderer->SetVSync = XBOX_SetVSync;
    renderer->internal = render_data;
    renderer->window = window;
    renderer->name = "nxdk_xgu";

    arena_init(renderer);

    // Initialize the default clip rect and viewport
    render_data->viewport = (SDL_Rect){ 0, 0, pb_back_buffer_width(), pb_back_buffer_height() };
    render_data->clip_rect = render_data->viewport;

    // These are supported texture formats, however not all of them are supported as render targets.
    // There appears to be no way to differentiate this, however CreateTexture will fail if the format is not supported.
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB1555);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_RGB565);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_XRGB8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_RGBA8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ABGR8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_BGRA8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB4444);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_XRGB1555);
    SDL_SetNumberProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 1024 * 1024);

    // This hint makes SDL use the geometry API to draw lines. This is more correct and allows for line thickness
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");

    return true;
}

SDL_RenderDriver nxdk_RenderDriver = {
    XBOX_CreateRenderer, "nxdk_xgu"
};

#endif // SDL_VIDEO_RENDER_XGU
