#include "texture_atlas.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>

// PICA200 needs its own native texel order, not a plain top-down raster: the
// image is divided into 8x8 tiles, texels within a tile are stored in Z-order
// (Morton code - low 3 bits of x and y bit-interleaved), and the whole image
// is vertically flipped versus a normal top-to-bottom source raster because
// the GPU's texture v-axis starts at the bottom. There is no asset pipeline
// doing this for us (procedural, no build-step per item 34), so it is
// hand-rolled here and swizzle() is the one place that has to get it right.
static const u16 xlut[8] = { 0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15 };
static const u16 ylut[8] = { 0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A };

// Swizzles a size x size linear (top-down, row-major) RGB565 buffer into the
// PICA200 native tiled order. size must be a multiple of 8.
static void swizzle(const u16* linear, u16* out, int size)
{
    int tilesPerRow = size / 8;
    for (int y = 0; y < size; y++)
    {
        int fy = size - 1 - y; // whole-image vertical flip
        int tileY = fy / 8, inY = fy & 7;
        for (int x = 0; x < size; x++)
        {
            int tileX = x / 8, inX = x & 7;
            int tileNum = tileY * tilesPerRow + tileX;
            int dst = tileNum * 64 + (xlut[inX] | ylut[inY]);
            out[dst] = linear[y * size + x];
        }
    }
}

static inline u16 rgb565(u8 r, u8 g, u8 b)
{
    return (u16)(((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F));
}

// Solid opaque white - MODULATE against this leaves the lit vertex colour
// untouched, so this is what every part with no real UV (uv defaults to
// {0,0}, which lands in this quadrant's corner) samples.
static void paintWhite(u16* linear, int size, int ox, int oy, int rw, int rh)
{
    (void)size;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++)
            linear[(oy + y) * ATLAS_SIZE + (ox + x)] = rgb565(31, 63, 31);
}

// Subtle procedural grain: a soft, low-contrast noise field close to white so
// it reads as surface texture rather than a colour shift. Deterministic
// (fixed seed) so the atlas is identical every boot.
static void paintGrain(u16* linear, int size, int ox, int oy, int rw, int rh)
{
    (void)size;
    unsigned seed = 1;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++)
        {
            seed = seed * 1103515245u + 12345u;
            int n = 26 + (int)((seed >> 16) % 6); // 26..31 of 31 - subtle
            int gShade = (int)(n * 2.0f);
            if (gShade > 63) gShade = 63;
            linear[(oy + y) * ATLAS_SIZE + (ox + x)] = rgb565((u8)n, (u8)gShade, (u8)n);
        }
}

// Diamond-plate tread: a coarse crosshatch of two diagonal bands, dark
// against a mid-grey base, sized to read at the Excavator tracks' scale.
static void paintTread(u16* linear, int size, int ox, int oy, int rw, int rh)
{
    (void)size;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++)
        {
            int a = (x + y) % 8;
            int b = (x - y + rw) % 8;
            bool ridge = (a == 0 || a == 1 || b == 0 || b == 1);
            u8 v = ridge ? 12 : 22; // dark ridge lines over a mid base
            u8 g = (u8)(v * 2.0f > 63 ? 63 : v * 2);
            linear[(oy + y) * ATLAS_SIZE + (ox + x)] = rgb565(v, g, v);
        }
}

// Thin panel-line grid: mostly white base with a fine dark line every eight
// texels, to read as panel seams on hull/body/cab-style parts.
static void paintPanel(u16* linear, int size, int ox, int oy, int rw, int rh)
{
    (void)size;
    for (int y = 0; y < rh; y++)
        for (int x = 0; x < rw; x++)
        {
            bool line = (x % 8 == 0) || (y % 8 == 0);
            u16 c = line ? rgb565(18, 36, 18) : rgb565(30, 61, 30);
            linear[(oy + y) * ATLAS_SIZE + (ox + x)] = c;
        }
}

// TEST_ATLAS_GRADIENT content: a smooth two-axis gradient across the whole
// atlas, for visually verifying swizzle() before trusting it with the real
// regions above. See debug.h's comment on this flag. Compiled out (rather
// than just unreferenced) when the flag is off, so -Werror's unused-function
// check stays clean without needing a fake reference.
#if TEST_ATLAS_GRADIENT
static void paintGradient(u16* linear, int size)
{
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
        {
            u8 r = (u8)((x * 31) / (size - 1));
            u8 g = (u8)((y * 63) / (size - 1));
            linear[y * size + x] = rgb565(r, g, 16);
        }
}
#endif

static C3D_Tex atlasTex;
static bool atlasReady = false;

bool textureAtlasInit(void)
{
    u16* linear = (u16*)malloc(ATLAS_SIZE * ATLAS_SIZE * sizeof(u16));
    if (!linear) return false;

    // Always paint the real regions - even under TEST_ATLAS_GRADIENT - so
    // none of the four painters below can ever go unreferenced and trip
    // -Werror=unused-function. The gradient (if enabled) is painted second,
    // overwriting the whole buffer for the swizzle-verification pass only.
    paintWhite(linear, ATLAS_SIZE, 0, 0, ATLAS_SIZE / 2, ATLAS_SIZE / 2);
    paintGrain(linear, ATLAS_SIZE, ATLAS_SIZE / 2, 0, ATLAS_SIZE / 2, ATLAS_SIZE / 2);
    paintTread(linear, ATLAS_SIZE, 0, ATLAS_SIZE / 2, ATLAS_SIZE / 2, ATLAS_SIZE / 2);
    paintPanel(linear, ATLAS_SIZE, ATLAS_SIZE / 2, ATLAS_SIZE / 2, ATLAS_SIZE / 2, ATLAS_SIZE / 2);
#if TEST_ATLAS_GRADIENT
    paintGradient(linear, ATLAS_SIZE);
#endif

    // VRAM-resident rather than C3D_TexInit's FCRAM: the whole point of
    // measuring VRAM% before and after (item 34's memory gate) is an honest
    // number, and an FCRAM-backed texture would not move it at all.
    if (!C3D_TexInitVRAM(&atlasTex, ATLAS_SIZE, ATLAS_SIZE, GPU_RGB565))
    {
        free(linear);
        return false;
    }

    // linearAlloc, not malloc: this buffer is the GX transfer engine's DMA
    // source for the VRAM upload below (C3D_TexInitVRAM's onVram=true makes
    // C3D_TexUpload use GX_TextureCopy, not a plain memcpy), and that engine
    // needs physically-contiguous, cache-coherent linear memory - a regular
    // heap malloc() is neither. Root cause of a solid-black bench once the
    // atlas was bound and sampled: the upload silently transferred
    // garbage/zero into VRAM from an unsuitable source buffer, so every
    // texel read back as (0,0,0), and MODULATE multiplied every lit vertex
    // colour down to black. Found via bisection in scratchpad/probe_bisect.
    u16* tiled = (u16*)linearAlloc(ATLAS_SIZE * ATLAS_SIZE * sizeof(u16));
    if (!tiled)
    {
        free(linear);
        C3D_TexDelete(&atlasTex);
        return false;
    }
    swizzle(linear, tiled, ATLAS_SIZE);
    free(linear);

    C3D_TexUpload(&atlasTex, tiled);
    linearFree(tiled);

    C3D_TexSetFilter(&atlasTex, GPU_LINEAR, GPU_LINEAR);
    // Sub-regions share one atlas, so hardware wrap must never bleed one
    // region into its neighbour. Tiling within a region (repeatU/repeatV) is
    // done CPU-side in mesh.c's applyAtlasUV, by fractional wrapping the UV
    // before it is mapped into the region's sub-rectangle - CLAMP here is
    // what keeps that sub-rectangle's edges from smearing into the atlas
    // next to it.
    C3D_TexSetWrap(&atlasTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);

    atlasReady = true;
    return true;
}

C3D_Tex* textureAtlasGet(void)
{
    return atlasReady ? &atlasTex : NULL;
}
