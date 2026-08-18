// Item 34: one small shared texture atlas for the whole scene. No image
// assets and no build-step - the atlas is painted procedurally into an RGB565
// buffer at boot, PICA200-swizzled, and uploaded once. Everything that is not
// explicitly given real UVs (mesh.h's vertex::uv defaults to {0,0} whenever a
// vertex is not touched by applyAtlasUV in mesh.c) samples the WHITE region's
// corner texel, which is opaque white - MODULATE against that leaves a plain
// part's lit vertex colour unchanged, so adding this atlas cannot regress any
// part nobody has textured.
//
// Layout: 64x64 RGB565, four 32x32 quadrants, one per named region below.
// 64x64x2 bytes = 8192 bytes (8 KB) total, VRAM-resident.
#pragma once
#include <citro3d.h>

#define ATLAS_SIZE 64

// Default/untouched vertices - solid opaque white so GPU_MODULATE is a no-op
// against the lit vertex colour.
#define ATLAS_REGION_WHITE_U0 0.0f
#define ATLAS_REGION_WHITE_V0 0.0f
#define ATLAS_REGION_WHITE_U1 0.5f
#define ATLAS_REGION_WHITE_V1 0.5f

// Subtle procedural grain for the kit-box print panel and the runner frame -
// close to white so it reads as surface texture, not a colour change.
#define ATLAS_REGION_GRAIN_U0 0.5f
#define ATLAS_REGION_GRAIN_V0 0.0f
#define ATLAS_REGION_GRAIN_U1 1.0f
#define ATLAS_REGION_GRAIN_V1 0.5f

// Diamond-plate tread, for the Excavator's tracks.
#define ATLAS_REGION_TREAD_U0 0.0f
#define ATLAS_REGION_TREAD_V0 0.5f
#define ATLAS_REGION_TREAD_U1 0.5f
#define ATLAS_REGION_TREAD_V1 1.0f

// Thin panel-line grid, for hull/body/cab/chassis/torso/fuselage/cabin/
// boiler/core parts across kits.
#define ATLAS_REGION_PANEL_U0 0.5f
#define ATLAS_REGION_PANEL_V0 0.5f
#define ATLAS_REGION_PANEL_U1 1.0f
#define ATLAS_REGION_PANEL_V1 1.0f

// Builds the atlas content, swizzles it into PICA200's native 8x8-tile
// Z-order layout, and uploads it into a VRAM-resident C3D_Tex. Call once
// during gameInit, after C3D_Init and before the first sceneBind(). Returns
// false if the VRAM allocation failed; the caller decides whether that is
// fatal (see texture_atlas.c's own comment on why it is not, currently).
bool textureAtlasInit(void);

// The atlas texture, or NULL if textureAtlasInit failed or has not run yet.
C3D_Tex* textureAtlasGet(void);
