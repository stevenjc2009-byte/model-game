// Model Kit - the workbench
//
// Step 5 of the project: the kit gets somewhere to be built. A wooden hobby desk
// with a green self-healing cutting mat on it, the mat's grid printed coarse
// enough to survive 320x240, and the runner standing on top of it.
//   bottom screen - the workbench in 3D. Drag to turn it, tap a gate to snip,
//                   tap a part to select it.
//   top screen    - hardware, memory, sprue size, cut count, what is selected
//   START         - close the game
//
// Still written to the Original 3DS budget: one VBO, one draw per material, no
// textures, no render-to-texture. Every colour here is a material on flat-shaded
// boxes, which is what reads at this size - detail would only turn to noise.

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"
#include "settings.h"
#include "controls.h"
#include "title.h"
#include "memory_status.h"
#include "vshader_shbin.h"

// Capture-only override. Leave at 0 for the shipped build; the verification
// pass temporarily selects one level without relying on emulator key mappings.
#ifndef TEST_CAPTURE_LEVEL
#define TEST_CAPTURE_LEVEL 0
#endif
#ifndef TEST_CAPTURE_MENU
#define TEST_CAPTURE_MENU 0
#endif
#ifndef TEST_CAPTURE_ASSEMBLED
#define TEST_CAPTURE_ASSEMBLED 0
#endif
#ifndef TEST_CAPTURE_OPEN
#define TEST_CAPTURE_OPEN 0
#endif
#ifndef TEST_CAPTURE_CUT_ONE
#define TEST_CAPTURE_CUT_ONE 0
#endif
#ifndef TEST_CAPTURE_RUNNER
#define TEST_CAPTURE_RUNNER 0
#endif
#ifndef TEST_CAPTURE_HIDE_GUIDE
#define TEST_CAPTURE_HIDE_GUIDE 0
#endif
#ifndef TEST_CAPTURE_VIEW
#define TEST_CAPTURE_VIEW 0
#endif
#ifndef TEST_AUDIT_ALL_KITS
#define TEST_AUDIT_ALL_KITS 0
#endif
#ifndef TEST_CAMERA_IDLE_AUDIT
#define TEST_CAMERA_IDLE_AUDIT 0
#endif
#ifndef TEST_LEVEL1_WORKSPACE_AUDIT
#define TEST_LEVEL1_WORKSPACE_AUDIT 0
#endif
#ifndef TEST_CEILING_AUDIT
#define TEST_CEILING_AUDIT 0
#endif
#ifndef TEST_COLLISION_AUDIT
#define TEST_COLLISION_AUDIT 0
#endif

// The room the bench sits in: a warm, dim studio. Dark enough that the desk and
// the mat carry the picture, warm enough that it does not read as a void.
#define CLEAR_COLOR 0x3A312AFF

#define DISPLAY_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define TOP_TRANSFER_FLAGS \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
	GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
	GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define BOTTOM_W 320
#define BOTTOM_H 240
#define FIELD_OF_VIEW 55.0f

// Camera limits: close enough to inspect a gate, far enough to see the runner
// and the mat below it in one shot.
#define CAM_NEAR_LIMIT 1.8f
#define CAM_FAR_LIMIT  6.5f
#define CAM_START      4.25f
#define PITCH_LIMIT    1.45f
#define CIRCLE_ORBIT_DEADZONE 45

// With a part selected the camera is looking at one small object instead of the
// whole bench, so it is allowed much closer - close enough to read a gate.
#define CAM_NEAR_FOCUS 1.0f
#define FOCUS_EASE     0.16f

// The runner stands on the bench, so the whole scene is lifted to put the pair
// of them in the middle of the screen instead of the runner alone. The lift also
// has to be deep enough to pull the desk's near legs into frame - they only
// start below the apron at y -1.61, well under the bottom edge otherwise.
#define SCENE_LIFT     1.00f

// A touch counts as a tap - not a drag - if it barely moves and is let go of
// quickly. Below the slop the sprue does not turn at all, so tapping a part
// never nudges the view.
#define TAP_SLOP_PX 10
#define TAP_MAX_MS  500

// A gate is 0.09 units across - a few pixels. Fatten its hit box so the stylus
// only has to land near it, the way nippers only have to reach the gate.
#define GATE_PAD_XZ 0.06f
#define GATE_PAD_Y  0.03f

// Where a cut part ends up: straight onto the cutting mat, in a grid of slots
// filled left to right, back row first. It stands on MAT_TOP, which mesh.h puts
// a hair above the mat's printed grid lines so a part never sinks into one.
//
// Four columns by three rows, which holds twelve parts loose on the mat at once
// - more than any kit will ever have uncut and unfitted at the same moment. The
// column pitch is 0.90 and the widest part is the 0.95 strut, so even the strut
// beside the next-widest part still leaves a visible gap between them.
//
// The rows thread between the fixed scenery: the back row is behind the runner,
// the middle row in its shadow, the front row between the runner and the build
// stand. Nothing lands inside the runner (z +/-0.06) or the plinth (z 0.63 to
// 1.07), and every row is inside the mat (z -1.00 to +1.20).
#define SLOT_COLS 4
#define SLOT_ROWS 3
static const float slotX[SLOT_COLS] = { -0.72f, -0.24f, 0.24f, 0.72f };
static const float slotZ[SLOT_ROWS] = { -0.42f, 0.08f, 0.48f };

#define CUT_ANIM_MS 350

// Filing: how far the stylus has to travel across a selected part, in touch
// pixels, to wear its nub flush. A part is only 20-40 px across at the default
// zoom, so this is about six back-and-forth rubs - enough to feel like work,
// short enough that nobody gives up halfway.
#define FILE_TRAVEL_PX 200.0f

// Assembly. A socket's hit box is the part-sized space it will be filled with,
// fattened so the stylus only has to land near it - the same allowance the gates
// get, and for the same reason: a resistive screen and a fingernail-sized target.
#define SOCKET_PAD 0.10f

// The one socket the ghost is standing in gets a much fatter allowance than the
// rest. It is the only socket on screen with a visible shape in it, the player
// is being shown exactly where the part goes, and asking them to hit it to
// within a couple of pixels after that is just annoying - so anywhere in or
// around the ghost counts as a tap on it. It is safe to be this generous because
// the fat box is only ever tested for the part currently in hand, once that part
// is snipped and filed and has somewhere to go.
#define GHOST_PAD 0.20f

// Once the last part is off it, the empty frame lifts up out of shot rather than
// standing there in the way. Slow enough to read as being lifted off the bench.
#define RUNNER_LIFT_Y    3.2f
#define RUNNER_LIFT_EASE 0.06f
#define BOX_OPEN_EASE    0.06f
#define MEM_REFRESH_MS   250u

// The App RAM bar is measured against a retail Original 3DS and nothing else,
// whichever console is actually running the game. A retail Old 3DS gives an
// application a 64 MB region; 224 KB of that is gone before main() starts
// (measured: heap + linear always came up 224 KB short of the region size), so
// 65312 KB is what the game can really have. See readMemUsage().
#define OLD3DS_APP_BUDGET (65312u * 1024u)

static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection, uLoc_modelView;
static int uLoc_lightVec, uLoc_lightHalfVec, uLoc_lightClr, uLoc_material;

static C3D_Mtx projection;     // what the GPU draws with (tilted for the screen)
static C3D_Mtx pickProjection; // same view, untilted, for hit-testing on the CPU

// Rows are ambient/diffuse/specular/emission; each is stored {a, b, g, r}.
static C3D_Mtx material =      // unpainted light grey styrene
{
	{
	{ { 0.0f, 0.22f, 0.20f, 0.20f } }, // Ambient
	{ { 0.0f, 0.60f, 0.56f, 0.54f } }, // Diffuse
	{ { 0.0f, 0.45f, 0.45f, 0.45f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

static C3D_Mtx materialRunner = // blue-grey moulded runner, distinct from box
{
	{ { { 0.0f, 0.13f, 0.19f, 0.22f } }, { { 0.0f, 0.18f, 0.29f, 0.34f } },
	  { { 0.0f, 0.22f, 0.22f, 0.22f } }, { { 1.0f, 0.0f, 0.0f, 0.0f } } }
};

static C3D_Mtx materialSelected = // the part under the stylus, in amber
{
	{
	{ { 0.0f, 0.06f, 0.16f, 0.26f } }, // Ambient
	{ { 0.0f, 0.10f, 0.46f, 0.85f } }, // Diffuse
	{ { 0.0f, 0.50f, 0.50f, 0.50f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

// Injection-moulded kits commonly split visible shells into a few purposeful
// colours. The cycle is deliberately restrained so a runner remains legible.
static C3D_Mtx materialPartBlue =
{
	{ { { 0.0f, 0.19f, 0.28f, 0.16f } }, { { 0.0f, 0.25f, 0.48f, 0.28f } },
	  { { 0.0f, 0.30f, 0.30f, 0.30f } }, { { 1.0f, 0.0f, 0.0f, 0.0f } } }
};
static C3D_Mtx materialPartRed =
{
	{ { { 0.0f, 0.14f, 0.16f, 0.29f } }, { { 0.0f, 0.20f, 0.24f, 0.52f } },
	  { { 0.0f, 0.30f, 0.30f, 0.30f } }, { { 1.0f, 0.0f, 0.0f, 0.0f } } }
};
static C3D_Mtx materialPartYellow =
{
	{ { { 0.0f, 0.10f, 0.30f, 0.34f } }, { { 0.0f, 0.14f, 0.50f, 0.58f } },
	  { { 0.0f, 0.30f, 0.30f, 0.30f } }, { { 1.0f, 0.0f, 0.0f, 0.0f } } }
};
static C3D_Mtx* partMaterialFor(int i)
{
	switch (i & 3) {
		case 1: return &materialPartBlue;
		case 2: return &materialPartRed;
		case 3: return &materialPartYellow;
		default: return &material;
	}
}

// The bench itself. Desk and mat are big flat slabs that face away from the
// light at most camera angles, so they carry most of their colour in ambient -
// otherwise the top of the mat goes black the moment you tilt the view.
static C3D_Mtx materialDesk =  // warm hobby-desk wood
{
	{
	{ { 0.0f, 0.15f, 0.24f, 0.33f } }, // Ambient
	{ { 0.0f, 0.16f, 0.25f, 0.35f } }, // Diffuse
	{ { 0.0f, 0.04f, 0.04f, 0.04f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

static C3D_Mtx materialRoom = // warm cream walls and medium oak floor
{
	{
	{ { 0.0f, 0.34f, 0.37f, 0.40f } },
	{ { 0.0f, 0.43f, 0.47f, 0.50f } },
	{ { 0.0f, 0.02f, 0.02f, 0.02f } },
	{ { 1.0f, 0.00f, 0.00f, 0.00f } },
	}
};

static C3D_Mtx materialRoomAccent __attribute__((unused)) = // navy bedding and warm wood furniture
{
	{
	{ { 0.0f, 0.19f, 0.20f, 0.28f } },
	{ { 0.0f, 0.25f, 0.27f, 0.38f } },
	{ { 0.0f, 0.06f, 0.06f, 0.06f } },
	{ { 1.0f, 0.00f, 0.00f, 0.00f } },
	}
};

static C3D_Mtx materialWindow = // daylight blue outside the bedroom window
{
	{
	{ { 0.0f, 0.46f, 0.39f, 0.22f } },
	{ { 0.0f, 0.62f, 0.54f, 0.30f } },
	{ { 0.0f, 0.05f, 0.05f, 0.05f } },
	{ { 1.0f, 0.00f, 0.00f, 0.00f } },
	}
};

static C3D_Mtx materialMat =   // green self-healing cutting mat
{
	{
	{ { 0.0f, 0.11f, 0.23f, 0.15f } }, // Ambient
	{ { 0.0f, 0.12f, 0.24f, 0.15f } }, // Diffuse
	{ { 0.0f, 0.02f, 0.02f, 0.02f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

static C3D_Mtx materialGrid =  // the mat's printed grid, a pale tint of it
{
	{
	{ { 0.0f, 0.26f, 0.34f, 0.28f } }, // Ambient
	{ { 0.0f, 0.28f, 0.38f, 0.31f } }, // Diffuse
	{ { 0.0f, 0.00f, 0.00f, 0.00f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

static C3D_Mtx materialStand = // the build stand, dark neutral plastic
{
	{
	{ { 0.0f, 0.10f, 0.10f, 0.11f } }, // Ambient
	{ { 0.0f, 0.20f, 0.20f, 0.22f } }, // Diffuse
	{ { 0.0f, 0.30f, 0.30f, 0.30f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

static C3D_Mtx materialKitBox = // off-white cardboard, not a bright toy box
{
	{
	{ { 0.0f, 0.48f, 0.46f, 0.44f } }, // Ambient
	{ { 0.0f, 0.66f, 0.63f, 0.59f } }, // Diffuse
	{ { 0.0f, 0.12f, 0.12f, 0.12f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

static C3D_Mtx materialKitArtwork = // a printed blue-and-red model illustration panel
{
	{
	{ { 0.0f, 0.18f, 0.24f, 0.34f } },
	{ { 0.0f, 0.24f, 0.36f, 0.54f } },
	{ { 0.0f, 0.12f, 0.12f, 0.12f } },
	{ { 1.0f, 0.00f, 0.00f, 0.00f } },
	}
};

// The preview of where the held part is going. Dull bronze, because it has to
// read as "not really there" against a green mat and a grey part without going
// so dim it vanishes on a handheld screen in daylight.
static C3D_Mtx materialGhost =
{
	{
	{ { 0.0f, 0.10f, 0.26f, 0.40f } }, // Ambient
	{ { 0.0f, 0.06f, 0.16f, 0.26f } }, // Diffuse
	{ { 0.0f, 0.00f, 0.00f, 0.00f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

// Where each part is relative to where it was moulded. Zero while it is still on
// the runner; the offset out to its place on the mat once it has been cut free.
//
// filed runs 0 to 1 as the stylus wears the nub down. smooth latches when it
// gets there, because that is the flag assembly asks about - a part is either
// ready to seat or it is not, and it never goes back.
//
// A part travels twice - off the runner onto the mat, then off the mat onto the
// build stand - so the move is stored as from -> target rather than as a plain
// target. Without the start point the second trip would snap back to the mat
// before setting off.
typedef struct
{
	bool  cut;
	bool  seated;
	bool  moving;
	u64   startMs;
	float from[3];
	float target[3];
	float offset[3];
	float filed;
	bool  smooth;
} partState;

// One record per mesh part, sized off the same ceiling mesh.c builds against so
// the two can never drift apart and let a part index run off the end of this.
static partState partStates[MESH_MAX_PARTS];
static int cutCount;
static int filedCount;
static int builtCount;
static int currentRunner;

// A build belongs to its level, not to the global scene.  Keeping every level's
// part state separately means leaving level 1 never leaks cut or filed pieces
// into level 2, while returning to level 1 restores exactly that kit.
#define LEVEL_SAVE_COUNT 20
typedef struct
{
	bool visited;
	partState parts[MESH_MAX_PARTS];
	int cutCount, filedCount, builtCount;
	float runnerLift, boxOpen;
	bool boxOpening;
	int manualPage;
	int currentRunner;
} levelBuildState;
static levelBuildState levelBuilds[LEVEL_SAVE_COUNT];
static int loadedLevel = 0;

// How far the emptied runner has been lifted away, 0 to 1.
static float runnerLift = 0.0f;
static float boxOpen = 0.0f;
static bool boxOpening = false;
static u64 filingAnimUntil = 0;

// What the top screen says about the held part - "tap the socket", or why the
// last attempt to seat it was refused.
static char seatMsg[48] = "- - -";

static void* vbo_data;
static float angleX = 0.70f, angleY = 3.4915927f;
static float camDist = CAM_START;
static int   selectedPart = -1;

// What the camera turns and zooms around. With nothing selected it sits at the
// middle of the bench; select a part and it eases across to that part, so from
// then on the view orbits the thing being worked on. focusAmt is how far that
// move has got - 0 is the whole bench, 1 is locked onto the part.
static float focus[3] = { 0.0f, 0.0f, 0.0f };
static float focusAmt = 0.0f;

static void frameWorkbenchCamera(void);
static void runCameraIdleAudit(void);
static void runCeilingAudit(void);

static const float noOffset[3] = { 0.0f, 0.0f, 0.0f };

// Level 1 replaces the text console with this white beginner sheet. It is a
// screen target in its own right; later levels continue using the console.
static C3D_RenderTarget* beginnerTarget;
static C2D_TextBuf beginnerText;

#define PAPER_WHITE C2D_Color32(0xFB, 0xFA, 0xF6, 0xFF)
#define PAPER_INK   C2D_Color32(0x25, 0x25, 0x25, 0xFF)
#define PAPER_LINE  C2D_Color32(0x95, 0x95, 0x90, 0xFF)
#define PAPER_BLUE  C2D_Color32(0x26, 0x5B, 0x8C, 0xFF)
#define PAPER_AMBER C2D_Color32(0xD7, 0x8D, 0x21, 0xFF)
#define PAPER_FADE  C2D_Color32(0xE8, 0xE7, 0xE0, 0xFF)

static void beginnerLabel(const char* label, float x, float y, float scale, u32 colour)
{
	C2D_Text text;
	C2D_TextParse(&text, beginnerText, label);
	C2D_TextOptimize(&text);
	C2D_DrawText(&text, C2D_WithColor, x, y, 0.5f, scale, scale, colour);
}

static int beginnerAction(void)
{
	if (boxOpen < 1.0f) return 0;
	if (selectedPart >= 0)
	{
		const partState* st = &partStates[selectedPart];
		if (!st->cut) return 1;
		if (!st->smooth) return 2;
		return 3;
	}
	for (int i = 0; i < meshPartCount(); i++)
		if (!partStates[i].cut) return 1;
	return 2;
}

static void drawBeginnerSheet(void)
{
	const char* const title[] = { "OPEN THE KIT BOX", "TAP THE SMALL JOIN", "RUB THE LOOSE PART", "TAP THE AMBER SHAPE" };
	const char* const sub[] = { "Tap the red mark on the lid to lift it off.", "Tap the red dot on the small gate.", "Keep rubbing until the nub is smooth.", "The part clicks into its final place." };
	const int active = beginnerAction();

	C3D_RenderTargetClear(beginnerTarget, C3D_CLEAR_ALL, 0xFBFAF6FF, 0);
	C3D_FrameDrawOn(beginnerTarget);
	C2D_Prepare();
	C2D_SceneBegin(beginnerTarget);
	C2D_TextBufClear(beginnerText);
	beginnerLabel("FIRST BUILD - ONE STEP AT A TIME", 34.0f, 10.0f, 0.60f, PAPER_INK);
	char stepLine[32]; snprintf(stepLine, sizeof(stepLine), "STEP %d OF 4", active + 1);
	beginnerLabel(stepLine, 145.0f, 35.0f, 0.42f, PAPER_BLUE);

	// Progress strip: completed steps carry a tick; the current one is blue.
	for (int i = 0; i < 4; i++)
	{
		float px = 82.0f + i * 62.0f;
		u32 c = i < active ? PAPER_BLUE : (i == active ? PAPER_BLUE : PAPER_LINE);
		C2D_DrawRectSolid(px, 57.0f, 0.0f, 46.0f, 6.0f, c);
		if (i < active) beginnerLabel("DONE", px + 4.0f, 67.0f, 0.30f, PAPER_BLUE);
		else
		{
			char n[4]; snprintf(n, sizeof(n), "%d", i + 1);
			beginnerLabel(n, px + 19.0f, 67.0f, 0.42f, c);
		}
	}

	C2D_DrawRectSolid(42.0f, 92.0f, 0.0f, 316.0f, 108.0f, C2D_Color32(0xE8, 0xF1, 0xF8, 0xFF));
	C2D_DrawRectSolid(42.0f, 92.0f, 0.0f, 316.0f, 4.0f, PAPER_BLUE);
	C2D_DrawRectSolid(42.0f, 196.0f, 0.0f, 316.0f, 4.0f, PAPER_BLUE);
	C2D_DrawRectSolid(42.0f, 92.0f, 0.0f, 4.0f, 108.0f, PAPER_BLUE);
	C2D_DrawRectSolid(354.0f, 92.0f, 0.0f, 4.0f, 108.0f, PAPER_BLUE);
	beginnerLabel(title[active], 73.0f, 104.0f, 0.64f, PAPER_INK);
	beginnerLabel(sub[active], 73.0f, 132.0f, 0.34f, PAPER_INK);
	if (active == 0)
	{
		char kitCaption[64];
		snprintf(kitCaption, sizeof(kitCaption), "%s  -  BOX / RUNNER %d OF %d",
			meshKitName(), currentRunner + 1, meshKitRunnerCount());
		beginnerLabel(kitCaption, 73.0f, 146.0f, 0.28f, PAPER_BLUE);
	}

	// One large, literal diagram for the action currently requested.
	if (active == 0)
	{
		// Shoebox in perspective: a lid, front face and one very clear red tap point.
		C2D_DrawRectSolid(132, 169, 0, 136, 20, C2D_Color32(0xE8, 0xE5, 0xDC, 0xFF));
		C2D_DrawRectSolid(139, 161, 0, 122, 10, PAPER_INK);
		C2D_DrawRectSolid(142, 163, 0, 116, 6, C2D_Color32(0xF5, 0xF2, 0xE9, 0xFF));
		C2D_DrawEllipseSolid(197, 160, 0, 12, 12, C2D_Color32(0xD9, 0x3D, 0x36, 0xFF));
	}
	else if (active == 1)
	{
		// An actual little runner: outer rails, two crossbars, a part and its gate.
		C2D_DrawRectSolid(118, 150, 0, 144, 5, PAPER_LINE);
		C2D_DrawRectSolid(118, 186, 0, 144, 5, PAPER_LINE);
		C2D_DrawRectSolid(118, 150, 0, 5, 41, PAPER_LINE);
		C2D_DrawRectSolid(257, 150, 0, 5, 41, PAPER_LINE);
		C2D_DrawRectSolid(183, 150, 0, 5, 41, PAPER_LINE);
		C2D_DrawRectSolid(123, 168, 0, 60, 5, PAPER_LINE);
		C2D_DrawRectSolid(207, 157, 0, 35, 25, PAPER_INK);
		C2D_DrawRectSolid(189, 168, 0, 18, 5, PAPER_AMBER);
		C2D_DrawEllipseSolid(185, 162, 0, 14, 14, C2D_Color32(0xD9, 0x3D, 0x36, 0xFF));
	}
	else if (active == 2)
	{
		C2D_DrawRectSolid(157, 163, 0, 79, 31, PAPER_INK); // loose part
		C2D_DrawRectSolid(139, 151, 0, 115, 10, PAPER_AMBER); // file
		C2D_DrawRectSolid(185, 143, 0, 8, 22, PAPER_BLUE);
		C2D_DrawRectSolid(171, 143, 0, 36, 8, PAPER_BLUE);
	}
	else
	{
		C2D_DrawRectSolid(191, 158, 0, 18, 37, PAPER_INK); // stand
		C2D_DrawRectSolid(151, 188, 0, 98, 7, PAPER_INK);
		C2D_DrawRectSolid(147, 169, 0, 106, 16, PAPER_AMBER); // target
		C2D_DrawRectSolid(176, 150, 0, 48, 19, PAPER_INK); // part
		C2D_DrawRectSolid(196, 141, 0, 8, 10, PAPER_BLUE);
		C2D_DrawRectSolid(182, 141, 0, 36, 8, PAPER_BLUE);
	}

	beginnerLabel("Use the stylus on the bottom screen.", 92.0f, 207.0f, 0.40f, PAPER_INK);
	beginnerLabel("COMPLETE THIS STEP TO CONTINUE", 84.0f, 223.0f, 0.35f, PAPER_BLUE);
	C2D_Flush();
}

// The GPU state the bench needs: its shader, its vertex layout, its buffer and
// its texture combiner. Split out of sceneInit because the front end draws with
// citro2d, and C2D_Prepare() binds all four of those to citro2d's own. Rather
// than track which screen was up last frame, whichever renderer is about to draw
// re-binds its own state first. It is a handful of register writes a frame.
static void sceneBind(void)
{
	C3D_BindProgram(&program);

	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3); // v1=normal

	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbo_data, sizeof(vertex), 2, 0x10);

	// No texture - the fragment stage just passes the lit vertex colour through
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

static void sceneInit(void)
{
	meshValidateKits();
	// Load the vertex shader, create a shader program and bind it
	vshader_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	shaderProgramInit(&program);
	shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
	C3D_BindProgram(&program);

	// Get the location of the uniforms
	uLoc_projection   = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
	uLoc_modelView    = shaderInstanceGetUniformLocation(program.vertexShader, "modelView");
	uLoc_lightVec     = shaderInstanceGetUniformLocation(program.vertexShader, "lightVec");
	uLoc_lightHalfVec = shaderInstanceGetUniformLocation(program.vertexShader, "lightHalfVec");
	uLoc_lightClr     = shaderInstanceGetUniformLocation(program.vertexShader, "lightClr");
	uLoc_material     = shaderInstanceGetUniformLocation(program.vertexShader, "material");

	// Two projections of the same view. The tilted one is what the bottom
	// screen is drawn with; the plain one is used to work out where a part
	// lands in touch coordinates, where x runs right and y runs down.
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(FIELD_OF_VIEW), C3D_AspectRatioBot, 0.01f, 1000.0f, false);
	Mtx_Persp(&pickProjection, C3D_AngleFromDegrees(FIELD_OF_VIEW), C3D_AspectRatioBot, 0.01f, 1000.0f, false);

	// Build the sprue and upload it
	meshBuildKit(1);
	size_t vboSize = meshVertexCount() * sizeof(vertex);
	vbo_data = linearAlloc(vboSize);
	memcpy(vbo_data, meshVertices(), vboSize);

	sceneBind();
}

// The selected level owns its mesh. Rebuild the single VBO only when a new
// level is entered, never during play, so runner/part picking uses the same
// geometry that is visibly on the bench.
static void sceneLoadKit(int level)
{
	meshBuildKit(level);
	size_t vboSize = meshVertexCount() * sizeof(vertex);
	if (vbo_data) linearFree(vbo_data);
	vbo_data = linearAlloc(vboSize);
	memcpy(vbo_data, meshVertices(), vboSize);
	sceneBind();
}

// The normal level view is a seated working view, not a room-tour view.  The
// runner/box occupies the rear-right zone and the stand occupies the tabletop
// centre, so this weighted point keeps both in frame with the loose-part area
// between them.  Everything comes from mesh accessors to stay aligned if the
// workstation moves again.
static void frameWorkbenchCamera(void)
{
	angleX = 0.74f;
	angleY = 3.4915927f;
	camDist = 4.70f;
	focus[0] = meshStandX() * 0.55f + meshRunnerX() * 0.45f;
	focus[1] = MAT_TOP + 0.24f;
	focus[2] = meshStandZ() * 0.55f + meshRunnerZ() * 0.45f;
	focusAmt = 0.20f;
}

static void frameRunnerCamera(void)
{
	angleX = 0.92f;
	angleY = 3.4915927f;
	// The flat runner is wider than the finished model; back out just enough
	// to retain the rails, loose-part area, mat and stand in one working view.
	camDist = 4.75f;
	focus[0] = meshRunnerX();
	focus[1] = MAT_TOP + 0.24f;
	focus[2] = meshRunnerZ();
	focusAmt = 0.55f;
}

static void frameAssemblyCamera(void)
{
	// Once the runner is empty, return from the rail-wide view to the centred
	// model stand. updateFocus supplies the exact socket/model centre each frame.
	angleX = 0.74f;
	angleY = 3.4915927f;
	camDist = 4.35f;
}

static void buildModelView(C3D_Mtx* out)
{
	// Read right to left: put the focus point at the origin, turn the scene
	// around it, then stand back. The lift that centres the whole bench is taken
	// away as the camera locks onto a part, so the part ends up dead centre
	// rather than sitting high.
	Mtx_Identity(out);
	Mtx_Translate(out, 0.0f, SCENE_LIFT * (1.0f - focusAmt), -camDist, true);
	Mtx_RotateX(out, angleX, true);
	Mtx_RotateY(out, angleY, true);
	Mtx_Translate(out, -focus[0], -focus[1], -focus[2], true);
}

// Ceiling slab bounds.  The walls end at its underside, so drawing it while
// the camera has crossed that plane turns the room into an opaque screen.
#define CEILING_UNDERSIDE 2.32f
#define CEILING_TOP       2.48f

static bool ceilingDrawnLast = true;

static void cameraWorldPosition(const C3D_Mtx* modelView, float out[3])
{
	C3D_Mtx inverse = *modelView;
	Mtx_Inverse(&inverse);
	C3D_FVec p = Mtx_MultiplyFVec4(&inverse, FVec4_New(0.0f, 0.0f, 0.0f, 1.0f));
	float iw = p.w != 0.0f ? 1.0f / p.w : 1.0f;
	out[0] = p.x * iw; out[1] = p.y * iw; out[2] = p.z * iw;
}

static bool shouldDrawCeiling(const C3D_Mtx* modelView)
{
	float camera[3];
	cameraWorldPosition(modelView, camera);
	// A camera in, through or above the slab must see the selected work area.
	if (camera[1] >= CEILING_UNDERSIDE - 0.06f) return false;
	// This also covers the unusual case of a focus target above the ceiling:
	// the sight line crosses the slab inside the room footprint.
	if (focus[1] > CEILING_UNDERSIDE && camera[1] < CEILING_UNDERSIDE) {
		float t = (CEILING_UNDERSIDE - camera[1]) / (focus[1] - camera[1]);
		float x = camera[0] + (focus[0] - camera[0]) * t;
		float z = camera[2] + (focus[2] - camera[2]) * t;
		if (x > -7.5f && x < 7.5f && z > -6.5f && z < 6.5f) return false;
	}
	return true;
}

#if TEST_CEILING_AUDIT
static void runCeilingAudit(void)
{
	float savedX=angleX, savedY=angleY, savedDist=camDist;
	float savedFocus[3]={focus[0],focus[1],focus[2]}, savedAmt=focusAmt;
	C3D_Mtx view;
	frameWorkbenchCamera(); buildModelView(&view);
	bool normal=shouldDrawCeiling(&view);
	// Highest legal manual orbit, retaining a real bench pivot.
	angleX=1.30f; camDist=5.40f;
	focus[0]=meshStandX(); focus[1]=MAT_TOP+.24f; focus[2]=meshStandZ(); focusAmt=.72f;
	buildModelView(&view); bool high=shouldDrawCeiling(&view);
	printf("CEILING AUDIT normal=%s high=%s\n",normal?"DRAW":"HIDE",high?"DRAW":"HIDE");
	angleX=savedX; angleY=savedY; camDist=savedDist;
	focus[0]=savedFocus[0]; focus[1]=savedFocus[1]; focus[2]=savedFocus[2]; focusAmt=savedAmt;
}
#else
static void runCeilingAudit(void) { }
#endif

// Opening beats: lid lifts and disappears, runner rises, empty tray moves left
// and disappears, then the runner lowers into the tray's former footprint.
static float boxStage(float start, float end)
{
	if (boxOpen <= start) return 0.0f;
	if (boxOpen >= end) return 1.0f;
	float range = end - start;
	return (range > 0.0001f) ? (boxOpen - start) / range : 0.0f;
}

static void buildRunnerView(C3D_Mtx* out, const C3D_Mtx* modelView)
{
	float rise = boxStage(0.16f, 0.42f);
	float lower = boxStage(0.72f, 0.94f);
	float boxed[3];
	meshKitBoxedOffset(boxed);
	*out = *modelView;
	// The authored runner is vertical; rotate it down while it is still in the
	// box, then keep it flat in the box's former place on the desk.
	Mtx_RotateX(out, 1.5707963f, true);
	// After the 90-degree X rotation, local Y is desk depth and local Z is
	// vertical. Map the authored vertical runner onto the rear desk footprint.
	Mtx_Translate(out, boxed[0], boxed[2], -boxed[1] - 0.15f - 0.72f * rise * (1.0f - lower), true);
}

// Converts an attached part's runner-local body centre into the same world
// coordinate system that loose parts, sockets and camera pivots use.  This is
// deliberately derived from buildRunnerView rather than copied coordinates:
// the front-right bench transform and the runner's 90-degree flattening then
// have a single authority.
static void runnerPartWorldCentre(const meshPart* p, float out[3])
{
	C3D_Mtx identity, runnerWorld;
	Mtx_Identity(&identity);
	buildRunnerView(&runnerWorld, &identity);
	C3D_FVec v = Mtx_MultiplyFVec4(&runnerWorld,
		FVec4_New(p->bodyCentre[0], p->bodyCentre[1], p->bodyCentre[2], 1.0f));
	float iw = v.w != 0.0f ? 1.0f / v.w : 1.0f;
	out[0] = v.x * iw; out[1] = v.y * iw; out[2] = v.z * iw;
}

static void __attribute__((unused)) partWorldCentre(int index, float out[3])
{
	const meshPart* p = &meshParts()[index];
	if (partStates[index].cut) {
		out[0] = p->bodyCentre[0] + partStates[index].offset[0];
		out[1] = p->bodyCentre[1] + partStates[index].offset[1];
		out[2] = p->bodyCentre[2] + partStates[index].offset[2];
	} else {
		runnerPartWorldCentre(p, out);
	}
}

static void runnerPartLooseOffset(const meshPart* p, float out[3])
{
	float centre[3];
	runnerPartWorldCentre(p, centre);
	out[0] = centre[0] - p->bodyCentre[0];
	out[1] = centre[1] - p->bodyCentre[1];
	out[2] = centre[2] - p->bodyCentre[2];
}

// Projects a model-space box, shifted by `offset`, into touch coordinates.
// Returns false if none of it is in front of the camera. `nearW` comes back as
// the closest corner, so overlapping boxes can be ordered front to back.
static bool projectBox(const C3D_Mtx* mvp, const float min[3], const float max[3],
	const float offset[3], float pad[3], float* outMinX, float* outMinY,
	float* outMaxX, float* outMaxY, float* nearW)
{
	float lo[3], hi[3];
	for (int a = 0; a < 3; a++)
	{
		lo[a] = min[a] + offset[a] - pad[a];
		hi[a] = max[a] + offset[a] + pad[a];
	}

	*outMinX = *outMinY =  1e9f;
	*outMaxX = *outMaxY = -1e9f;
	*nearW = 1e9f;
	bool visible = false;

	for (int c = 0; c < 8; c++)
	{
		C3D_FVec corner = FVec4_New(
			(c & 1) ? hi[0] : lo[0],
			(c & 2) ? hi[1] : lo[1],
			(c & 4) ? hi[2] : lo[2],
			1.0f);

		C3D_FVec clip = Mtx_MultiplyFVec4(mvp, corner);
		if (clip.w <= 0.01f) continue; // behind the camera

		float sx = (clip.x / clip.w * 0.5f + 0.5f) * BOTTOM_W;
		float sy = (0.5f - clip.y / clip.w * 0.5f) * BOTTOM_H;

		if (sx < *outMinX) *outMinX = sx;
		if (sx > *outMaxX) *outMaxX = sx;
		if (sy < *outMinY) *outMinY = sy;
		if (sy > *outMaxY) *outMaxY = sy;
		if (clip.w < *nearW) *nearW = clip.w;
		visible = true;
	}

	return visible;
}

// Which part's gate is under this touch point? Only parts still on the runner
// have one. Returns -1 if the stylus missed every gate.
static int pickGate(const C3D_Mtx* modelView, int tx, int ty)
{
	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);

	float pad[3] = { GATE_PAD_XZ, GATE_PAD_Y, GATE_PAD_XZ };
	const meshPart* parts = meshParts();
	int   best  = -1;
	float bestW = 1e9f;

	for (int i = 0; i < meshPartCount(); i++)
	{
		if (partStates[i].cut) continue;
		if (meshParts()[i].runner != currentRunner) continue;

		float minX, minY, maxX, maxY, nearW;
		if (!projectBox(&mvp, parts[i].gateMin, parts[i].gateMax, noOffset, pad,
				&minX, &minY, &maxX, &maxY, &nearW))
			continue;

		if (tx >= minX && tx <= maxX && ty >= minY && ty <= maxY && nearW < bestW)
		{
			bestW = nearW;
			best  = i;
		}
	}

	return best;
}

// The closed box is the only thing on the bench that can be tapped before the
// runner has arrived. Its fixed AABB is deliberately tested ahead of all kit
// picking so the concealed frame cannot be cut through the lid.
static bool pickKitBox(const C3D_Mtx* modelView, int tx, int ty)
{
	C3D_Mtx mvp;
	float min[3], max[3], pad[3] = { 0.08f, 0.08f, 0.08f };
	float minX, minY, maxX, maxY, nearW;
	meshKitBoxBounds(min, max);
	Mtx_Multiply(&mvp, &pickProjection, modelView);
	if (!projectBox(&mvp, min, max, noOffset, pad, &minX, &minY, &maxX, &maxY, &nearW))
		return false;
	return tx >= minX && tx <= maxX && ty >= minY && ty <= maxY;
}

// Which part is under this touch point? Takes the nearest box the point falls
// inside. Returns -1 for empty space.
static int pickPart(const C3D_Mtx* modelView, int tx, int ty)
{
	float pad[3] = { 0.0f, 0.0f, 0.0f };
	const meshPart* parts = meshParts();
	int   best  = -1;
	float bestW = 1e9f;

	for (int i = 0; i < meshPartCount(); i++)
	{
		// Uncut pieces travel with the rotated, rear-desk runner. Cut pieces use
		// their independent mat/stand offset. This mirrors sceneRender exactly.
		C3D_Mtx partView = *modelView;
		const float* offset = partStates[i].offset;
		if (partStates[i].cut)
			Mtx_Translate(&partView, offset[0], offset[1], offset[2], true);
		else
			buildRunnerView(&partView, modelView);

		C3D_Mtx partMvp;
		Mtx_Multiply(&partMvp, &pickProjection, &partView);
		float minX, minY, maxX, maxY, nearW;
		if (!projectBox(&partMvp, parts[i].min, parts[i].max, noOffset, pad,
				&minX, &minY, &maxX, &maxY, &nearW))
			continue;

		if (tx >= minX && tx <= maxX && ty >= minY && ty <= maxY && nearW < bestW)
		{
			bestW = nearW;
			best  = i;
		}
	}

	return best;
}

// Which socket on the build stand is waiting for this part, or -1 if the part
// has nowhere to go.
static int socketForPart(int part)
{
	const meshSocket* sockets = meshSockets();
	for (int i = 0; i < meshSocketCount(); i++)
		if (sockets[i].part == part) return i;
	return -1;
}

// ---------------------------------------------------------------------------
// The manual
//
// A real kit manual is a numbered list of steps, and the socket table already
// is one - socket 0 is step 1, and so on - so the manual is a page number into
// that table rather than a second copy of the build order.
//
// The page follows the build on its own: it lands on the first step that is not
// finished, which means the whole thing works with nothing but the stylus. The
// D-Pad overrides that to flick back through steps already done; the next bit
// of progress takes the manual back off manual.
// ---------------------------------------------------------------------------
static int  manualPage     = 0;   // step being shown, 0-based
static bool manualHeld     = false;   // player has paged by hand, stop following

static void saveCurrentLevel(void)
{
	if (loadedLevel < 1 || loadedLevel > LEVEL_SAVE_COUNT) return;
	levelBuildState* save = &levelBuilds[loadedLevel - 1];
	save->visited = true;
	memcpy(save->parts, partStates, sizeof(partStates));
	save->cutCount = cutCount; save->filedCount = filedCount; save->builtCount = builtCount;
	save->runnerLift = runnerLift; save->boxOpen = boxOpen; save->boxOpening = boxOpening;
	save->manualPage = manualPage;
	save->currentRunner = currentRunner;
}

static void loadLevelState(int level)
{
	if (level < 1 || level > LEVEL_SAVE_COUNT) return;
	levelBuildState* save = &levelBuilds[level - 1];
	loadedLevel = level;
	if (save->visited)
	{
		memcpy(partStates, save->parts, sizeof(partStates));
		cutCount = save->cutCount; filedCount = save->filedCount; builtCount = save->builtCount;
		runnerLift = save->runnerLift; boxOpen = save->boxOpen; boxOpening = save->boxOpening;
		manualPage = save->manualPage;
		currentRunner = save->currentRunner;
	}
	else
	{
		memset(partStates, 0, sizeof(partStates));
		cutCount = filedCount = builtCount = 0;
		runnerLift = boxOpen = 0.0f; boxOpening = false; manualPage = 0; currentRunner = 0;
		save->visited = true;
	}
	selectedPart = -1;
	manualHeld = false;
	filingAnimUntil = 0;
}
static int  manualProgress = -1;  // cut+filed+built last seen, to spot progress

// The first step whose part is not yet fitted, or the last step once the kit is
// finished - there is nowhere further to point.
static int manualNextStep(void)
{
	int steps = meshSocketCount();
	for (int i = 0; i < steps; i++)
		if (!partStates[meshSockets()[i].part].seated) return i;
	return steps > 0 ? steps - 1 : 0;
}

// The open page, clamped into the socket table, or -1 if there is no kit.
static int manualPageIndex(void)
{
	int steps = meshSocketCount();
	if (steps <= 0) return -1;
	return manualPage < steps ? manualPage : steps - 1;
}

// Called once a frame. Re-syncs the page to the build whenever anything has
// been snipped, filed or fitted since the last look, so a manual page never
// sits stale over work that has moved on.
static void manualUpdate(void)
{
	int steps = meshSocketCount();
	if (steps <= 0) return;

	int progress = cutCount + filedCount + builtCount;
	if (progress != manualProgress)
	{
		manualProgress = progress;
		manualHeld = false;
	}
	if (!manualHeld) manualPage = manualNextStep();
	if (manualPage >= steps) manualPage = steps - 1;
}

// Which socket is under this touch point? Same nearest-box rule as pickPart,
// with a fatter allowance, since a socket is an empty space rather than
// something you can see the edges of.
static int pickSocket(const C3D_Mtx* modelView, int tx, int ty)
{
	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);

	float pad[3] = { SOCKET_PAD, SOCKET_PAD, SOCKET_PAD };
	const meshSocket* sockets = meshSockets();
	int   best  = -1;
	float bestW = 1e9f;

	for (int i = 0; i < meshSocketCount(); i++)
	{
		float minX, minY, maxX, maxY, nearW;
		if (!projectBox(&mvp, sockets[i].min, sockets[i].max, noOffset, pad,
				&minX, &minY, &maxX, &maxY, &nearW))
			continue;

		if (tx >= minX && tx <= maxX && ty >= minY && ty <= maxY && nearW < bestW)
		{
			bestW = nearW;
			best  = i;
		}
	}

	return best;
}

// Whether the piece this socket hangs off is already on the stand.
//
// The first socket has no parent and is always ready. Every other one waits:
// until its parent is seated there is physically nothing there for the part to
// click into, so a target drawn at it would be pointing at empty air.
static bool socketParentReady(int sock)
{
	const meshSocket* sockets = meshSockets();
	if (sockets[sock].parent < 0) return true;
	return partStates[sockets[sockets[sock].parent].part].seated;
}

// The generous version, for the one socket the ghost is standing in.
//
// It tests a single box - the home of the part in hand - rather than all of
// them, so a fat allowance costs nothing: there is no other socket for it to
// swallow. It answers -1 unless that part is genuinely ready to go in, which is
// what keeps it from stealing a tap meant for the runner: an uncut part has no
// ghost on the stand, so its box is not in play at all.
static int pickGhostSocket(const C3D_Mtx* modelView, int tx, int ty)
{
	// The same part sceneRender draws the ghost for, so the box on screen and the
	// box a tap can hit are always the same box.
	int ghostPart = selectedPart;
	if (ghostPart < 0) return -1;

	const partState* st = &partStates[ghostPart];
	if (st->seated || !st->cut || !st->smooth) return -1;

	int sock = socketForPart(ghostPart);
	if (sock < 0) return -1;
	if (!socketParentReady(sock)) return -1;

	C3D_Mtx mvp;
	Mtx_Multiply(&mvp, &pickProjection, modelView);

	float pad[3] = { GHOST_PAD, GHOST_PAD, GHOST_PAD };
	const meshSocket* s = &meshSockets()[sock];
	float minX, minY, maxX, maxY, nearW;

	if (!projectBox(&mvp, s->min, s->max, noOffset, pad,
			&minX, &minY, &maxX, &maxY, &nearW))
		return -1;

	if (tx < minX || tx > maxX || ty < minY || ty > maxY) return -1;
	return sock;
}

// Writes the top screen's line about the selected part: where it goes, and what
// still has to happen to it first. Called when the selection changes and when
// filing finishes - never every frame, because a refusal message has to stay up
// long enough to be read.
static void describeSelection(void)
{
	if (selectedPart < 0)
	{
		snprintf(seatMsg, sizeof(seatMsg), "- - -");
		return;
	}

	const partState* st = &partStates[selectedPart];
	int sock = socketForPart(selectedPart);
	const char* where = (sock >= 0) ? meshSockets()[sock].name : "nowhere yet";

	if (!st->cut)         snprintf(seatMsg, sizeof(seatMsg), "still on the runner");
	else if (st->seated)  snprintf(seatMsg, sizeof(seatMsg), "fitted - %s", where);
	else if (!st->smooth) snprintf(seatMsg, sizeof(seatMsg), "%s - file the nub first", where);
	else                  snprintf(seatMsg, sizeof(seatMsg), "%s - tap the socket", where);
}

// Snips a part off the runner and books it a spot on the mat. Slots are handed
// out left to right, back row first, and wrap once the grid is full - by the
// time a kit has cut more than SLOT_COLS*SLOT_ROWS parts the early ones are
// fitted to the stand and their slots are standing empty again.
static void cutPart(int index, u64 now)
{
	const meshPart* p = &meshParts()[index];
	partState* st = &partStates[index];
	int slot = cutCount % (SLOT_COLS * SLOT_ROWS);

	st->cut     = true;
	st->moving  = true;
	st->startMs = now;
	// The runner is rotated and translated onto the front-right bench.  Starting
	// from zero used its packed local coordinates as world coordinates, which
	// visibly teleported a fresh cut to the room origin and pulled focus there.
	runnerPartLooseOffset(p, st->from);
	for (int a = 0; a < 3; a++) st->offset[a] = st->from[a];

	float x = meshLooseX() + slotX[slot % SLOT_COLS];
	float z = meshLooseZ() + slotZ[slot / SLOT_COLS];

	// Line the part's centre up with the slot, and stand it on the mat.
	st->target[0] = x - (p->min[0] + p->max[0]) * 0.5f;
	st->target[1] = MAT_TOP - p->min[1];
	st->target[2] = z - (p->min[2] + p->max[2]) * 0.5f;

	cutCount++;
}

// Eases the cut parts down onto the mat. Fast at first, then settling - a part
// falling free, not sliding on rails.
//
// Rebuilt from the target every frame rather than nudged, because filing also
// moves a part and the two have to agree on where it ends up.
static void updateCuts(u64 now)
{
	const meshPart* parts = meshParts();

	for (int i = 0; i < meshPartCount(); i++)
	{
		partState* st = &partStates[i];
		if (!st->cut) continue;

		float e = 1.0f;
		if (st->moving)
		{
			float t = (float)(now - st->startMs) / (float)CUT_ANIM_MS;
			if (t >= 1.0f)
			{
				t = 1.0f;
				st->moving = false;
			}

			float u = 1.0f - t;
			e = 1.0f - u*u*u;
		}

		for (int a = 0; a < 3; a++)
			st->offset[a] = st->from[a] + (st->target[a] - st->from[a]) * e;

		// A part whose nub hangs below it is standing on that nub. File the nub
		// away and the part has to come down with it, or it hovers. Once it is
		// off the mat and on the stand there is nothing to stand on, and the
		// settle is already baked into the position it set off from.
		if (!st->seated)
			st->offset[1] -= parts[i].nubDrop * st->filed;
	}

	if (boxOpening && boxOpen < 1.0f)
	{
		boxOpen += (1.0f - boxOpen) * BOX_OPEN_EASE;
		if (boxOpen > 0.999f) boxOpen = 1.0f;
	}
	// A box presents one moulded frame at a time. Once all gates on this frame
	// are cut, the next assigned runner becomes the only visible/pickable one.
	bool runnerClear = true;
	for (int i = 0; i < meshPartCount(); i++)
		if (meshParts()[i].runner == currentRunner && !partStates[i].cut) runnerClear = false;
	if (runnerClear && currentRunner + 1 < meshKitRunnerCount())
	{
		currentRunner++;
		selectedPart = -1;
		frameRunnerCamera();
	}

	// The frame has given up everything it was holding; lift it out of the way.
	if (cutCount >= meshPartCount() && runnerLift < 1.0f)
	{
		if (runnerLift == 0.0f) frameAssemblyCamera();
		runnerLift += (1.0f - runnerLift) * RUNNER_LIFT_EASE;
		if (runnerLift > 0.999f) runnerLift = 1.0f;   // an eased approach never lands on 1
	}
}

// A part can be filed once it is off the runner and while any nub is left.
static bool canFile(int index)
{
	return index >= 0 && partStates[index].cut && partStates[index].filed < 1.0f;
}

// Rubs the file across the selected part. Distance travelled is what counts,
// not direction, so back-and-forth strokes add up the way real sanding does.
static void fileStroke(int index, float travelPx)
{
	partState* st = &partStates[index];
	filingAnimUntil = osGetTime() + 90;

	st->filed += travelPx / FILE_TRAVEL_PX;
	if (st->filed >= 1.0f)
	{
		st->filed = 1.0f;
		if (!st->smooth)
		{
			st->smooth = true;
			filedCount++;
			describeSelection();   // it stops needing filing the instant it is smooth
		}
	}
}

// Clicks the held part into a socket. Returns false and says why if it will not
// go - a wrong socket, a part still on the frame, or one with its nub still on.
//
// The order of the refusals matters: with nothing in hand, tapping a socket
// falls into the wrong-part branch and answers "what goes here", which is what a
// tap on an empty socket ought to tell you.
static bool seatPart(int socketIndex, u64 now)
{
	const meshSocket* s = &meshSockets()[socketIndex];

	if (partStates[s->part].seated)
	{
		snprintf(seatMsg, sizeof(seatMsg), "%s is already fitted", s->name);
		return false;
	}
	if (selectedPart != s->part)
	{
		snprintf(seatMsg, sizeof(seatMsg), "%s takes %s", s->name, meshParts()[s->part].name);
		return false;
	}

	// Nothing gets fitted to thin air. A part hangs off the piece before it, and
	// until that piece is on the stand there is nothing here for this one to
	// click into - so the model builds up from the hips the way the manual reads,
	// instead of leaving an arm floating where its shoulder should be.
	if (s->parent >= 0)
	{
		const meshSocket* parent = &meshSockets()[s->parent];
		if (!partStates[parent->part].seated)
		{
			snprintf(seatMsg, sizeof(seatMsg), "%s needs %s first", s->name, parent->name);
			return false;
		}
	}

	if (!partStates[selectedPart].cut)
	{
		snprintf(seatMsg, sizeof(seatMsg), "%s - snip it off first", s->name);
		return false;
	}
	if (!partStates[selectedPart].smooth)
	{
		snprintf(seatMsg, sizeof(seatMsg), "%s - file the nub first", s->name);
		return false;
	}

	const meshPart* p = &meshParts()[selectedPart];
	partState* st = &partStates[selectedPart];

	// The travel starts from wherever the part is sitting now, nub settle and
	// all, so it glides across instead of jumping back to the mat first. The
	// socket lines up against the body centre, not the whole part - by now the
	// nub is filed away, so counting it would seat the part crooked.
	for (int a = 0; a < 3; a++)
	{
		st->from[a]   = st->offset[a];
		st->target[a] = s->pos[a] - p->bodyCentre[a];
	}

	st->seated  = true;
	st->moving  = true;
	st->startMs = now;
	builtCount++;
	snprintf(seatMsg, sizeof(seatMsg), "fitted - %s", s->name);
	return true;
}

// Capture-only deterministic state audit. It drives the same cut/file/seat
// helpers used by play, including runner advancement and parent gates.
#if TEST_AUDIT_ALL_KITS
static bool runGameplayAuditAllKits(void)
{
	bool all=true; int passed=0;
	for (int level=1; level<=20; level++) {
		sceneLoadKit(level); memset(partStates,0,sizeof(partStates));
		cutCount=filedCount=builtCount=0; currentRunner=0; runnerLift=0; boxOpen=1; boxOpening=false; selectedPart=-1;
		bool ok=true; u64 now=osGetTime();
		// A truly uncut part must never be able to add itself to the build.
		selectedPart=0; int before=builtCount;
		if(seatPart(0,now) || builtCount!=before) ok=false;
		for(int r=0;r<meshKitRunnerCount();r++) {
			if(currentRunner!=r) ok=false;
			for(int i=0;i<meshPartCount();i++) if(meshParts()[i].runner==r) cutPart(i,now);
			updateCuts(now+CUT_ANIM_MS+1);
		}
		if(cutCount!=meshPartCount() || runnerLift<=0) ok=false;
		// Assert unfiled and parent-gated seating both leave the count unchanged.
		selectedPart=0; before=builtCount; if(seatPart(0,now) || builtCount!=before) ok=false;
		if (meshSocketCount()>1) {
			selectedPart=meshSockets()[1].part;
			fileStroke(selectedPart,FILE_TRAVEL_PX);
			before=builtCount;
			if(seatPart(1,now) || builtCount!=before) ok=false;
		}
		for(int s=0;s<meshSocketCount();s++) {
			int i=meshSockets()[s].part; selectedPart=i;
			fileStroke(i,FILE_TRAVEL_PX);
			if(!partStates[i].smooth) ok=false;
			if(!seatPart(s,now)) ok=false;
			updateCuts(now+CUT_ANIM_MS+1);
		}
		if(builtCount!=meshPartCount()) ok=false;
		if(!ok) printf("GAMEPLAY AUDIT L%02d FAIL cut=%d file=%d fit=%d\n",level,cutCount,filedCount,builtCount);
		if(ok) passed++; else all=false;
	}
	printf("GAMEPLAY AUDIT %d/20 %s\n",passed,all?"OK":"FAIL");
	sceneLoadKit(1); memset(partStates,0,sizeof(partStates)); cutCount=filedCount=builtCount=currentRunner=0; runnerLift=boxOpen=0; selectedPart=-1;
	return all;
}
#endif

// Slides the camera's pivot onto whatever is selected, and back to the middle
// of the bench when nothing is. Worked out fresh every frame rather than once
// on selection, because a part that has just been snipped is still on its way
// down to the mat and the camera should ride down with it.
static void updateFocus(void)
{
	#if TEST_CAPTURE_VIEW > 0
	// A verification camera has an authored framing.  Do not let the ordinary
	// closed-box easing overwrite it while Azahar waits to take the still.
	return;
	#endif
	float want[3]  = {
		meshStandX() * 0.55f + meshRunnerX() * 0.45f,
		MAT_TOP + 0.24f,
		meshStandZ() * 0.55f + meshRunnerZ() * 0.45f
	};
	float wantAmt  = 0.20f;

	if (selectedPart >= 0)
	{
		// Selection is an interaction state, not a licence to orbit out of the
		// seated work area. Keep the pivot in the relevant bench zone; this also
		// avoids a runner-local body centre ever being mistaken for room-world.
		const partState* st = &partStates[selectedPart];
		if (!st->cut) {
			want[0]=meshRunnerX(); want[2]=meshRunnerZ(); wantAmt=0.55f;
		} else if (st->seated) {
			want[0]=meshStandX(); want[2]=meshStandZ(); wantAmt=0.72f;
		} else {
			want[0]=meshLooseX(); want[2]=meshLooseZ(); wantAmt=0.60f;
		}
		want[1] = MAT_TOP + 0.24f;
	}
	else if (boxOpen >= 0.72f)
	{
		// Preserve the room-entry overview while the kit is closed.  Once the
		// runner is out, pivot around the relocated work area instead of world
		// origin: it makes the mat and stand readable without losing all room
		// context.  Selected parts above deliberately remain the stronger focus.
		bool runnerStillFull = false;
		for (int i=0; i<meshPartCount(); i++)
			if (!partStates[i].cut && meshParts()[i].runner == currentRunner) runnerStillFull = true;
		// The sockets are model-local and can be tall or asymmetric.  The shared
		// physical stand is the stable assembly pivot, so it cannot push the
		// camera through a room wall after a kit-specific target layout changes.
		want[0] = runnerStillFull ? meshRunnerX() : meshStandX();
		want[1] = MAT_TOP + 0.24f;
		want[2] = runnerStillFull ? meshRunnerZ() : meshStandZ();
		wantAmt = runnerStillFull ? 0.55f : 0.72f;
	}

	for (int a = 0; a < 3; a++)
		focus[a] += (want[a] - focus[a]) * FOCUS_EASE;
	focusAmt += (wantAmt - focusAmt) * FOCUS_EASE;
}

#if TEST_CAMERA_IDLE_AUDIT
static bool idleCameraPoseStable(int frames)
{
	float beforeX = angleX, beforeY = angleY, beforeDist = camDist;
	for (int i = 0; i < frames; ++i) updateFocus();
	return angleX == beforeX && angleY == beforeY && camDist == beforeDist;
}

static void runCameraIdleAudit(void)
{
	bool closed, runner, assembly;
	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = runnerLift = 0.0f; boxOpening = false; selectedPart = -1;
	frameWorkbenchCamera(); closed = idleCameraPoseStable(240);
	boxOpen = 1.0f; frameRunnerCamera(); runner = idleCameraPoseStable(240);
	for (int i = 0; i < meshPartCount(); ++i) partStates[i].cut = true;
	frameAssemblyCamera(); assembly = idleCameraPoseStable(240);
	printf("CAM IDLE closed=%s runner=%s assembly=%s (240 frames)\n",
		closed ? "OK" : "FAIL", runner ? "OK" : "FAIL", assembly ? "OK" : "FAIL");
	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = runnerLift = 0.0f; boxOpening = false; selectedPart = -1;
	frameWorkbenchCamera();
}
#else
static void runCameraIdleAudit(void) { }
#endif

#if TEST_LEVEL1_WORKSPACE_AUDIT
static bool inBenchWorkspace(const float p[3])
{
	// Desk top: x=-.06..7.34, z=1.54..6.34.  A small apron allows the raised
	// stand/model while still rejecting the room origin or any wall position.
	return p[0] >= -0.20f && p[0] <= 7.50f &&
	       p[1] >= -2.20f && p[1] <= 3.20f &&
	       p[2] >=  1.35f && p[2] <= 6.50f;
}

static void runLevel1WorkspaceAudit(void)
{
	bool ok = true; u64 now = osGetTime(); float p[3];
	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = 1.0f; boxOpening = false; runnerLift = 0.0f; selectedPart = -1;
	frameRunnerCamera();
	for (int i = 0; i < meshPartCount(); ++i) {
		partWorldCentre(i, p); if (!inBenchWorkspace(p)) { printf("WORK L1 P%d runner out %.2f %.2f %.2f\n",i,p[0],p[1],p[2]); ok=false; }
		selectedPart=i;
		for(int f=0;f<32;f++) updateFocus();
		if (!inBenchWorkspace(focus)) { printf("WORK L1 P%d select focus out\n",i); ok=false; }
		cutPart(i, now);
		partWorldCentre(i, p); if (!inBenchWorkspace(p)) { printf("WORK L1 P%d cut-source out %.2f %.2f %.2f\n",i,p[0],p[1],p[2]); ok=false; }
		updateCuts(now + CUT_ANIM_MS + 1);
		partWorldCentre(i, p); if (!inBenchWorkspace(p)) { printf("WORK L1 P%d loose out %.2f %.2f %.2f\n",i,p[0],p[1],p[2]); ok=false; }
		for(int f=0;f<32;f++) updateFocus();
		if (!inBenchWorkspace(focus)) { printf("WORK L1 P%d loose focus out\n",i); ok=false; }
		fileStroke(i, FILE_TRAVEL_PX);
	}
	for (int s=0; s<meshSocketCount(); ++s) {
		const meshSocket* sock=&meshSockets()[s];
		if (!inBenchWorkspace(sock->pos)) { printf("WORK L1 socket %d out\n",s); ok=false; }
		selectedPart=sock->part;
		if (!seatPart(s, now)) { printf("WORK L1 seat %d failed\n",s); ok=false; }
		updateCuts(now + CUT_ANIM_MS + 1); partWorldCentre(sock->part,p);
		if (!inBenchWorkspace(p)) { printf("WORK L1 P%d fit out\n",sock->part); ok=false; }
	}
	printf("WORKSPACE L1 10/10 %s\n", ok ? "OK" : "FAIL");
	sceneLoadKit(1); memset(partStates,0,sizeof(partStates));
	cutCount=filedCount=builtCount=currentRunner=0; boxOpen=runnerLift=0; boxOpening=false; selectedPart=-1;
	frameWorkbenchCamera();
}
#else
static void runLevel1WorkspaceAudit(void) { }
#endif

static void sceneRender(const C3D_Mtx* modelView)
{
	// Uniforms that are the same for every draw
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &projection);
	// The shader wants the direction the light TRAVELS, in view space, and gives
	// a surface max(0, -dot(lightVec, normal)). Straight down the camera axis
	// would leave every upward-facing surface - the desk, the mat - unlit, so the
	// lamp is set up and to the left of the viewer, the way a desk lamp sits.
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightVec,     -0.20f, -0.55f, -0.81f, 0.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightHalfVec, -0.20f, -0.55f, -0.81f, 0.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightClr,     1.0f, 1.0f,  1.0f, 1.0f);

	// Scenery first: the desk, the mat and its grid, then the runner with its
	// leftover gate stubs. None of it ever moves, so it all goes out with the
	// plain scene transform - only the material changes between draws.
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, modelView);

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialRoom);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomFirstVertex(), meshRoomVertexCount());
	ceilingDrawnLast = shouldDrawCeiling(modelView);
	if (ceilingDrawnLast)
		C3D_DrawArrays(GPU_TRIANGLES, meshRoomCeilingFirstVertex(), meshRoomCeilingVertexCount());
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialDesk);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomFloorFirstVertex(), meshRoomFloorVertexCount());
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomBedWoodFirstVertex(), meshRoomBedWoodVertexCount());
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomShelfFirstVertex(), meshRoomShelfVertexCount());
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitBox);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomMattressFirstVertex(), meshRoomMattressVertexCount());
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomPillowFirstVertex(), meshRoomPillowVertexCount());
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialPartBlue);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomBlanketFirstVertex(), meshRoomBlanketVertexCount());
	// A dark wood door reads as a deliberate opening rather than a black gap.
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialDesk);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomDoorFirstVertex(), meshRoomDoorVertexCount());
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitArtwork);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomKnobFirstVertex(), meshRoomKnobVertexCount());
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitBox);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomTrimFirstVertex(), meshRoomTrimVertexCount());
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialWindow);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomSkyFirstVertex(), meshRoomSkyVertexCount());
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialMat);
	C3D_DrawArrays(GPU_TRIANGLES, meshRoomGroundFirstVertex(), meshRoomGroundVertexCount());

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialDesk);
	C3D_DrawArrays(GPU_TRIANGLES, meshDeskFirstVertex(), meshDeskVertexCount());

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialMat);
	C3D_DrawArrays(GPU_TRIANGLES, meshMatFirstVertex(), meshMatVertexCount());

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialGrid);
	C3D_DrawArrays(GPU_TRIANGLES, meshGridFirstVertex(), meshGridVertexCount());

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialStand);
	C3D_DrawArrays(GPU_TRIANGLES, meshStandFirstVertex(), meshStandVertexCount());

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitBox);
	C3D_DrawArrays(GPU_TRIANGLES, meshKitSpareFirstVertex(), meshKitSpareVertexCount());
	float boxClear = boxStage(0.48f, 0.70f);
	if (boxClear < 1.0f)
	{
		C3D_Mtx trayMv = *modelView;
		Mtx_Translate(&trayMv, -3.40f * boxClear, 0.0f, 0.0f, true);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &trayMv);
		C3D_DrawArrays(GPU_TRIANGLES, meshKitTrayFirstVertex(), meshKitTrayVertexCount());
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, modelView);
	}
	if (boxOpen < 1.0f)
	{
		C3D_Mtx lidMv = *modelView;
		Mtx_Translate(&lidMv, 0.0f, 1.55f * boxStage(0.0f, 0.16f), 0.0f, true);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &lidMv);
		C3D_DrawArrays(GPU_TRIANGLES, meshKitLidFirstVertex(), meshKitLidVertexCount());
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitArtwork);
		C3D_DrawArrays(GPU_TRIANGLES, meshKitArtFirstVertex(), meshKitArtVertexCount());
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitBox);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, modelView);
	}

	// The runner is the one bit of scenery that does move: once it has nothing
	// left on it, it lifts away and is eventually not drawn at all.
	if (runnerLift < 1.0f)
	{
		// The frame stays hidden by the closed shoebox lid. Once the lid has
		// cleared halfway, it rises out of the box and settles on the mat.
		float emerge = boxStage(0.16f, 0.42f);
		if (emerge > 1.0f) emerge = 1.0f;
		if (emerge > 0.0f)
		{
		C3D_Mtx rv;
		buildRunnerView(&rv, modelView);
		if (runnerLift > 0.0f)
		{
			Mtx_Translate(&rv, 0.0f, RUNNER_LIFT_Y * runnerLift, 0.0f, true);
		}
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &rv);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialRunner);
		C3D_DrawArrays(GPU_TRIANGLES, meshRunnerFirstVertex(), meshRunnerBaseVertexCount());
		// Only gates belonging to the active runner stay attached to its frame.
		for (int i=0; i<meshPartCount(); i++)
			if (meshParts()[i].runner == currentRunner && !partStates[i].cut)
				C3D_DrawArrays(GPU_TRIANGLES, meshRunnerStubFirstVertex(i), meshRunnerStubVertexCount(i));
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, modelView);
		}
	}

	// Then the parts, each with its own transform so a cut one can sit on the
	// mat while the rest are still on the frame.
	const meshPart* parts = meshParts();
	for (int i = 0; i < meshPartCount(); i++)
	{
		const partState* st = &partStates[i];
		if (!st->cut && parts[i].runner != currentRunner) continue;
		float emerge = boxStage(0.16f, 0.42f);
		if (!st->cut && emerge <= 0.0f) continue;

		C3D_Mtx mv = *modelView;
		if (st->cut)
			Mtx_Translate(&mv, st->offset[0], st->offset[1], st->offset[2], true);
		else
		{
			buildRunnerView(&mv, modelView);
		}
		if (i == selectedPart && osGetTime() < filingAnimUntil)
		{
			// A tiny, rapid scrub makes an active file stroke visible without
			// changing the part's true location or its filing progress.
			float scrub = ((osGetTime() / 18) & 1) ? 0.035f : -0.035f;
			Mtx_Translate(&mv, scrub, 0.018f, 0.0f, true);
		}

		// Runner packing scales only the body about its packed centre. Once cut,
		// the full authored mesh is used for loose filing and final assembly.
		C3D_Mtx bodyMv = mv;
		if (!st->cut) {
			Mtx_Translate(&bodyMv, parts[i].bodyCentre[0], parts[i].bodyCentre[1], parts[i].bodyCentre[2], true);
			Mtx_Scale(&bodyMv, parts[i].runnerScale[0], parts[i].runnerScale[1], parts[i].runnerScale[2]);
			Mtx_Translate(&bodyMv, -parts[i].bodyCentre[0], -parts[i].bodyCentre[1], -parts[i].bodyCentre[2], true);
		}
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &bodyMv);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material,
			i == selectedPart ? &materialSelected : partMaterialFor(parts[i].colour));

		// Body and nub are drawn apart so filing can sink the nub into the body
		// on its own. A nub that is all the way in is not drawn at all - the
		// faces would z-fight with the body they are buried in.
		int nubVerts  = parts[i].nubVertexCount;
		int bodyFirst = parts[i].firstVertex + nubVerts;
		C3D_DrawArrays(GPU_TRIANGLES, bodyFirst, parts[i].vertexCount - nubVerts);

		if (st->filed < 1.0f)
		{
			if (st->cut || st->filed > 0.0f)
			{
				C3D_Mtx nubMv = mv;
				if (st->cut) Mtx_Translate(&nubMv, parts[i].nubFullOffset[0], parts[i].nubFullOffset[1], parts[i].nubFullOffset[2], true);
				Mtx_Translate(&nubMv, 0.0f, parts[i].nubSink * st->filed, 0.0f, true);
				C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &nubMv);
			}
			C3D_DrawArrays(GPU_TRIANGLES, parts[i].firstVertex, nubVerts);
		}
	}

	// Last, the ghost: where the part in hand is going, drawn sitting in its
	// socket so the fit can be seen before committing to it. Body only - the nub
	// has to be filed off before anything can be seated, so drawing one would be
	// a lie.
	//
	// It is the held part and nothing else. It used to fall back to whatever the
	// manual's open page was about, which meant a level opened with an amber box
	// already standing on the stand before the player had touched anything - it
	// read as a stray piece rather than as a target. Which piece goes where is
	// the top screen's job now; down here the ghost only ever answers "the thing
	// you are holding goes there".
	//
	// It stays hidden until the piece it hangs off is on the stand. A target
	// floating where its parent is not yet built reads as somewhere you can put
	// the part, and it is not - so it waits its turn rather than offering a fit
	// that would only be refused.
	int ghostPart = selectedPart;
	if (ghostPart >= 0 && !partStates[ghostPart].seated)
	{
		int sock = socketForPart(ghostPart);
		if (sock >= 0 && socketParentReady(sock))
		{
			const meshSocket* s = &meshSockets()[sock];
			const meshPart*   p = &parts[ghostPart];

			C3D_Mtx gv = *modelView;
			Mtx_Translate(&gv,
				s->pos[0] - p->bodyCentre[0],
				s->pos[1] - p->bodyCentre[1],
				s->pos[2] - p->bodyCentre[2], true);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &gv);
			C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialGhost);

			int nubVerts = p->nubVertexCount;
			C3D_DrawArrays(GPU_TRIANGLES, p->firstVertex + nubVerts, p->vertexCount - nubVerts);
		}
	}
}

static void sceneExit(void)
{
	linearFree(vbo_data);
	shaderProgramFree(&program);
	DVLB_Free(vshader_dvlb);
}

// ---------------------------------------------------------------------------
// Memory readout
//
// svcGetSystemInfo - what osGetMemRegionUsed() is built on - is not filled in
// correctly by emulators; Azahar claims ~124 MB used out of a 96 MB region,
// which underflows into nonsense. So none of the numbers below come from it.
// They are read from libctru's own allocator bookkeeping, which lives in this
// process and is therefore exactly as true under Azahar as on hardware.
//
// Three pools, because they run out separately:
//   heap    - malloc / new. Everything ordinary.
//   linear  - the GPU-visible pool. VBOs and citro3d's command lists.
//   VRAM    - separate silicon. Framebuffers, and anything vramAlloc'd.
// ---------------------------------------------------------------------------

// VRAM has no size query, so it is measured once before the framebuffers claim
// any of it. libctru hands the whole bank to its pool on the first allocation,
// so one throwaway alloc-and-free leaves the pool reporting the true total.
static u32 vramTotalBytes = 0;

typedef struct
{
	u32 heapUsed,   heapTotal;
	u32 linearUsed, linearTotal;
	u32 vramUsed,   vramTotal;
	u32 appUsed,    appTotal;
} memUsage;

static u32 usedOf(u32 total, u32 freeNow)
{
	return (total > freeNow) ? total - freeNow : 0;
}

static unsigned int pctOf(u32 used, u32 total)
{
	return total ? (unsigned int)(((u64)used * 100 + total / 2) / total) : 0;
}

static void readMemUsage(memUsage* m)
{
	struct mallinfo mi = mallinfo();

	m->heapTotal   = envGetHeapSize();
	m->heapUsed    = (u32)mi.uordblks;

	m->linearTotal = envGetLinearHeapSize();
	m->linearUsed  = usedOf(m->linearTotal, linearSpaceFree());

	m->vramTotal   = vramTotalBytes;
	m->vramUsed    = usedOf(m->vramTotal, vramSpaceFree());

	// The heap and the linear heap are both carved out of the application's
	// slice of FCRAM, so together they are the whole of this app's footprint.
	// VRAM is separate silicon and is deliberately not in this total.
	//
	// The ceiling is fixed at the retail Original 3DS budget rather than at
	// whatever the running console hands out, so the bar always answers the
	// only question worth asking: does this still fit the machine we target?
	// Measured, same binary, Azahar 2126.0:
	//   New 3DS mode  126752 KB   (124 MB region)
	//   Old 3DS mode   98080 KB   ( 96 MB - a DEV unit memory mode)
	// A retail Original 3DS gives an app 64 MB, so even the emulator's Old 3DS
	// mode is a third too generous and can never make this bar go red. The
	// number below is the only honest one.
	m->appUsed     = m->heapUsed + m->linearUsed;
	m->appTotal    = OLD3DS_APP_BUDGET;
}

// The four separate memory bars - heap, linear, VRAM, app - used to be printed
// here, one row each. The manual took those rows; only the app figure survives,
// on the status line at the bottom of the live block, because it is the only
// one that answers a question worth asking while building: does this still fit
// an Original 3DS? The other three are still measured in readMemUsage, so they
// are one printf away if a memory hunt ever needs them back.

// The button list. The lines themselves live in controls.c, because the touch
// screen's Controls page prints the same eight and two hand-kept copies would
// drift. The key column is 18 wide: the longest key is "Stylus drag / Pad" at
// 17 characters, so every action still starts in the same column.
static void printControls(void)
{
	const controlLine* lines = controlList();
	for (int i = 0; i < controlCount(); i++)
		printf("%-18s %s\n", lines[i].key, lines[i].action);
}

// ---------------------------------------------------------------------------
// The top screen is the kit's instruction manual
//
// The console is 50 columns by 30 rows, split in two so that nothing scrolls
// and nothing is redrawn that has not changed:
//
//   rows 1 to LIVE_ROW-1  printed once, on entering a level and again on
//                         leaving the pause menu. Header, then the guide -
//                         level 1 gets a walkthrough for somebody who has
//                         never built a kit, every other level gets the short
//                         reminder and the button list.
//   rows LIVE_ROW to 30   reprinted every frame: which step the manual is
//                         open at, what that step still needs, and one line
//                         of status at the bottom.
//
// The old debug block - hardware, scene counts, stylus, camera and the four
// memory bars - used to fill these rows. The manual needs the room; what is
// left of the numbers is the single status line on row 30.
// ---------------------------------------------------------------------------
#define LIVE_ROW 18

// Exactly 50 characters, so it fills its row edge to edge and leaves the cursor
// at the start of the next one without a newline of its own.
#define RULE_LINE "--------------------------------------------------"

// Level 1's walkthrough, rows 3 to 17. Level 1 is the smallest kit in the game,
// so there is room to read this without it being in the way, and the three jobs
// are spelled out in the order the game insists on them - a part will not file
// until it is cut, and will not seat until it is filed.
static void printBeginnerGuide(void)
{
	printf("\n");
	printf("  Open the box, then build every part in order.\n");
	printf("\n");
	printf("   0 OPEN  tap the kit box. Runner comes out.\n");
	printf("\n");
	printf("   1 SNIP  tap the stub; it drops to the mat.\n");
	printf("   2 FILE  tap it, then rub to sand the nub.\n");
	printf("   3 FIT   tap the amber ghost to click home.\n");
	printf("  Drag to turn.  L/R zoom.  A resets the view.\n");
}

// Every level after the first: the same three jobs in one line, then the button
// list. Eight rows of buttons and four of prose leaves rows 15 to 17 blank,
// which is deliberate - it keeps the manual clear of the live block below.
static void printBuildGuide(void)
{
	printf("\n");
	printf("  Snip the stub, file the nub, tap the ghost.\n");
	printf("\n");
	printControls();
}

static void printStaticInfo(bool isNew3DS)
{
	// Level 1 owns the top display with its illustrated beginner sheet.
	if (titleLevel() == 1) return;
	// The hardware line has gone with the rest of the debug block. The flag
	// stays in the signature because every caller has it to hand and the New
	// 3DS extras will want somewhere to announce themselves later.
	(void)isNew3DS;

	printf("\x1b[2J\x1b[1;1H");
	printf("Model Kit  -  build manual       level %2d\n", titleLevel());
	printf(RULE_LINE);

	if (titleLevel() == 1) printBeginnerGuide();
	else                   printBuildGuide();
}

// Every row of the live block goes through here. The block is reprinted in
// place each frame, so each row is padded out to the console's width and cut at
// it: a line that got shorter has to wipe the tail the last frame left behind,
// and a line that got longer must not wrap into the row below and shunt the
// whole block down one.
static void liveRow(const char* fmt, ...)
{
	char buf[80];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	printf("%-49.49s\n", buf);
}

// What the open step still needs, in the imperative. One line, and it is the
// line somebody reads when they are stuck, so it names the next action rather
// than the state.
static const char* stepInstruction(int sock)
{
	const meshSocket* s  = &meshSockets()[sock];
	const partState*  st = &partStates[s->part];

	if (st->seated)               return "fitted - this step is done";
	if (!st->cut)                 return "snip the stub that holds it on";
	if (!st->smooth)              return "tap it, then rub to file the nub";
	if (!socketParentReady(sock)) return "fit the piece it hangs off first";
	return "tap the amber ghost on the stand";
}

// Rows LIVE_ROW to 30: the manual page, then the state of the bench under it.
static void printLiveBlock(int fps, const memUsage* m)
{
	if (titleLevel() == 1) return;
	printf("\x1b[%d;1H", LIVE_ROW);

	int page = manualPageIndex();
	if (boxOpen < 1.0f)
	{
		liveRow("Kit box: closed");
		liveRow("");
		liveRow("  Now    tap the kit box to open it");
		liveRow("");
		liveRow("");
		liveRow("");
	}
	else if (page < 0)
	{
		liveRow("No kit on the bench.");
		for (int i = 0; i < 5; i++) liveRow("");
	}
	else
	{
		const meshSocket* s  = &meshSockets()[page];
		const partState*  st = &partStates[s->part];

		liveRow("Step %2d of %2d  %s", page + 1, meshSocketCount(),
			manualHeld ? "D-Pad L/R  (paged)" : "D-Pad L/R to page");
		liveRow("");
		liveRow("  Part   %s", meshParts()[s->part].name);
		liveRow("  Goes   %s", s->name);
		liveRow("  Now    %s", stepInstruction(page));
		liveRow("  Done   [%c] snip   [%c] file   [%c] fit",
			st->cut ? 'x' : ' ', st->smooth ? 'x' : ' ', st->seated ? 'x' : ' ');
	}
	liveRow("");

	liveRow("In hand  %s",
		selectedPart >= 0 ? meshParts()[selectedPart].name : "- nothing -");

	// How far the part in hand has been worn down. A part still on the runner
	// has nothing to file yet, so the bar reads as a prompt rather than as an
	// empty gauge somebody is waiting to fill.
	int filePct = 0;
	const char* fileNote = "pick a cut part";
	if (selectedPart >= 0 && partStates[selectedPart].cut)
	{
		filePct  = (int)(partStates[selectedPart].filed * 100.0f + 0.5f);
		fileNote = partStates[selectedPart].smooth ? "smooth" : "rub to file";
	}
	char bar[11];
	for (int b = 0; b < 10; b++) bar[b] = (b < filePct / 10) ? '#' : '.';
	bar[10] = '\0';
	liveRow("Filing   [%s] %3d%%  %s", bar, filePct, fileNote);

	// Where the held part goes, or why the last fit was turned down.
	liveRow("Fits     %s", seatMsg);

	liveRow("Kit      snip %2d/%-2d  file %2d/%-2d  fit %2d/%-2d",
		cutCount,   meshPartCount(),
		filedCount, meshPartCount(),
		builtCount, meshPartCount());
	liveRow("");

	// Row 30, printed without a newline: a newline on the bottom row scrolls the
	// console and drags the whole manual up by one. u32 is a long on this ABI,
	// so both figures are cast down for printf.
	char status[80];
	snprintf(status, sizeof(status), "FPS %3d    App RAM %3u%% - %u of %u KB O3DS",
		fps, pctOf(m->appUsed, m->appTotal),
		(unsigned int)(m->appUsed / 1024), (unsigned int)(m->appTotal / 1024));
	printf("%-49.49s", status);
}

// ---------------------------------------------------------------------------
// Pause menu
//
// It lives on the top screen because that is where the text is: the bottom
// screen is a citro3d render target with no font behind it. So the menu is
// driven by the D-pad rather than the stylus. The bench carries on drawing
// underneath it, frozen - it just stops taking input.
// ---------------------------------------------------------------------------

typedef enum { PAGE_NONE, PAGE_MAIN, PAGE_CONTROLS, PAGE_OPTIONS } menuPage;

static const char* const menuItems[] = { "Resume", "Controls", "Options", "Quit" };
#define MENU_ITEM_COUNT 4

static menuPage menuOn     = PAGE_NONE;
static int      menuCursor = 0;
static bool     menuDirty  = false;

// The in-level pages use the same paper sheet as the first-build guide.  Keeping
// the controls as short labelled actions makes them useful without showing a
// controller diagram that a new player has to decipher.
static void drawInLevelMenu(void)
{
	C3D_RenderTargetClear(beginnerTarget, C3D_CLEAR_ALL, 0xFBFAF6FF, 0);
	C3D_FrameDrawOn(beginnerTarget);
	C2D_Prepare();
	C2D_SceneBegin(beginnerTarget);
	C2D_TextBufClear(beginnerText);

	const char* heading = menuOn == PAGE_MAIN ? "PAUSED" :
	                      menuOn == PAGE_CONTROLS ? "CONTROLS" : "OPTIONS";
	beginnerLabel(heading, 142.0f, 14.0f, 0.66f, PAPER_INK);
	beginnerLabel(menuOn == PAGE_MAIN ? "BUILD IS PAUSED" : "MODEL KIT", 145.0f, 42.0f, 0.38f, PAPER_BLUE);
	beginnerLabel(meshKitName(), 78.0f, 42.0f, 0.30f, PAPER_INK);
	C2D_DrawRectSolid(42.0f, 66.0f, 0.0f, 316.0f, 4.0f, PAPER_BLUE);

	if (menuOn == PAGE_MAIN)
	{
		for (int i = 0; i < MENU_ITEM_COUNT; i++)
		{
			float y = 82.0f + i * 31.0f;
			bool selected = i == menuCursor;
			C2D_DrawRectSolid(64.0f, y, 0.0f, 272.0f, 25.0f,
				selected ? C2D_Color32(0xE8, 0xF1, 0xF8, 0xFF) : PAPER_WHITE);
			C2D_DrawRectSolid(64.0f, y, 0.0f, 4.0f, 25.0f, selected ? PAPER_BLUE : PAPER_LINE);
			C2D_DrawRectSolid(64.0f, y, 0.0f, 272.0f, 2.0f, selected ? PAPER_BLUE : PAPER_LINE);
			beginnerLabel(menuItems[i], 82.0f, y + 5.0f, 0.47f, PAPER_INK);
			if (selected) beginnerLabel("SELECT", 258.0f, y + 7.0f, 0.27f, PAPER_BLUE);
		}
		beginnerLabel("D-PAD  MOVE     A  SELECT     B  RESUME", 62.0f, 221.0f, 0.31f, PAPER_BLUE);
	}
	else if (menuOn == PAGE_CONTROLS)
	{
		const char* action[] = { "TAP", "RUB", "DRAG", "L / R", "SELECT" };
		const char* meaning[] = { "choose, snip, or fit", "file a loose part smooth", "turn the workbench", "zoom the view", "pause the build" };
		for (int i = 0; i < 5; i++)
		{
			float y = 82.0f + i * 25.0f;
			C2D_DrawRectSolid(63.0f, y, 0.0f, 77.0f, 19.0f, C2D_Color32(0xE8, 0xF1, 0xF8, 0xFF));
			beginnerLabel(action[i], 76.0f, y + 3.0f, 0.34f, PAPER_BLUE);
			beginnerLabel(meaning[i], 153.0f, y + 3.0f, 0.34f, PAPER_INK);
		}
		beginnerLabel("B  BACK TO PAUSE MENU", 111.0f, 221.0f, 0.34f, PAPER_BLUE);
	}
	else
	{
		char ram[18];
		snprintf(ram, sizeof(ram), "RAM %u%%", memoryStatusAppPercent());
		// A dedicated low-poly paper panel makes Options read as a real page,
		// rather than a few values floating on the guide background.
		C2D_DrawRectSolid(54.0f, 76.0f, 0.0f, 292.0f, 125.0f, C2D_Color32(0xF4,0xF7,0xF1,0xFF));
		C2D_DrawRectSolid(54.0f, 76.0f, 0.0f, 292.0f, 3.0f, PAPER_BLUE);
		C2D_DrawRectSolid(54.0f, 198.0f, 0.0f, 292.0f, 3.0f, PAPER_BLUE);
		C2D_DrawRectSolid(245.0f, 29.0f, 0.0f, 88.0f, 22.0f, C2D_Color32(0xE8,0xF1,0xF8,0xFF));
		beginnerLabel(ram, 255.0f, 32.0f, 0.32f, PAPER_BLUE);
		beginnerLabel("AUDIO", 76.0f, 86.0f, 0.32f, PAPER_BLUE);
		beginnerLabel("MASTER VOLUME", 89.0f, 106.0f, 0.45f, PAPER_INK);
		for (int i = 0; i < VOLUME_CELLS; i++)
		{
			u32 colour = i < settingsVolume() / VOLUME_STEP ? PAPER_BLUE : PAPER_LINE;
			C2D_DrawRectSolid(89.0f + i * 17.0f, 139.0f, 0.0f, 12.0f, 16.0f, colour);
		}
		char volume[12]; snprintf(volume, sizeof(volume), "%d%%", settingsVolume());
		beginnerLabel(volume, 179.0f, 165.0f, 0.46f, PAPER_BLUE);
		beginnerLabel("LEFT / RIGHT  ADJUST", 104.0f, 184.0f, 0.34f, PAPER_INK);
		beginnerLabel("B  BACK TO PAUSE MENU", 111.0f, 221.0f, 0.34f, PAPER_BLUE);
	}

	C2D_Flush();
}

static void drawMenu(void)
{
	// During a level the top screen is a citro2d paper sheet, not a console.
	if (beginnerTarget) return;
	printf("\x1b[2J\x1b[1;1H");
	printf("Model Kit  -  paused\n");
	printf("--------------------------------------------------");

	if (menuOn == PAGE_MAIN)
	{
		printf("\n");
		for (int i = 0; i < MENU_ITEM_COUNT; i++)
			printf("    %s %s\n", (i == menuCursor) ? ">" : " ", menuItems[i]);
		printf("\n");
		printf("Up / Down  move    A  choose    B  resume\n");
	}
	else if (menuOn == PAGE_CONTROLS)
	{
		printf("Controls\n\n");
		printControls();
		printf("\nB  back\n");
	}
	else
	{
		printf("Options\n\n");

		// Same ten-cell bar the workbench uses for filing, so a bar reads the
		// same way wherever it turns up.
		printf("Master volume  [");
		for (int i = 0; i < VOLUME_CELLS; i++)
			putchar(i < settingsVolume() / VOLUME_STEP ? '#' : '.');
		printf("] %3d%%\n", settingsVolume());

		printf("\nLeft / Right  set the level\n");
		printf("\nThere is no sound in the game yet. This\n");
		printf("sets the level for when there is.\n");
		printf("\nB  back\n");
	}
}

// Runs the menu for one frame. Returns true if Quit was chosen.
static bool menuInput(u32 kDown, bool isNew3DS)
{
	if (menuOn == PAGE_OPTIONS)
	{
		// The one page with something on it to change. settingsVolumeStep does
		// the clamping, and it is the same level the front end's Options page
		// sets - there is only one of it.
		if (kDown & KEY_DLEFT)
		{
			settingsVolumeStep(-VOLUME_STEP);
			menuDirty = true;
		}
		if (kDown & KEY_DRIGHT)
		{
			settingsVolumeStep(VOLUME_STEP);
			menuDirty = true;
		}
		if (kDown & (KEY_A | KEY_B | KEY_SELECT))
		{
			menuOn    = PAGE_MAIN;
			menuDirty = true;
		}
	}
	else if (menuOn != PAGE_MAIN)
	{
		// The Controls page only reads; anything that means "done" goes back.
		if (kDown & (KEY_A | KEY_B | KEY_SELECT))
		{
			menuOn    = PAGE_MAIN;
			menuDirty = true;
		}
	}
	else
	{
		if (kDown & KEY_DUP)
		{
			menuCursor = (menuCursor + MENU_ITEM_COUNT - 1) % MENU_ITEM_COUNT;
			menuDirty  = true;
		}
		if (kDown & KEY_DDOWN)
		{
			menuCursor = (menuCursor + 1) % MENU_ITEM_COUNT;
			menuDirty  = true;
		}
		if (kDown & (KEY_B | KEY_SELECT))
		{
			menuOn = PAGE_NONE;
			printStaticInfo(isNew3DS);
		}
		else if (kDown & KEY_A)
		{
			switch (menuCursor)
			{
				case 0: menuOn = PAGE_NONE; printStaticInfo(isNew3DS); break;
				case 1: menuOn = PAGE_CONTROLS; menuDirty = true;      break;
				case 2: menuOn = PAGE_OPTIONS;  menuDirty = true;      break;
				case 3: return true;
			}
		}
	}

	if (menuDirty && menuOn != PAGE_NONE)
	{
		drawMenu();
		menuDirty = false;
	}

	return false;
}

int main(void)
{
	// Measure VRAM before the framebuffers take a bite out of it.
	void* vramProbe = vramAlloc(16);
	if (vramProbe)
	{
		vramFree(vramProbe);
		vramTotalBytes = vramSpaceFree();
	}

	// Initialize graphics. The console takes the top screen; the bottom screen
	// belongs to the 3D view so the stylus lands on the model itself.
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	consoleInit(GFX_TOP, NULL);
	// The top screen changes owner between the legacy console and Level 1's
	// rendered sheet. One stable framebuffer prevents either renderer from
	// presenting the other's stale back buffer.
	gfxSetDoubleBuffering(GFX_TOP, false);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	C3D_RenderTarget* target = C3D_RenderTargetCreate(BOTTOM_H, BOTTOM_W, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(target, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	sceneInit();
	frameWorkbenchCamera();

	// citro2d draws the front end - it is the only thing here with a font. 1024
	// objects rather than the default 4096, because that number sizes a linear
	// vertex buffer and the busiest page is the Controls list at roughly thirty
	// quads and three hundred glyphs.
	C2D_Init(1024);
	beginnerTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	C3D_RenderTargetSetOutput(beginnerTarget, GFX_TOP, GFX_LEFT, TOP_TRANSFER_FLAGS);
	beginnerText = C2D_TextBufNew(4096);
	titleInit();
	runCameraIdleAudit();
	runCeilingAudit();
	runLevel1WorkspaceAudit();
	#if TEST_AUDIT_ALL_KITS
	meshAuditAllKits();
	runGameplayAuditAllKits();
	#endif
	#if TEST_COLLISION_AUDIT
	meshCollisionAuditAllKits();
	#endif
	#if TEST_CAPTURE_LEVEL > 0
	titleCaptureStartLevel(TEST_CAPTURE_LEVEL);
	sceneLoadKit(TEST_CAPTURE_LEVEL);
	loadLevelState(TEST_CAPTURE_LEVEL);
	frameWorkbenchCamera();
	#if TEST_CAPTURE_OPEN
	boxOpen = 1.0f; boxOpening = false; runnerLift = 0.0f;
	#if TEST_CAPTURE_RUNNER > 0
	currentRunner = TEST_CAPTURE_RUNNER - 1;
	for (int i=0;i<meshPartCount();i++) if (meshParts()[i].runner < currentRunner) partStates[i].cut=true;
	#endif
	frameRunnerCamera();
	#endif
	#if TEST_CAPTURE_CUT_ONE
	{
		// Exercise the live cut transform, then settle it for a useful still.
		u64 captureNow = osGetTime();
		cutPart(0, captureNow);
		updateCuts(captureNow + CUT_ANIM_MS + 1);
		selectedPart = 0;
	}
	#endif
	#if TEST_CAPTURE_ASSEMBLED
	boxOpen = 1.0f; boxOpening = false; runnerLift = 1.0f;
	for (int i = 0; i < meshPartCount(); i++) {
		// A capture preview must not enter the game's completed state; Azahar can
		// otherwise skip the 3D pass after the completion update.
		partStates[i].cut = partStates[i].smooth = true;
		partStates[i].seated = false;
		partStates[i].filed = 1.0f;
		const meshSocket* s = &meshSockets()[i];
		const meshPart* p = &meshParts()[i];
		for (int a = 0; a < 3; a++) {
			float want = s->pos[a] - p->bodyCentre[a];
			partStates[i].offset[a] = want;
			partStates[i].from[a] = want;
			partStates[i].target[a] = want;
		}
		partStates[i].moving = false;
	}
	cutCount = filedCount = meshPartCount(); builtCount = 0;
	frameAssemblyCamera();
	// Match the normal assembly transition's stable physical stand pivot.
	focus[0]=meshStandX(); focus[1]=MAT_TOP + 0.24f; focus[2]=meshStandZ(); focusAmt=0.72f;
	#endif
	if (TEST_CAPTURE_MENU) menuOn = PAGE_OPTIONS;
	#if TEST_CAPTURE_VIEW == 1
		// Wide room overview: keeps bed, rear wall, and desk in one frame.
		angleX=.58f; angleY=3.4915927f; camDist=5.25f; focus[0]=0.0f; focus[1]=-1.1f; focus[2]=0.0f; focusAmt=0.0f;
	#elif TEST_CAPTURE_VIEW == 2
		// Rear-wall close-up: the recessed window, door, and wall junction.
		angleX=.48f; angleY=0.18f; camDist=5.8f; focus[0]=1.4f; focus[1]=-.3f; focus[2]=-5.1f; focusAmt=1.0f;
	#elif TEST_CAPTURE_VIEW == 3
		// Direct tabletop composition centred on the mathematically centred stand.
		angleX=.78f; angleY=3.49f; camDist=4.3f; focus[0]=meshStandX(); focus[1]=MAT_TOP; focus[2]=meshStandZ(); focusAmt=1.0f;
	#elif TEST_CAPTURE_VIEW == 4
		// Side-on room audit: the long bed against the shelf wall and rear door.
		angleX=.54f; angleY=3.90f; camDist=5.8f; focus[0]=-2.4f; focus[1]=-1.3f; focus[2]=1.6f; focusAmt=0.0f;
	#elif TEST_CAPTURE_VIEW == 5
		// Reverse room audit: rear door clearance behind the left-wall bed.
		angleX=.54f; angleY=.35f; camDist=5.5f; focus[0]=-2.4f; focus[1]=-1.3f; focus[2]=0.0f; focusAmt=0.0f;
	#elif TEST_CAPTURE_VIEW == 6
		// High manual orbit: ceiling suppresses so the bench remains visible.
		angleX=1.30f; angleY=3.4915927f; camDist=5.40f; focus[0]=meshStandX(); focus[1]=MAT_TOP+.24f; focus[2]=meshStandZ(); focusAmt=.72f;
	#elif TEST_CAPTURE_VIEW == 7
		// Ordinary inside-room bench view: ceiling remains in view.
		angleX=.42f; angleY=3.4915927f; camDist=4.70f; focus[0]=meshStandX()*.55f+meshRunnerX()*.45f; focus[1]=MAT_TOP+.24f; focus[2]=meshStandZ()*.55f+meshRunnerZ()*.45f; focusAmt=.20f;
	#elif TEST_CAPTURE_VIEW == 8
		// Low inside-room look: captures the returning ceiling edge above the
		// bench without changing the production work-camera framing.
		angleX=-.18f; angleY=3.4915927f; camDist=4.70f; focus[0]=meshStandX()*.55f+meshRunnerX()*.45f; focus[1]=MAT_TOP+.24f; focus[2]=meshStandZ()*.55f+meshRunnerZ()*.45f; focusAmt=.20f;
	#endif
	#endif

	bool isNew3DS = false;
	APT_CheckNew3DS(&isNew3DS);

	// The front end writes its own top screen from here on. The workbench does
	// not get the console until Play is tapped, so nothing about the kit - no
	// manual page, no part name - can appear behind a menu.
	#if !TEST_AUDIT_ALL_KITS && !TEST_CAMERA_IDLE_AUDIT && !TEST_LEVEL1_WORKSPACE_AUDIT && !TEST_CEILING_AUDIT && !TEST_COLLISION_AUDIT
	titlePrintTop(isNew3DS);
	#endif

	touchPosition lastTouch = { 0, 0 };
	u64  touchStartMs = 0;
	int  touchMoved   = 0;
	bool tapPending   = false;
	bool filingStroke = false;
	int  lastTapX = -1, lastTapY = -1;

	u64 fpsTick = osGetTime();
	u64 memTick = fpsTick;
	int fpsFrames = 0, fps = 0;

	memUsage mem;
	readMemUsage(&mem);

	// Kept across frames so the stylus can be tested against the view that was
	// actually on screen when it landed, before this frame moves anything.
	C3D_Mtx modelView;
	buildModelView(&modelView);

	// Main loop
	while (aptMainLoop())
	{
		hidScanInput();

		u32 kDown = hidKeysDown();
		u32 kHeld = hidKeysHeld();
		u32 kUp   = hidKeysUp();
		if (kDown & KEY_START)
			break; // break in order to close the game

		// The front end owns the game from boot until Play is tapped. Held as the
		// state at the top of the frame rather than re-read later, so the release
		// that fires Play cannot also land on the bench behind it.
	const bool inTitle = (TEST_AUDIT_ALL_KITS || TEST_COLLISION_AUDIT) ? false : titleActive();

		if (inTitle)
		{
			titleAction act = titleInput(kDown, kHeld, kUp);
			if (act == TITLE_QUIT)
				break; // Quit
			if (act == TITLE_PLAY)
			{
				sceneLoadKit(titleLevel());
				loadLevelState(titleLevel());
				frameWorkbenchCamera();
				printStaticInfo(isNew3DS);
			}
			else {
			#if !TEST_AUDIT_ALL_KITS && !TEST_CAMERA_IDLE_AUDIT && !TEST_LEVEL1_WORKSPACE_AUDIT
				titlePrintTop(isNew3DS);   // reprints only when the page moves
			#endif
			}
		}
		// SELECT pauses. The frame that opens the menu does not also feed that
		// press to the menu, so it cannot open and immediately act on itself.
		else if (menuOn == PAGE_NONE)
		{
			if (kDown & KEY_SELECT)
			{
				menuOn     = PAGE_MAIN;
				menuCursor = 0;
				menuDirty  = false;
				tapPending = false;   // drop any tap that was mid-flight
				drawMenu();
			}
		}
		else if (menuInput(kDown, isNew3DS))
		{
			// The pause menu's Quit leaves the level, not the game: the front end
			// takes the bottom screen back on the level select, showing the page
			// the level just left sits on. Only the Quit on the front end's own
			// title page closes the game. The kit is left exactly as it was, so
			// coming back into the level resumes the same build.
			saveCurrentLevel();
			menuOn = PAGE_NONE;
			titleReturnToLevels();
			#if !TEST_AUDIT_ALL_KITS && !TEST_CAMERA_IDLE_AUDIT && !TEST_LEVEL1_WORKSPACE_AUDIT
			titlePrintTop(isNew3DS);
			#endif
		}

		// Re-read, because the front end can have come back this very frame.
		// inTitle above is deliberately the state at the top of the frame, so the
		// release that fires Play cannot also land on the bench behind it; this
		// one is what the screen shows and what freezes the bench. Without it the
		// A press that chose Quit would fall straight through and reset the
		// bench camera on its way out.
		const bool showTitle = (TEST_AUDIT_ALL_KITS || TEST_COLLISION_AUDIT) ? false : titleActive();
		const bool paused = inTitle || showTitle || (menuOn != PAGE_NONE);

		if (!paused)
		{
			if (kDown & KEY_A)
			{
				frameWorkbenchCamera();
			}

			// Stylus: a small quick touch acts on the kit, anything longer turns
			// the bench. Nothing rotates until the touch passes the slop, so a
			// tap is never mistaken for a drag.
			if (kDown & KEY_TOUCH)
			{
				hidTouchRead(&lastTouch);
				touchStartMs = osGetTime();
				touchMoved   = 0;

				// Rubbing a selected part files it; rubbing anywhere else turns
				// the bench. Decided once, on contact, against the view that was
				// on screen at the time - so the camera can never swing out from
				// under a stroke that has already started.
				filingStroke = canFile(selectedPart) &&
					pickPart(&modelView, (int)lastTouch.px, (int)lastTouch.py) == selectedPart;
			}
			else if (kHeld & KEY_TOUCH)
			{
				touchPosition touch;
				hidTouchRead(&touch);
				int dx = (int)touch.px - (int)lastTouch.px;
				int dy = (int)touch.py - (int)lastTouch.py;
				touchMoved += abs(dx) + abs(dy);
				if (touchMoved > TAP_SLOP_PX)
				{
					if (filingStroke)
						fileStroke(selectedPart, (float)(abs(dx) + abs(dy)));
					else
					{
						angleY += dx * 0.012f;
						angleX += dy * 0.012f;
					}
				}
				lastTouch = touch;
			}
			else if (kUp & KEY_TOUCH)
			{
				// The release coordinates are unreliable on hardware, so the tap
				// is tested at the last position read while the stylus was down.
				if (touchMoved <= TAP_SLOP_PX && (osGetTime() - touchStartMs) <= TAP_MAX_MS)
					tapPending = true;
			}

			// Circle Pad does the same as dragging, but its neutral hardware noise is
			// ignored. Removing the deadzone from the response prevents a tiny lean
			// from becoming a continuous slow camera spin.
			circlePosition circle;
			hidCircleRead(&circle);
			if (circle.dx < -CIRCLE_ORBIT_DEADZONE || circle.dx > CIRCLE_ORBIT_DEADZONE) {
				int dx = circle.dx - (circle.dx > 0 ? CIRCLE_ORBIT_DEADZONE : -CIRCLE_ORBIT_DEADZONE);
				angleY += dx * 0.00025f;
			}
			if (circle.dy < -CIRCLE_ORBIT_DEADZONE || circle.dy > CIRCLE_ORBIT_DEADZONE) {
				int dy = circle.dy - (circle.dy > 0 ? CIRCLE_ORBIT_DEADZONE : -CIRCLE_ORBIT_DEADZONE);
				angleX -= dy * 0.00025f;
			}

			// Shoulder buttons zoom. The near limit closes in as the camera locks
			// onto a part, so a selected part can be brought right up to the
			// glass; dropping the selection walks the limit back out and takes
			// the camera with it, instead of snapping.
			float nearLimit = CAM_NEAR_LIMIT + (CAM_NEAR_FOCUS - CAM_NEAR_LIMIT) * focusAmt;
			if (kHeld & KEY_L) camDist += 0.04f;
			if (kHeld & KEY_R) camDist -= 0.04f;

			// Paging the manual by hand. Wraps both ways, and takes the manual
			// off auto-follow until the next thing gets snipped, filed or fitted.
			int manualSteps = meshSocketCount();
			if (manualSteps > 0 && (kDown & (KEY_DLEFT | KEY_DRIGHT)))
			{
				int step = (kDown & KEY_DLEFT) ? manualSteps - 1 : 1;
				manualPage = (manualPage + step) % manualSteps;
				manualHeld = true;
			}

			if (camDist < nearLimit)      camDist = nearLimit;
			if (camDist > CAM_FAR_LIMIT)  camDist = CAM_FAR_LIMIT;

			// Stop the view rolling over the top or under the bottom of the bench
			if (angleX >  PITCH_LIMIT) angleX =  PITCH_LIMIT;
			if (angleX < -PITCH_LIMIT) angleX = -PITCH_LIMIT;
		}
		if (!showTitle) saveCurrentLevel();

		u64 now = osGetTime();
		if (!paused)
		{
			updateCuts(now);
			updateFocus();
		}

		// One transform for both the hit test and the frame it is tested
		// against, so a tap always hits what is on screen.
		buildModelView(&modelView);

		if (tapPending)
		{
			lastTapX = (int)lastTouch.px;
			lastTapY = (int)lastTouch.py;

			// The ghost outranks everything. When a part is snipped, filed and
			// waiting, the game has drawn the player a target and anywhere on or
			// near it means "put it there" - even over a part already fitted next
			// to it, which the part-before-socket rule below would otherwise
			// re-select instead. It can only fire for the part in hand, so it
			// never takes a tap that was meant for anything else.
			if (boxOpen < 1.0f)
			{
				if (pickKitBox(&modelView, lastTapX, lastTapY)) { boxOpening = true; frameRunnerCamera(); }
				else { selectedPart = -1; describeSelection(); }
				tapPending = false;
			}

			int ghost = tapPending ? pickGhostSocket(&modelView, lastTapX, lastTapY) : -1;
			C3D_Mtx runnerView;
			buildRunnerView(&runnerView, &modelView);
			int gate  = (ghost >= 0) ? -1 : (tapPending ? pickGate(&runnerView, lastTapX, lastTapY) : -1);

			if (tapPending && ghost >= 0)
			{
				seatPart(ghost, now);
			}
			// Gates win ties: a tap that could be either is a cut, because that
			// is the only thing you can do to a part still on the frame.
			else if (tapPending && gate >= 0)
			{
				cutPart(gate, now);
				selectedPart = gate;
				describeSelection();
			}
			else if (tapPending)
			{
				// Parts before sockets: a fitted part still has its socket around
				// it, and tapping it should look at the part rather than answer
				// that the socket is full.
				int part = pickPart(&modelView, lastTapX, lastTapY);
				if (part >= 0)
				{
					selectedPart = part;
					describeSelection();
				}
				else
				{
					int sock = pickSocket(&modelView, lastTapX, lastTapY);
					if (sock >= 0)
					{
						seatPart(sock, now);
					}
					else
					{
						selectedPart = -1;
						describeSelection();
					}
				}
			}

			tapPending = false;
		}

		// After the tap, so the page and the ghost both answer for the work just
		// done rather than trailing it by a frame.
		manualUpdate();

		// Live readout, parked below the static block so nothing scrolls.
		// FPS needs a one-second window; allocator bookkeeping is sampled four
		// times a second so the App RAM line stays visibly live without walking
		// the heap every frame.
		fpsFrames++;
		if (now - fpsTick >= 1000)
		{
			fps = fpsFrames;
			fpsFrames = 0;
			fpsTick = now;
		}
		if (now - memTick >= MEM_REFRESH_MS)
		{
			readMemUsage(&mem);
			memTick = now;
		}

		// While the menu is up it owns the whole console, so the readout holds
		// off rather than overwriting it row by row.
		if (!paused)
			printLiveBlock(fps, &mem);

		// Render. Two renderers share this one target, and each one leaves the
		// GPU set up for itself, so whichever draws re-binds its own state first
		// - sceneBind() here, C2D_Prepare() inside titleDraw().
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
			if (showTitle)
			{
				titleDraw(target);
			}
			else
			{
				C3D_RenderTargetClear(target, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
				C3D_FrameDrawOn(target);
				sceneBind();
				sceneRender(&modelView);
				if (menuOn != PAGE_NONE) drawInLevelMenu();
				else if (!TEST_AUDIT_ALL_KITS && !TEST_COLLISION_AUDIT && !TEST_CAPTURE_HIDE_GUIDE && titleLevel() == 1) drawBeginnerSheet();
				else if (TEST_CAPTURE_HIDE_GUIDE)
				{
					// Keep the top target participating in this frame so a guide-free
					// capture still presents the bottom 3D target on every emulator.
					C3D_RenderTargetClear(beginnerTarget, C3D_CLEAR_ALL, 0xFBFAF6FF, 0);
					C3D_FrameDrawOn(beginnerTarget);
				}
			}
		C3D_FrameEnd(0);

		// The top screen has no citro3d render target - it belongs to the
		// console - so C3D_FrameEnd never swaps it. Left unswapped, NEITHER
		// screen presents and the whole app goes black. Swapping it by hand is
		// what makes a bottom-screen-only layout work at all.
		if (TEST_AUDIT_ALL_KITS || TEST_COLLISION_AUDIT || TEST_CEILING_AUDIT || TEST_CAPTURE_HIDE_GUIDE || !(!showTitle && (titleLevel() == 1 || menuOn != PAGE_NONE)))
			gfxScreenSwapBuffers(GFX_TOP, false);
	}

	titleExit();
	C2D_TextBufDelete(beginnerText);
	C2D_Fini();
	sceneExit();
	C3D_Fini();
	gfxExit();
	return 0;
}
