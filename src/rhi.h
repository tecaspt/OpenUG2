/* rhi.h - backend-neutral rendering interface (issue #14).
 *
 * Rules for this header:
 *   - C99, depends on <stdint.h>/<stddef.h> only: no SDL, GL or Vulkan types.
 *     The window is passed as void*.
 *   - Nothing game-specific: no scene or gameplay concepts, no shader or uniform
 *     names.
 *   - Single thread, one device per process.
 *
 * Covered so far: device/context lifetime, capabilities, the frame begin/end and
 * vertex/index buffers. Textures, samplers, shaders, pipelines, passes, draws
 * and readback are added by later steps; until then the game still issues GL
 * calls for them directly. Backend: rhi_gl.c (desktop GL 2.1 or OpenGL ES 2.0, chosen
 * at build time). */
#ifndef RHI_H
#define RHI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. */
typedef enum RhiResult {
    RHI_OK = 0,
    RHI_ERR_UNSUPPORTED = -1,   /* backend not compiled in or not available */
    RHI_ERR_INVALID = -2,       /* bad argument or wrong call order */
    RHI_ERR_OUT_OF_MEMORY = -3, /* the device could not allocate the resource */
    RHI_ERR_DEVICE_LOST = -4,
    RHI_ERR_SKIP_FRAME = -5     /* nothing can be presented this frame: skip drawing */
} RhiResult;

/* ------------------------------------------------------------------------------ */
/* Device / context lifetime                                                      */
/* ------------------------------------------------------------------------------ */
typedef enum RhiBackend {
    RHI_BACKEND_DEFAULT = 0, /* the backend this build was compiled for */
    RHI_BACKEND_GL,          /* desktop GL 2.1 compatibility (macOS: legacy 2.1 context) */
    RHI_BACKEND_GLES2,       /* OpenGL ES 2.0 */
    RHI_BACKEND_VULKAN       /* Vulkan 1.1+ (offscreen v0; swapchain + MoltenVK later) */
} RhiBackend;

typedef enum RhiDeviceFlags {
    RHI_DEVICE_VSYNC = 1u << 0  /* present synchronised to the display refresh */
} RhiDeviceFlags;

typedef struct RhiDeviceDesc {
    RhiBackend backend;
    uint32_t   flags;      /* RhiDeviceFlags */
    void      *window;     /* the SDL_Window, created with rhi_window_flags() */
} RhiDeviceDesc;

/* Window setup happens before the window exists, so it is split from device
 * creation: rhi_prepare_window() sets what the backend needs on the windowing
 * system (GL: context attributes) and rhi_window_flags() returns the
 * SDL_WindowFlags bits to create the window with.
 * RHI_ERR_UNSUPPORTED / 0 if the backend is not compiled in. */
RhiResult rhi_prepare_window(RhiBackend backend);
uint32_t  rhi_window_flags(RhiBackend backend);

/* Creates the device for desc->window (GL: the context, made current) and fills
 * the capabilities. Logs the reason to stderr on failure. */
RhiResult rhi_device_create(const RhiDeviceDesc *desc);
/* Destroys the device. The window stays owned by the caller. */
void      rhi_device_destroy(void);

/* Size of the main target in pixels (the window's drawable size, which can
 * differ from the window size on high-DPI displays). */
void      rhi_drawable_size(int32_t *width, int32_t *height);

/* ------------------------------------------------------------------------------ */
/* Capabilities                                                                   */
/* ------------------------------------------------------------------------------ */
typedef struct RhiCaps {
    RhiBackend backend;          /* never RHI_BACKEND_DEFAULT */
    int32_t    max_viewport[2];  /* largest main target / viewport, pixels */
    float      max_anisotropy;   /* 1.0 = anisotropic filtering unavailable */
    uint8_t    tex_bc1, tex_bc2, tex_bc3;  /* S3TC DXT1/3/5 uploads supported */
    uint8_t    wireframe;        /* rhi_cmd_set_wireframe works (debug views) */
    uint8_t    scene_target;     /* RGBA8 + packed depth/stencil scene targets */
    uint8_t    gpu_timer;        /* RhiTimer measurements work (profiling) */
    int32_t    max_texture_size; /* largest texture width/height, pixels */
} RhiCaps;

const RhiCaps *rhi_caps(void);   /* valid after rhi_device_create */

/* ------------------------------------------------------------------------------ */
/* Frame lifecycle                                                                */
/* ------------------------------------------------------------------------------ */
typedef struct RhiFrameInfo {
    int32_t  width, height;      /* main target size for this frame */
    uint64_t frame_index;        /* 0 for the first frame */
} RhiFrameInfo;

/* Begins a frame. RHI_ERR_SKIP_FRAME: draw nothing and do not call
 * rhi_frame_end for it (the GL backend never returns it). */
RhiResult rhi_frame_begin(RhiFrameInfo *out);
/* Ends the frame and presents it (GL: SDL_GL_SwapWindow). */
RhiResult rhi_frame_end(void);

/* ------------------------------------------------------------------------------ */
/* Readback                                                                       */
/* ------------------------------------------------------------------------------ */
typedef enum RhiReadFormat {
    RHI_READ_RGB8  = 1,   /* 3 bytes per pixel */
    RHI_READ_RGBA8 = 2    /* 4 bytes per pixel */
} RhiReadFormat;

/* Reads a w x h rectangle of the main target into dst, rows top-down. x, y is
 * the rectangle's top-left corner, measured from the top-left of the target.
 * stride is the byte distance between rows in dst (0 = w * bytes per pixel).
 * Only valid between rhi_frame_begin and rhi_frame_end, after the last draw
 * that should be visible; the rectangle must lie inside the frame. Blocks
 * until the pixels are available. */
RhiResult rhi_read_pixels(int32_t x, int32_t y, int32_t w, int32_t h,
                          RhiReadFormat format, void *dst, size_t stride);

/* ------------------------------------------------------------------------------ */
/* Buffers                                                                        */
/* ------------------------------------------------------------------------------ */
/* Opaque handle: slot index + generation, id 0 = no buffer. A destroyed handle
 * is detected (and logged) instead of reaching the GPU API. */
typedef struct RhiBuffer { uint32_t id; } RhiBuffer;

typedef enum RhiBufferUsage {
    RHI_BUFFER_VERTEX = 1u << 0,
    RHI_BUFFER_INDEX  = 1u << 1   /* 16-bit indices */
} RhiBufferUsage;

typedef enum RhiBufferUpdate {
    RHI_UPDATE_STATIC = 0,  /* written at creation; rare partial updates between
                               draws (rhi_buffer_update) */
    RHI_UPDATE_STREAM = 1   /* contents replaced every frame with
                               rhi_buffer_replace; draws issued before a replace
                               keep using the old contents */
} RhiBufferUpdate;

typedef struct RhiBufferDesc {
    uint32_t        usage;  /* RhiBufferUsage: exactly one bit */
    RhiBufferUpdate update;
    size_t          size;   /* bytes; STREAM may be 0 = no storage until the first
                               rhi_buffer_replace */
    const void     *data;   /* size bytes, or NULL = undefined contents */
} RhiBufferDesc;

/* Allocation failures are reported for STATIC buffers and for textures (by
 * {0} or RHI_ERR_OUT_OF_MEMORY / RHI_ERR_UNSUPPORTED), so loaders can reject a
 * resource. STREAM buffers do not report them: checking would make every
 * per-frame update wait for the driver (GL: glGetError). */

/* Returns {0} on failure (logged to stderr). */
RhiBuffer rhi_buffer_create(const RhiBufferDesc *desc);
/* Writes size bytes at offset into the existing storage; the range must lie
 * inside the buffer. */
RhiResult rhi_buffer_update(RhiBuffer buf, size_t offset, size_t size, const void *data);
/* Gives the buffer fresh storage of size bytes (data may be NULL = undefined
 * contents). Earlier draws are unaffected (GL: buffer orphaning). */
RhiResult rhi_buffer_replace(RhiBuffer buf, size_t size, const void *data);
/* Releases the buffer; {0} is ignored. The handle becomes invalid. */
void      rhi_buffer_destroy(RhiBuffer buf);

/* ------------------------------------------------------------------------------ */
/* Textures                                                                       */
/* ------------------------------------------------------------------------------ */
/* Opaque handle like RhiBuffer: slot index + generation, id 0 = no texture. */
typedef struct RhiTexture { uint32_t id; } RhiTexture;
typedef struct RhiSceneTarget { uint32_t id; } RhiSceneTarget;

/* Optional logical-main RGBA8 + depth/stencil target (see caps.scene_target).
 * Select/unselect inside a frame and outside ordinary passes. Selection is not
 * part of snapshots; save/restore around ordinary passes to return to the scene.
 * Color is borrowed until destroy. Frame end recovers a missed unselect without
 * compositing. The selection query is CPU-only; Vulkan reports unsupported. */
RhiResult rhi_scene_target_create(int32_t w, int32_t h, RhiSceneTarget *out);
RhiTexture rhi_scene_target_color(RhiSceneTarget target);
int rhi_scene_target_is_selected(RhiSceneTarget target);
void rhi_scene_target_destroy(RhiSceneTarget target);
RhiResult rhi_cmd_select_scene_target(RhiSceneTarget target);
RhiResult rhi_cmd_unselect_scene_target(void);

typedef enum RhiTextureType {
    RHI_TEXTURE_2D   = 0,
    RHI_TEXTURE_CUBE = 1   /* six square faces, +X -X +Y -Y +Z -Z */
} RhiTextureType;

typedef enum RhiFormat {
    RHI_FORMAT_RGB8  = 1,  /* upload convenience; a backend may store RGBA8 */
    RHI_FORMAT_RGBA8 = 2,
    RHI_FORMAT_BC1   = 3,  /* S3TC DXT1, caps.tex_bc1 */
    RHI_FORMAT_BC2   = 4,  /* S3TC DXT3, caps.tex_bc2 */
    RHI_FORMAT_BC3   = 5,  /* S3TC DXT5, caps.tex_bc3 */
    RHI_FORMAT_D16   = 6,  /* 16-bit depth; render target only for now */
    RHI_FORMAT_DEPTH = 7   /* depth with the main target's precision: copies of
                              the main depth (rhi_cmd_copy_main_to_texture) */
} RhiFormat;

typedef enum RhiTextureUsage {
    RHI_TEXTURE_SAMPLED      = 1u << 0,
    RHI_TEXTURE_COLOR_TARGET = 1u << 1,   /* colour attachment of a pass */
    RHI_TEXTURE_DEPTH_TARGET = 1u << 2,   /* depth attachment of a pass */
    RHI_TEXTURE_COPY_DST     = 1u << 3    /* target of rhi_cmd_copy_main_to_texture */
} RhiTextureUsage;

typedef struct RhiTextureDesc {
    RhiTextureType type;
    RhiFormat      format;
    int32_t        width, height;   /* level 0; equal for CUBE */
    uint32_t       usage;           /* RhiTextureUsage bits */
} RhiTextureDesc;

/* Creates a texture without contents. Returns {0} on failure (logged). A D16
 * texture must be DEPTH_TARGET and not SAMPLED (GL: a renderbuffer); it is not
 * uploaded to. */
RhiTexture rhi_texture_create(const RhiTextureDesc *desc);
/* Sets the contents of one mip level (face 0..5 for CUBE, 0 for 2D). Level n
 * is max(1, width >> n) x max(1, height >> n). data == NULL leaves an
 * uncompressed level allocated with undefined contents (render targets);
 * compressed levels need data and its size in bytes. */
RhiResult  rhi_texture_upload(RhiTexture tex, int32_t face, int32_t level,
                              const void *data, size_t size);
/* Fills levels 1.. from level 0 (RGB8/RGBA8 2D textures). */
RhiResult  rhi_texture_generate_mips(RhiTexture tex);
/* Releases the texture; {0} is ignored. The handle becomes invalid. */
void       rhi_texture_destroy(RhiTexture tex);
/* 1 while the handle refers to a texture that has not been destroyed. */
int        rhi_texture_is_live(RhiTexture tex);
/* Backend's native object id for the texture: the GL texture name under the GL
 * backend, 0 for {0} or a destroyed handle. Backend-neutral spelling for code
 * outside the backend that needs a stable per-texture key (batch ordering) or a
 * diagnostic id without touching GL; a non-GL backend returns its own stable id
 * or 0. It is an identity, not an ordering contract: compare for equality. */
uint32_t   rhi_texture_native_id(RhiTexture tex);

/* ------------------------------------------------------------------------------ */
/* Samplers                                                                       */
/* ------------------------------------------------------------------------------ */
/* Immutable sampling state, created by value: equal descriptions give the same
 * handle, and samplers live as long as the backend. */
typedef struct RhiSampler { uint32_t id; } RhiSampler;

typedef enum RhiFilter    { RHI_FILTER_NEAREST = 0, RHI_FILTER_LINEAR = 1 } RhiFilter;
typedef enum RhiMipFilter {
    RHI_MIP_NONE    = 0,   /* sample level 0 only */
    RHI_MIP_NEAREST = 1,
    RHI_MIP_LINEAR  = 2
} RhiMipFilter;
typedef enum RhiWrap      { RHI_WRAP_REPEAT = 0, RHI_WRAP_CLAMP = 1 } RhiWrap;

typedef struct RhiSamplerDesc {
    RhiFilter    min_filter, mag_filter;
    RhiMipFilter mip_filter;
    RhiWrap      wrap_u, wrap_v;
    float        max_anisotropy;   /* 1 = off; limited to caps.max_anisotropy */
} RhiSamplerDesc;

/* Returns {0} on failure (logged). */
RhiSampler            rhi_sampler_create(const RhiSamplerDesc *desc);
/* The description of a sampler; NULL for {0} or an unknown handle. */
const RhiSamplerDesc *rhi_sampler_desc(RhiSampler sampler);

/* The sampler a texture is sampled with. A new texture starts with
 * { NEAREST min, LINEAR mag, LINEAR mip, REPEAT, anisotropy 1 }. */
RhiResult  rhi_texture_set_sampler(RhiTexture tex, RhiSampler sampler);
RhiSampler rhi_texture_sampler(RhiTexture tex);

/* ------------------------------------------------------------------------------ */
/* Shaders                                                                        */
/* ------------------------------------------------------------------------------ */
/* One vertex + fragment program. Opaque handle like RhiBuffer, {0} = none. */
typedef struct RhiShader { uint32_t id; } RhiShader;

/* Parameter blocks: a C struct owned by the game, described by a layout table.
 * Offsets follow std140 (rhi_param_layout_check), so a backend can upload the
 * bytes as one uniform buffer; the GL backend maps fields to uniforms by name. */
typedef enum RhiParamType {
    RHI_PARAM_FLOAT = 1,
    RHI_PARAM_VEC2  = 2,
    RHI_PARAM_VEC3  = 3,
    RHI_PARAM_VEC4  = 4,
    RHI_PARAM_MAT4  = 5,   /* column-major, 16 floats */
    RHI_PARAM_INT   = 6    /* 32-bit; also sampler units until texture binding */
} RhiParamType;

typedef struct RhiParamField {
    const char  *name;     /* uniform name in the shader */
    RhiParamType type;
    uint32_t     offset;   /* bytes from the start of the block */
    uint32_t     count;    /* array length; 1 = not an array */
} RhiParamField;

typedef struct RhiParamLayout {
    const RhiParamField *fields;
    uint32_t             field_count;
    uint32_t             size;   /* sizeof the C struct */
    const char          *name;
} RhiParamLayout;

/* RHI_OK when every field is aligned and sized as std140 requires and lies
 * inside the block (arrays only of VEC4/MAT4, whose std140 stride is their
 * size); otherwise RHI_ERR_INVALID with the first problem on stderr. */
RhiResult rhi_param_layout_check(const RhiParamLayout *layout);

typedef struct RhiShaderDesc {
    /* Complete source text in the backend's language (GL: GLSL 1.20, or
     * GLSL ES 1.00 for RHI_BACKEND_GLES2); NUL-terminated. */
    const char        *vertex_source, *fragment_source;
    /* Vertex attribute i is named attribute_names[i]. */
    const char *const *attribute_names;
    uint32_t           attribute_count;
    const char        *label;   /* for error messages; may be NULL */
    /* Parameter blocks the shader reads (block i = param_blocks[i]). */
    const RhiParamLayout *const *param_blocks;
    uint32_t           param_block_count;
} RhiShaderDesc;

/* Compiles and links. Returns {0} on failure; the compiler log goes to stderr. */
RhiShader rhi_shader_create(const RhiShaderDesc *desc);
/* {0} is ignored. The handle becomes invalid. */
void      rhi_shader_destroy(RhiShader shader);
/* The storage of parameter block `block` (layout->size bytes, zero at
 * creation). Its contents at each draw with the shader are what that draw
 * sees; write it any time. NULL for an invalid shader or block. */
void     *rhi_shader_params(RhiShader shader, uint32_t block);
/* Backend's native program object for the shader: the GL program name under the
 * GL backend, 0 for {0} or a destroyed handle. Backend-neutral spelling so code
 * outside the backend can hand the number to hand-drawing tools without touching
 * GL; a non-GL backend returns its own id or 0. */
uint32_t  rhi_shader_native_program(RhiShader shader);

/* ------------------------------------------------------------------------------ */
/* Pipelines and draws                                                            */
/* ------------------------------------------------------------------------------ */
#define RHI_MAX_VERTEX_ATTRIBS 8u
#define RHI_MAX_VERTEX_STREAMS 4u

typedef enum RhiVertexFormat {
    RHI_VERTEX_FLOAT2 = 1, RHI_VERTEX_FLOAT3, RHI_VERTEX_FLOAT4,
    RHI_VERTEX_UNORM8X4,     /* 4 bytes read as 0..1 */
    RHI_VERTEX_FLOAT1
} RhiVertexFormat;

typedef struct RhiVertexAttrib {
    uint32_t        location;   /* unique index < RHI_MAX_VERTEX_ATTRIBS (RhiShaderDesc order) */
    uint32_t        stream;     /* vertex buffer slot */
    uint32_t        offset;     /* bytes from the start of a vertex */
    RhiVertexFormat format;
} RhiVertexAttrib;

typedef struct RhiVertexLayout {
    RhiVertexAttrib attribs[RHI_MAX_VERTEX_ATTRIBS];
    uint32_t        attrib_count;
    uint32_t        stride[RHI_MAX_VERTEX_STREAMS];   /* bytes per vertex; 0 = tightly packed, offset 0 */
} RhiVertexLayout;

typedef enum RhiPrimitive { RHI_PRIMITIVE_TRIANGLES = 0, RHI_PRIMITIVE_LINES = 1 } RhiPrimitive;
typedef enum RhiCullMode  { RHI_CULL_NONE = 0, RHI_CULL_BACK = 1, RHI_CULL_FRONT = 2 } RhiCullMode;
typedef enum RhiCompare {
    RHI_COMPARE_NEVER = 0, RHI_COMPARE_LESS, RHI_COMPARE_EQUAL, RHI_COMPARE_LESS_EQUAL,
    RHI_COMPARE_GREATER, RHI_COMPARE_NOT_EQUAL, RHI_COMPARE_GREATER_EQUAL, RHI_COMPARE_ALWAYS
} RhiCompare;
typedef enum RhiStencilOp {
    RHI_STENCIL_KEEP = 0, RHI_STENCIL_ZERO, RHI_STENCIL_REPLACE, RHI_STENCIL_INCR,
    RHI_STENCIL_DECR, RHI_STENCIL_INVERT
} RhiStencilOp;
typedef enum RhiBlendFactor {
    RHI_BLEND_ZERO = 0, RHI_BLEND_ONE, RHI_BLEND_SRC_ALPHA, RHI_BLEND_ONE_MINUS_SRC_ALPHA,
    RHI_BLEND_DST_ALPHA, RHI_BLEND_ONE_MINUS_DST_ALPHA, RHI_BLEND_SRC_COLOR,
    RHI_BLEND_ONE_MINUS_SRC_COLOR, RHI_BLEND_DST_COLOR, RHI_BLEND_ONE_MINUS_DST_COLOR
} RhiBlendFactor;

/* Everything a draw needs besides its buffers, textures and parameters.
 * Viewport and scissor are not part of it. Blending is "add" (src*f + dst*f).
 * The same stencil state applies to both faces. There is no depth bias yet:
 * binding a pipeline turns it off (GL: polygon offset fill).
 * Shader attributes that are not in the vertex layout read their default
 * value (no array). */
typedef struct RhiPipelineDesc {
    RhiShader       shader;
    RhiVertexLayout vertex;
    RhiPrimitive    primitive;
    RhiCullMode     cull;          /* counter-clockwise faces are front faces */
    uint8_t         depth_test, depth_write;
    RhiCompare      depth_compare;
    uint8_t         blend;         /* the blend equation is always ADD */
    RhiBlendFactor  src_rgb, dst_rgb, src_alpha, dst_alpha;
    uint8_t         color_write;   /* bits 0..3 = R, G, B, A */
    float           line_width;    /* RHI_PRIMITIVE_LINES width in pixels; <= 0
                                      means 1 (GL: glLineWidth, clamped to the
                                      driver's aliased-line range; ignored for
                                      triangles). */
    uint8_t         stencil_test;
    RhiCompare      stencil_compare;
    uint8_t         stencil_ref, stencil_read_mask, stencil_write_mask;
    RhiStencilOp    stencil_fail, stencil_depth_fail, stencil_pass;
} RhiPipelineDesc;

typedef struct RhiPipeline { uint32_t id; } RhiPipeline;

/* GL's initial state: no depth test, LESS, depth write on, no blend (ONE,
 * ZERO), no culling, all colour channels written, triangles, no stencil test
 * (ALWAYS, ref 0, masks 0xff, KEEP). */
void        rhi_pipeline_desc_init(RhiPipelineDesc *desc);
/* Created by value: equal descriptions give the same handle. {0} on failure. */
RhiPipeline rhi_pipeline_create(const RhiPipelineDesc *desc);

/* Commands (GL: executed immediately). A draw uses the pipeline, vertex and
 * index buffers and textures bound last, and the shader's parameter blocks as
 * they are at that moment. */
void rhi_cmd_bind_pipeline(RhiPipeline pipeline);
/* A stream bound to {0} leaves its attributes without an array: the shader
 * reads (0, 0, 0, 1) unless the backend's default value was changed. */
/* {0} disables the stream's attributes; the caller supplies their constants. */
void rhi_cmd_bind_vertex_buffer(uint32_t stream, RhiBuffer buffer, uint32_t offset);
void rhi_cmd_bind_index_buffer(RhiBuffer buffer);           /* 16-bit indices */
/* Texture for shader sampler unit `slot` (< RHI_TEXTURE_SLOTS), sampled with
 * the texture's sampler. Creating, uploading or copying into a texture does
 * not change what is bound. */
#define RHI_TEXTURE_SLOTS 7u
void rhi_cmd_bind_texture(uint32_t slot, RhiTexture texture);
/* Invalid resources or byte ranges are skipped before GL setup. Callers must
 * ensure index values reference valid vertices; the backend does not scan them. */
void rhi_cmd_draw_indexed(uint32_t index_count, uint32_t first_index);
/* No index buffer; the first/count byte extent must fit every enabled stream. */
void rhi_cmd_draw(uint32_t vertex_count, uint32_t first_vertex);

/* ------------------------------------------------------------------------------ */
/* Passes                                                                         */
/* ------------------------------------------------------------------------------ */
typedef enum RhiLoadOp {
    RHI_LOAD_KEEP = 0,    /* previous contents */
    RHI_LOAD_CLEAR = 1    /* clear to the pass's clear value */
} RhiLoadOp;

typedef struct RhiAttachment {
    RhiTexture texture;   /* {0} = none */
    int32_t    face;      /* cube face 0..5, 0 for 2D */
} RhiAttachment;

/* Rendering into textures. Viewport becomes the attachment size, the
 * scissor test is off and the output is not dithered. Passes do not nest. Scene selection is independent of these passes;
 * callers save/restore state around an ordinary pass to return to the scene.
 * GL: clearing writes all colour channels / depth regardless of the last
 * pipeline's write masks. */
typedef struct RhiPassDesc {
    RhiAttachment color;          /* RGBA8 COLOR_TARGET texture */
    RhiAttachment depth;          /* D16 DEPTH_TARGET texture, or none */
    RhiLoadOp     color_load, depth_load;
    float         clear_color[4];
    float         clear_depth;
} RhiPassDesc;

/* RHI_ERR_UNSUPPORTED if the attachments cannot be rendered to together. */
RhiResult rhi_cmd_begin_pass(const RhiPassDesc *desc);
void      rhi_cmd_end_pass(void);

/* Copies a rectangle of the main target (x, y from the bottom-left) into level
 * 0 of a COPY_DST texture, outside any pass: the colour for an RGBA8 texture,
 * the depth for an RHI_FORMAT_DEPTH one. The texture takes the rectangle's
 * size. For screen-space effects that sample the frame drawn so far.
 * RHI_ERR_INVALID for a texture without COPY_DST or a rectangle outside the
 * selected scene (or physical open frame if none); RHI_ERR_UNSUPPORTED if
 * the backend cannot copy it (GLES2: depth). */
RhiResult rhi_cmd_copy_main_to_texture(RhiTexture texture, int32_t x, int32_t y,
                                       int32_t w, int32_t h);

/* Clears the whole main target: colour to `color`, depth to `depth`, stencil
 * to 0. The main target is the selected scene, or the physical drawable.
 * Like rhi_cmd_begin_pass, the viewport becomes that target's size, the
 * main target is bound, the scissor test goes off and the last pipeline's
 * write masks do not apply (they are restored afterwards).
 * Only inside a frame and outside a pass. */
RhiResult rhi_cmd_clear_main(const float color[4], float depth);

/* The viewport the next draw uses: x, y, width, height in pixels from the
 * bottom left of the target. */
void rhi_viewport(int32_t viewport[4]);

/* The state the next draw uses when no pipeline or texture is bound in
 * between: the bound pipeline and its raster state, the texture on sampler
 * slots 0..RHI_STATE_TEXTURE_SLOTS-1, the viewport, the target, the blend
 * equation, the array-buffer and element-buffer bindings, which vertex
 * attribute arrays are enabled, and the pointer description (size, type,
 * normalization, stride, offset, source buffer) of attributes
 * 0..RHI_MAX_VERTEX_ATTRIBS-1. Helpers
 * that bind their own pipelines save it on entry and restore it on exit, so
 * callers that still draw with ambient state (HUD, frontend, debug UI) get
 * theirs back. Restore sends only what differs. Not included: stencil
 * functions and the cull face (set by every pipeline that uses them), the
 * scissor rectangle, wireframe mode, or cube textures.
 * Current helpers do not change the scissor rectangle or wireframe mode;
 * a helper that does must preserve those explicitly. */
#define RHI_STATE_TEXTURE_SLOTS 8
typedef struct RhiStateSnapshot { uint32_t opaque[112]; } RhiStateSnapshot;
void rhi_cmd_save_state(RhiStateSnapshot *out);
void rhi_cmd_restore_state(const RhiStateSnapshot *state);
/* Makes `state` restore `texture` (2D) on `slot` instead of what was there:
 * for helpers whose output is a bound texture. */
void rhi_state_set_texture(RhiStateSnapshot *state, uint32_t slot, RhiTexture texture);

/* The area of the target that draws map to, in pixels from the bottom left.
 * It stays until the next call, rhi_cmd_begin_pass, rhi_cmd_clear_main or
 * rhi_cmd_restore_state. RHI_ERR_INVALID for a negative size. */
RhiResult rhi_cmd_set_viewport(int32_t x, int32_t y, int32_t width, int32_t height);

/* Debug views: draw triangles as their edges, whatever pipeline is bound,
 * until switched off again. RHI_ERR_UNSUPPORTED without caps.wireframe. */
RhiResult rhi_cmd_set_wireframe(int on);

/* ------------------------------------------------------------------------------ */
/* GPU timers                                                                     */
/* ------------------------------------------------------------------------------ */
/* Measure the GPU time of the commands between begin and end, for profiling.
 * The result arrives some frames later and is read without waiting. Only with
 * caps.gpu_timer; one timer runs at a time. */
typedef struct RhiTimer { uint32_t id; } RhiTimer;

RhiTimer  rhi_timer_create(void);   /* {0} without caps.gpu_timer */
void      rhi_timer_destroy(RhiTimer timer);
RhiResult rhi_cmd_timer_begin(RhiTimer timer);
RhiResult rhi_cmd_timer_end(void);   /* ends the running timer */
/* 1 with *ns set once the timer's last measurement has finished on the GPU;
 * 0 while it has not (or for an invalid timer). */
int       rhi_timer_read(RhiTimer timer, uint64_t *ns);

#ifdef __cplusplus
}
#endif
#endif /* RHI_H */
