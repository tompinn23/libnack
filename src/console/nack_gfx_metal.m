/*
 * The Metal implementation of the console's graphics backend, for macOS.
 *
 * Apple deprecated OpenGL in 10.14 and caps it at 4.1, so Metal is the path
 * that will still be there in a few years. The console layer above this is
 * unchanged: it produces the same quads and calls the same interface.
 *
 * The OpenGL backend is compiled into the macOS build too, and nack_gfx.c
 * falls back to it if anything here refuses to start. Restoring the view is
 * therefore part of the contract: nack__mtl_shutdown has to undo whatever
 * nack__mtl_init managed to do, however far it got.
 *
 * The shaders are Metal Shading Language, which is C++ based - that is not a
 * choice, it is the only shading language Metal accepts. No host code here is
 * anything but Objective-C.
 */
#include "nack_gfx.h"
#include "nack_console_internal.h"

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <stdlib.h>
#include <string.h>

/*
 * A texture is opaque to everything above, and both backends are linked
 * together here, so each keeps its own type rather than two definitions of one
 * tag. Only the backend that made a texture ever looks inside it.
 */
struct nack__mtl_texture {
    id<MTLTexture> texture;
};

struct nack_metal_backend {
    struct nack_window *window;
    NSView *view;
    CAMetalLayer *layer;

    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLRenderPipelineState> pipeline;
    id<MTLSamplerState> sampler;

    id<MTLBuffer> vertices;
    size_t vertex_capacity;          /* in vertices, not bytes */

    /* Valid only between begin_frame and end_frame. */
    id<CAMetalDrawable> drawable;
    id<MTLCommandBuffer> commands;
    id<MTLRenderCommandEncoder> encoder;

    /* Kept after the frame so the tests can read pixels back. */
    id<MTLCommandBuffer> last_commands;
    id<MTLTexture> last_texture;

    int fb_width, fb_height;
    int viewport_w, viewport_h;
    bool vsync;
};

static struct nack_metal_backend nack__mtl;

/*
 * Matches the interleaved float layout the console produces: position, uv,
 * foreground, background. float4 is 16-byte aligned, and 2+2 floats of
 * position and uv fill exactly one such slot, so the strides line up without
 * padding.
 */
static NSString *const nack__shader_source = @""
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"struct Vertex {\n"
"    float2 position;\n"
"    float2 uv;\n"
"    float4 fg;\n"
"    float4 bg;\n"
"};\n"
"\n"
"struct Uniforms {\n"
"    float2 viewport;\n"
"    int mode;\n"
"};\n"
"\n"
"struct Varying {\n"
"    float4 position [[position]];\n"
"    float2 uv;\n"
"    float4 fg;\n"
"    float4 bg;\n"
"};\n"
"\n"
"vertex Varying nack_vertex(uint id [[vertex_id]],\n"
"                           const device Vertex *vertices [[buffer(0)]],\n"
"                           constant Uniforms &uniforms [[buffer(1)]])\n"
"{\n"
"    Varying out;\n"
"    float2 ndc = (vertices[id].position / uniforms.viewport) * 2.0 - 1.0;\n"
"    out.position = float4(ndc.x, -ndc.y, 0.0, 1.0);\n"
"    out.uv = vertices[id].uv;\n"
"    out.fg = vertices[id].fg;\n"
"    out.bg = vertices[id].bg;\n"
"    return out;\n"
"}\n"
"\n"
"fragment float4 nack_fragment(Varying in [[stage_in]],\n"
"                              texture2d<float> atlas [[texture(0)]],\n"
"                              sampler atlas_sampler [[sampler(0)]],\n"
"                              constant Uniforms &uniforms [[buffer(1)]])\n"
"{\n"
"    if (uniforms.mode == 0) {\n"
"        return in.bg;\n"
"    }\n"
"    float4 texel = atlas.sample(atlas_sampler, in.uv);\n"
"    float4 colour = float4(in.fg.rgb * texel.rgb, in.fg.a * texel.a);\n"
"    if (colour.a <= 0.0) {\n"
"        discard_fragment();\n"
"    }\n"
"    return colour;\n"
"}\n";

struct nack_uniforms {
    float viewport[2];
    int mode;
    int padding;         /* keep the struct 16-byte aligned for Metal */
};

static bool nack__mtl_init(struct nack_window *window)
{
    @autoreleasepool {
        struct nack_native_window native;
        MTLRenderPipelineDescriptor *descriptor;
        MTLSamplerDescriptor *sampler_descriptor;
        MTLCompileOptions *options;
        id<MTLLibrary> library;
        NSError *error = nil;

        memset(&nack__mtl, 0, sizeof nack__mtl);
        nack__mtl.window = window;

        nack_window_get_native(window, &native);
        nack__mtl.view = (NSView *)native.view;
        if (!nack__mtl.view)
            return nack__error("the window has no view to attach Metal to");

        nack__mtl.device = MTLCreateSystemDefaultDevice();
        if (!nack__mtl.device)
            return nack__error("no Metal device is available");

        nack__mtl.queue = [nack__mtl.device newCommandQueue];
        if (!nack__mtl.queue)
            return nack__error("cannot create a Metal command queue");

        /* +layer returns an autoreleased object; the view will retain it, but
         * hold our own reference so the struct pointer cannot dangle. */
        nack__mtl.layer = [[CAMetalLayer layer] retain];
        nack__mtl.layer.device = nack__mtl.device;
        nack__mtl.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        /* Readable so the tests can check what was drawn. */
        nack__mtl.layer.framebufferOnly = NO;
        {
            CGFloat scale = [[nack__mtl.view window] backingScaleFactor];
            nack__mtl.layer.contentsScale = scale > 0.0 ? scale : 1.0;
        }

        [nack__mtl.view setWantsLayer:YES];
        [nack__mtl.view setLayer:nack__mtl.layer];

        options = [[MTLCompileOptions alloc] init];
        library = [nack__mtl.device newLibraryWithSource:nack__shader_source
                                                 options:options
                                                   error:&error];
        [options release];
        if (!library) {
            return nack__error("console shader failed to compile: %s",
                               error ? [[error localizedDescription] UTF8String]
                                     : "unknown");
        }

        descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        {
            id<MTLFunction> vertex_fn =
                [library newFunctionWithName:@"nack_vertex"];
            id<MTLFunction> fragment_fn =
                [library newFunctionWithName:@"nack_fragment"];
            descriptor.vertexFunction = vertex_fn;
            descriptor.fragmentFunction = fragment_fn;
            [vertex_fn release];
            [fragment_fn release];
        }
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        descriptor.colorAttachments[0].blendingEnabled = YES;
        descriptor.colorAttachments[0].sourceRGBBlendFactor =
            MTLBlendFactorSourceAlpha;
        descriptor.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        descriptor.colorAttachments[0].sourceAlphaBlendFactor =
            MTLBlendFactorSourceAlpha;
        descriptor.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;

        nack__mtl.pipeline =
            [nack__mtl.device newRenderPipelineStateWithDescriptor:descriptor
                                                             error:&error];
        [descriptor release];
        [library release];
        if (!nack__mtl.pipeline) {
            return nack__error("console pipeline failed to build: %s",
                               error ? [[error localizedDescription] UTF8String]
                                     : "unknown");
        }

        /* Nearest keeps pixel art crisp; a console never wants smoothing. */
        sampler_descriptor = [[MTLSamplerDescriptor alloc] init];
        sampler_descriptor.minFilter = MTLSamplerMinMagFilterNearest;
        sampler_descriptor.magFilter = MTLSamplerMinMagFilterNearest;
        sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
        nack__mtl.sampler =
            [nack__mtl.device newSamplerStateWithDescriptor:sampler_descriptor];
        [sampler_descriptor release];

        nack__mtl.vsync = true;
        return true;
    }
}

static void nack__mtl_shutdown(void)
{
    @autoreleasepool {
        [nack__mtl.vertices release];
        [nack__mtl.sampler release];
        [nack__mtl.pipeline release];
        [nack__mtl.queue release];
        [nack__mtl.device release];
        [nack__mtl.last_commands release];
        [nack__mtl.last_texture release];
        if (nack__mtl.view) {
            [nack__mtl.view setLayer:nil];
            [nack__mtl.view setWantsLayer:NO];
        }
        [nack__mtl.layer release];
        memset(&nack__mtl, 0, sizeof nack__mtl);
    }
}

static struct nack_texture *nack__mtl_texture_create(const uint8_t *rgba,
                                                     int width, int height)
{
    @autoreleasepool {
        struct nack__mtl_texture *texture;
        MTLTextureDescriptor *descriptor;

        texture = (struct nack__mtl_texture *)calloc(1, sizeof *texture);
        if (!texture) {
            nack__error("out of memory");
            return NULL;
        }

        descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:(NSUInteger)width
                                        height:(NSUInteger)height
                                     mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;

        texture->texture = [nack__mtl.device newTextureWithDescriptor:descriptor];
        if (!texture->texture) {
            free(texture);
            nack__error("cannot create a Metal texture");
            return NULL;
        }

        [texture->texture replaceRegion:MTLRegionMake2D(0, 0, (NSUInteger)width,
                                                        (NSUInteger)height)
                            mipmapLevel:0
                              withBytes:rgba
                            bytesPerRow:(NSUInteger)width * 4];
        return (struct nack_texture *)texture;
    }
}

static void nack__mtl_texture_destroy(struct nack_texture *handle)
{
    struct nack__mtl_texture *texture = (struct nack__mtl_texture *)handle;

    if (!texture)
        return;
    [texture->texture release];
    free(texture);
}

static void nack__mtl_begin_frame(struct nack_color clear, int fb_width,
                                  int fb_height, int viewport_x,
                                  int viewport_y, int viewport_w,
                                  int viewport_h)
{
    @autoreleasepool {
        MTLRenderPassDescriptor *pass;
        MTLViewport viewport;

        if (!nack__mtl.pipeline)
            return;

        if (fb_width != nack__mtl.fb_width || fb_height != nack__mtl.fb_height) {
            nack__mtl.fb_width = fb_width;
            nack__mtl.fb_height = fb_height;
            nack__mtl.layer.drawableSize = CGSizeMake(fb_width, fb_height);
        }

        nack__mtl.drawable = [[nack__mtl.layer nextDrawable] retain];
        if (!nack__mtl.drawable)
            return;   /* the window is off screen; skip the frame */

        pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = nack__mtl.drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        /* The clear fills the whole drawable, so it is the letterbox colour. */
        pass.colorAttachments[0].clearColor =
            MTLClearColorMake(clear.r / 255.0, clear.g / 255.0,
                              clear.b / 255.0, 1.0);

        nack__mtl.commands = [[nack__mtl.queue commandBuffer] retain];
        nack__mtl.encoder =
            [[nack__mtl.commands renderCommandEncoderWithDescriptor:pass] retain];

        /* Metal's viewport origin is top left, which is where the console
         * layer measures from too, so this needs no flip. */
        viewport.originX = viewport_x;
        viewport.originY = viewport_y;
        viewport.width = viewport_w;
        viewport.height = viewport_h;
        viewport.znear = 0.0;
        viewport.zfar = 1.0;
        nack__mtl.viewport_w = viewport_w;
        nack__mtl.viewport_h = viewport_h;
        [nack__mtl.encoder setViewport:viewport];
        [nack__mtl.encoder setRenderPipelineState:nack__mtl.pipeline];
        [nack__mtl.encoder setFragmentSamplerState:nack__mtl.sampler atIndex:0];

        {
            struct nack_uniforms uniforms;
            memset(&uniforms, 0, sizeof uniforms);
            uniforms.viewport[0] = (float)viewport_w;
            uniforms.viewport[1] = (float)viewport_h;
            uniforms.mode = 0;
            [nack__mtl.encoder setVertexBytes:&uniforms
                                       length:sizeof uniforms
                                      atIndex:1];
            [nack__mtl.encoder setFragmentBytes:&uniforms
                                         length:sizeof uniforms
                                        atIndex:1];
        }
    }
}

static bool nack__reserve(size_t vertex_count)
{
    size_t bytes;

    if (vertex_count <= nack__mtl.vertex_capacity)
        return true;

    bytes = vertex_count * NACK_FLOATS_PER_VERTEX * sizeof(float);
    [nack__mtl.vertices release];
    nack__mtl.vertices =
        [nack__mtl.device newBufferWithLength:bytes
                                      options:MTLResourceStorageModeShared];
    if (!nack__mtl.vertices) {
        nack__mtl.vertex_capacity = 0;
        return nack__error("cannot allocate a Metal vertex buffer");
    }
    nack__mtl.vertex_capacity = vertex_count;
    return true;
}

static void nack__mtl_draw(const float *vertices, size_t vertex_count,
                           int mode, struct nack_texture *handle)
{
    struct nack__mtl_texture *texture = (struct nack__mtl_texture *)handle;
    struct nack_uniforms uniforms;

    if (!nack__mtl.encoder || vertex_count == 0)
        return;
    if (!nack__reserve(vertex_count))
        return;

    memcpy([nack__mtl.vertices contents], vertices,
           vertex_count * NACK_FLOATS_PER_VERTEX * sizeof(float));

    memset(&uniforms, 0, sizeof uniforms);
    uniforms.viewport[0] = (float)nack__mtl.viewport_w;
    uniforms.viewport[1] = (float)nack__mtl.viewport_h;
    uniforms.mode = mode;

    [nack__mtl.encoder setVertexBuffer:nack__mtl.vertices offset:0 atIndex:0];
    [nack__mtl.encoder setVertexBytes:&uniforms length:sizeof uniforms atIndex:1];
    [nack__mtl.encoder setFragmentBytes:&uniforms length:sizeof uniforms atIndex:1];
    if (texture)
        [nack__mtl.encoder setFragmentTexture:texture->texture atIndex:0];

    [nack__mtl.encoder drawPrimitives:MTLPrimitiveTypeTriangle
                          vertexStart:0
                          vertexCount:(NSUInteger)vertex_count];
}

static void nack__mtl_end_frame(void)
{
    @autoreleasepool {
        if (!nack__mtl.encoder)
            return;

        [nack__mtl.encoder endEncoding];
        [nack__mtl.commands presentDrawable:nack__mtl.drawable];
        [nack__mtl.commands commit];

        /* Kept so a readback can wait on this frame rather than stalling
         * every frame that nobody inspects. */
        [nack__mtl.last_commands release];
        [nack__mtl.last_texture release];
        nack__mtl.last_commands = nack__mtl.commands;
        nack__mtl.last_texture = [nack__mtl.drawable.texture retain];

        [nack__mtl.encoder release];
        [nack__mtl.drawable release];
        nack__mtl.encoder = nil;
        nack__mtl.drawable = nil;
        nack__mtl.commands = nil;
    }
}

static void nack__mtl_resize(int fb_width, int fb_height)
{
    @autoreleasepool {
        nack__mtl.fb_width = fb_width;
        nack__mtl.fb_height = fb_height;
        if (nack__mtl.layer)
            nack__mtl.layer.drawableSize = CGSizeMake(fb_width, fb_height);
    }
}

static void nack__mtl_set_vsync(bool vsync)
{
    nack__mtl.vsync = vsync;
    if (!nack__mtl.layer)
        return;
    /* displaySyncEnabled arrived in 10.13; older systems are always synced. */
    if ([nack__mtl.layer respondsToSelector:@selector(setDisplaySyncEnabled:)])
        nack__mtl.layer.displaySyncEnabled = vsync ? YES : NO;
}

static bool nack__mtl_read_pixel(int x, int y, uint8_t rgba[4])
{
    @autoreleasepool {
        uint8_t bgra[4] = { 0, 0, 0, 0 };

        if (!nack__mtl.last_texture)
            return false;
        if (x < 0 || y < 0 ||
            (NSUInteger)x >= [nack__mtl.last_texture width] ||
            (NSUInteger)y >= [nack__mtl.last_texture height])
            return false;

        /* The frame has to have finished before its pixels can be read. */
        [nack__mtl.last_commands waitUntilCompleted];

        /*
         * A managed texture, which is what a drawable is on an Intel Mac, has
         * a separate GPU copy that has to be synchronised before the CPU can
         * see it. On Apple silicon the drawable is shared and this is skipped.
         */
        if ([nack__mtl.last_texture storageMode] == MTLStorageModeManaged) {
            id<MTLCommandBuffer> blit_commands = [nack__mtl.queue commandBuffer];
            id<MTLBlitCommandEncoder> blit = [blit_commands blitCommandEncoder];
            [blit synchronizeResource:nack__mtl.last_texture];
            [blit endEncoding];
            [blit_commands commit];
            [blit_commands waitUntilCompleted];
        }

        [nack__mtl.last_texture getBytes:bgra
                            bytesPerRow:4
                             fromRegion:MTLRegionMake2D((NSUInteger)x,
                                                        (NSUInteger)y, 1, 1)
                            mipmapLevel:0];

        /* The drawable is BGRA; the interface promises RGBA. */
        rgba[0] = bgra[2];
        rgba[1] = bgra[1];
        rgba[2] = bgra[0];
        rgba[3] = bgra[3];
        return true;
    }
}

static const struct nack_gfx_backend nack__mtl_backend = {
    "metal",
    nack__mtl_init,
    nack__mtl_shutdown,
    nack__mtl_texture_create,
    nack__mtl_texture_destroy,
    nack__mtl_begin_frame,
    nack__mtl_draw,
    nack__mtl_end_frame,
    nack__mtl_resize,
    nack__mtl_set_vsync,
    nack__mtl_read_pixel
};

const struct nack_gfx_backend *nack__gfx_backend_metal(void)
{
    return &nack__mtl_backend;
}
