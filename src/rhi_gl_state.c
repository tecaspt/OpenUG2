/* rhi_gl_state.c — see rhi_gl_state.h. The shims record, then make the identical GL
 * call; the getters answer from the record. */
#define GLS_IMPLEMENTATION
#include "rhi_gl_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef N2_IOS
#if defined(__APPLE__)
#include <SDL_syswm.h>
#ifndef SDL_VIDEO_DRIVER_UIKIT
#error "N2_IOS needs SDL built with the UIKit video driver"
#endif
#endif
static GLuint g_main_fbo, g_main_rb;
GLuint gls_main_framebuffer(void) { return g_main_fbo; }
GLuint gls_main_renderbuffer(void) { return g_main_rb; }
#endif

#if defined(__APPLE__) && !defined(N2_GLES)
#define glBindFramebuffer glBindFramebufferEXT
#define glDeleteFramebuffers glDeleteFramebuffersEXT
#define glBindRenderbuffer glBindRenderbufferEXT
#define glDeleteRenderbuffers glDeleteRenderbuffersEXT
#endif

#define GLS_UNITS 16
#define GLS_ATTRIBS 16
#define GLS_PROGRAMS 32
static const GLenum caps[] = {GL_DEPTH_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST,
                              GL_STENCIL_TEST, GL_DITHER, GL_POLYGON_OFFSET_FILL};
#define GLS_NCAPS ((int)(sizeof caps / sizeof caps[0]))

/* One uniform location: up to a mat4, plus how many floats the last write
 * stored there (array elements continue at the following locations). */
typedef struct { GLfloat v[16]; int n; } GlsSlot;
typedef struct { GLuint id; int nslot; GlsSlot *slot; } GlsProgram;

typedef struct {
    SDL_GLContext ctx;
    int live;
    GLboolean enabled[GLS_NCAPS];
    GLint blend[4];                 /* src_rgb, dst_rgb, src_alpha, dst_alpha */
    GLint blend_eq[2];              /* rgb, alpha */
    GLint depth_func;
    GLboolean depth_mask, color_mask[4];
    GLfloat clear_color[4], clear_depth;
    GLfloat line_width;
    GLint viewport[4];
    GLint program, framebuffer, renderbuffer, array_buffer, element_buffer;
    GLint active;                   /* unit index, 0-based */
    GLint tex2d[GLS_UNITS];
    GLint attrib[GLS_ATTRIBS];
    GlsAttribPointer aptr[GLS_ATTRIBS];   /* glVertexAttribPointer state, GL initial values until set */
    GlsProgram prog[GLS_PROGRAMS];
} GlsState;

static GlsState S;
static unsigned g_fallback;
static int g_prog_overflow;         /* table filled up: unknown programs ask GL */

static void drop_program(GlsProgram *p) { free(p->slot); memset(p, 0, sizeof *p); }

/* GL spec initial values; the viewport starts at the drawable size. */
static void gls_reset(SDL_GLContext ctx) {
    for (int i = 0; i < GLS_PROGRAMS; i++) drop_program(&S.prog[i]);
    memset(&S, 0, sizeof S);
    S.ctx = ctx; S.live = 1;
#ifdef N2_IOS
    S.framebuffer = (GLint)g_main_fbo; S.renderbuffer = (GLint)g_main_rb;
#endif
    for (int i = 0; i < GLS_NCAPS; i++) S.enabled[i] = caps[i] == GL_DITHER;
    S.blend[0] = S.blend[2] = GL_ONE; S.blend[1] = S.blend[3] = GL_ZERO;
    S.blend_eq[0] = S.blend_eq[1] = GL_FUNC_ADD;
    S.depth_func = GL_LESS; S.depth_mask = GL_TRUE;
    for (int i = 0; i < 4; i++) S.color_mask[i] = GL_TRUE;
    S.clear_depth = 1.0f;
    S.line_width = 1.0f;
    for (int i = 0; i < GLS_ATTRIBS; i++) S.aptr[i] = (GlsAttribPointer){ 4, (GLint)GL_FLOAT, 0, 0, 0, NULL };
    SDL_Window *w = SDL_GL_GetCurrentWindow();
    if (w) SDL_GL_GetDrawableSize(w, &S.viewport[2], &S.viewport[3]);
}

static GlsState *st(void) {
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    if (!S.live || S.ctx != ctx) gls_reset(ctx);
    return &S;
}

/* A new context may reuse a destroyed one's address: always start clean. */
SDL_GLContext glsCreateContext(SDL_Window *window) {
    SDL_GLContext ctx = SDL_GL_CreateContext(window);
    S.live = 0;
#if defined(N2_IOS) && defined(SDL_VIDEO_DRIVER_UIKIT)
    g_main_fbo = g_main_rb = 0;
    if (ctx) {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_UIKIT) {
            g_main_fbo = info.info.uikit.framebuffer;
            g_main_rb = info.info.uikit.colorbuffer;
        }
    }
#endif
#if defined(_WIN32) && !defined(N2_GLES)
    if (ctx) {
        glewExperimental = GL_TRUE;
        GLenum status = glewInit();
        if (status != GLEW_OK) {
            SDL_GL_DeleteContext(ctx);
            SDL_SetError("GLEW initialization failed: %s",
                         (const char *)glewGetErrorString(status));
            return NULL;
        }
    }
#endif
    return ctx;
}
void glsDeleteContext(SDL_GLContext ctx) {
    if (ctx == S.ctx) S.live = 0;
    SDL_GL_DeleteContext(ctx);
}

static int cap_index(GLenum cap) {
    for (int i = 0; i < GLS_NCAPS; i++) if (caps[i] == cap) return i;
    return -1;
}

/* ---- fixed-function shims ---- */
void glsEnable(GLenum cap) {
    int i = cap_index(cap); if (i >= 0) st()->enabled[i] = GL_TRUE;
    glEnable(cap);
}
void glsDisable(GLenum cap) {
    int i = cap_index(cap); if (i >= 0) st()->enabled[i] = GL_FALSE;
    glDisable(cap);
}
void glsBlendFunc(GLenum src, GLenum dst) {
    GlsState *s = st();
    s->blend[0] = s->blend[2] = (GLint)src; s->blend[1] = s->blend[3] = (GLint)dst;
    glBlendFunc(src, dst);
}
void glsBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_a, GLenum dst_a) {
    GlsState *s = st();
    s->blend[0] = (GLint)src_rgb; s->blend[1] = (GLint)dst_rgb;
    s->blend[2] = (GLint)src_a;   s->blend[3] = (GLint)dst_a;
    glBlendFuncSeparate(src_rgb, dst_rgb, src_a, dst_a);
}
void glsBlendEquation(GLenum mode) {
    GlsState *s = st();
    s->blend_eq[0] = s->blend_eq[1] = (GLint)mode;
    glBlendEquation(mode);
}
void glsBlendEquationSeparate(GLenum rgb, GLenum alpha) {
    GlsState *s = st();
    s->blend_eq[0] = (GLint)rgb; s->blend_eq[1] = (GLint)alpha;
    glBlendEquationSeparate(rgb, alpha);
}
void glsDepthFunc(GLenum func) { st()->depth_func = (GLint)func; glDepthFunc(func); }
void glsDepthMask(GLboolean mask) { st()->depth_mask = mask ? GL_TRUE : GL_FALSE; glDepthMask(mask); }
void glsColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    GlsState *s = st();
    s->color_mask[0] = r ? GL_TRUE : GL_FALSE; s->color_mask[1] = g ? GL_TRUE : GL_FALSE;
    s->color_mask[2] = b ? GL_TRUE : GL_FALSE; s->color_mask[3] = a ? GL_TRUE : GL_FALSE;
    glColorMask(r, g, b, a);
}
/* The clear colour is stored as given: callers only ever restore it, and
 * re-setting the value they set yields the same clear. */
void glsClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    GlsState *s = st();
    s->clear_color[0] = r; s->clear_color[1] = g; s->clear_color[2] = b; s->clear_color[3] = a;
    glClearColor(r, g, b, a);
}
static GLfloat clamp01(GLfloat v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
#ifdef N2_GLES
void glsClearDepthf(GLfloat depth) { st()->clear_depth = clamp01(depth); glClearDepthf(depth); }
#else
void glsClearDepth(GLdouble depth) { st()->clear_depth = clamp01((GLfloat)depth); glClearDepth(depth); }
#endif
void glsViewport(GLint x, GLint y, GLsizei w, GLsizei h) {
    GlsState *s = st();
    s->viewport[0] = x; s->viewport[1] = y; s->viewport[2] = w; s->viewport[3] = h;
    glViewport(x, y, w, h);
}

void glsLineWidth(GLfloat width) {
    st()->line_width = width;   /* GL returns the set value, not the raster clamp */
    glLineWidth(width);
}
void glsUseProgram(GLuint program) { st()->program = (GLint)program; glUseProgram(program); }
void glsBindFramebuffer(GLenum target, GLuint fbo) {
    st()->framebuffer = (GLint)fbo; glBindFramebuffer(target, fbo);
}
void glsBindRenderbuffer(GLenum target, GLuint rb) {
    st()->renderbuffer = (GLint)rb; glBindRenderbuffer(target, rb);
}
/* Deleting a bound object reverts that binding to 0, as GL does. */
void glsDeleteFramebuffers(GLsizei n, const GLuint *ids) {
    GlsState *s = st();
    for (GLsizei i = 0; i < n; i++) if (ids[i] && (GLint)ids[i] == s->framebuffer) s->framebuffer = 0;
    glDeleteFramebuffers(n, ids);
}
void glsDeleteRenderbuffers(GLsizei n, const GLuint *ids) {
    GlsState *s = st();
    for (GLsizei i = 0; i < n; i++) if (ids[i] && (GLint)ids[i] == s->renderbuffer) s->renderbuffer = 0;
    glDeleteRenderbuffers(n, ids);
}
void glsActiveTexture(GLenum unit) { st()->active = (GLint)(unit - GL_TEXTURE0); glActiveTexture(unit); }
void glsBindTexture(GLenum target, GLuint texture) {
    GlsState *s = st();
    if (target == GL_TEXTURE_2D && s->active >= 0 && s->active < GLS_UNITS)
        s->tex2d[s->active] = (GLint)texture;
    glBindTexture(target, texture);
}
void glsDeleteTextures(GLsizei n, const GLuint *ids) {
    GlsState *s = st();
    for (GLsizei i = 0; i < n; i++)
        for (int u = 0; ids[i] && u < GLS_UNITS; u++) if (s->tex2d[u] == (GLint)ids[i]) s->tex2d[u] = 0;
    glDeleteTextures(n, ids);
}
void glsBindBuffer(GLenum target, GLuint buffer) {
    if (target == GL_ARRAY_BUFFER) st()->array_buffer = (GLint)buffer;
    else if (target == GL_ELEMENT_ARRAY_BUFFER) st()->element_buffer = (GLint)buffer;
    glBindBuffer(target, buffer);
}
void glsDeleteBuffers(GLsizei n, const GLuint *ids) {
    GlsState *s = st();
    for (GLsizei i = 0; i < n; i++) {
        if (!ids[i]) continue;
        if ((GLint)ids[i] == s->array_buffer) s->array_buffer = 0;
        if ((GLint)ids[i] == s->element_buffer) s->element_buffer = 0;
        for (int a = 0; a < GLS_ATTRIBS; a++) if (s->aptr[a].buffer == (GLint)ids[i]) s->aptr[a].buffer = 0;   /* GL detaches it */
    }
    glDeleteBuffers(n, ids);
}
void glsEnableVertexAttribArray(GLuint index) {
    if (index < GLS_ATTRIBS) st()->attrib[index] = 1;
    glEnableVertexAttribArray(index);
}
void glsVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    GlsState *s = st();
    if (index < GLS_ATTRIBS) s->aptr[index] = (GlsAttribPointer){ size, (GLint)type, stride, s->array_buffer, normalized ? 1 : 0, pointer };
    glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}
void glsDisableVertexAttribArray(GLuint index) {
    if (index < GLS_ATTRIBS) st()->attrib[index] = 0;
    glDisableVertexAttribArray(index);
}

/* ---- uniform mirror ---- */
static GlsProgram *find_program(GlsState *s, GLuint id, int create) {
    GlsProgram *free_slot = NULL;
    for (int i = 0; i < GLS_PROGRAMS; i++) {
        if (s->prog[i].id == id) return &s->prog[i];
        if (!s->prog[i].id && !free_slot) free_slot = &s->prog[i];
    }
    if (!create) return NULL;
    if (!free_slot) { g_prog_overflow = 1; return NULL; }
    free_slot->id = id;
    return free_slot;
}

void glsLinkProgram(GLuint program) {
    GlsState *s = st();
    GlsProgram *p = find_program(s, program, 0);
    if (p) drop_program(p);         /* a (re)link resets every uniform to 0 */
    glLinkProgram(program);
}
void glsDeleteProgram(GLuint program) {
    GlsProgram *p = program ? find_program(st(), program, 0) : NULL;
    if (p) drop_program(p);
    glDeleteProgram(program);
}

/* Record `count` elements of `comp` floats starting at `loc` of the current
 * program. Element i lands in slot loc+i (consecutive array locations). */
static void record(GLint loc, int comp, GLsizei count, const GLfloat *v) {
    GlsState *s = st();
    if (loc < 0 || count <= 0 || !s->program) return;
    GlsProgram *p = find_program(s, (GLuint)s->program, 1);
    if (!p) return;
    int need = loc + count;
    if (need > p->nslot) {
        int n = p->nslot ? p->nslot : 64;
        while (n < need) n *= 2;
        GlsSlot *grown = realloc(p->slot, (size_t)n * sizeof *grown);
        if (!grown) return;
        memset(grown + p->nslot, 0, (size_t)(n - p->nslot) * sizeof *grown);
        p->slot = grown; p->nslot = n;
    }
    for (GLsizei i = 0; i < count; i++) {
        memcpy(p->slot[loc + i].v, v + (size_t)i * comp, (size_t)comp * sizeof *v);
        p->slot[loc + i].n = comp;
    }
}

void glsUniform1f(GLint loc, GLfloat x) { record(loc, 1, 1, &x); glUniform1f(loc, x); }
void glsUniform2f(GLint loc, GLfloat x, GLfloat y) {
    GLfloat v[2] = {x, y}; record(loc, 2, 1, v); glUniform2f(loc, x, y);
}
void glsUniform3f(GLint loc, GLfloat x, GLfloat y, GLfloat z) {
    GLfloat v[3] = {x, y, z}; record(loc, 3, 1, v); glUniform3f(loc, x, y, z);
}
void glsUniform4f(GLint loc, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    GLfloat v[4] = {x, y, z, w}; record(loc, 4, 1, v); glUniform4f(loc, x, y, z, w);
}
void glsUniform1i(GLint loc, GLint x) {
    GLfloat f = (GLfloat)x; record(loc, 1, 1, &f); glUniform1i(loc, x);
}
void glsUniform1fv(GLint loc, GLsizei count, const GLfloat *v) { record(loc, 1, count, v); glUniform1fv(loc, count, v); }
void glsUniform2fv(GLint loc, GLsizei count, const GLfloat *v) { record(loc, 2, count, v); glUniform2fv(loc, count, v); }
void glsUniform3fv(GLint loc, GLsizei count, const GLfloat *v) { record(loc, 3, count, v); glUniform3fv(loc, count, v); }
void glsUniform4fv(GLint loc, GLsizei count, const GLfloat *v) { record(loc, 4, count, v); glUniform4fv(loc, count, v); }
void glsUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *v) {
    if (!transpose) record(loc, 16, count, v);
    else {                          /* store column-major, as GL reports it */
        for (GLsizei i = 0; i < count; i++) {
            GLfloat m[16];
            for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) m[c * 4 + r] = v[i * 16 + r * 4 + c];
            record(loc + i, 16, 1, m);
        }
    }
    glUniformMatrix4fv(loc, count, transpose, v);
}

#ifdef GLS_VERIFY
static void verify_fail(const char *what, GLenum pname) {
    fprintf(stderr, "gl_state: tracked %s 0x%04X differs from GL\n", what, (unsigned)pname);
    abort();
}
#endif

void gls_get_uniformfv(GLuint program, GLint loc, GLfloat *out, int n) {
    if (loc < 0 || n <= 0) return;
    GlsProgram *p = find_program(st(), program, 0);
    if (!p && g_prog_overflow) {    /* untracked program: read GL per element */
        g_fallback++;
        GLint step = 1; GLfloat tmp[16];
        for (int at = 0; at < n; at += step) {
            glGetUniformfv(program, loc++, tmp);
            step = n - at < 16 ? n - at : 16;   /* caller's n is one element */
            memcpy(out + at, tmp, (size_t)step * sizeof *tmp);
        }
        return;
    }
    int at = 0, slot = loc;
    while (at < n) {
        const GlsSlot *sl = p && slot < p->nslot ? &p->slot[slot] : NULL;
        int take = sl && sl->n ? sl->n : n - at;   /* unwritten: zero, as after link */
        if (take > n - at) take = n - at;
        for (int i = 0; i < take; i++) out[at + i] = sl && sl->n ? sl->v[i] : 0.0f;
        at += take; slot++;
    }
#ifdef GLS_VERIFY
    GLfloat real[16];
    for (int at2 = 0, sl2 = loc; at2 < n; sl2++) {
        glGetUniformfv(program, sl2, real);
        const GlsSlot *sl = p && sl2 < p->nslot ? &p->slot[sl2] : NULL;
        int take = sl && sl->n ? sl->n : n - at2;
        if (take > n - at2) take = n - at2;
        if (memcmp(real, out + at2, (size_t)take * sizeof *real)) verify_fail("uniform", (GLenum)loc);
        at2 += take;
    }
#endif
}

/* ---- fixed-function getters ---- */
GLboolean gls_is_enabled(GLenum cap) {
    int i = cap_index(cap);
    if (i < 0) { g_fallback++; return glIsEnabled(cap); }
    GLboolean v = st()->enabled[i];
#ifdef GLS_VERIFY
    if ((glIsEnabled(cap) ? 1 : 0) != (v ? 1 : 0)) verify_fail("enable", cap);
#endif
    return v;
}

/* Returns the number of ints written, 0 when the pname is not modelled. */
static int tracked_integerv(GLenum pname, GLint *out) {
    GlsState *s = st();
    switch (pname) {
#ifdef GL_BLEND_SRC
    case GL_BLEND_SRC: out[0] = s->blend[0]; return 1;
    case GL_BLEND_DST: out[0] = s->blend[1]; return 1;
#endif
    case GL_BLEND_SRC_RGB: out[0] = s->blend[0]; return 1;
    case GL_BLEND_DST_RGB: out[0] = s->blend[1]; return 1;
    case GL_BLEND_SRC_ALPHA: out[0] = s->blend[2]; return 1;
    case GL_BLEND_DST_ALPHA: out[0] = s->blend[3]; return 1;
    case GL_BLEND_EQUATION_RGB: out[0] = s->blend_eq[0]; return 1;
    case GL_BLEND_EQUATION_ALPHA: out[0] = s->blend_eq[1]; return 1;
    case GL_DEPTH_FUNC: out[0] = s->depth_func; return 1;
    case GL_VIEWPORT: memcpy(out, s->viewport, sizeof s->viewport); return 4;
    case GL_CURRENT_PROGRAM: out[0] = s->program; return 1;
    case GL_FRAMEBUFFER_BINDING: out[0] = s->framebuffer; return 1;
    case GL_RENDERBUFFER_BINDING: out[0] = s->renderbuffer; return 1;
    case GL_ARRAY_BUFFER_BINDING: out[0] = s->array_buffer; return 1;
    case GL_ELEMENT_ARRAY_BUFFER_BINDING: out[0] = s->element_buffer; return 1;
    case GL_ACTIVE_TEXTURE: out[0] = (GLint)GL_TEXTURE0 + s->active; return 1;
    case GL_TEXTURE_BINDING_2D:
        if (s->active < 0 || s->active >= GLS_UNITS) return 0;
        out[0] = s->tex2d[s->active]; return 1;
    default: return 0;
    }
}

void gls_get_integerv(GLenum pname, GLint *out) {
    int n = tracked_integerv(pname, out);
    if (!n) { g_fallback++; glGetIntegerv(pname, out); return; }
#ifdef GLS_VERIFY
    GLint real[4]; glGetIntegerv(pname, real);
    if (memcmp(real, out, (size_t)n * sizeof *out)) verify_fail("integer", pname);
#endif
}

void gls_get_booleanv(GLenum pname, GLboolean *out) {
    GlsState *s = st();
    int n = 0;
    if (pname == GL_DEPTH_WRITEMASK) { out[0] = s->depth_mask; n = 1; }
    else if (pname == GL_COLOR_WRITEMASK) { memcpy(out, s->color_mask, sizeof s->color_mask); n = 4; }
    if (!n) { g_fallback++; glGetBooleanv(pname, out); return; }
#ifdef GLS_VERIFY
    GLboolean real[4]; glGetBooleanv(pname, real);
    if (memcmp(real, out, (size_t)n)) verify_fail("boolean", pname);
#endif
}

void gls_get_floatv(GLenum pname, GLfloat *out) {
    GlsState *s = st();
    int n = 0;
    if (pname == GL_COLOR_CLEAR_VALUE) { memcpy(out, s->clear_color, sizeof s->clear_color); n = 4; }
    else if (pname == GL_DEPTH_CLEAR_VALUE) { out[0] = s->clear_depth; n = 1; }
    else if (pname == GL_LINE_WIDTH) { out[0] = s->line_width; n = 1; }
    if (!n) { g_fallback++; glGetFloatv(pname, out); return; }
#ifdef GLS_VERIFY
    GLfloat real[4]; glGetFloatv(pname, real);
    if (memcmp(real, out, (size_t)n * sizeof *out)) verify_fail("float", pname);
#endif
}

GLint gls_texture_2d(int unit) {
    GlsState *s = st();
    if (unit >= 0 && unit < GLS_UNITS && unit == s->active) {
        GLint v; gls_get_integerv(GL_TEXTURE_BINDING_2D, &v); return v;   /* verified there */
    }
    if (unit < 0 || unit >= GLS_UNITS) return 0;
    return s->tex2d[unit];
}

GLint gls_attrib_enabled(GLuint index) {
    if (index >= GLS_ATTRIBS) { GLint v = 0; g_fallback++;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &v); return v; }
    GLint v = st()->attrib[index];
#ifdef GLS_VERIFY
    GLint real = 0; glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &real);
    if (!real != !v) verify_fail("attrib", (GLenum)index);
#endif
    return v;
}

GLint gls_attrib_effective_stride(GLint size, GLint type, GLint stride) {
    if (stride) return stride;
    GLint bytes = 0;
    switch ((GLenum)type) {
    case GL_BYTE: case GL_UNSIGNED_BYTE: bytes = 1; break;
    case GL_SHORT: case GL_UNSIGNED_SHORT: bytes = 2; break;
    case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: bytes = 4; break;
    default: return 0;   /* other types: compare literally */
    }
    return size * bytes;
}

void gls_attrib_pointer(GLuint index, GlsAttribPointer *out) {
    if (index >= GLS_ATTRIBS) {   /* not modelled: ask GL */
        GLint v = 0; g_fallback++;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &out->size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &out->type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &out->stride);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &out->buffer);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &v); out->normalized = v ? 1 : 0;
        out->pointer = NULL; glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, (void **)&out->pointer);
        return;
    }
    *out = st()->aptr[index];
#ifdef GLS_VERIFY
    {
        GlsAttribPointer r = {0}; GLint v = 0;
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_SIZE, &r.size);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_TYPE, &r.type);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &r.stride);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &r.buffer);
        glGetVertexAttribiv(index, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &v); r.normalized = v ? 1 : 0;
        glGetVertexAttribPointerv(index, GL_VERTEX_ATTRIB_ARRAY_POINTER, (void **)&r.pointer);
        if (r.size != out->size || r.type != out->type ||
            gls_attrib_effective_stride(r.size, r.type, r.stride) != gls_attrib_effective_stride(out->size, out->type, out->stride) || r.buffer != out->buffer ||
            r.normalized != out->normalized || r.pointer != out->pointer) verify_fail("attrib pointer", (GLenum)index);
    }
#endif
}

unsigned gls_fallback_queries(void) { return g_fallback; }
