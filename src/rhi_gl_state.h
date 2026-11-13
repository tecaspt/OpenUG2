/* rhi_gl_state.h — CPU-side GL state tracker + per-program uniform mirror.
 *
 * Renderer-boundary migration steps 2 and 3 (issue #14). Helpers used to
 * save/restore the caller's state by reading it back from GL (glIsEnabled,
 * glGetIntegerv, glGetBooleanv, glGetFloatv, glGetUniformfv). A Vulkan
 * backend has no equivalent, so the state is shadowed on the CPU instead:
 *
 *  - Each state-setting entry point below is redirected by a function-like
 *    macro to a gls* shim that records the value and then issues exactly the
 *    same GL call. The GL command stream (calls, order, values) is unchanged;
 *    only the queries disappear.
 *  - gls_get_* answer from the shadow copy, which starts from the GL spec
 *    defaults whenever the current SDL GL context changes. They never query
 *    GL, except for a pname the tracker does not model (counted below).
 *  - glUniform* writes are mirrored per (program, location). Linking a
 *    program zeroes its mirror as GL does, so gls_get_uniformfv() returns the
 *    floats glGetUniformfv would have.
 *
 * Every TU that includes render.h (or this header) is tracked. Code that
 * changes this state through raw GL (the ImGui GL2 backend) must restore it
 * before returning, as it already does.
 *
 * Build with -DGLS_VERIFY to cross-check every getter against the real GL
 * query and abort on the first mismatch.
 *
 * This tracker belongs to the GL backend of rhi.h (rhi_gl.c creates the
 * context through it). It is still included by render.h, frontend_draw.c and
 * debugui.cpp because those files issue GL calls directly; later rhi steps
 * move those calls behind rhi.h and make this header private to the backend. */
#ifndef OPENUG2_GL_STATE_H
#define OPENUG2_GL_STATE_H

#include <SDL.h>
#ifdef N2_GLES
#  include <SDL_opengles2.h>
#elif defined(_WIN32)
#  include <GL/glew.h>
#elif defined(__APPLE__)
#  ifndef GL_SILENCE_DEPRECATION
#    define GL_SILENCE_DEPRECATION 1
#  endif
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#else
#  ifndef GL_GLEXT_PROTOTYPES
#    define GL_GLEXT_PROTOTYPES 1
#  endif
#  include <SDL_opengl.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- recording shims (reached through the macros below) ---- */
SDL_GLContext glsCreateContext(SDL_Window *window);   /* resets the tracker */
void glsDeleteContext(SDL_GLContext ctx);
#if defined(N2_IOS) && !defined(N2_GLES)
#error "N2_IOS requires N2_GLES"
#endif
#ifdef N2_IOS
/* iOS has no framebuffer 0: SDL's UIKit view owns the real FBO/renderbuffer. */
GLuint gls_main_framebuffer(void);
GLuint gls_main_renderbuffer(void);
#define GLS_MAIN_FRAMEBUFFER gls_main_framebuffer()
#else
#define GLS_MAIN_FRAMEBUFFER 0
#endif
void glsEnable(GLenum cap);
void glsDisable(GLenum cap);
void glsBlendFunc(GLenum src, GLenum dst);
void glsBlendFuncSeparate(GLenum src_rgb, GLenum dst_rgb, GLenum src_a, GLenum dst_a);
void glsBlendEquation(GLenum mode);
void glsBlendEquationSeparate(GLenum rgb, GLenum alpha);
void glsDepthFunc(GLenum func);
void glsDepthMask(GLboolean mask);
void glsColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glsClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
#ifdef N2_GLES
void glsClearDepthf(GLfloat depth);
#else
void glsClearDepth(GLdouble depth);
#endif
void glsViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glsLineWidth(GLfloat width);
void glsUseProgram(GLuint program);
void glsLinkProgram(GLuint program);
void glsDeleteProgram(GLuint program);
void glsBindFramebuffer(GLenum target, GLuint fbo);
void glsBindRenderbuffer(GLenum target, GLuint rb);
void glsDeleteFramebuffers(GLsizei n, const GLuint *ids);
void glsDeleteRenderbuffers(GLsizei n, const GLuint *ids);
void glsActiveTexture(GLenum unit);
void glsBindTexture(GLenum target, GLuint texture);
void glsDeleteTextures(GLsizei n, const GLuint *ids);
void glsBindBuffer(GLenum target, GLuint buffer);
void glsDeleteBuffers(GLsizei n, const GLuint *ids);
void glsEnableVertexAttribArray(GLuint index);
void glsVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
void glsDisableVertexAttribArray(GLuint index);
void glsUniform1f(GLint loc, GLfloat x);
void glsUniform2f(GLint loc, GLfloat x, GLfloat y);
void glsUniform3f(GLint loc, GLfloat x, GLfloat y, GLfloat z);
void glsUniform4f(GLint loc, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glsUniform1i(GLint loc, GLint x);
void glsUniform1fv(GLint loc, GLsizei count, const GLfloat *v);
void glsUniform2fv(GLint loc, GLsizei count, const GLfloat *v);
void glsUniform3fv(GLint loc, GLsizei count, const GLfloat *v);
void glsUniform4fv(GLint loc, GLsizei count, const GLfloat *v);
void glsUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *v);

/* ---- tracked-state getters: drop-in for the glIsEnabled / glGet* forms ----
 * Modelled caps: DEPTH_TEST BLEND CULL_FACE SCISSOR_TEST STENCIL_TEST DITHER
 * POLYGON_OFFSET_FILL. Modelled pnames: BLEND_{SRC,DST}[_RGB|_ALPHA],
 * BLEND_EQUATION_{RGB,ALPHA}, DEPTH_FUNC, VIEWPORT, CURRENT_PROGRAM, FRAMEBUFFER_BINDING,
 * RENDERBUFFER_BINDING, ACTIVE_TEXTURE, TEXTURE_BINDING_2D (active unit),
 * ARRAY_BUFFER_BINDING, ELEMENT_ARRAY_BUFFER_BINDING, DEPTH_WRITEMASK, COLOR_WRITEMASK, COLOR_CLEAR_VALUE,
 * DEPTH_CLEAR_VALUE. */
GLboolean gls_is_enabled(GLenum cap);
void gls_get_integerv(GLenum pname, GLint *out);
void gls_get_booleanv(GLenum pname, GLboolean *out);
void gls_get_floatv(GLenum pname, GLfloat *out);
GLint gls_attrib_enabled(GLuint index);  /* GL_VERTEX_ATTRIB_ARRAY_ENABLED */

/* glVertexAttribPointer state of attribute `index`. Until set it has the GL initial values (size 4, GL_FLOAT, not normalized,
 * stride 0, NULL pointer, buffer 0). `buffer` is the ARRAY_BUFFER that was bound when the pointer was set, as GL records it. */
typedef struct GlsAttribPointer { GLint size, type, stride, buffer, normalized; const void *pointer; } GlsAttribPointer;
void gls_attrib_pointer(GLuint index, GlsAttribPointer *out);
/* Stride 0 means tightly packed (size * type size); drivers may report either form for the same attribute. Returns the packed value for 0. */
GLint gls_attrib_effective_stride(GLint size, GLint type, GLint stride);
GLint gls_texture_2d(int unit);          /* GL_TEXTURE_BINDING_2D on `unit`, any active unit */

/* ---- uniform mirror (replaces glGetUniformfv) ----
 * Copies the floats last written at `loc` of `program` (n = component count,
 * e.g. 1, 3, 4 or 16). A glUniform*v(loc, count, ...) array write is stored
 * contiguously from `loc`, so n may span elements (8 for both uHeadPos
 * vec4s). loc < 0 leaves `out` untouched, matching glGetUniformfv. */
void gls_get_uniformfv(GLuint program, GLint loc, GLfloat *out, int n);

/* Reads that had to go to GL because the pname/cap/location is not modelled
 * (diagnostic; 0 in every current path). */
unsigned gls_fallback_queries(void);

#ifdef __cplusplus
}
#endif

#ifndef GLS_IMPLEMENTATION
#  if defined(_WIN32) && !defined(N2_GLES)
/* GLEW's function-pointer macros stay intact in the shim implementation. */
#    undef glBlendFuncSeparate
#    undef glBlendEquation
#    undef glBlendEquationSeparate
#    undef glUseProgram
#    undef glLinkProgram
#    undef glDeleteProgram
#    undef glBindFramebuffer
#    undef glBindRenderbuffer
#    undef glDeleteFramebuffers
#    undef glDeleteRenderbuffers
#    undef glActiveTexture
#    undef glBindBuffer
#    undef glDeleteBuffers
#    undef glEnableVertexAttribArray
#    undef glVertexAttribPointer
#    undef glDisableVertexAttribArray
#    undef glUniform1f
#    undef glUniform2f
#    undef glUniform3f
#    undef glUniform4f
#    undef glUniform1i
#    undef glUniform1fv
#    undef glUniform2fv
#    undef glUniform3fv
#    undef glUniform4fv
#    undef glUniformMatrix4fv
#  endif
#  define SDL_GL_CreateContext(w)         glsCreateContext(w)
#  define SDL_GL_DeleteContext(c)         glsDeleteContext(c)
#  define glEnable(c)                     glsEnable(c)
#  define glDisable(c)                    glsDisable(c)
#  define glBlendFunc(s,d)                glsBlendFunc(s,d)
#  define glBlendFuncSeparate(a,b,c,d)    glsBlendFuncSeparate(a,b,c,d)
#  define glBlendEquation(m)              glsBlendEquation(m)
#  define glBlendEquationSeparate(a,b)    glsBlendEquationSeparate(a,b)
#  define glDepthFunc(f)                  glsDepthFunc(f)
#  define glDepthMask(m)                  glsDepthMask(m)
#  define glColorMask(r,g,b,a)            glsColorMask(r,g,b,a)
#  define glClearColor(r,g,b,a)           glsClearColor(r,g,b,a)
#  ifdef N2_GLES
#    define glClearDepthf(d)              glsClearDepthf(d)
#  else
#    define glClearDepth(d)               glsClearDepth(d)
#  endif
#  define glViewport(x,y,w,h)             glsViewport(x,y,w,h)
#  define glLineWidth(w)                  glsLineWidth(w)
#  define glUseProgram(p)                 glsUseProgram(p)
#  define glLinkProgram(p)                glsLinkProgram(p)
#  define glDeleteProgram(p)              glsDeleteProgram(p)
#  define glBindFramebuffer(t,f)          glsBindFramebuffer(t,f)
#  define glBindRenderbuffer(t,r)         glsBindRenderbuffer(t,r)
#  define glDeleteFramebuffers(n,p)       glsDeleteFramebuffers(n,p)
#  define glDeleteRenderbuffers(n,p)      glsDeleteRenderbuffers(n,p)
#  define glActiveTexture(u)              glsActiveTexture(u)
#  define glBindTexture(t,x)              glsBindTexture(t,x)
#  define glDeleteTextures(n,p)           glsDeleteTextures(n,p)
#  define glBindBuffer(t,b)               glsBindBuffer(t,b)
#  define glDeleteBuffers(n,p)            glsDeleteBuffers(n,p)
#  define glEnableVertexAttribArray(i)    glsEnableVertexAttribArray(i)
#  define glVertexAttribPointer(i,s,t,n,st,p) glsVertexAttribPointer(i,s,t,n,st,p)
#  define glDisableVertexAttribArray(i)   glsDisableVertexAttribArray(i)
#  define glUniform1f(l,x)                glsUniform1f(l,x)
#  define glUniform2f(l,x,y)              glsUniform2f(l,x,y)
#  define glUniform3f(l,x,y,z)            glsUniform3f(l,x,y,z)
#  define glUniform4f(l,x,y,z,w)          glsUniform4f(l,x,y,z,w)
#  define glUniform1i(l,x)                glsUniform1i(l,x)
#  define glUniform1fv(l,n,v)             glsUniform1fv(l,n,v)
#  define glUniform2fv(l,n,v)             glsUniform2fv(l,n,v)
#  define glUniform3fv(l,n,v)             glsUniform3fv(l,n,v)
#  define glUniform4fv(l,n,v)             glsUniform4fv(l,n,v)
#  define glUniformMatrix4fv(l,n,t,v)     glsUniformMatrix4fv(l,n,t,v)
#endif

#endif /* OPENUG2_GL_STATE_H */
