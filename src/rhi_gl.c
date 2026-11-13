/* rhi_gl.c - OpenGL backend of rhi.h: desktop GL 2.1, or OpenGL ES 2.0 when
 * built with -DN2_GLES. The CPU-side GL state tracker (rhi_gl_state.c) is part
 * of this backend; the context is created through it so it starts from the GL
 * defaults. */
#include "rhi.h"
#include "rhi_gl.h"
#include "rhi_gl_state.h"   /* GL headers; SDL_GL_*Context go through the tracker */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#if defined(__APPLE__) && !defined(N2_GLES)
/* The legacy macOS GL 2.1 context has framebuffer objects only as the EXT
   extension. Bind/Delete Framebuffers/Renderbuffers go through
   rhi_gl_state.c, which maps them to the EXT entry points itself. */
#define glGenFramebuffers glGenFramebuffersEXT
#define glFramebufferTexture2D glFramebufferTexture2DEXT
#define glCheckFramebufferStatus glCheckFramebufferStatusEXT
#define glGenRenderbuffers glGenRenderbuffersEXT
#define glRenderbufferStorage glRenderbufferStorageEXT
#define glFramebufferRenderbuffer glFramebufferRenderbufferEXT
#endif

#ifdef N2_GLES
#  define RHI_GL_BUILD_BACKEND RHI_BACKEND_GLES2
#else
#  define RHI_GL_BUILD_BACKEND RHI_BACKEND_GL
#endif

#define RHI_GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF   /* EXT_texture_filter_anisotropic */
#define RHI_GL_TIME_ELAPSED 0x88BF                 /* EXT_timer_query */
#define RHI_GL_QUERY_RESULT 0x8866
#define RHI_GL_QUERY_RESULT_AVAILABLE 0x8867

/* EXT_timer_query entry points, loaded with the caps (GL 2.1 headers lack them). */
static struct {
    void (*gen)(GLsizei, GLuint *);
    void (*del)(GLsizei, const GLuint *);
    void (*begin)(GLenum, GLuint);
    void (*end)(GLenum);
    void (*get_uiv)(GLuint, GLenum, GLuint *);
    void (*get_ui64v)(GLuint, GLenum, unsigned long long *);
} g_timer_gl;

/* Timer handle id = generation << 24 | (slot + 1), like buffers and textures.
 * Queries belong to the context, so the table is reset with the device. */
#define RHI_GL_MAX_TIMERS 64u

static struct {
    GLuint  name[RHI_GL_MAX_TIMERS];
    uint8_t generation[RHI_GL_MAX_TIMERS];
    uint8_t ended[RHI_GL_MAX_TIMERS];   /* measured at least once: has a result */
    int     open;   /* slot + 1 of the running timer, 0 = none */
} g_timers;

static struct {
    SDL_Window   *window;
    SDL_GLContext context;
    RhiCaps       caps;
    uint64_t      frame_index;
    int32_t       frame_w, frame_h;   /* main target size of the open frame */
    int           in_frame;
} g_rhi;

static RhiBackend resolve(RhiBackend backend) {
    return backend == RHI_BACKEND_DEFAULT ? RHI_GL_BUILD_BACKEND : backend;
}

RhiResult rhi_prepare_window(RhiBackend backend) {
    if (resolve(backend) != RHI_GL_BUILD_BACKEND) return RHI_ERR_UNSUPPORTED;
#ifdef N2_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
#ifdef N2_IOS
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#endif
#elif !defined(__APPLE__)
    /* Desktop Linux/Windows: request a GL 3.3 *compatibility* context so SDL
       creates it through (wgl/glX)CreateContextAttribs, which frame-capture
       tools such as RenderDoc require ("Context not created via
       CreateContextAttribs" otherwise, issue #90). Compatibility keeps the
       legacy GL 2.1 feature set this engine draws with, so output is unchanged;
       rhi_device_create falls back to an implicit context if a driver refuses.
       macOS is left implicit on purpose: it offers no compatibility profile
       above 2.1, and core would need caps/VAO/ImGui changes. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#else
    /* macOS: no profile requested -> the legacy 2.1 context. */
#endif
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    return RHI_OK;
}

uint32_t rhi_window_flags(RhiBackend backend) {
    return resolve(backend) == RHI_GL_BUILD_BACKEND ? (uint32_t)SDL_WINDOW_OPENGL : 0u;
}

static int has_extension(const char *ext, const char *name) {
    return ext && strstr(ext, name) != NULL;
}

static void query_caps(RhiCaps *c) {
    GLint limit[2] = {0, 0};
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    memset(c, 0, sizeof *c);
    c->backend = RHI_GL_BUILD_BACKEND;
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, limit);
    c->max_viewport[0] = limit[0];
    c->max_viewport[1] = limit[1];
    /* DXT1-only drivers expose texture_compression_dxt1 (GLES). */
    c->tex_bc2 = c->tex_bc3 = (uint8_t)has_extension(ext, "GL_EXT_texture_compression_s3tc");
    c->tex_bc1 = (uint8_t)(c->tex_bc3 || has_extension(ext, "texture_compression_dxt1"));
#ifndef N2_GLES
    c->wireframe = 1;   /* glPolygonMode; ES2 has none */
#endif
    c->max_anisotropy = 1.0f;
    if (has_extension(ext, "GL_EXT_texture_filter_anisotropic")) {
        GLfloat mx = 1.0f;
        glGetFloatv(RHI_GL_MAX_TEXTURE_MAX_ANISOTROPY, &mx);
        if (mx > 1.0f) c->max_anisotropy = mx;
    }
    GLint max_texture = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture);
    c->max_texture_size = max_texture;
    memset(&g_timer_gl, 0, sizeof g_timer_gl);
    if (has_extension(ext, "GL_EXT_timer_query")) {
        g_timer_gl.gen = (void (*)(GLsizei, GLuint *))SDL_GL_GetProcAddress("glGenQueries");
        g_timer_gl.del = (void (*)(GLsizei, const GLuint *))SDL_GL_GetProcAddress("glDeleteQueries");
        g_timer_gl.begin = (void (*)(GLenum, GLuint))SDL_GL_GetProcAddress("glBeginQuery");
        g_timer_gl.end = (void (*)(GLenum))SDL_GL_GetProcAddress("glEndQuery");
        g_timer_gl.get_uiv = (void (*)(GLuint, GLenum, GLuint *))SDL_GL_GetProcAddress("glGetQueryObjectuiv");
        g_timer_gl.get_ui64v = (void (*)(GLuint, GLenum, unsigned long long *))
            SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT");
        c->gpu_timer = g_timer_gl.gen && g_timer_gl.del && g_timer_gl.begin && g_timer_gl.end &&
                       g_timer_gl.get_uiv && g_timer_gl.get_ui64v;
    }
}

/* GL reports allocation and upload failures through glGetError. An error left
 * by an earlier call is reported here too, as the callers' own checks did.
 * Bounded: some drivers report errors forever without a context. */
static GLenum gl_take_error(void) {
    GLenum first = glGetError();
    if (first == GL_NO_ERROR) return first;
    for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; i++) {}
    return first;
}

static const char *gl_error_name(GLenum e) {
    switch (e) {
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    default: return "unknown GL error";
    }
}

static RhiResult gl_result(void) {
    GLenum first = gl_take_error();
    if (first == GL_NO_ERROR) return RHI_OK;
    return first == GL_OUT_OF_MEMORY ? RHI_ERR_OUT_OF_MEMORY : RHI_ERR_UNSUPPORTED;
}

RhiResult rhi_device_create(const RhiDeviceDesc *desc) {
    if (!desc || !desc->window || g_rhi.context) return RHI_ERR_INVALID;
    if (resolve(desc->backend) != RHI_GL_BUILD_BACKEND) return RHI_ERR_UNSUPPORTED;
    SDL_Window *win = (SDL_Window *)desc->window;
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
#if !defined(N2_GLES) && !defined(__APPLE__)
    if (!ctx) {
        /* A driver refused the requested 3.3 compatibility context; retry with
           an implicit one (legacy GL, no frame-capture attributes). */
        fprintf(stderr, "GL ctx (3.3 compat): %s -- retrying implicit\n", SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
        ctx = SDL_GL_CreateContext(win);
    }
#endif
    if (!ctx) { fprintf(stderr, "GL ctx: %s\n", SDL_GetError()); return RHI_ERR_UNSUPPORTED; }
    g_rhi.window = win;
    g_rhi.context = ctx;
    g_rhi.frame_index = 0;
    /* Readbacks are tightly packed rows, including odd widths. */
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    query_caps(&g_rhi.caps);
    SDL_GL_SetSwapInterval((desc->flags & RHI_DEVICE_VSYNC) ? 1 : 0);
    return RHI_OK;
}

void rhi_device_destroy(void) {
    if (!g_rhi.context) return;
    SDL_GL_DeleteContext(g_rhi.context);
    memset(&g_rhi, 0, sizeof g_rhi);
    memset(&g_timers, 0, sizeof g_timers);
}

void rhi_drawable_size(int32_t *width, int32_t *height) {
    int w = 0, h = 0;
    if (g_rhi.window) SDL_GL_GetDrawableSize(g_rhi.window, &w, &h);
    if (width) *width = w;
    if (height) *height = h;
}

const RhiCaps *rhi_caps(void) {
    return &g_rhi.caps;
}

RhiResult rhi_frame_begin(RhiFrameInfo *out) {
    if (!g_rhi.context) return RHI_ERR_INVALID;
    rhi_drawable_size(&g_rhi.frame_w, &g_rhi.frame_h);
    g_rhi.in_frame = 1;
    if (out) {
        out->width = g_rhi.frame_w;
        out->height = g_rhi.frame_h;
        out->frame_index = g_rhi.frame_index;
    }
    return RHI_OK;
}

RhiResult rhi_frame_end(void) {
    if (!g_rhi.context) return RHI_ERR_INVALID;
#ifdef N2_IOS
    glBindRenderbuffer(GL_RENDERBUFFER, gls_main_renderbuffer());
#endif
    SDL_GL_SwapWindow(g_rhi.window);
    g_rhi.frame_index++;
    g_rhi.in_frame = 0;
    return RHI_OK;
}

/* GL_RGBA + GL_UNSIGNED_BYTE is the one combination GLES2 guarantees, so both
 * GL flavours read RGBA and convert; GL rows are bottom-up, so they are flipped
 * here. PACK_ALIGNMENT is 1 (rhi_device_create). */
RhiResult rhi_read_pixels(int32_t x, int32_t y, int32_t w, int32_t h,
                          RhiReadFormat format, void *dst, size_t stride) {
    size_t bpp = format == RHI_READ_RGB8 ? 3 : format == RHI_READ_RGBA8 ? 4 : 0;
    if (!g_rhi.in_frame || !dst || !bpp || w <= 0 || h <= 0 || x < 0 || y < 0 ||
        w > g_rhi.frame_w - x || h > g_rhi.frame_h - y) return RHI_ERR_INVALID;
    size_t limit = (size_t)PTRDIFF_MAX;
    if ((size_t)w > limit / 4 / (size_t)h) return RHI_ERR_INVALID;
    size_t row_bytes = (size_t)w * bpp;
    if (!stride) stride = row_bytes;
    if (stride < row_bytes || (size_t)(h - 1) > (limit - row_bytes) / stride)
        return RHI_ERR_INVALID;
    unsigned char *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return RHI_ERR_INVALID;
    glReadPixels(x, g_rhi.frame_h - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    for (int32_t row = 0; row < h; row++) {
        const unsigned char *src = rgba + (size_t)(h - 1 - row) * w * 4;
        unsigned char *out = (unsigned char *)dst + (size_t)row * stride;
        if (bpp == 4) { memcpy(out, src, (size_t)w * 4); continue; }
        for (int32_t i = 0; i < w; i++) {
            out[i*3] = src[i*4]; out[i*3+1] = src[i*4+1]; out[i*3+2] = src[i*4+2];
        }
    }
    free(rgba);
    return RHI_OK;
}

/* ------------------------------------------------------------------------------ */
/* Buffers                                                                        */
/* ------------------------------------------------------------------------------ */
/* Handle id = generation << 24 | (slot + 1). The table is independent of the
 * device so code that creates its own GL context (tests) can use buffers too.
 * Binding goes through the state tracker macros like any other GL caller. */
#define RHI_GL_SLOT_BITS 24u
#define RHI_GL_SLOT_MASK ((1u << RHI_GL_SLOT_BITS) - 1u)

typedef struct {
    GLuint   name;     /* 0 = free slot */
    GLenum   target;   /* GL_ARRAY_BUFFER / GL_ELEMENT_ARRAY_BUFFER */
    GLenum   hint;     /* GL_STATIC_DRAW / GL_STREAM_DRAW */
    uint8_t  generation;
    size_t   size;
} RhiGlBuffer;

static struct {
    RhiGlBuffer *slots;
    uint32_t     count, capacity;
    uint32_t    *free_list;
    uint32_t     free_count;
    unsigned     stale_logged;
} g_buffers;

static RhiGlBuffer *buffer_lookup(RhiBuffer buf, const char *what) {
    uint32_t slot = (buf.id & RHI_GL_SLOT_MASK);
    if (!buf.id) return NULL;
    if (slot && slot <= g_buffers.count) {
        RhiGlBuffer *b = &g_buffers.slots[slot - 1];
        if (b->name && b->generation == (uint8_t)(buf.id >> RHI_GL_SLOT_BITS)) return b;
    }
    if (g_buffers.stale_logged < 8) {
        g_buffers.stale_logged++;
        fprintf(stderr, "rhi: %s: stale or invalid buffer handle 0x%08x\n",
                what, (unsigned)buf.id);
    }
    return NULL;
}

static int buffer_size_ok(size_t size) {
    return size <= (size_t)(((uint64_t)1 << (sizeof(GLsizeiptr) * 8 - 1)) - 1);
}

GLuint rhi_gl_buffer_name(RhiBuffer buf) {
    const RhiGlBuffer *b = buffer_lookup(buf, "bind");
    return b ? b->name : 0;
}

static uint32_t buffer_slot_alloc(void) {
    if (g_buffers.free_count) return g_buffers.free_list[--g_buffers.free_count];
    if (g_buffers.count == g_buffers.capacity) {
        uint32_t cap = g_buffers.capacity ? g_buffers.capacity * 2u : 256u;
        if (cap > RHI_GL_SLOT_MASK) return 0;
        RhiGlBuffer *slots = (RhiGlBuffer *)realloc(g_buffers.slots, cap * sizeof *slots);
        if (!slots) return 0;
        g_buffers.slots = slots;
        uint32_t *list = (uint32_t *)realloc(g_buffers.free_list, cap * sizeof *list);
        if (!list) return 0;
        g_buffers.free_list = list;
        g_buffers.capacity = cap;
    }
    memset(&g_buffers.slots[g_buffers.count], 0, sizeof g_buffers.slots[0]);
    return ++g_buffers.count;
}

RhiBuffer rhi_buffer_create(const RhiBufferDesc *desc) {
    RhiBuffer out = {0};
    if (!desc || (desc->usage != RHI_BUFFER_VERTEX && desc->usage != RHI_BUFFER_INDEX) ||
        (desc->update != RHI_UPDATE_STATIC && desc->update != RHI_UPDATE_STREAM) ||
        !buffer_size_ok(desc->size)) {
        fprintf(stderr, "rhi: buffer_create: invalid description\n");
        return out;
    }
    uint32_t slot = buffer_slot_alloc();
    if (!slot) { fprintf(stderr, "rhi: buffer_create: out of handles\n"); return out; }
    RhiGlBuffer *b = &g_buffers.slots[slot - 1];
    GLuint name = 0;
    glGenBuffers(1, &name);
    if (!name) {
        g_buffers.free_list[g_buffers.free_count++] = slot;
        fprintf(stderr, "rhi: buffer_create: glGenBuffers failed\n");
        return out;
    }
    b->name = name;
    b->target = desc->usage == RHI_BUFFER_INDEX ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
    b->hint = desc->update == RHI_UPDATE_STREAM ? GL_STREAM_DRAW : GL_STATIC_DRAW;
    b->size = 0;
    out.id = ((uint32_t)b->generation << RHI_GL_SLOT_BITS) | slot;
    /* A STREAM buffer without a size gets its storage from the first replace. */
    if (desc->update == RHI_UPDATE_STATIC || desc->size || desc->data) {
        /* STATIC uploads are checked: an error already pending counts as a failure,
           but is named separately from the one glBufferData raised. */
        GLenum stale = desc->update == RHI_UPDATE_STATIC ? gl_take_error() : GL_NO_ERROR;
        glBindBuffer(b->target, name);
        glBufferData(b->target, (GLsizeiptr)desc->size, desc->data, b->hint);
        b->size = desc->size;
        GLenum err = desc->update == RHI_UPDATE_STATIC ? gl_take_error() : GL_NO_ERROR;
        if (stale || err) {
            if (stale)
                fprintf(stderr, "rhi: buffer_create: %zu bytes not uploaded: %s (0x%04x) was already pending before the call\n",
                        desc->size, gl_error_name(stale), (unsigned)stale);
            if (err)
                fprintf(stderr, "rhi: buffer_create: %zu bytes could not be allocated: glBufferData raised %s (0x%04x)\n",
                        desc->size, gl_error_name(err), (unsigned)err);
            rhi_buffer_destroy(out);
            return (RhiBuffer){0};
        }
    }
    return out;
}

RhiResult rhi_buffer_update(RhiBuffer buf, size_t offset, size_t size, const void *data) {
    RhiGlBuffer *b = buffer_lookup(buf, "buffer_update");
    if (!b || !data || offset > b->size || size > b->size - offset) return RHI_ERR_INVALID;
    glBindBuffer(b->target, b->name);
    glBufferSubData(b->target, (GLintptr)offset, (GLsizeiptr)size, data);
    return b->hint == GL_STATIC_DRAW ? gl_result() : RHI_OK;
}

RhiResult rhi_buffer_replace(RhiBuffer buf, size_t size, const void *data) {
    RhiGlBuffer *b = buffer_lookup(buf, "buffer_replace");
    if (!b || !buffer_size_ok(size)) return RHI_ERR_INVALID;
    glBindBuffer(b->target, b->name);
    glBufferData(b->target, (GLsizeiptr)size, data, b->hint);
    b->size = size;
    return b->hint == GL_STATIC_DRAW ? gl_result() : RHI_OK;
}

void rhi_buffer_destroy(RhiBuffer buf) {
    RhiGlBuffer *b = buffer_lookup(buf, "buffer_destroy");
    if (!b) return;
    glDeleteBuffers(1, &b->name);
    b->name = 0;
    b->size = 0;
    /* Retire the slot before its generation wraps and revives stale handles. */
    if (b->generation == UINT8_MAX) return;
    b->generation++;
    g_buffers.free_list[g_buffers.free_count++] = buf.id & RHI_GL_SLOT_MASK;
}

/* ------------------------------------------------------------------------------ */
/* Textures                                                                       */
/* ------------------------------------------------------------------------------ */
/* Same handle scheme as buffers, separate table. GL calls go through the state
 * tracker macros, so the binding they leave behind is tracked. */
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#  define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#  define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#  define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

static const RhiSamplerDesc k_default_sampler = {
    RHI_FILTER_NEAREST, RHI_FILTER_LINEAR, RHI_MIP_LINEAR, RHI_WRAP_REPEAT, RHI_WRAP_REPEAT, 1.0f
};

typedef struct {
    GLuint    name;     /* 0 = free slot */
    GLenum    target;   /* GL_TEXTURE_2D / GL_TEXTURE_CUBE_MAP */
    RhiFormat format;
    uint32_t  usage;
    int32_t   width, height;
    uint8_t   generation;
    RhiSampler sampler;
    int32_t   copied_w, copied_h;   /* level 0 allocated by a copy, 0 = not yet */
    GLint     wrap_s, wrap_t, min_filter, mag_filter;   /* parameters GL has now */
    float     anisotropy;
} RhiGlTexture;

static struct {
    RhiGlTexture *slots;
    uint32_t      count, capacity;
    uint32_t     *free_list;
    uint32_t      free_count;
    unsigned      stale_logged;
} g_textures;

static void rhi_gl_forget_framebuffers(RhiTexture tex);   /* passes, below */

static RhiGlTexture *texture_find(RhiTexture tex) {
    uint32_t slot = tex.id & RHI_GL_SLOT_MASK;
    if (!slot || slot > g_textures.count) return NULL;
    RhiGlTexture *t = &g_textures.slots[slot - 1];
    return t->name && t->generation == (uint8_t)(tex.id >> RHI_GL_SLOT_BITS) ? t : NULL;
}

static RhiGlTexture *texture_lookup(RhiTexture tex, const char *what) {
    RhiGlTexture *t = texture_find(tex);
    if (!t && tex.id && g_textures.stale_logged < 8) {
        g_textures.stale_logged++;
        fprintf(stderr, "rhi: %s: stale or invalid texture handle 0x%08x\n",
                what, (unsigned)tex.id);
    }
    return t;
}

GLuint rhi_gl_texture_name(RhiTexture tex) {
    const RhiGlTexture *t = texture_lookup(tex, "bind");
    return t ? t->name : 0;
}

uint32_t rhi_texture_native_id(RhiTexture tex) {
    return rhi_gl_texture_name(tex);   /* GL object name = native id */
}

int rhi_texture_is_live(RhiTexture tex) {
    return texture_find(tex) != NULL;
}

static uint32_t texture_slot_alloc(void) {
    if (g_textures.free_count) return g_textures.free_list[--g_textures.free_count];
    if (g_textures.count == g_textures.capacity) {
        uint32_t cap = g_textures.capacity ? g_textures.capacity * 2u : 1024u;
        if (cap > RHI_GL_SLOT_MASK) return 0;
        RhiGlTexture *slots = (RhiGlTexture *)realloc(g_textures.slots, cap * sizeof *slots);
        if (!slots) return 0;
        g_textures.slots = slots;
        uint32_t *list = (uint32_t *)realloc(g_textures.free_list, cap * sizeof *list);
        if (!list) return 0;
        g_textures.free_list = list;
        g_textures.capacity = cap;
    }
    memset(&g_textures.slots[g_textures.count], 0, sizeof g_textures.slots[0]);
    return ++g_textures.count;
}

static int format_compressed(RhiFormat f) {
    return f == RHI_FORMAT_BC1 || f == RHI_FORMAT_BC2 || f == RHI_FORMAT_BC3;
}

/* Texture work (create, upload, sampler, mips, copies) binds on the unit after
 * the last bindable slot, then the caller's active unit is selected again, so the
 * bindings rhi_cmd_bind_texture made stay as they were. */
#define RHI_GL_WORK_UNIT (GL_TEXTURE0 + RHI_TEXTURE_SLOTS)
static GLint work_unit_bind(GLenum target, GLuint name) {
    GLint active = 0;
    gls_get_integerv(GL_ACTIVE_TEXTURE, &active);
    if (active != (GLint)RHI_GL_WORK_UNIT) glActiveTexture(RHI_GL_WORK_UNIT);
    glBindTexture(target, name);
    return active;
}
static void work_unit_done(GLint active) {
    if (active != (GLint)RHI_GL_WORK_UNIT) glActiveTexture((GLenum)active);
}

RhiTexture rhi_texture_create(const RhiTextureDesc *desc) {
    RhiTexture out = {0};
    int depth = desc && desc->format == RHI_FORMAT_D16;
    if (!desc || (desc->type != RHI_TEXTURE_2D && desc->type != RHI_TEXTURE_CUBE) ||
        desc->format < RHI_FORMAT_RGB8 || desc->format > RHI_FORMAT_DEPTH ||
        desc->width <= 0 || desc->height <= 0 || !desc->usage ||
        (desc->usage & ~(uint32_t)(RHI_TEXTURE_SAMPLED | RHI_TEXTURE_COLOR_TARGET |
                                   RHI_TEXTURE_DEPTH_TARGET | RHI_TEXTURE_COPY_DST)) ||
        (desc->format == RHI_FORMAT_DEPTH &&
         (desc->type != RHI_TEXTURE_2D || desc->usage != (RHI_TEXTURE_SAMPLED | RHI_TEXTURE_COPY_DST))) ||
        (depth && (desc->type != RHI_TEXTURE_2D || desc->usage != RHI_TEXTURE_DEPTH_TARGET)) ||
        (!depth && (desc->usage & RHI_TEXTURE_DEPTH_TARGET)) ||
        (desc->type == RHI_TEXTURE_CUBE && desc->width != desc->height)) {
        fprintf(stderr, "rhi: texture_create: invalid description\n");
        return out;
    }
    RhiSampler sampler = rhi_sampler_create(&k_default_sampler);
    if (!sampler.id) return out;
    uint32_t slot = texture_slot_alloc();
    if (!slot) { fprintf(stderr, "rhi: texture_create: out of handles\n"); return out; }
    RhiGlTexture *t = &g_textures.slots[slot - 1];
    GLuint name = 0;
    if (depth) glGenRenderbuffers(1, &name);   /* depth target, not sampled */
    else glGenTextures(1, &name);
    if (!name) {
        g_textures.free_list[g_textures.free_count++] = slot;
        fprintf(stderr, "rhi: texture_create: glGenTextures failed\n");
        return out;
    }
    t->name = name;
    t->target = depth ? GL_RENDERBUFFER
              : desc->type == RHI_TEXTURE_CUBE ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
    t->format = desc->format;
    t->usage = desc->usage;
    t->width = desc->width;
    t->height = desc->height;
    t->copied_w = t->copied_h = 0;
    t->sampler = sampler;
    t->wrap_s = t->wrap_t = GL_REPEAT;               /* GL's initial parameters */
    t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
    t->mag_filter = GL_LINEAR;
    t->anisotropy = 1.0f;
    out.id = ((uint32_t)t->generation << RHI_GL_SLOT_BITS) | slot;
    if (depth) {
        glBindRenderbuffer(GL_RENDERBUFFER, name);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, desc->width, desc->height);
        if (gl_result() != RHI_OK) {
            fprintf(stderr, "rhi: texture_create: %dx%d depth target could not be allocated\n",
                    (int)desc->width, (int)desc->height);
            rhi_texture_destroy(out);
            return (RhiTexture){0};
        }
        return out;
    }
    work_unit_done(work_unit_bind(t->target, name));
    return out;
}

RhiResult rhi_texture_upload(RhiTexture tex, int32_t face, int32_t level,
                             const void *data, size_t size) {
    RhiGlTexture *t = texture_lookup(tex, "texture_upload");
    int faces = t && t->target == GL_TEXTURE_CUBE_MAP ? 6 : 1;
    if (!t || t->target == GL_RENDERBUFFER || t->format == RHI_FORMAT_DEPTH ||
        face < 0 || face >= faces || level < 0 || level > 30)
        return RHI_ERR_INVALID;
    GLsizei w = t->width >> level, h = t->height >> level;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    GLenum target = faces == 6 ? (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : GL_TEXTURE_2D;
    if (format_compressed(t->format) && (!data || !size || size > 0x7fffffff)) return RHI_ERR_INVALID;
    GLint active = work_unit_bind(t->target, t->name);
    if (format_compressed(t->format)) {
        GLenum fmt = t->format == RHI_FORMAT_BC1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                   : t->format == RHI_FORMAT_BC2 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                                                 : GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
        glCompressedTexImage2D(target, level, fmt, w, h, 0, (GLsizei)size, data);
    } else {
        GLenum fmt = t->format == RHI_FORMAT_RGB8 ? GL_RGB : GL_RGBA;
        glTexImage2D(target, level, (GLint)fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, data);
    }
    work_unit_done(active);
    return gl_result();
}

RhiResult rhi_texture_generate_mips(RhiTexture tex) {
    RhiGlTexture *t = texture_lookup(tex, "texture_generate_mips");
    if (!t || t->target != GL_TEXTURE_2D ||
        (t->format != RHI_FORMAT_RGB8 && t->format != RHI_FORMAT_RGBA8)) return RHI_ERR_INVALID;
    GLint active = work_unit_bind(GL_TEXTURE_2D, t->name);
    glGenerateMipmap(GL_TEXTURE_2D);
    work_unit_done(active);
    return gl_result();
}

void rhi_texture_destroy(RhiTexture tex) {
    RhiGlTexture *t = texture_lookup(tex, "texture_destroy");
    if (!t) return;
    rhi_gl_forget_framebuffers(tex);
    if (t->target == GL_RENDERBUFFER) glDeleteRenderbuffers(1, &t->name);
    else glDeleteTextures(1, &t->name);
    t->name = 0;
    /* Retire the slot before its generation can alias an old handle. */
    if (t->generation == UINT8_MAX) return;
    t->generation++;
    g_textures.free_list[g_textures.free_count++] = tex.id & RHI_GL_SLOT_MASK;
}

/* ------------------------------------------------------------------------------ */
/* Samplers                                                                       */
/* ------------------------------------------------------------------------------ */
/* A short list searched by value; handle id = index + 1. The game uses a
 * handful of samplers (wrap x filter x anisotropy setting). */
#define RHI_GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE

static struct {
    RhiSamplerDesc *descs;
    uint32_t        count, capacity;
} g_samplers;

static int sampler_desc_valid(const RhiSamplerDesc *d) {
    return d && (d->min_filter == RHI_FILTER_NEAREST || d->min_filter == RHI_FILTER_LINEAR) &&
           (d->mag_filter == RHI_FILTER_NEAREST || d->mag_filter == RHI_FILTER_LINEAR) &&
           d->mip_filter >= RHI_MIP_NONE && d->mip_filter <= RHI_MIP_LINEAR &&
           (d->wrap_u == RHI_WRAP_REPEAT || d->wrap_u == RHI_WRAP_CLAMP) &&
           (d->wrap_v == RHI_WRAP_REPEAT || d->wrap_v == RHI_WRAP_CLAMP) &&
           d->max_anisotropy >= 1.0f;
}

RhiSampler rhi_sampler_create(const RhiSamplerDesc *desc) {
    RhiSampler out = {0};
    if (!sampler_desc_valid(desc)) {
        fprintf(stderr, "rhi: sampler_create: invalid description\n");
        return out;
    }
    for (uint32_t i = 0; i < g_samplers.count; i++) {
        const RhiSamplerDesc *d = &g_samplers.descs[i];
        if (d->min_filter == desc->min_filter && d->mag_filter == desc->mag_filter &&
            d->mip_filter == desc->mip_filter && d->wrap_u == desc->wrap_u &&
            d->wrap_v == desc->wrap_v && d->max_anisotropy == desc->max_anisotropy) {
            out.id = i + 1;
            return out;
        }
    }
    if (g_samplers.count == g_samplers.capacity) {
        uint32_t cap = g_samplers.capacity ? g_samplers.capacity * 2u : 16u;
        RhiSamplerDesc *descs = (RhiSamplerDesc *)realloc(g_samplers.descs, cap * sizeof *descs);
        if (!descs) { fprintf(stderr, "rhi: sampler_create: out of memory\n"); return out; }
        g_samplers.descs = descs;
        g_samplers.capacity = cap;
    }
    g_samplers.descs[g_samplers.count++] = *desc;
    out.id = g_samplers.count;
    return out;
}

const RhiSamplerDesc *rhi_sampler_desc(RhiSampler sampler) {
    if (!sampler.id || sampler.id > g_samplers.count) return NULL;
    return &g_samplers.descs[sampler.id - 1];
}

RhiSampler rhi_texture_sampler(RhiTexture tex) {
    const RhiGlTexture *t = texture_lookup(tex, "texture_sampler");
    return t ? t->sampler : (RhiSampler){0};
}

static GLint gl_min_filter(const RhiSamplerDesc *d) {
    static const GLint table[2][3] = {
        { GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR },
        { GL_LINEAR,  GL_LINEAR_MIPMAP_NEAREST,  GL_LINEAR_MIPMAP_LINEAR  },
    };
    return table[d->min_filter][d->mip_filter];
}

/* Only parameters that differ from what the texture has are set. Anisotropy
 * is left alone when the driver has no anisotropic filtering. */
RhiResult rhi_texture_set_sampler(RhiTexture tex, RhiSampler sampler) {
    RhiGlTexture *t = texture_lookup(tex, "texture_set_sampler");
    const RhiSamplerDesc *d = rhi_sampler_desc(sampler);
    if (!t || !d || t->target == GL_RENDERBUFFER) return RHI_ERR_INVALID;
    GLint wrap_s = d->wrap_u == RHI_WRAP_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    GLint wrap_t = d->wrap_v == RHI_WRAP_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT;
    GLint min_filter = gl_min_filter(d);
    GLint mag_filter = d->mag_filter == RHI_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST;
    float aniso = 1.0f;
    if (g_rhi.caps.max_anisotropy > 1.0f)
        aniso = d->max_anisotropy < g_rhi.caps.max_anisotropy ? d->max_anisotropy
                                                              : g_rhi.caps.max_anisotropy;
    GLint active = work_unit_bind(t->target, t->name);
    if (wrap_s != t->wrap_s) glTexParameteri(t->target, GL_TEXTURE_WRAP_S, wrap_s);
    if (wrap_t != t->wrap_t) glTexParameteri(t->target, GL_TEXTURE_WRAP_T, wrap_t);
    if (min_filter != t->min_filter) glTexParameteri(t->target, GL_TEXTURE_MIN_FILTER, min_filter);
    if (mag_filter != t->mag_filter) glTexParameteri(t->target, GL_TEXTURE_MAG_FILTER, mag_filter);
    if (aniso != t->anisotropy)
        glTexParameterf(t->target, RHI_GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    t->wrap_s = wrap_s; t->wrap_t = wrap_t;
    t->min_filter = min_filter; t->mag_filter = mag_filter;
    t->anisotropy = aniso;
    t->sampler = sampler;
    work_unit_done(active);
    return RHI_OK;
}

/* ------------------------------------------------------------------------------ */
/* Shaders                                                                        */
/* ------------------------------------------------------------------------------ */
/* A handful of programs: a fixed table with generation-checked ids. */
#define RHI_GL_MAX_SHADERS 64u
#define RHI_GL_MAX_PARAM_BLOCKS 4u

/* One parameter block of a program: the game-visible storage, the bytes last
 * uploaded (GL's own values: zero after linking) and a location per field. */
typedef struct {
    const RhiParamLayout *layout;   /* the caller's table, kept by pointer */
    unsigned char        *data, *uploaded;
    GLint                *location;
} RhiGlParamBlock;

static struct {
    GLuint          program[RHI_GL_MAX_SHADERS];   /* 0 = free slot */
    uint8_t         generation[RHI_GL_MAX_SHADERS];
    RhiGlParamBlock block[RHI_GL_MAX_SHADERS][RHI_GL_MAX_PARAM_BLOCKS];
    uint32_t        block_count[RHI_GL_MAX_SHADERS];
} g_shaders;

static void param_blocks_free(int slot) {
    for (uint32_t b = 0; b < g_shaders.block_count[slot]; b++) {
        RhiGlParamBlock *pb = &g_shaders.block[slot][b];
        free(pb->data); free(pb->uploaded); free(pb->location);
        memset(pb, 0, sizeof *pb);
    }
    g_shaders.block_count[slot] = 0;
}

/* ---- std140 checks ---- */
static uint32_t param_align(RhiParamType t) {
    return t == RHI_PARAM_VEC2 ? 8u : (t == RHI_PARAM_VEC3 || t == RHI_PARAM_VEC4 ||
                                       t == RHI_PARAM_MAT4) ? 16u : 4u;
}
static uint32_t param_size(RhiParamType t) {
    switch (t) {
    case RHI_PARAM_VEC2: return 8u;
    case RHI_PARAM_VEC3: return 12u;
    case RHI_PARAM_VEC4: return 16u;
    case RHI_PARAM_MAT4: return 64u;
    default:             return 4u;
    }
}

RhiResult rhi_param_layout_check(const RhiParamLayout *l) {
    const char *name = l && l->name ? l->name : "params";
    if (!l || !l->fields || !l->field_count) {
        fprintf(stderr, "rhi: %s: empty parameter layout\n", name);
        return RHI_ERR_INVALID;
    }
    for (uint32_t i = 0; i < l->field_count; i++) {
        const RhiParamField *f = &l->fields[i];
        if (!f->name || f->type < RHI_PARAM_FLOAT || f->type > RHI_PARAM_INT || !f->count) {
            fprintf(stderr, "rhi: %s: field %u is malformed\n", name, (unsigned)i);
            return RHI_ERR_INVALID;
        }
        if (f->count > 1 && f->type != RHI_PARAM_VEC4 && f->type != RHI_PARAM_MAT4) {
            fprintf(stderr, "rhi: %s: %s: std140 arrays must be vec4 or mat4\n", name, f->name);
            return RHI_ERR_INVALID;
        }
        uint64_t end = (uint64_t)f->offset + (uint64_t)param_size(f->type) * f->count;
        if (f->offset % param_align(f->type) || end > l->size || end < f->offset) {
            fprintf(stderr, "rhi: %s: %s: offset %u breaks std140 alignment or the block size\n",
                    name, f->name, (unsigned)f->offset);
            return RHI_ERR_INVALID;
        }
        for (uint32_t j = 0; j < i; j++) {
            const RhiParamField *g = &l->fields[j];
            uint64_t gend = (uint64_t)g->offset + (uint64_t)param_size(g->type) * g->count;
            if (f->offset < gend && g->offset < end) {
                fprintf(stderr, "rhi: %s: %s overlaps %s\n", name, f->name, g->name);
                return RHI_ERR_INVALID;
            }
        }
    }
    return RHI_OK;
}

static int shader_slot(RhiShader shader) {
    uint32_t slot = shader.id & RHI_GL_SLOT_MASK;
    if (!slot || slot > RHI_GL_MAX_SHADERS || !g_shaders.program[slot - 1] ||
        g_shaders.generation[slot - 1] != (uint8_t)(shader.id >> RHI_GL_SLOT_BITS)) return -1;
    return (int)slot - 1;
}

GLuint rhi_gl_shader_program(RhiShader shader) {
    int i = shader_slot(shader);
    return i < 0 ? 0 : g_shaders.program[i];
}

uint32_t rhi_shader_native_program(RhiShader shader) {
    return rhi_gl_shader_program(shader);   /* GL program name = native id */
}

static GLuint shader_compile(GLenum type, const char *src, const char *label) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "rhi: %s %s shader: %s\n", label,
                type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

RhiShader rhi_shader_create(const RhiShaderDesc *desc) {
    RhiShader out = {0};
    if (!desc || !desc->vertex_source || !desc->fragment_source ||
        (desc->attribute_count && !desc->attribute_names)) {
        fprintf(stderr, "rhi: shader_create: invalid description\n");
        return out;
    }
    const char *label = desc->label ? desc->label : "unnamed";
    int slot = -1;
    for (uint32_t i = 0; i < RHI_GL_MAX_SHADERS && slot < 0; i++)
        if (!g_shaders.program[i] && g_shaders.generation[i] != UINT8_MAX) slot = (int)i;
    if (slot < 0) { fprintf(stderr, "rhi: shader_create: out of handles\n"); return out; }
    GLuint v = shader_compile(GL_VERTEX_SHADER, desc->vertex_source, label);
    GLuint f = shader_compile(GL_FRAGMENT_SHADER, desc->fragment_source, label);
    if (!v || !f) {
        if (v) glDeleteShader(v);
        if (f) glDeleteShader(f);
        return out;
    }
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    for (uint32_t i = 0; i < desc->attribute_count; i++)
        glBindAttribLocation(p, i, desc->attribute_names[i]);
    glLinkProgram(p);
    glDeleteShader(v);   /* freed with the program */
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, (GLsizei)sizeof log, NULL, log);
        fprintf(stderr, "rhi: %s shader link: %s\n", label, log);
        glDeleteProgram(p);
        return out;
    }
    if (desc->param_block_count > RHI_GL_MAX_PARAM_BLOCKS ||
        (desc->param_block_count && !desc->param_blocks)) {
        fprintf(stderr, "rhi: %s: too many parameter blocks\n", label);
        glDeleteProgram(p);
        return out;
    }
    for (uint32_t b = 0; b < desc->param_block_count; b++) {
        const RhiParamLayout *l = desc->param_blocks[b];
        RhiGlParamBlock *pb = &g_shaders.block[slot][b];
        g_shaders.block_count[slot] = b + 1;
        if (!l || rhi_param_layout_check(l) != RHI_OK ||
            !(pb->data = (unsigned char *)calloc(1, l->size)) ||
            !(pb->uploaded = (unsigned char *)calloc(1, l->size)) ||
            !(pb->location = (GLint *)malloc(l->field_count * sizeof *pb->location))) {
            fprintf(stderr, "rhi: %s: parameter block %u unusable\n", label, (unsigned)b);
            param_blocks_free(slot);
            glDeleteProgram(p);
            return out;
        }
        pb->layout = l;
        for (uint32_t i = 0; i < l->field_count; i++)   /* -1: unused by the shader */
            pb->location[i] = glGetUniformLocation(p, l->fields[i].name);
    }
    g_shaders.program[slot] = p;
    out.id = ((uint32_t)g_shaders.generation[slot] << RHI_GL_SLOT_BITS) | (uint32_t)(slot + 1);
    return out;
}

void *rhi_shader_params(RhiShader shader, uint32_t block) {
    int i = shader_slot(shader);
    if (i < 0 || block >= g_shaders.block_count[i]) return NULL;
    return g_shaders.block[i][block].data;
}

static void param_upload(const RhiParamField *f, GLint loc, const void *v) {
    GLsizei n = (GLsizei)f->count;
    switch (f->type) {
    case RHI_PARAM_FLOAT: glUniform1fv(loc, n, (const GLfloat *)v); break;
    case RHI_PARAM_VEC2:  glUniform2fv(loc, n, (const GLfloat *)v); break;
    case RHI_PARAM_VEC3:  glUniform3fv(loc, n, (const GLfloat *)v); break;
    case RHI_PARAM_VEC4:  glUniform4fv(loc, n, (const GLfloat *)v); break;
    case RHI_PARAM_MAT4:  glUniformMatrix4fv(loc, n, GL_FALSE, (const GLfloat *)v); break;
    case RHI_PARAM_INT:   glUniform1i(loc, *(const GLint *)v); break;
    }
}

void rhi_gl_flush_params(void) {
    static int last = -1;   /* the shader found last time, usually the same */
    GLint current = 0;
    gls_get_integerv(GL_CURRENT_PROGRAM, &current);
    if (!current) return;
    int slot = -1;
    if (last >= 0 && g_shaders.program[last] == (GLuint)current) slot = last;
    for (uint32_t i = 0; slot < 0 && i < RHI_GL_MAX_SHADERS; i++)
        if (g_shaders.program[i] == (GLuint)current) slot = (int)i;
    if (slot < 0) return;   /* a program not created through rhi_shader_create */
    last = slot;
    for (uint32_t b = 0; b < g_shaders.block_count[slot]; b++) {
        RhiGlParamBlock *pb = &g_shaders.block[slot][b];
        const RhiParamLayout *l = pb->layout;
        for (uint32_t i = 0; i < l->field_count; i++) {
            const RhiParamField *f = &l->fields[i];
            size_t off = f->offset, n = (size_t)param_size(f->type) * f->count;
            if (pb->location[i] < 0 || !memcmp(pb->data + off, pb->uploaded + off, n)) continue;
            param_upload(f, pb->location[i], pb->data + off);
            memcpy(pb->uploaded + off, pb->data + off, n);
        }
    }
}

void rhi_shader_destroy(RhiShader shader) {
    int i = shader_slot(shader);
    if (i < 0) return;
    glDeleteProgram(g_shaders.program[i]);
    param_blocks_free(i);
    g_shaders.program[i] = 0;
    if (g_shaders.generation[i] != UINT8_MAX) g_shaders.generation[i]++;
}

/* ------------------------------------------------------------------------------ */
/* Pipelines and draws                                                            */
/* ------------------------------------------------------------------------------ */
static struct {
    RhiPipelineDesc *desc;
    uint32_t        count, capacity;
} g_pipelines;

static struct {
    int        pipeline;                          /* index, -1 = none bound */
    RhiBuffer  vertex[RHI_MAX_VERTEX_STREAMS];
    uint32_t   vertex_offset[RHI_MAX_VERTEX_STREAMS];
    RhiBuffer  index;
} g_cmd = { -1, {{0}}, {0}, {0} };

void rhi_pipeline_desc_init(RhiPipelineDesc *d) {
    memset(d, 0, sizeof *d);
    d->primitive = RHI_PRIMITIVE_TRIANGLES;
    d->cull = RHI_CULL_NONE;
    d->depth_test = 0;
    d->depth_write = 1;
    d->depth_compare = RHI_COMPARE_LESS;
    d->blend = 0;
    d->src_rgb = d->src_alpha = RHI_BLEND_ONE;
    d->dst_rgb = d->dst_alpha = RHI_BLEND_ZERO;
    d->color_write = 0xF;
    d->line_width = 1.0f;
    d->stencil_test = 0;
    d->stencil_compare = RHI_COMPARE_ALWAYS;
    d->stencil_ref = 0;
    d->stencil_read_mask = d->stencil_write_mask = 0xFF;
    d->stencil_fail = d->stencil_depth_fail = d->stencil_pass = RHI_STENCIL_KEEP;
}

static int vertex_layout_equal(const RhiVertexLayout *a, const RhiVertexLayout *b) {
    if (a->attrib_count != b->attrib_count) return 0;
    for (uint32_t i = 0; i < RHI_MAX_VERTEX_STREAMS; i++)
        if (a->stride[i] != b->stride[i]) return 0;
    for (uint32_t i = 0; i < a->attrib_count; i++) {
        const RhiVertexAttrib *x = &a->attribs[i], *y = &b->attribs[i];
        if (x->location != y->location || x->stream != y->stream || x->offset != y->offset ||
            x->format != y->format) return 0;
    }
    return 1;
}

static int pipeline_desc_equal(const RhiPipelineDesc *a, const RhiPipelineDesc *b) {
    return a->shader.id == b->shader.id && vertex_layout_equal(&a->vertex, &b->vertex) &&
           a->primitive == b->primitive && a->cull == b->cull &&
           !a->depth_test == !b->depth_test && !a->depth_write == !b->depth_write &&
           a->depth_compare == b->depth_compare && !a->blend == !b->blend &&
           a->src_rgb == b->src_rgb && a->dst_rgb == b->dst_rgb &&
           a->src_alpha == b->src_alpha && a->dst_alpha == b->dst_alpha &&
           (a->color_write & 0xF) == (b->color_write & 0xF) &&
           (a->line_width > 0 ? a->line_width : 1.0f) == (b->line_width > 0 ? b->line_width : 1.0f) &&
           !a->stencil_test == !b->stencil_test && a->stencil_compare == b->stencil_compare &&
           a->stencil_ref == b->stencil_ref && a->stencil_read_mask == b->stencil_read_mask &&
           a->stencil_write_mask == b->stencil_write_mask && a->stencil_fail == b->stencil_fail &&
           a->stencil_depth_fail == b->stencil_depth_fail && a->stencil_pass == b->stencil_pass;
}

static uint32_t vertex_format_bytes(RhiVertexFormat format) {
    static const uint32_t bytes[] = {0, 8, 12, 16, 4, 4};
    return format >= RHI_VERTEX_FLOAT2 && format <= RHI_VERTEX_FLOAT1 ? bytes[format] : 0;
}

RhiPipeline rhi_pipeline_create(const RhiPipelineDesc *desc) {
    RhiPipeline out = {0};
    if (!desc || shader_slot(desc->shader) < 0 ||
        desc->vertex.attrib_count > RHI_MAX_VERTEX_ATTRIBS ||
        desc->primitive > RHI_PRIMITIVE_LINES || desc->cull > RHI_CULL_FRONT ||
        desc->depth_compare > RHI_COMPARE_ALWAYS || desc->stencil_compare > RHI_COMPARE_ALWAYS ||
        desc->stencil_fail > RHI_STENCIL_INVERT || desc->stencil_depth_fail > RHI_STENCIL_INVERT ||
        desc->stencil_pass > RHI_STENCIL_INVERT ||
        desc->src_rgb > RHI_BLEND_ONE_MINUS_DST_COLOR || desc->dst_rgb > RHI_BLEND_ONE_MINUS_DST_COLOR ||
        desc->src_alpha > RHI_BLEND_ONE_MINUS_DST_COLOR || desc->dst_alpha > RHI_BLEND_ONE_MINUS_DST_COLOR) {
        fprintf(stderr, "rhi: pipeline_create: invalid description\n");
        return out;
    }
    uint32_t locations = 0;
    for (uint32_t i = 0; i < desc->vertex.attrib_count; i++) {
        const RhiVertexAttrib *a = &desc->vertex.attribs[i];
        uint32_t bytes = vertex_format_bytes(a->format);
        if (a->stream >= RHI_MAX_VERTEX_STREAMS || !bytes ||
            a->location >= RHI_MAX_VERTEX_ATTRIBS || (locations & (1u << a->location)) ||
            desc->vertex.stride[a->stream] > INT_MAX ||
            (uint64_t)a->offset + bytes > (desc->vertex.stride[a->stream] ? desc->vertex.stride[a->stream] : bytes)) {
            fprintf(stderr, "rhi: pipeline_create: invalid vertex attribute %u\n", (unsigned)i);
            return out;
        }
        locations |= 1u << a->location;
    }
    for (uint32_t i = 0; i < g_pipelines.count; i++)
        if (pipeline_desc_equal(&g_pipelines.desc[i], desc)) { out.id = i + 1; return out; }
    if (g_pipelines.count == g_pipelines.capacity) {
        uint32_t cap = g_pipelines.capacity ? g_pipelines.capacity * 2u : 64u;
        if (cap > INT_MAX || cap < g_pipelines.capacity ||
            (size_t)cap > SIZE_MAX / sizeof *g_pipelines.desc) {
            fprintf(stderr, "rhi: pipeline_create: out of handles\n"); return out;
        }
        RhiPipelineDesc *next = realloc(g_pipelines.desc, (size_t)cap * sizeof *next);
        if (!next) { fprintf(stderr, "rhi: pipeline_create: out of memory\n"); return out; }
        g_pipelines.desc = next;
        g_pipelines.capacity = cap;
    }
    g_pipelines.desc[g_pipelines.count++] = *desc;
    out.id = g_pipelines.count;
    return out;
}

static void set_cap(GLenum cap, int on) {
    if (!gls_is_enabled(cap) != !on) { if (on) glEnable(cap); else glDisable(cap); }
}

static GLenum gl_compare(RhiCompare c) {
    static const GLenum t[] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL,
                                GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
    return t[c];
}
static GLenum gl_stencil_op(RhiStencilOp op) {
    static const GLenum t[] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT };
    return t[op];
}
static GLenum gl_blend(RhiBlendFactor f) {
    static const GLenum t[] = { GL_ZERO, GL_ONE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_SRC_COLOR,
                                GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR };
    return t[f];
}

void rhi_cmd_bind_pipeline(RhiPipeline pipeline) {
    if (!pipeline.id || pipeline.id > g_pipelines.count) {
        fprintf(stderr, "rhi: bind_pipeline: invalid handle %u\n", (unsigned)pipeline.id);
        g_cmd.pipeline = -1;
        return;
    }
    const RhiPipelineDesc *d = &g_pipelines.desc[pipeline.id - 1];
    if (shader_slot(d->shader) < 0) {
        fprintf(stderr, "rhi: bind_pipeline: destroyed shader\n");
        g_cmd.pipeline = -1;
        return;
    }
    g_cmd.pipeline = (int)pipeline.id - 1;
    GLint current = 0;
    GLuint program = rhi_gl_shader_program(d->shader);
    gls_get_integerv(GL_CURRENT_PROGRAM, &current);
    if ((GLuint)current != program) glUseProgram(program);
    /* Only what differs from the tracked GL state is sent. */
    set_cap(GL_DEPTH_TEST, d->depth_test);
    if (d->depth_test) {
        GLint func = 0;
        gls_get_integerv(GL_DEPTH_FUNC, &func);
        if ((GLenum)func != gl_compare(d->depth_compare)) glDepthFunc(gl_compare(d->depth_compare));
    }
    GLboolean mask = GL_FALSE;
    gls_get_booleanv(GL_DEPTH_WRITEMASK, &mask);
    if (!mask != !d->depth_write) glDepthMask(d->depth_write ? GL_TRUE : GL_FALSE);
    set_cap(GL_BLEND, d->blend);
    if (d->blend) {
        GLint eq[2];   /* the equation is always ADD (rhi.h has no blend op) */
        gls_get_integerv(GL_BLEND_EQUATION_RGB, &eq[0]); gls_get_integerv(GL_BLEND_EQUATION_ALPHA, &eq[1]);
        if ((GLenum)eq[0] != GL_FUNC_ADD || (GLenum)eq[1] != GL_FUNC_ADD)
            glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        GLint f[4];
        gls_get_integerv(GL_BLEND_SRC_RGB, &f[0]); gls_get_integerv(GL_BLEND_DST_RGB, &f[1]);
        gls_get_integerv(GL_BLEND_SRC_ALPHA, &f[2]); gls_get_integerv(GL_BLEND_DST_ALPHA, &f[3]);
        GLenum want[4] = { gl_blend(d->src_rgb), gl_blend(d->dst_rgb),
                           gl_blend(d->src_alpha), gl_blend(d->dst_alpha) };
        if ((GLenum)f[0] != want[0] || (GLenum)f[1] != want[1] ||
            (GLenum)f[2] != want[2] || (GLenum)f[3] != want[3])
            glBlendFuncSeparate(want[0], want[1], want[2], want[3]);
    }
    set_cap(GL_CULL_FACE, d->cull != RHI_CULL_NONE);
    set_cap(GL_POLYGON_OFFSET_FILL, 0);
    if (d->cull != RHI_CULL_NONE) glCullFace(d->cull == RHI_CULL_BACK ? GL_BACK : GL_FRONT);
    {   /* line width (no effect on triangles); only sent when it changes */
        GLfloat lw = 1.0f, want = d->line_width > 0 ? d->line_width : 1.0f;
        gls_get_floatv(GL_LINE_WIDTH, &lw);
        if (lw != want) glLineWidth(want);
    }
    GLboolean cm[4];
    gls_get_booleanv(GL_COLOR_WRITEMASK, cm);
    GLboolean want_cm[4] = { (d->color_write & 1) ? GL_TRUE : GL_FALSE, (d->color_write & 2) ? GL_TRUE : GL_FALSE,
                             (d->color_write & 4) ? GL_TRUE : GL_FALSE, (d->color_write & 8) ? GL_TRUE : GL_FALSE };
    if (memcmp(cm, want_cm, sizeof cm)) glColorMask(want_cm[0], want_cm[1], want_cm[2], want_cm[3]);
    /* Stencil functions are not tracked: set whenever the test is on, left as
       they are when it is off (like the blend factors). */
    set_cap(GL_STENCIL_TEST, d->stencil_test);
    if (d->stencil_test) {
        glStencilMask(d->stencil_write_mask);
        glStencilFunc(gl_compare(d->stencil_compare), d->stencil_ref, d->stencil_read_mask);
        glStencilOp(gl_stencil_op(d->stencil_fail), gl_stencil_op(d->stencil_depth_fail),
                    gl_stencil_op(d->stencil_pass));
    }
}

void rhi_cmd_bind_vertex_buffer(uint32_t stream, RhiBuffer buffer, uint32_t offset) {
    if (stream >= RHI_MAX_VERTEX_STREAMS) return;
    g_cmd.vertex[stream] = buffer;
    g_cmd.vertex_offset[stream] = offset;
}

void rhi_cmd_bind_index_buffer(RhiBuffer buffer) { g_cmd.index = buffer; }

void rhi_cmd_bind_texture(uint32_t slot, RhiTexture texture) {
    if (slot >= RHI_TEXTURE_SLOTS) {
        fprintf(stderr, "rhi: bind_texture: slot %u out of range\n", (unsigned)slot);
        return;
    }
    const RhiGlTexture *t = texture_lookup(texture, "bind_texture");
    if (t && !(t->usage & RHI_TEXTURE_SAMPLED)) t = NULL;   /* not sampleable */
    GLint active = 0;
    gls_get_integerv(GL_ACTIVE_TEXTURE, &active);
    if ((GLenum)active != GL_TEXTURE0 + slot) glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(t ? t->target : GL_TEXTURE_2D, t ? t->name : 0);
}

/* Vertex setup for a draw: per layout attribute (in layout order) its buffer,
 * enable and pointer -- the GL calls, in the same order, that the draw code
 * issued before rhi.h had draws -- then attributes the layout does not name
 * are switched off, so no stale array is read. */
static const RhiPipelineDesc *draw_setup(const char *what, uint32_t vertex_count, uint32_t first_vertex) {
    if (g_cmd.pipeline < 0) {
        fprintf(stderr, "rhi: %s: no pipeline bound\n", what);
        return NULL;
    }
    const RhiPipelineDesc *d = &g_pipelines.desc[g_cmd.pipeline];
    if (shader_slot(d->shader) < 0) {
        fprintf(stderr, "rhi: %s: destroyed shader\n", what);
        g_cmd.pipeline = -1;
        return NULL;
    }
    GLuint names[RHI_MAX_VERTEX_STREAMS] = {0};
    for (uint32_t i = 0; i < d->vertex.attrib_count; i++) {
        const RhiVertexAttrib *a = &d->vertex.attribs[i];
        if (!g_cmd.vertex[a->stream].id) continue; /* disabled constant attribute */
        const RhiGlBuffer *b = buffer_lookup(g_cmd.vertex[a->stream], what);
        uint32_t bytes = vertex_format_bytes(a->format);
        uint32_t stride = d->vertex.stride[a->stream] ? d->vertex.stride[a->stream] : bytes;
        uint64_t last = vertex_count ? (uint64_t)first_vertex + vertex_count - 1u : 0;
        uint64_t end = (uint64_t)g_cmd.vertex_offset[a->stream] + a->offset + last * stride + bytes;
        if (!b || b->target != GL_ARRAY_BUFFER || end > PTRDIFF_MAX || end > b->size) {
            fprintf(stderr, "rhi: %s: invalid vertex buffer or range\n", what);
            return NULL;
        }
        names[a->stream] = b->name;
    }
    unsigned used = 0;
    int bound_stream = -1;
    for (uint32_t i = 0; i < d->vertex.attrib_count; i++) {
        const RhiVertexAttrib *a = &d->vertex.attribs[i];
        used |= 1u << a->location;
        if (!names[a->stream]) {
            glDisableVertexAttribArray(a->location);
            continue;
        }
        if ((int)a->stream != bound_stream) {
            glBindBuffer(GL_ARRAY_BUFFER, names[a->stream]);
            bound_stream = (int)a->stream;
        }
        static const GLint comps[] = { 0, 2, 3, 4, 4, 1 };
        uintptr_t off = (uintptr_t)g_cmd.vertex_offset[a->stream] + a->offset;
        glEnableVertexAttribArray(a->location);
        glVertexAttribPointer(a->location, comps[a->format],
                              a->format == RHI_VERTEX_UNORM8X4 ? GL_UNSIGNED_BYTE : GL_FLOAT,
                              a->format == RHI_VERTEX_UNORM8X4 ? GL_TRUE : GL_FALSE,
                              (GLsizei)d->vertex.stride[a->stream], (const void *)off);
    }
    for (GLuint loc = 0; loc < RHI_MAX_VERTEX_ATTRIBS; loc++)
        if (!(used & (1u << loc)) && gls_attrib_enabled(loc)) glDisableVertexAttribArray(loc);
    return d;
}

static GLenum gl_primitive(RhiPrimitive p) { return p == RHI_PRIMITIVE_LINES ? GL_LINES : GL_TRIANGLES; }

void rhi_cmd_draw_indexed(uint32_t index_count, uint32_t first_index) {
    if (!index_count) return;
    const RhiGlBuffer *index = buffer_lookup(g_cmd.index, "draw_indexed");
    if (!index || index->target != GL_ELEMENT_ARRAY_BUFFER || index_count > INT_MAX ||
        (uint64_t)first_index * 2u > PTRDIFF_MAX || first_index > index->size / 2u ||
        index_count > index->size / 2u - first_index) {
        fprintf(stderr, "rhi: draw_indexed: invalid index buffer or range\n");
        return;
    }
    const RhiPipelineDesc *d = draw_setup("draw_indexed", 0, 0);
    if (!d) return;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index->name);
    rhi_gl_flush_params();
    glDrawElements(gl_primitive(d->primitive), (GLsizei)index_count, GL_UNSIGNED_SHORT,
                   (const void *)((uintptr_t)first_index * 2u));
}

void rhi_cmd_draw(uint32_t vertex_count, uint32_t first_vertex) {
    if (!vertex_count) return;
    if (vertex_count > INT_MAX || first_vertex > INT_MAX) {
        fprintf(stderr, "rhi: draw: invalid vertex count or first vertex\n");
        return;
    }
    const RhiPipelineDesc *d = draw_setup("draw", vertex_count, first_vertex);
    if (!d) return;
    rhi_gl_flush_params();
    glDrawArrays(gl_primitive(d->primitive), (GLint)first_vertex, (GLsizei)vertex_count);
}

/* ------------------------------------------------------------------------------ */
/* Passes                                                                         */
/* ------------------------------------------------------------------------------ */
/* One framebuffer object per depth attachment; the colour attachment is
 * re-pointed when a pass names a different one, as the offscreen code did by
 * hand before rhi.h had passes. */
#define RHI_GL_MAX_FRAMEBUFFERS 8u

static struct {
    GLuint     fbo;
    RhiTexture depth;
    GLuint     color_name;     /* attached colour texture, 0 = none */
    GLenum     color_target;   /* GL_TEXTURE_2D or a cube face */
    int        complete;       /* checked since the last attachment change */
} g_fbos[RHI_GL_MAX_FRAMEBUFFERS];
static int g_in_pass;

static void rhi_gl_forget_framebuffers(RhiTexture tex) {
    for (uint32_t i = 0; i < RHI_GL_MAX_FRAMEBUFFERS; i++) {
        if (!g_fbos[i].fbo) continue;
        const RhiGlTexture *t = texture_find(tex);
        if (g_fbos[i].depth.id == tex.id || (t && t->target != GL_RENDERBUFFER && g_fbos[i].color_name == t->name)) {
            glDeleteFramebuffers(1, &g_fbos[i].fbo);
            memset(&g_fbos[i], 0, sizeof g_fbos[i]);
        }
    }
}

/* Clears `bits` of the bound framebuffer with every colour channel and depth
 * writable, then puts the write masks back. Stencil is cleared to 0 with all
 * bits writable, then both stencil write masks are restored too. */
static void clear_target(GLbitfield bits, const float color[4], float depth) {
    GLboolean cm[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE}, dm = GL_TRUE;
    GLint stencil_mask = 0, stencil_back_mask = 0;
    if (!bits) return;
    if (bits & GL_COLOR_BUFFER_BIT) {
        GLfloat cc[4];
        gls_get_booleanv(GL_COLOR_WRITEMASK, cm);
        if (!cm[0] || !cm[1] || !cm[2] || !cm[3]) glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        gls_get_floatv(GL_COLOR_CLEAR_VALUE, cc);
        if (memcmp(cc, color, sizeof cc)) glClearColor(color[0], color[1], color[2], color[3]);
    }
    if (bits & GL_DEPTH_BUFFER_BIT) {
        GLfloat cd;
        gls_get_booleanv(GL_DEPTH_WRITEMASK, &dm);
        if (!dm) glDepthMask(GL_TRUE);
        gls_get_floatv(GL_DEPTH_CLEAR_VALUE, &cd);
        if (cd != depth) {
#ifdef N2_GLES
            glClearDepthf(depth);
#else
            glClearDepth(depth);
#endif
        }
    }
    if (bits & GL_STENCIL_BUFFER_BIT) {
        glGetIntegerv(GL_STENCIL_WRITEMASK, &stencil_mask);
        glGetIntegerv(GL_STENCIL_BACK_WRITEMASK, &stencil_back_mask);
        glStencilMask(0xFF); glClearStencil(0);
    }
    glClear(bits);
    if (!cm[0] || !cm[1] || !cm[2] || !cm[3]) glColorMask(cm[0], cm[1], cm[2], cm[3]);
    if (!dm) glDepthMask(GL_FALSE);
    if (bits & GL_STENCIL_BUFFER_BIT) {
        glStencilMaskSeparate(GL_FRONT, (GLuint)stencil_mask);
        glStencilMaskSeparate(GL_BACK, (GLuint)stencil_back_mask);
    }
}

RhiResult rhi_cmd_begin_pass(const RhiPassDesc *desc) {
    if (!desc || g_in_pass) return RHI_ERR_INVALID;
    const RhiGlTexture *color = texture_lookup(desc->color.texture, "begin_pass");
    const RhiGlTexture *depth = texture_lookup(desc->depth.texture, "begin_pass");
    if (!color || color->format != RHI_FORMAT_RGBA8 || !(color->usage & RHI_TEXTURE_COLOR_TARGET) ||
        (desc->depth.texture.id && (!depth || depth->target != GL_RENDERBUFFER))) return RHI_ERR_INVALID;
    int faces = color->target == GL_TEXTURE_CUBE_MAP ? 6 : 1;
    if (desc->color.face < 0 || desc->color.face >= faces ||
        (depth && desc->depth.face != 0) ||
        (desc->color_load != RHI_LOAD_KEEP && desc->color_load != RHI_LOAD_CLEAR) ||
        (desc->depth_load != RHI_LOAD_KEEP && desc->depth_load != RHI_LOAD_CLEAR)) return RHI_ERR_INVALID;
    GLenum color_target = faces == 6 ? (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + desc->color.face)
                                     : GL_TEXTURE_2D;
    int f = -1, free_slot = -1;
    for (uint32_t i = 0; i < RHI_GL_MAX_FRAMEBUFFERS; i++) {
        if (g_fbos[i].fbo && g_fbos[i].depth.id == desc->depth.texture.id) { f = (int)i; break; }
        if (!g_fbos[i].fbo && free_slot < 0) free_slot = (int)i;
    }
    if (f < 0) {
        if (free_slot < 0) { fprintf(stderr, "rhi: begin_pass: out of framebuffers\n"); return RHI_ERR_INVALID; }
        f = free_slot;
        glGenFramebuffers(1, &g_fbos[f].fbo);
        if (!g_fbos[f].fbo) return RHI_ERR_UNSUPPORTED;
        g_fbos[f].depth = desc->depth.texture;
        glBindFramebuffer(GL_FRAMEBUFFER, g_fbos[f].fbo);
        if (depth) glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth->name);
    } else glBindFramebuffer(GL_FRAMEBUFFER, g_fbos[f].fbo);
    if (g_fbos[f].color_name != color->name || g_fbos[f].color_target != color_target) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color_target, color->name, 0);
        g_fbos[f].color_name = color->name;
        g_fbos[f].color_target = color_target;
        g_fbos[f].complete = 0;
    }
    if (!g_fbos[f].complete) {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return RHI_ERR_UNSUPPORTED;
        g_fbos[f].complete = 1;
    }
    glViewport(0, 0, color->width, color->height);
    set_cap(GL_SCISSOR_TEST, 0);
    set_cap(GL_DITHER, 0);
    clear_target((desc->color_load == RHI_LOAD_CLEAR ? GL_COLOR_BUFFER_BIT : 0) |
                 (desc->depth_load == RHI_LOAD_CLEAR && depth ? GL_DEPTH_BUFFER_BIT : 0),
                 desc->clear_color, desc->clear_depth);
    g_in_pass = 1;
    return RHI_OK;
}

void rhi_cmd_end_pass(void) { g_in_pass = 0; }

/* ------------------------------------------------------------------------------ */
/* State snapshots                                                                */
/* ------------------------------------------------------------------------------ */
/* GL: the tracked context state. Read from the tracker, so state set with raw
 * GL calls is included. */
static const GLenum k_snapshot_caps[] = { GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST,
                                          GL_STENCIL_TEST, GL_DITHER, GL_POLYGON_OFFSET_FILL };
#define RHI_GL_SNAPSHOT_CAPS (sizeof k_snapshot_caps / sizeof k_snapshot_caps[0])

typedef struct {
    int32_t   pipeline;
    GLint     blend[4], blend_eq[2], array_buffer, depth_func, viewport[4], program, framebuffer, renderbuffer, active;
    GLint     texture[RHI_STATE_TEXTURE_SLOTS];
    GLfloat   clear_color[4], clear_depth, line_width;
    uint32_t  attrib_mask;   /* bit i: vertex attribute array i enabled */
    GLint     element_buffer;
    GlsAttribPointer attrib[RHI_MAX_VERTEX_ATTRIBS];   /* glVertexAttribPointer state of attributes 0.. */
    GLboolean caps[RHI_GL_SNAPSHOT_CAPS], depth_mask, color_mask[4];
} RhiGlSnapshot;
typedef char rhi_gl_snapshot_fits[sizeof(RhiGlSnapshot) <= sizeof(RhiStateSnapshot) ? 1 : -1];

void rhi_cmd_save_state(RhiStateSnapshot *out) {
    RhiGlSnapshot g;
    if (!out) return;
    memset(&g, 0, sizeof g);
    g.pipeline = g_cmd.pipeline;
    gls_get_integerv(GL_BLEND_SRC_RGB, &g.blend[0]); gls_get_integerv(GL_BLEND_DST_RGB, &g.blend[1]);
    gls_get_integerv(GL_BLEND_SRC_ALPHA, &g.blend[2]); gls_get_integerv(GL_BLEND_DST_ALPHA, &g.blend[3]);
    gls_get_integerv(GL_BLEND_EQUATION_RGB, &g.blend_eq[0]); gls_get_integerv(GL_BLEND_EQUATION_ALPHA, &g.blend_eq[1]);
    gls_get_integerv(GL_ARRAY_BUFFER_BINDING, &g.array_buffer);
    gls_get_integerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &g.element_buffer);
    for (GLuint i = 0; i < RHI_MAX_VERTEX_ATTRIBS; i++) {
        if (gls_attrib_enabled(i)) g.attrib_mask |= 1u << i;
        gls_attrib_pointer(i, &g.attrib[i]);
    }
    gls_get_integerv(GL_DEPTH_FUNC, &g.depth_func);
    gls_get_integerv(GL_VIEWPORT, g.viewport);
    gls_get_integerv(GL_CURRENT_PROGRAM, &g.program);
    gls_get_integerv(GL_FRAMEBUFFER_BINDING, &g.framebuffer);
    gls_get_integerv(GL_RENDERBUFFER_BINDING, &g.renderbuffer);
    gls_get_integerv(GL_ACTIVE_TEXTURE, &g.active);
    for (int i = 0; i < RHI_STATE_TEXTURE_SLOTS; i++) g.texture[i] = gls_texture_2d(i);
    gls_get_floatv(GL_COLOR_CLEAR_VALUE, g.clear_color);
    gls_get_floatv(GL_DEPTH_CLEAR_VALUE, &g.clear_depth);
    gls_get_floatv(GL_LINE_WIDTH, &g.line_width);
    for (size_t i = 0; i < RHI_GL_SNAPSHOT_CAPS; i++) g.caps[i] = gls_is_enabled(k_snapshot_caps[i]);
    gls_get_booleanv(GL_DEPTH_WRITEMASK, &g.depth_mask);
    gls_get_booleanv(GL_COLOR_WRITEMASK, g.color_mask);
    memset(out, 0, sizeof *out);
    memcpy(out, &g, sizeof g);
}

void rhi_state_set_texture(RhiStateSnapshot *state, uint32_t slot, RhiTexture texture) {
    RhiGlSnapshot g;
    const RhiGlTexture *t = texture_lookup(texture, "state_set_texture");
    if (!state || slot >= RHI_STATE_TEXTURE_SLOTS || (t && t->target != GL_TEXTURE_2D)) return;
    memcpy(&g, state, sizeof g);
    g.texture[slot] = t ? (GLint)t->name : 0;
    memcpy(state, &g, sizeof g);
}

void rhi_cmd_restore_state(const RhiStateSnapshot *state) {
    RhiGlSnapshot g, now;
    if (!state) return;
    memcpy(&g, state, sizeof g);
    RhiStateSnapshot current;
    rhi_cmd_save_state(&current);
    memcpy(&now, &current, sizeof now);
    g_cmd.pipeline = g.pipeline;
    if (now.framebuffer != g.framebuffer) glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)g.framebuffer);
    if (now.renderbuffer != g.renderbuffer) glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)g.renderbuffer);
    if (memcmp(now.viewport, g.viewport, sizeof g.viewport))
        glViewport(g.viewport[0], g.viewport[1], g.viewport[2], g.viewport[3]);
    if (now.program != g.program) glUseProgram((GLuint)g.program);
    if (memcmp(now.clear_color, g.clear_color, sizeof g.clear_color))
        glClearColor(g.clear_color[0], g.clear_color[1], g.clear_color[2], g.clear_color[3]);
    if (now.clear_depth != g.clear_depth) {
#ifdef N2_GLES
        glClearDepthf(g.clear_depth);
#else
        glClearDepth(g.clear_depth);
#endif
    }
    if (memcmp(now.color_mask, g.color_mask, sizeof g.color_mask))
        glColorMask(g.color_mask[0], g.color_mask[1], g.color_mask[2], g.color_mask[3]);
    if (now.depth_func != g.depth_func) glDepthFunc((GLenum)g.depth_func);
    if (now.line_width != g.line_width) glLineWidth(g.line_width);
    if (!now.depth_mask != !g.depth_mask) glDepthMask(g.depth_mask);
    if (memcmp(now.blend, g.blend, sizeof g.blend))
        glBlendFuncSeparate((GLenum)g.blend[0], (GLenum)g.blend[1], (GLenum)g.blend[2], (GLenum)g.blend[3]);
    if (memcmp(now.blend_eq, g.blend_eq, sizeof g.blend_eq))
        glBlendEquationSeparate((GLenum)g.blend_eq[0], (GLenum)g.blend_eq[1]);
    /* Vertex attributes: GL records the ARRAY_BUFFER bound when a pointer is set, so bind each saved buffer around its glVertexAttribPointer
     * and put the saved ARRAY_BUFFER binding back afterwards; only attributes whose description differs are re-pointed. */
    GLint bound = now.array_buffer;
    for (GLuint i = 0; i < RHI_MAX_VERTEX_ATTRIBS; i++) {
        const GlsAttribPointer *a = &g.attrib[i], *c = &now.attrib[i];
        if (a->size != c->size || a->type != c->type || a->stride != c->stride || a->buffer != c->buffer ||
            a->normalized != c->normalized || a->pointer != c->pointer) {
            if (bound != a->buffer) { glBindBuffer(GL_ARRAY_BUFFER, (GLuint)a->buffer); bound = a->buffer; }
            glVertexAttribPointer(i, a->size, (GLenum)a->type, a->normalized ? GL_TRUE : GL_FALSE, a->stride, a->pointer);
        }
        if (((now.attrib_mask ^ g.attrib_mask) >> i & 1u) == 0) continue;
        if (g.attrib_mask >> i & 1u) glEnableVertexAttribArray(i); else glDisableVertexAttribArray(i);
    }
    if (bound != g.array_buffer) glBindBuffer(GL_ARRAY_BUFFER, (GLuint)g.array_buffer);
    if (now.element_buffer != g.element_buffer) glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, (GLuint)g.element_buffer);
    for (size_t i = 0; i < RHI_GL_SNAPSHOT_CAPS; i++) set_cap(k_snapshot_caps[i], g.caps[i]);
    GLint active = now.active;
    for (int i = 0; i < RHI_STATE_TEXTURE_SLOTS; i++) {
        if (now.texture[i] == g.texture[i]) continue;
        if (active != (GLint)GL_TEXTURE0 + i) glActiveTexture(GL_TEXTURE0 + (GLenum)i);
        active = (GLint)GL_TEXTURE0 + i;
        glBindTexture(GL_TEXTURE_2D, (GLuint)g.texture[i]);
    }
    if (active != g.active) glActiveTexture((GLenum)g.active);
}

void rhi_viewport(int32_t viewport[4]) {
    GLint v[4];
    gls_get_integerv(GL_VIEWPORT, v);
    for (int i = 0; i < 4; i++) viewport[i] = v[i];
}

RhiResult rhi_cmd_clear_main(const float color[4], float depth) {
    if (!color || !g_rhi.in_frame || g_in_pass) return RHI_ERR_INVALID;
    glBindFramebuffer(GL_FRAMEBUFFER, GLS_MAIN_FRAMEBUFFER);
    glViewport(0, 0, g_rhi.frame_w, g_rhi.frame_h);
    set_cap(GL_SCISSOR_TEST, 0);
    clear_target(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, color, depth);
    return RHI_OK;
}

/* ------------------------------------------------------------------------------ */
/* Copies from the main target                                                    */
/* ------------------------------------------------------------------------------ */
/* The first copy (or one of a new size) allocates level 0 with glCopyTexImage2D, later ones overwrite it. */
RhiResult rhi_cmd_copy_main_to_texture(RhiTexture texture, int32_t x, int32_t y,
                                       int32_t w, int32_t h) {
    RhiGlTexture *t = texture_lookup(texture, "copy_main_to_texture");
    if (!t || t->target != GL_TEXTURE_2D || w <= 0 || h <= 0 || x < 0 || y < 0 || g_in_pass ||
        !(t->usage & RHI_TEXTURE_COPY_DST) ||
        (t->format != RHI_FORMAT_RGBA8 && t->format != RHI_FORMAT_DEPTH) ||
        w > INT_MAX - x || h > INT_MAX - y ||
        (g_rhi.in_frame && (w > g_rhi.frame_w - x || h > g_rhi.frame_h - y))) return RHI_ERR_INVALID;
#ifdef N2_GLES
    if (t->format == RHI_FORMAT_DEPTH) return RHI_ERR_UNSUPPORTED;
#endif
    SDL_Window *window = SDL_GL_GetCurrentWindow();
    int frame_w = 0, frame_h = 0;
    if (window) SDL_GL_GetDrawableSize(window, &frame_w, &frame_h);
    if (w > frame_w - x || h > frame_h - y) return RHI_ERR_INVALID;
    GLint framebuffer;
    gls_get_integerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    if (framebuffer != (GLint)GLS_MAIN_FRAMEBUFFER) glBindFramebuffer(GL_FRAMEBUFFER, GLS_MAIN_FRAMEBUFFER);
    GLint active = work_unit_bind(GL_TEXTURE_2D, t->name);
    if (t->copied_w != w || t->copied_h != h) {
        glCopyTexImage2D(GL_TEXTURE_2D, 0, t->format == RHI_FORMAT_DEPTH ? GL_DEPTH_COMPONENT : GL_RGBA,
                         x, y, w, h, 0);
        for (uint32_t i = 0; i < RHI_GL_MAX_FRAMEBUFFERS; i++)
            if (g_fbos[i].color_name == t->name) g_fbos[i].complete = 0;
    } else glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, y, w, h);
    RhiResult result = glGetError() == GL_NO_ERROR ? RHI_OK : RHI_ERR_UNSUPPORTED;
    work_unit_done(active);
    if (framebuffer != (GLint)GLS_MAIN_FRAMEBUFFER) glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)framebuffer);
    if (result != RHI_OK) return result;
    t->copied_w = w; t->copied_h = h;
    t->width = w; t->height = h;
    return RHI_OK;
}

RhiResult rhi_cmd_set_viewport(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (width < 0 || height < 0) return RHI_ERR_INVALID;
    glViewport(x, y, width, height);
    return RHI_OK;
}

/* ------------------------------------------------------------------------------ */
/* Debug fill mode                                                                */
/* ------------------------------------------------------------------------------ */
RhiResult rhi_cmd_set_wireframe(int on) {
    if (!g_rhi.caps.wireframe) return RHI_ERR_UNSUPPORTED;
#ifndef N2_GLES
    glPolygonMode(GL_FRONT_AND_BACK, on ? GL_LINE : GL_FILL);
#endif
    return RHI_OK;
}

/* ------------------------------------------------------------------------------ */
/* GPU timers                                                                     */
/* ------------------------------------------------------------------------------ */
static GLuint timer_lookup(RhiTimer timer) {
    uint32_t slot = timer.id & RHI_GL_SLOT_MASK;
    if (!slot || slot > RHI_GL_MAX_TIMERS || !g_timers.name[slot - 1] ||
        g_timers.generation[slot - 1] != (uint8_t)(timer.id >> RHI_GL_SLOT_BITS)) return 0;
    return g_timers.name[slot - 1];
}

RhiTimer rhi_timer_create(void) {
    RhiTimer out = {0};
    if (!g_rhi.caps.gpu_timer) return out;
    for (uint32_t i = 0; i < RHI_GL_MAX_TIMERS; i++) {
        if (g_timers.name[i] || g_timers.generation[i] == UINT8_MAX) continue;
        g_timer_gl.gen(1, &g_timers.name[i]);
        if (!g_timers.name[i]) return out;
        g_timers.ended[i] = 0;
        out.id = ((uint32_t)g_timers.generation[i] << RHI_GL_SLOT_BITS) | (i + 1);
        return out;
    }
    fprintf(stderr, "rhi: timer_create: out of timers\n");
    return out;
}

void rhi_timer_destroy(RhiTimer timer) {
    GLuint name = timer_lookup(timer);
    if (!name) return;
    uint32_t slot = (timer.id & RHI_GL_SLOT_MASK) - 1;
    if (g_timers.open == (int)slot + 1) rhi_cmd_timer_end();
    g_timer_gl.del(1, &name);
    g_timers.name[slot] = 0;
    g_timers.generation[slot]++;   /* a slot at UINT8_MAX is retired */
}

RhiResult rhi_cmd_timer_begin(RhiTimer timer) {
    GLuint name = timer_lookup(timer);
    if (!name || g_timers.open) return RHI_ERR_INVALID;
    g_timer_gl.begin(RHI_GL_TIME_ELAPSED, name);
    g_timers.open = (int)(timer.id & RHI_GL_SLOT_MASK);
    return RHI_OK;
}

RhiResult rhi_cmd_timer_end(void) {
    if (!g_timers.open) return RHI_ERR_INVALID;
    g_timer_gl.end(RHI_GL_TIME_ELAPSED);
    g_timers.ended[g_timers.open - 1] = 1;
    g_timers.open = 0;
    return RHI_OK;
}

int rhi_timer_read(RhiTimer timer, uint64_t *ns) {
    GLuint name = timer_lookup(timer), ready = 0;
    unsigned long long elapsed = 0;
    uint32_t slot = (timer.id & RHI_GL_SLOT_MASK) - 1;
    if (!name || !ns || !g_timers.ended[slot] || g_timers.open == (int)slot + 1) return 0;
    g_timer_gl.get_uiv(name, RHI_GL_QUERY_RESULT_AVAILABLE, &ready);
    if (!ready) return 0;
    g_timer_gl.get_ui64v(name, RHI_GL_QUERY_RESULT, &elapsed);
    *ns = elapsed;
    return 1;
}
