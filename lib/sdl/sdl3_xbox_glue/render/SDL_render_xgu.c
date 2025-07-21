// 10-geometry // Something about triangle strip
// 14-viewport

#include <../src/SDL_internal.h>

#ifdef SDL_VIDEO_RENDER_XGU

#include "xgu/xgu.h"
#include "xgu/xgux.h"
#include <../src/render/SDL_sysrender.h>
#include <hal/video.h>

#include <SDL3/SDL_pixels.h>
#include <stdint.h>

#define nxdk_RenderDriver GPU_RenderDriver

#ifndef XGU_ARENA_SIZE
#define XGU_ARENA_SIZE (4 * PAGE_SIZE)
#endif

#define XGU_ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

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
    uint8_t locked;
} xgu_texture_t;

typedef struct xgu_texture_boundary
{
    float s0;
    float s1;
    float t0;
    float t1;
} xgu_texture_boundary_t;

typedef struct xgu_texture_tint
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} xgu_texture_tint_t;

// Point
typedef struct xgu_point
{
    float x;
    float y;
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
    int texture_combiner_active;
    int vsync_enabled;
    const xgu_texture_t *active_texture;
    void *arena_buffer;
    int arena_pointer;
    int arena_last_size;
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
    // case SDL_PIXELFORMAT_A8:
    //     *xgu_format = XGU_TEXTURE_FORMAT_A8;
    //     *bytes_per_pixel = 1;
    //     return true;
    // case SDL_PIXELFORMAT_INDEX8:
    //     *xgu_format = XGU_TEXTURE_FORMAT_Y8;
    //     *bytes_per_pixel = 1;
    //     return true;
    case SDL_PIXELFORMAT_RGBA4444:
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

static inline void clear_attribute_pointers(void)
{
    for (int i = 0; i < XGU_ATTRIBUTE_COUNT; i++) {
        xgux_set_attrib_pointer(i, XGU_FLOAT, 0, 0, NULL);
    }
}

static inline void *arena_allocate(SDL_Renderer *renderer, size_t size)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    if (render_data->arena_pointer + size > XGU_ARENA_SIZE) {
        SDL_Log("XGU arena overflow! Requested %zu bytes, but only %d bytes available. Increase XGU_ARENA_SIZE",
                size, XGU_ARENA_SIZE - render_data->arena_pointer);
        return NULL;
    }

    void *ptr = (void *)((uintptr_t)render_data->arena_buffer + render_data->arena_pointer);
    render_data->arena_pointer += size;
    render_data->arena_last_size = size;
    return ptr;
}

// clang-format off

#undef MASK
#define MASK(mask, val) (((val) << (__builtin_ffs(mask)-1)) & (mask))

static inline void shader_init()
{
   pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT,
    MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE1, 0)
    | MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE2, 0)
    | MASK(NV097_SET_SHADER_OTHER_STAGE_INPUT_STAGE3, 0));
    p += 2;
    pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM,
        MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_PROGRAM_NONE)
        | MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE1, NV097_SET_SHADER_STAGE_PROGRAM_STAGE1_PROGRAM_NONE)
        | MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE2, NV097_SET_SHADER_STAGE_PROGRAM_STAGE2_PROGRAM_NONE)
        | MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE3, NV097_SET_SHADER_STAGE_PROGRAM_STAGE3_PROGRAM_NONE));
    p += 2;

    pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x1)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_COLOR_OCW + 0 * 4,
        MASK(NV097_SET_COMBINER_COLOR_OCW_AB_DST, 0x4)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_CD_DST, 0x0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_SUM_DST, 0x0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_MUX_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_AB_DOT_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_CD_DOT_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_COLOR_OCW_OP, NV097_SET_COMBINER_COLOR_OCW_OP_NOSHIFT));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x1)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_ALPHA_OCW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_OCW_AB_DST, 0x4)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_CD_DST, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_SUM_DST, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_MUX_ENABLE, 0)
        | MASK(NV097_SET_COMBINER_ALPHA_OCW_OP, NV097_SET_COMBINER_ALPHA_OCW_OP_NOSHIFT));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_CONTROL,
        MASK(NV097_SET_COMBINER_CONTROL_FACTOR0, NV097_SET_COMBINER_CONTROL_FACTOR0_SAME_FACTOR_ALL)
        | MASK(NV097_SET_COMBINER_CONTROL_FACTOR1, NV097_SET_COMBINER_CONTROL_FACTOR1_SAME_FACTOR_ALL)
        | MASK(NV097_SET_COMBINER_CONTROL_ITERATION_COUNT, 1));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW0,
        MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_A_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_B_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_C_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW0_D_INVERSE, 0));
    p += 2;
    pb_push1(p, NV097_SET_COMBINER_SPECULAR_FOG_CW1,
        MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_E_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_ALPHA, 0) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_F_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_ALPHA, 1) | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_G_INVERSE, 0)
        | MASK(NV097_SET_COMBINER_SPECULAR_FOG_CW1_SPECULAR_CLAMP, 0));
    p += 2;
}

static inline void unlit_shader_apply ()
{
    p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, 0);

    p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x1)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));

    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x1)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
}

static inline void texture_shader_apply ()
{
    p = pb_push1(p, NV097_SET_SHADER_OTHER_STAGE_INPUT, 0);
    p = pb_push1(p, NV097_SET_SHADER_STAGE_PROGRAM, MASK(NV097_SET_SHADER_STAGE_PROGRAM_STAGE0, NV097_SET_SHADER_STAGE_PROGRAM_STAGE0_2D_PROJECTIVE));

    p = pb_push1(p, NV097_SET_COMBINER_COLOR_ICW + 0 * 4,
    MASK(NV097_SET_COMBINER_COLOR_ICW_A_SOURCE, 0x8) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_A_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_B_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_B_MAP, 0x6)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_C_MAP, 0x0)
    | MASK(NV097_SET_COMBINER_COLOR_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_ALPHA, 0) | MASK(NV097_SET_COMBINER_COLOR_ICW_D_MAP, 0x0));
  
    p = pb_push1(p, NV097_SET_COMBINER_ALPHA_ICW + 0 * 4,
        MASK(NV097_SET_COMBINER_ALPHA_ICW_A_SOURCE, 0x8) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_A_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_SOURCE, 0x4) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_B_MAP, 0x6)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_C_MAP, 0x0)
        | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_SOURCE, 0x0) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_ALPHA, 1) | MASK(NV097_SET_COMBINER_ALPHA_ICW_D_MAP, 0x0));
}
// clang-format on

static void XBOX_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
}

static bool XBOX_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture, SDL_PropertiesID create_props)
{

    xgu_texture_t *xgu_texture = (xgu_texture_t *)SDL_calloc(1, sizeof(xgu_texture_t));
    if (xgu_texture == NULL) {
        return false;
    }

    xgu_texture->tex_height = texture->h;
    xgu_texture->tex_width = texture->w;
    xgu_texture->data_height = npot2pot(texture->h);
    xgu_texture->data_width = npot2pot(texture->w);
    xgu_texture->locked = 0;

    int texture_status = sdl_to_xgu_texture_format(texture->format, &xgu_texture->format, &xgu_texture->bytes_per_pixel);
    if (!texture_status) {
        SDL_free(xgu_texture);
        return SDL_SetError("Texture format %s not supported by XGU", SDL_GetPixelFormatName(texture->format));
    }

    size_t allocation_size = xgu_texture->data_height * xgu_texture->data_width * xgu_texture->bytes_per_pixel;
    xgu_texture->data = MmAllocateContiguousMemoryEx(allocation_size, 0, 0xFFFFFFFF, 0, PAGE_WRITECOMBINE | PAGE_READWRITE);
    if (xgu_texture->data == NULL) {
        SDL_free(xgu_texture);
        return SDL_SetError("Failed to allocate memory for texture data");
    }

    xgu_texture->data_physical_address = (uint8_t *)MmGetPhysicalAddress(xgu_texture->data);

    texture->internal = xgu_texture;
    return true;
}

static bool XBOX_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                             const SDL_Rect *rect, void **pixels, int *pitch)
{
    xgu_texture_t *xgu_texture = (xgu_texture_t *)texture->internal;
    if (xgu_texture->locked) {
        return SDL_SetError("Texture is already locked");
    }

    uint8_t *pixels8 = (uint8_t *)xgu_texture->data;
    *pixels = &pixels8[rect->y * xgu_texture->data_width * xgu_texture->bytes_per_pixel +
                       rect->x * xgu_texture->bytes_per_pixel];

    *pitch = xgu_texture->data_width * xgu_texture->bytes_per_pixel;

    xgu_texture->locked = 1;
    return true;
}

static void XBOX_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    xgu_texture_t *xgu_texture = (xgu_texture_t *)texture->internal;
    xgu_texture->locked = 0;
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
    assert(0); // Not implemented in this backend.
    return false;
}

static bool XBOX_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return true;
}

static bool XBOX_QueueNoOp(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return true;
}

static bool XBOX_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    xgu_render_data_t *data = (xgu_render_data_t *)renderer->internal;
    float *verts = (float *)SDL_AllocateRenderVertices(renderer, count * 2 * sizeof(float), 0, &cmd->data.draw.first);
    int i;

    if (!verts) {
        return false;
    }

    cmd->data.draw.count = count;
    for (i = 0; i < count; i++) {
        *(verts++) = 0.5f + points[i].x;
        *(verts++) = 0.5f + points[i].y;
    }
    return true;
}

static bool XBOX_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                               const float *xy, int xy_stride, const SDL_FColor *color, int color_stride, const float *uv, int uv_stride,
                               int num_vertices, const void *indices, int num_indices, int size_indices,
                               float scale_x, float scale_y)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    xgu_texture_t *xgu_texture = NULL;
    int i;
    int count = indices ? num_indices : num_vertices;
    float *verts;
    size_t sz = (texture) ? sizeof(xgu_vertex_textured_t) : sizeof(xgu_vertex_t);
    const float color_scale = cmd->data.draw.color_scale;

    verts = (float *)SDL_AllocateRenderVertices(renderer, count * sz, 0, &cmd->data.draw.first);
    if (!verts) {
        return false;
    }

    if (texture) {
        xgu_texture = (xgu_texture_t *)texture->internal;
    }

    cmd->data.draw.count = count;
    size_indices = indices ? size_indices : 0;

    for (i = 0; i < count; i++) {
        int j;
        float *xy_;
        SDL_FColor *col_;
        if (size_indices == 4) {
            j = ((const Uint32 *)indices)[i];
        } else if (size_indices == 2) {
            j = ((const Uint16 *)indices)[i];
        } else if (size_indices == 1) {
            j = ((const Uint8 *)indices)[i];
        } else {
            j = i;
        }

        xy_ = (float *)((char *)xy + j * xy_stride);

        *(verts++) = xy_[0] * scale_x;
        *(verts++) = xy_[1] * scale_y;

        col_ = (SDL_FColor *)((char *)color + j * color_stride);
        *(verts++) = col_->r * color_scale;
        *(verts++) = col_->g * color_scale;
        *(verts++) = col_->b * color_scale;
        *(verts++) = col_->a;

        if (texture) {
            float *uv_ = (float *)((char *)uv + j * uv_stride);
            *(verts++) = uv_[0] * (xgu_texture->tex_width - 1);
            *(verts++) = uv_[1] * (xgu_texture->tex_height - 1);
        }
    }

    return true;
}

static bool XBOX_RenderSetViewPort(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const SDL_Rect *viewport = &cmd->data.viewport.rect;

    const float x1 = viewport->x; // / (float)pb_back_buffer_width();
    const float y1 = viewport->y; // / (float)pb_back_buffer_height();
    const float x2 = SDL_min(viewport->x + viewport->w, pb_back_buffer_width() - 1);
    const float y2 = SDL_min(viewport->y + viewport->h, pb_back_buffer_height() - 1);

    p = pb_begin();
    p = xgu_set_viewport_offset(p, x1, y1, 0.0f, 0.0f);
    p = xgu_set_scissor_rect(p, false, x1, y1, x2 - x1, y2 - y1);
    pb_end(p);
    return true;
}

static bool XBOX_RenderSetClipRect(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const SDL_Rect *rect = &cmd->data.cliprect.rect;

    if (cmd->data.cliprect.enabled == false) {
        p = pb_begin();
        p = xgu_set_scissor_rect(p, false, 0, 0, pb_back_buffer_width() - 1, pb_back_buffer_height() - 1);
        pb_end(p);
        return true;
    }

    p = pb_begin();
    p = xgu_set_scissor_rect(p, false, rect->x, rect->y, rect->w, rect->h);
    pb_end(p);

    return true;
}

static bool XBOX_RenderSetDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const SDL_FColor *color = &cmd->data.color.color;
    p = pb_begin();
    p = xgux_set_color4f(p, color->r, color->g, color->b, color->a);
    pb_end(p);

    return true;
}

static bool XBOX_RenderClear(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    int offsetX, offsetY;

    p = pb_begin();
    p = xgu_set_scissor_rect(p, false, 0, 0, pb_back_buffer_width() - 1, pb_back_buffer_height() - 1);
    p = xgu_set_color_clear_value(p, 0xff000000);
    p = xgu_clear_surface(p, XGU_CLEAR_Z | XGU_CLEAR_STENCIL | XGU_CLEAR_COLOR);
    pb_end(p);

    return true;
}

static void XBOX_SetBlendMode(SDL_Renderer *renderer, int blendMode)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    p = pb_begin();

    switch (blendMode) {
    case SDL_BLENDMODE_NONE:
    {
        p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_ONE);
        p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ZERO);
        p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
        break;
    }
    case SDL_BLENDMODE_BLEND:
    {
        p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_SRC_ALPHA);
        p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ONE_MINUS_SRC_ALPHA);
        p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
        break;
    }
    case SDL_BLENDMODE_BLEND_PREMULTIPLIED:
    {
        p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_ONE);
        p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ONE_MINUS_SRC_ALPHA);
        p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
        break;
    }
    case SDL_BLENDMODE_ADD:
    {
        p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_SRC_ALPHA);
        p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ONE);
        p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
        break;
    }
    case SDL_BLENDMODE_ADD_PREMULTIPLIED:
    {
        p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_ONE);
        p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ONE);
        p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
        break;
    }
    case SDL_BLENDMODE_MUL:
    {
        p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_DST_COLOR);
        p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_ONE_MINUS_SRC_ALPHA);
        p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
        break;
    }
    case SDL_BLENDMODE_MOD:
    {
        p = xgu_set_blend_func_sfactor(p, XGU_FACTOR_ZERO);
        p = xgu_set_blend_func_dfactor(p, XGU_FACTOR_SRC_COLOR);
        p = push_command_parameter(p, NV097_SET_BLEND_EQUATION, NV097_SET_BLEND_EQUATION_V_FUNC_ADD);
        break;
    }
    }
    pb_end(p);
}

static bool XBOX_RenderGeometry(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const size_t count = cmd->data.draw.count;

    XBOX_SetBlendMode(renderer, cmd->data.draw.blend);

    if (cmd->data.draw.texture) {
        xgu_texture_t *xgu_texture = (xgu_texture_t *)cmd->data.draw.texture->internal;

        p = pb_begin();

        if (render_data->texture_combiner_active == 0) {
            texture_shader_apply();
            render_data->texture_combiner_active = 1;
        }

        const XguTexFilter texture_filter = (cmd->data.draw.texture_scale_mode == SDL_SCALEMODE_NEAREST) ? XGU_TEXTURE_FILTER_NEAREST : XGU_TEXTURE_FILTER_LINEAR;

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

        // We need to copy the verts to contiguous memory for XGU.
        xgu_vertex_textured_t *xgu_verts = arena_allocate(renderer, count * sizeof(xgu_vertex_textured_t));
        if (!xgu_verts) {
            return SDL_SetError("Failed to allocate memory for vertices");
        }
        SDL_memcpy(xgu_verts, vertices + cmd->data.draw.first, count * sizeof(xgu_vertex_textured_t));

        clear_attribute_pointers();
        xgux_set_attrib_pointer(XGU_VERTEX_ARRAY, XGU_FLOAT, XGU_ARRAY_SIZE(xgu_verts->pos), sizeof(xgu_vertex_textured_t), xgu_verts->pos);
        xgux_set_attrib_pointer(XGU_COLOR_ARRAY, XGU_FLOAT, XGU_ARRAY_SIZE(xgu_verts->color), sizeof(xgu_vertex_textured_t), xgu_verts->color);
        xgux_set_attrib_pointer(XGU_TEXCOORD0_ARRAY, XGU_FLOAT, XGU_ARRAY_SIZE(xgu_verts->tex), sizeof(xgu_vertex_textured_t), xgu_verts->tex);
        xgux_draw_arrays(XGU_TRIANGLES, 0, count);

    } else {
        p = pb_begin();

        if (render_data->texture_combiner_active == 1) {
            unlit_shader_apply();
            render_data->texture_combiner_active = 0;
        }

        pb_end(p);

        // We need to copy the verts to contiguous memory for XGU.
        xgu_vertex_t *xgu_verts = arena_allocate(renderer, count * sizeof(xgu_vertex_t));
        if (!xgu_verts) {
            return SDL_SetError("Failed to allocate memory for vertices");
        }
        SDL_memcpy(xgu_verts, vertices + cmd->data.draw.first, count * sizeof(xgu_vertex_t));

        clear_attribute_pointers();
        xgux_set_attrib_pointer(XGU_VERTEX_ARRAY, XGU_FLOAT, XGU_ARRAY_SIZE(xgu_verts->pos), sizeof(xgu_vertex_t), xgu_verts->pos);
        xgux_set_attrib_pointer(XGU_COLOR_ARRAY, XGU_FLOAT, XGU_ARRAY_SIZE(xgu_verts->color), sizeof(xgu_vertex_t), xgu_verts->color);
        xgux_draw_arrays(XGU_TRIANGLES, 0, count);
    }

    return true;
}

static bool XBOX_RenderLines(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    // SDL_HINT_RENDER_LINE_METHOD is set to use the geometry API so this shouldn't be needed.
    return SDL_Unsupported();
}

static bool XBOX_RenderPoints(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    const size_t count = cmd->data.draw.count;

    XBOX_SetBlendMode(renderer, cmd->data.draw.blend);

    // We need to copy the verts to contiguous memory for XGU.
    xgu_vertex_t *xgu_verts = arena_allocate(renderer, count * sizeof(xgu_point_t));
    if (!xgu_verts) {
        return SDL_SetError("Failed to allocate memory for vertices");
    }
    SDL_memcpy(xgu_verts, vertices + cmd->data.draw.first, count * sizeof(xgu_point_t));

    clear_attribute_pointers();
    xgux_set_attrib_pointer(XGU_VERTEX_ARRAY, XGU_FLOAT, 2, sizeof(xgu_point_t), xgu_verts);
    xgux_draw_arrays(XGU_POINTS, 0, count);

    return true;
}

static void XBOX_InvalidateCachedState(SDL_Renderer *renderer)
{
    // currently this doesn't do anything. If this needs to do something (and someone is mixing their own rendering calls in!), update this.
}

static bool XBOX_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
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
            XBOX_RenderPoints(renderer, vertices, cmd);
            break;
        }
        case SDL_RENDERCMD_DRAW_LINES:
        {
            XBOX_RenderLines(renderer, vertices, cmd);
            break;
        }
        case SDL_RENDERCMD_FILL_RECTS: // unused
            break;
        case SDL_RENDERCMD_COPY: // unused
            break;
        case SDL_RENDERCMD_COPY_EX: // unused
            break;
        case SDL_RENDERCMD_GEOMETRY:
        {
            XBOX_RenderGeometry(renderer, vertices, cmd);
            break;
        }
        case SDL_RENDERCMD_NO_OP:
            break;
        }
        cmd = cmd->next;
    }
    return true;
}

static SDL_Surface *XBOX_RenderReadPixels(SDL_Renderer *renderer, const SDL_Rect *rect)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    SDL_PixelFormat format = renderer->target ? renderer->target->format : SDL_PIXELFORMAT_ARGB8888;

    SDL_Surface *surface = SDL_CreateSurface(rect->w, rect->h, format);
    if (!surface) {
        return NULL;
    }
    const SDL_PixelFormat dst_format = surface->format;
    const int dst_bytes_per_pixel = SDL_BYTESPERPIXEL(dst_format);
    const int dst_pitch = surface->pitch;
    uint8_t *dst8 = surface->pixels;

    // Need to ensure that the GPU has finished rendering before we read the pixels.
    while (pb_busy()) {
        Sleep(0);
    }
    XVideoFlushFB();

    VIDEO_MODE vm = XVideoGetMode();
    const SDL_PixelFormat src_format = (vm.bpp == 32) ? SDL_PIXELFORMAT_XRGB8888 : (vm.bpp == 16) ? SDL_PIXELFORMAT_RGB565
                                                                                                  : SDL_PIXELFORMAT_XRGB1555;
    const int src_bytes_per_pixel = SDL_BYTESPERPIXEL(dst_format);
    const int src_pitch = vm.width * dst_bytes_per_pixel;
    const uint8_t *src8 = (const uint8_t *)pb_back_buffer();

    SDL_ConvertPixels(rect->w, rect->h,
                      src_format, &src8[rect->y * src_pitch + rect->x * dst_bytes_per_pixel], src_pitch,
                      dst_format, &dst8[rect->y * dst_pitch + rect->x * dst_bytes_per_pixel], dst_pitch);

    return surface;
}

static bool XBOX_RenderPresent(SDL_Renderer *renderer)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;

    if (1 || render_data->vsync_enabled) {
        pb_wait_for_vbl();
    }

    while (pb_busy()) {
        Sleep(0);
    }

    while (pb_finished()) {
        Sleep(0);
    }

    pb_reset();
    pb_target_back_buffer();
    p = pb_begin();
    p = xgu_set_color_clear_value(p, 0xff000000);
    p = xgu_clear_surface(p, XGU_CLEAR_Z | XGU_CLEAR_STENCIL | XGU_CLEAR_COLOR);
    pb_end(p);

    render_data->arena_pointer = 0;
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
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;
    MmFreeContiguousMemory(render_data->arena_buffer);
    SDL_free(render_data);
    renderer->internal = NULL;
}

static bool XBOX_SetVSync(SDL_Renderer *renderer, const int vsync)
{
    xgu_render_data_t *render_data = (xgu_render_data_t *)renderer->internal;

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
    xgu_render_data_t *render_data = (xgu_render_data_t *)SDL_calloc(1, sizeof(xgu_render_data_t));
    if (render_data == NULL) {
        return SDL_OutOfMemory();
    }

    render_data->arena_buffer = MmAllocateContiguousMemoryEx(
        XGU_ARENA_SIZE, 0, 0xFFFFFFFF, 0, PAGE_WRITECOMBINE | PAGE_READWRITE);

    const float m_identity[4 * 4] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };

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

    for (int i = 0; i < XGU_TEXTURE_COUNT; i++) {
        p = xgu_set_texgen_s(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_t(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_r(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texgen_q(p, i, XGU_TEXGEN_DISABLE);
        p = xgu_set_texture_matrix_enable(p, i, false);
        p = xgu_set_texture_matrix(p, i, m_identity);
    }

    for (int i = 0; i < XGU_WEIGHT_COUNT; i++) {
        p = xgu_set_model_view_matrix(p, i, m_identity);
        p = xgu_set_inverse_model_view_matrix(p, i, m_identity);
    }

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
    renderer->QueueSetViewport = XBOX_QueueSetViewport;
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

    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB1555);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_RGB565);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_XRGB8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_RGBA8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ABGR8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_BGRA8888);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_RGBA4444);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_ARGB4444);
    SDL_AddSupportedTextureFormat(renderer, SDL_PIXELFORMAT_XRGB1555);
    SDL_SetNumberProperty(SDL_GetRendererProperties(renderer), SDL_PROP_RENDERER_MAX_TEXTURE_SIZE_NUMBER, 1024 * 1024);

    // Set this hint SDL_GetHint(SDL_HINT_RENDER_LINE_METHOD)
    SDL_SetHint(SDL_HINT_RENDER_LINE_METHOD, "3");

    return true;
}

SDL_RenderDriver nxdk_RenderDriver = {
    XBOX_CreateRenderer, "nxdk_xgu"
};

#endif // SDL_VIDEO_RENDER_XGU
