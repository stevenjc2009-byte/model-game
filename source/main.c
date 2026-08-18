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
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"
#include "settings.h"
#include "controls.h"
#include "title.h"
#include "updater.h"
#include "audio.h"
#include "memory_status.h"
#include "save.h"
#include "strings.h"
#include "tutorial_art.h"
#include "hint.h"
#include "texture_atlas.h"
#include "screenshot.h"
#include "camera.h"
#include "picking.h"
#include "debug.h"
#include "vshader_shbin.h"

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

// Camera limits (CAM_NEAR_LIMIT, CAM_FAR_LIMIT, CAM_START, PITCH_LIMIT) moved
// to camera.h - this file's own zoom/orbit input handling still uses them,
// just via that header now.
#define CIRCLE_ORBIT_DEADZONE 45

// The Circle Pad slides the view along the bench instead of orbiting it, so the
// far end of the table can be brought into shot without spinning the whole room
// round to get there. Turning the bench is still stylus drag, which is where it
// was always the more natural gesture.
//
// The travel limits are the bench with a little air either side: the loose pile
// sits at x -3.675 and the runner at x 4.89, so there is nothing to look at past
// these, and letting the pivot wander off the desk only loses the player. At a
// full lean this crosses the bench end to end in about three seconds.
#define CIRCLE_PAN_SPEED 0.00045f
#define PAN_MIN_X (-4.60f)
#define PAN_MAX_X   5.80f
#define PAN_MIN_Z   2.10f
#define PAN_MAX_Z   5.80f

// How close to a room surface counts as already being past it. A camera sitting
// exactly on a plane sees nothing but that plane, so it is treated as outside.
// Up here with the other camera limits because the lift-over-obstacles search
// needs it to know where the ceiling is, and that runs well before the room
// shell is described.
#define ROOM_SKIN      0.06f

// With a part selected the camera is looking at one small object instead of the
// whole bench, so it is allowed much closer - close enough to read a gate.
// CAM_NEAR_FOCUS itself now lives in camera.h; FOCUS_EASE is only used by
// updateFocus, here in main.c.
#define FOCUS_EASE     0.16f

// SCENE_LIFT moved to camera.c - it is only ever used inside the camera
// functions that moved there with it.

// A touch counts as a tap - not a drag - if it barely moves and is let go of
// quickly. Below the slop the sprue does not turn at all, so tapping a part
// never nudges the view.
#define TAP_SLOP_PX 10
#define TAP_MAX_MS  500

// Item 35: how many degrees a held L/R adds to a posed joint per frame. At
// 60fps this covers an 80-degree joint (the widest authored range) in under
// two seconds - fast enough not to feel like a chore, slow enough to stop on
// a chosen angle rather than overshoot it every time.
#define POSE_DEG_PER_FRAME 0.6f
#define POSE_DEG_TO_RAD (3.14159265f / 180.0f)

// GATE_PAD_XZ/GATE_PAD_Y moved into picking.c, the only file that uses them.

// Where a cut part ends up: on the bare wood to the left of the cutting mat,
// well clear of the mat, the build stand and the kit box. mesh.c reserves each
// part its own patch of that wood when the kit is built - see meshLooseSlot -
// so the layout is the same however the player works through the runner, and
// two parts can never be put down on top of one another.

#define CUT_ANIM_MS 350

// Filing: how far the stylus has to travel across a selected part, in touch
// pixels, to wear its nub flush. A part is only 20-40 px across at the default
// zoom, so this is about six back-and-forth rubs - enough to feel like work,
// short enough that nobody gives up halfway.
#define FILE_TRAVEL_PX 200.0f
// fileStroke divides by it. A zero here would not be a tuning mistake, it would
// be an infinity into partState.filed and a nub that can never be filed off, so
// it is worth a build failure rather than a bench that stops working.
_Static_assert(FILE_TRAVEL_PX > 0.0f, "FILE_TRAVEL_PX is a divisor in fileStroke");

// SOCKET_PAD and GHOST_PAD moved into picking.c alongside the pick functions
// that are their only users.

// Once the last part is off it, the empty frame lifts up out of shot rather than
// standing there in the way. Slow enough to read as being lifted off the bench.
#define RUNNER_LIFT_Y    3.2f
#define RUNNER_LIFT_EASE 0.06f
#define BOX_OPEN_EASE    0.06f
#define MEM_REFRESH_MS   250u

// The App RAM bar is measured against a retail Original 3DS and nothing else,
// whichever console is actually running the game. Both the used figure and the
// 64 MB ceiling come from memory_status.c so there is only one of each.

static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection, uLoc_modelView;
static int uLoc_lightVec, uLoc_lightHalfVec, uLoc_lightClr, uLoc_material;
// Per-axis correction the shader applies to normals, so a squashed part is still
// lit as the shape it looks like. Only the packed runner body is drawn with a
// non-uniform scale; see setNormalScale().
static int uLoc_nrmScale;

static C3D_Mtx projection;     // what the GPU draws with (tilted for the screen)
// pickProjection - same view, untilted, for hit-testing on the CPU - now lives
// in picking.c; picking.h supplies the extern declaration.

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

// The part the stylus is resting on, before the tap that would take it.
//
// A resistive screen has no hover to read - it knows contact or nothing - but
// this game does not act on contact, it acts on release, and only if the stylus
// stayed put and was quick about it. That gap between touching down and letting
// go is a hover in everything but name, and it was invisible: the only way to
// find out what a tap would hit was to take it. Cool, and brighter than any
// part colour, so it always reads as a part lighting up rather than one going
// dark - a dimmed amber was tried first and on the orange body it looked like
// the part had been switched off. The hue keeps it clear of the amber that
// means selected, since a hold on one part while another is selected shows
// both at once. Sliding past the tap slop, which turns the touch into a camera
// drag, both clears it and cancels the tap.
static C3D_Mtx materialHover =
{
	{
	{ { 0.0f, 0.28f, 0.24f, 0.10f } }, // Ambient
	{ { 0.0f, 0.82f, 0.68f, 0.22f } }, // Diffuse
	{ { 0.0f, 0.55f, 0.55f, 0.55f } }, // Specular
	{ { 1.0f, 0.00f, 0.00f, 0.00f } }, // Emission
	}
};

// Injection-moulded kits commonly split visible shells into a few purposeful
// colours. The cycle is deliberately restrained so a runner remains legible.
// Named for what it renders as: rows are stored {a, b, g, r}, so this one's
// diffuse is r .19 g .44 b .22 - a moulded green, not a blue.
//
// The four are spaced by brightness as well as hue, because hue alone is not a
// cue everyone has. The earlier green and red sat at almost the same lightness
// and simulating deuteranopia over them measured dE 8 in Lab - the range where
// two colours stop being two colours. Darkening the red and deepening the green
// pulls that pair to dE 30-odd without either ceasing to look like plastic, and
// it also means the runner still reads in greyscale. scratchpad/cvd.py measures
// every pair under normal, protan and deutan vision and fails under dE 20; run
// it after touching any of these numbers.
static C3D_Mtx materialPartGreen =
{
	{ { { 0.0f, 0.13f, 0.27f, 0.12f } }, { { 0.0f, 0.25f, 0.47f, 0.22f } },
	  { { 0.0f, 0.30f, 0.30f, 0.30f } }, { { 1.0f, 0.0f, 0.0f, 0.0f } } }
};
static C3D_Mtx materialPartRed =
{
	{ { { 0.0f, 0.04f, 0.05f, 0.20f } }, { { 0.0f, 0.07f, 0.09f, 0.36f } },
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
		case 1: return &materialPartGreen;
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

// partState (cut/seated/moving/from/target/offset/filed/smooth) now lives in
// picking.h - picking.c tests it directly, and a struct shared across
// translation units can only be defined in one place.

// One record per mesh part, sized off the same ceiling mesh.c builds against so
// the two can never drift apart and let a part index run off the end of this.
// Not static: picking.c reads it directly when working out what a tap hit.
partState partStates[MESH_MAX_PARTS];
static int cutCount;
static int filedCount;
static int builtCount;
// Not static: picking.c needs it to know which parts are on the bench right now.
int currentRunner;

// A build belongs to its level, not to the global scene.  Keeping every level's
// part state separately means leaving level 1 never leaks cut or filed pieces
// into level 2, while returning to level 1 restores exactly that kit.
// One slot per level the kit tables define - mesh.h owns that number, so this
// array cannot end up shorter than the set of levels that can be entered.
#define LEVEL_SAVE_COUNT MESH_KIT_LEVELS
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
// Not static: camera.c and picking.c both read it (the kit-box blocker and
// the runner-view lift/lower stages both key off how open the lid is).
float boxOpen = 0.0f;
static bool boxOpening = false;
static u64 filingAnimUntil = 0;

// What the top screen says about the held part - "tap the socket", or why the
// last attempt to seat it was refused.
//
// Sized from the authored name limit rather than a round number: the longest
// message here is "<socket> needs <part> first", so two names plus the thirteen
// characters of fixed text and the terminator. meshValidateKits refuses to start
// with a name longer than MESH_NAME_MAX, which is what makes this arithmetic
// hold - the 48 it used to be was headroom by inspection, with nothing checking
// the names it had to hold.
#define SEAT_MSG_MAX (2*MESH_NAME_MAX + 14)
static char seatMsg[SEAT_MSG_MAX] = "- - -";

static void* vbo_data;
// Item 36: the index buffer meshIndices() fills - draw calls read a slice of
// this through C3D_DrawElements rather than reading vbo_data directly through
// C3D_DrawArrays. Allocated and freed alongside vbo_data, same lifetime, same
// linear-memory requirement (the GPU DMAs both).
static void* idx_data;

// angleX/angleY/camDist/focus/focusAmt/camPanX/camPanZ (the orbit/zoom/pan
// state) moved to camera.c along with the camera functions; camera.h
// supplies the extern declarations this file's input handling still uses.
//
// Not static: picking.c reads it when deciding what a tap in the socket
// hand-off actually selected.
int   selectedPart = -1;

// The part a release would act on right now, or -1. Only ever set while the
// stylus is down and the touch still counts as a tap; see materialHover.
static int   hoverPart = -1;

// Item 35: whether the bench is in posing mode. Only reachable once the kit
// is fully built (see the D-Pad Down handling in gameUpdateFrame) - it never
// interferes with cutting, filing or fitting because none of those inputs are
// read differently while it is on, only the stylus tap and L/R are. Turning
// it off drops the current pose selection but keeps every posed part exactly
// where it was left; there is no separate "posed angle reset".
static bool poseMode = false;

static void runCameraIdleAudit(void);
static void runCeilingAudit(void);

// Level 1 replaces the text console with this white beginner sheet. It is a
// screen target in its own right; later levels continue using the console.
static C3D_RenderTarget* beginnerTarget;
// The bottom screen, where the bench is drawn. Both targets live out here so
// sceneExit can take them down alongside the rest of the scene's resources.
static C3D_RenderTarget* benchTarget;
static C2D_TextBuf beginnerText;

#define PAPER_WHITE C2D_Color32(0xFB, 0xFA, 0xF6, 0xFF)
#define PAPER_INK   C2D_Color32(0x25, 0x25, 0x25, 0xFF)
#define PAPER_LINE  C2D_Color32(0x95, 0x95, 0x90, 0xFF)
#define PAPER_BLUE  C2D_Color32(0x26, 0x5B, 0x8C, 0xFF)
#define PAPER_AMBER C2D_Color32(0xD7, 0x8D, 0x21, 0xFF)

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

	// Filing is never mentioned while a single part is still attached to the
	// frame. Snipping a part also selects it, and the sheet used to follow that
	// selection straight on to "rub the loose part" with nine parts still on the
	// runner - so a first-time player was told to do something they were nowhere
	// near ready for. This gate is deliberately blind to what is held: nothing
	// gets past it until the whole kit is off the frame.
	if (cutCount < meshPartCount()) return 1;

	// Past that gate the sheet follows what is in the player's hand. Picking up
	// an unfiled part takes them back to filing and explains it again, which is
	// what makes it safe to file half the kit, build that half, and come back to
	// the rest later.
	if (selectedPart >= 0 && selectedPart < meshPartCount())
		return partStates[selectedPart].smooth ? 3 : 2;

	// Holding nothing changes nothing. The sheet rests on the earliest work still
	// outstanding rather than guessing, so it cannot move on by itself while the
	// player is sitting still deciding what to do next.
	return (filedCount < meshPartCount()) ? 2 : 3;
}

// The console-screen guide panel: centred on the 400-wide top screen with a
// 42px margin either side (42 + 316 + 42 = 400), same panel border reused by
// drawInLevelMenu's pause-menu header divider below.
#define BEGINNER_PANEL_X      42.0f
#define BEGINNER_PANEL_WIDTH 316.0f
#define BEGINNER_PANEL_BORDER  4.0f

// Bring the top screen up for whatever is about to be drawn on it. Both the
// tutorial sheet and the plain card need exactly this preamble, and getting one
// of the five calls out of order is how the top screen ends up blank.
static void beginnerFrameBegin(void)
{
	C3D_RenderTargetClear(beginnerTarget, C3D_CLEAR_ALL, 0xFBFAF6FF, 0);
	C3D_FrameDrawOn(beginnerTarget);
	C2D_Prepare();
	C2D_SceneBegin(beginnerTarget);
	C2D_TextBufClear(beginnerText);
}

// The illustrated tutorial. The drawing itself lives in tutorial_art.c, which is
// a direct transcription of the approved mockup; all this does is answer the
// questions that module asks about where the build has got to.
//
// The old artwork drawn here - a hollow grey outline for the runner and a plain
// black square for the part - was replaced because it read as an empty box with
// a hole in it rather than a plastic frame with a piece on it, and nothing on
// the page showed what tapping would actually do.
static void drawBeginnerSheet(void)
{
	tutorialInfo info;
	info.step        = beginnerAction() + 1;   // tutorial_art numbers its pages from 1
	info.cutDone     = cutCount;
	info.filedDone   = filedCount;
	info.builtDone   = builtCount;
	info.partTotal   = meshPartCount();
	// Nothing in hand means no per-part filing progress to report, and the file
	// page only shows this figure while a part is actually being rubbed.
	info.filePct     = (selectedPart >= 0 && selectedPart < meshPartCount())
	                 ? partStates[selectedPart].filed : 0.0f;
	info.runnerNow   = currentRunner + 1;
	info.runnerTotal = meshKitRunnerCount();
	info.kitName     = meshKitName();

	beginnerFrameBegin();
	tutorialDrawSheet(beginnerText, &info);
	C2D_Flush();
}

// What the top screen shows when the tutorial has been switched off in Options.
//
// Deliberately not the old text manual: switching the tutorial off is meant to
// clear the screen, not swap one wall of instructions for another. So it is the
// game's own name on a plain sheet, with the styrene runner motif the title
// screen uses so it still looks like this game and not a crash.
static void drawTopIdleCard(void)
{
	beginnerFrameBegin();

	// A length of runner across the middle: two rails, three crossbars and four
	// parts still on it. Same shapes the front end is built from.
	const float railX = 74.0f, railW = 252.0f, top = 96.0f, height = 56.0f;
	C2D_DrawRectSolid(railX, top, 0.0f, railW, 6.0f, PAPER_LINE);
	C2D_DrawRectSolid(railX, top + height - 6.0f, 0.0f, railW, 6.0f, PAPER_LINE);
	C2D_DrawRectSolid(railX, top, 0.0f, 6.0f, height, PAPER_LINE);
	C2D_DrawRectSolid(railX + railW - 6.0f, top, 0.0f, 6.0f, height, PAPER_LINE);
	for (int i = 1; i < 4; i++)
		C2D_DrawRectSolid(railX + i * (railW / 4.0f), top, 0.0f, 5.0f, height, PAPER_LINE);
	for (int i = 0; i < 4; i++)
	{
		float cx = railX + 12.0f + i * (railW / 4.0f);
		C2D_DrawRectSolid(cx, top + 17.0f, 0.0f, 38.0f, 22.0f,
			i & 1 ? PAPER_BLUE : C2D_Color32(0xE8, 0xE6, 0xDF, 0xFF));
	}

	// Measured rather than positioned by hand: the name is a translated string,
	// and "MODEL KIT" and "MAQUETTE" are not the same width.
	{
		C2D_Text name;
		C2D_TextParse(&name, beginnerText, STR(STR_TITLE));
		C2D_TextOptimize(&name);
		float w = 0.0f, h = 0.0f;
		C2D_TextGetDimensions(&name, 0.86f, 0.86f, &w, &h);
		C2D_DrawText(&name, C2D_WithColor, (400.0f - w) * 0.5f, 36.0f, 0.5f, 0.86f, 0.86f, PAPER_INK);
	}
	C2D_Flush();
}

// Push the two geometry buffers out of the CPU's data cache and into the FCRAM
// the GPU actually reads.
//
// Both are filled by a plain memcpy, which on real hardware leaves the bytes in
// the ARM11's write-back cache. The GPU is a separate bus master: BufInfo_Add
// hands it a physical address and it DMAs straight from memory, so anything
// still dirty in cache is simply not there as far as it is concerned. The tail
// of a fresh copy is the part most likely to still be dirty, which shows up as
// missing or scrambled triangles rather than a clean failure.
//
// citro3d cannot do this for us - BufInfo_Add only records an address, so the
// library never knows how much of the buffer we wrote. Emulators have no cache
// to be stale, which is why this never appeared in Azahar.
static void flushGeometryToGpu(size_t vboSize, size_t idxSize)
{
	GSPGPU_FlushDataCache(vbo_data, vboSize);
	GSPGPU_FlushDataCache(idx_data, idxSize);
}

// The GPU state the bench needs: its shader, its vertex layout, its buffer and
// its texture combiner. Split out of sceneInit because the front end draws with
// citro2d, and C2D_Prepare() binds all four of those to citro2d's own. Rather
// than track which screen was up last frame, whichever renderer is about to draw
// re-binds its own state first. It is a handful of register writes a frame.
static void sceneBind(void)
{
	// A failed linearAlloc leaves vbo_data NULL, and BufInfo_Add would hand that
	// straight to the GPU as a base address. sceneRender already skips the bench
	// while the buffer is missing, so there is nothing to bind for - every caller
	// (sceneInit, sceneLoadKit, and the return-from-front-end path) goes through
	// here, which makes this the one place the check has to be.
	if (!vbo_data) return;

	C3D_BindProgram(&program);

	// Configure attributes for use with the vertex shader
	C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3); // v0=position
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3); // v1=normal
	AttrInfo_AddLoader(attrInfo, 2, GPU_FLOAT, 2); // v2=uv (item 34)

	// Configure buffers
	C3D_BufInfo* bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbo_data, sizeof(vertex), 3, 0x210);

	// Item 34: the shared atlas modulated against the lit vertex colour, not
	// a straight replace - a part with no real UV samples the atlas's WHITE
	// region (uv defaults to {0,0}), which is opaque white, so MODULATE
	// leaves that part's colour exactly as it was before this atlas existed.
	// If the atlas failed to build (textureAtlasInit's own failure path),
	// there is nothing bound to unit 0 and MODULATE would multiply every lit
	// colour by whatever garbage is sitting in that texture unit - so this
	// falls back to the exact original untextured REPLACE path instead,
	// which is what makes atlas failure a no-op rather than a black scene.
	C3D_TexEnv* env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_Tex* atlas = textureAtlasGet();
	if (atlas)
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		C3D_TexBind(0, atlas);
	}
	else
	{
		C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}

	// The depth test belongs in here for the same reason as the four above:
	// C2D_Prepare() sets GPU_GEQUAL and nothing puts it back, so once the front
	// end has drawn, the bench would be depth-testing with the wrong function.
	// C3D_Init's own default is GPU_GREATER, which is what this scene expects.
	C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
}

// Returns false if the bench could not be set up. The only failure that is not
// fatal on the spot is the VBO allocation, but it leaves the app with a scene it
// can never draw, so main treats it as a startup failure rather than running on
// into an empty bench the player cannot act on.
static bool sceneInit(void)
{
	// The kit tables are authored data checked for parenting, packing overlap and
	// collision. A failure means the sprue this builds would be wrong for every
	// player in the same way, so it stops here rather than uploading geometry the
	// validator has already said does not hold together. It prints its own reason.
	if (!meshValidateKits()) return false;

	// Load the vertex shader, create a shader program and bind it. The binary is
	// linked into the executable, so a failure here is a broken build rather than
	// a bad disc - but it fails the same way at runtime either way, and reading
	// DVLE[0] out of a NULL parse is an immediate fault.
	vshader_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
	if (!vshader_dvlb || vshader_dvlb->numDVLE < 1)
	{
		printf("SHADER PARSE FAILED\n");
		return false;
	}
	if (R_FAILED(shaderProgramInit(&program)))
	{
		printf("SHADER PROGRAM INIT FAILED\n");
		return false;
	}
	if (R_FAILED(shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0])))
	{
		printf("SHADER VSH BIND FAILED\n");
		return false;
	}
	C3D_BindProgram(&program);

	// Get the location of the uniforms. A renamed or optimised-out uniform comes
	// back as -1, which C3D_FVUnifMtx4x4 would then write at a location the shader
	// never reads - the bench draws, but unlit or in the wrong place, with nothing
	// said. Every one of the seven is required, so check them as a set.
	struct { const char* name; int* out; } uniforms[] = {
		{ "projection",   &uLoc_projection   },
		{ "modelView",    &uLoc_modelView    },
		{ "lightVec",     &uLoc_lightVec     },
		{ "lightHalfVec", &uLoc_lightHalfVec },
		{ "lightClr",     &uLoc_lightClr     },
		{ "material",     &uLoc_material     },
		{ "nrmScale",     &uLoc_nrmScale     },
	};
	bool uniformsOk = true;
	for (size_t i = 0; i < sizeof(uniforms)/sizeof(uniforms[0]); i++)
	{
		*uniforms[i].out = shaderInstanceGetUniformLocation(program.vertexShader, uniforms[i].name);
		if (*uniforms[i].out < 0)
		{
			printf("SHADER UNIFORM MISSING: %s\n", uniforms[i].name);
			uniformsOk = false;
		}
	}
	if (!uniformsOk) return false;

	// Two projections of the same view. The tilted one is what the bottom
	// screen is drawn with; the plain one is used to work out where a part
	// lands in touch coordinates, where x runs right and y runs down.
	Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(FIELD_OF_VIEW), C3D_AspectRatioBot, 0.01f, 1000.0f, false);
	Mtx_Persp(&pickProjection, C3D_AngleFromDegrees(FIELD_OF_VIEW), C3D_AspectRatioBot, 0.01f, 1000.0f, false);

	// Build the sprue and upload it
	meshBuildKit(1);
	refreshBlockers();
	size_t vboSize = meshVertexCount() * sizeof(vertex);
	size_t idxSize = meshIndexCount() * sizeof(unsigned short);
	vbo_data = linearAlloc(vboSize);
	idx_data = linearAlloc(idxSize);
	// Linear memory is a fixed pool and these are the two large allocations the
	// app makes, so there is nowhere to fall back to if either fails. The copy
	// has to be skipped or it writes through NULL; sceneRender draws nothing
	// while either buffer is missing, so both are torn back down together
	// rather than leaving one allocated with nothing to pair it against.
	if (!vbo_data || !idx_data)
	{
		printf("VBO ALLOC FAILED: %u+%u bytes\n", (unsigned)vboSize, (unsigned)idxSize);
		if (vbo_data) { linearFree(vbo_data); vbo_data = NULL; }
		if (idx_data) { linearFree(idx_data); idx_data = NULL; }
		return false;
	}
	memcpy(vbo_data, meshVertices(), vboSize);
	memcpy(idx_data, meshIndices(), idxSize);
	flushGeometryToGpu(vboSize, idxSize);

	sceneBind();
	return true;
}

// The selected level owns its mesh. Rebuild the single VBO only when a new
// level is entered, never during play, so runner/part picking uses the same
// geometry that is visibly on the bench.
static void sceneLoadKit(int level)
{
	meshBuildKit(level);
	// The kit box stack is one box per runner, so the blocker list changes shape
	// with the level and has to be taken again here.
	refreshBlockers();
	size_t vboSize = meshVertexCount() * sizeof(vertex);
	size_t idxSize = meshIndexCount() * sizeof(unsigned short);
	// Free before allocating: every kit is a similar size, so handing the old
	// blocks back first is the likeliest way for the new ones to be satisfied.
	if (vbo_data) { linearFree(vbo_data); vbo_data = NULL; }
	if (idx_data) { linearFree(idx_data); idx_data = NULL; }
	vbo_data = linearAlloc(vboSize);
	idx_data = linearAlloc(idxSize);
	if (!vbo_data || !idx_data)
	{
		// The mesh accessors already describe the new kit, so there is no older
		// buffer worth keeping - its draw ranges no longer match what they
		// report. sceneRender skips the whole bench while either is NULL rather
		// than draw from a buffer the ranges overrun.
		printf("VBO ALLOC FAILED L%d: %u+%u bytes\n", level, (unsigned)vboSize, (unsigned)idxSize);
		if (vbo_data) { linearFree(vbo_data); vbo_data = NULL; }
		if (idx_data) { linearFree(idx_data); idx_data = NULL; }
		return;
	}
	memcpy(vbo_data, meshVertices(), vboSize);
	memcpy(idx_data, meshIndices(), idxSize);
	flushGeometryToGpu(vboSize, idxSize);
	sceneBind();
}

// benchPivot, frameWorkbenchCamera, the photoPreset table and framePhotoCamera,
// frameRunnerCamera, and frameAssemblyCamera all moved to camera.c along with
// the rest of the camera code; camera.h declares the ones this file still
// calls (state resets, mode switches, the TEST_* audits below).

// cameraOffsetVecAt, the blocker list (refreshBlockers/blockerLive/
// pointInBlocker), cameraBlockFractionAt, cameraWantedStand, cameraViewPitch
// and buildModelView all moved to camera.c. main.c's own use of the blocker
// list was only ever through refreshBlockers, which camera.h still declares.

// The six surfaces of the room shell, in the order the hide test uses them.
#define SURF_CEILING 0
#define SURF_FLOOR   1
#define SURF_REAR    2
#define SURF_FRONT   3
#define SURF_LEFT    4
#define SURF_RIGHT   5

// cameraWorldPosition moved to camera.c; camera.h declares it for the audits
// below, which still need to know where the camera actually ended up.

// Has the camera crossed to the far side of one of the room's own surfaces?
static bool cameraOutsideRoomSurface(const float camera[3], int surface)
{
	switch (surface)
	{
	case SURF_CEILING: return camera[1] >= ROOM_CEILING_UNDER - ROOM_SKIN;
	case SURF_FLOOR:   return camera[1] <= ROOM_FLOOR_TOP     + ROOM_SKIN;
	case SURF_REAR:    return camera[2] <= ROOM_WALL_REAR     + ROOM_SKIN;
	case SURF_FRONT:   return camera[2] >= ROOM_WALL_FRONT    - ROOM_SKIN;
	case SURF_LEFT:    return camera[0] <= ROOM_WALL_LEFT     + ROOM_SKIN;
	default:           return camera[0] >= ROOM_WALL_RIGHT    - ROOM_SKIN;
	}
}

// Whether a given surface of the shell is drawn this frame.
//
// This is the same trick the ceiling already used, applied to the floor and all
// four walls. A surface the camera has gone behind is simply not drawn, so the
// camera can never be looking at the back of it - which is what "cannot see
// past" means for something you stand outside of. Furniture is handled the
// other way round, by stopping the camera instead.
static bool shouldDrawRoomSurface(int surface, const float camera[3])
{
	if (cameraOutsideRoomSurface(camera, surface)) return false;
	// The unusual case of a focus target above the ceiling: the sight line
	// crosses the slab inside the room footprint even though the camera is under it.
	if (surface == SURF_CEILING && focus[1] > ROOM_CEILING_UNDER && camera[1] < ROOM_CEILING_UNDER) {
		float t = (ROOM_CEILING_UNDER - camera[1]) / (focus[1] - camera[1]);
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
	C3D_Mtx view; float cam[3];
	frameWorkbenchCamera(); buildModelView(&view); cameraWorldPosition(&view,cam);
	bool normal=shouldDrawRoomSurface(SURF_CEILING,cam);
	// Highest legal manual orbit, retaining a real bench pivot.
	angleX=1.30f; camDist=5.40f;
	focus[0]=meshStandX(); focus[1]=MAT_TOP+.24f; focus[2]=meshStandZ(); focusAmt=.72f;
	buildModelView(&view); cameraWorldPosition(&view,cam);
	bool high=shouldDrawRoomSurface(SURF_CEILING,cam);
	printf("CEILING AUDIT normal=%s high=%s\n",normal?"DRAW":"HIDE",high?"DRAW":"HIDE");
	angleX=savedX; angleY=savedY; camDist=savedDist;
	focus[0]=savedFocus[0]; focus[1]=savedFocus[1]; focus[2]=savedFocus[2]; focusAmt=savedAmt;
}
#else
static void runCeilingAudit(void) { }
#endif

// Opening beats: lid lifts and disappears, runner rises, empty tray moves left
// and disappears, then the runner lowers into the tray's former footprint.
//
// Not static: camera.c's buildRunnerView and picking.c's pickPart both key
// off this same 0..1 curve, so there is exactly one definition of "how open
// is the box".
float boxStage(float start, float end)
{
	if (boxOpen <= start) return 0.0f;
	if (boxOpen >= end) return 1.0f;
	float range = end - start;
	return (range > 0.0001f) ? (boxOpen - start) / range : 0.0f;
}

// buildRunnerView moved to camera.c; camera.h declares it for the callers
// below that still need the runner's own world transform.

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

// projectBox, pickGate, pickKitBox and pickPart all moved to picking.c along
// with the rest of the ray-picking code; picking.h declares them for the
// input-handling code below that calls them.

// Which socket on the build stand is waiting for this part, or -1 if the part
// has nowhere to go.
//
// Not static: picking.c's pickGhostSocket calls this to find the one socket
// the ghost is standing in.
int socketForPart(int part)
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
static int  manualProgress = -1;  // cut+filed+built last seen, to spot progress

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
	// Item 35: posing is a per-visit mode, not a per-part fact like poseDeg -
	// walking back into the level select and opening a different, unbuilt
	// kit must not leave the stylus still wired for posing. Without this a
	// tap on the new kit's runner would fall into the posing branch of
	// handleTap and find nothing seated to select, reading as a dead tap
	// with no way to cut a single part loose.
	poseMode = false;
	manualHeld = false;
	filingAnimUntil = 0;
	// Progress is a running total, not a per-level one, so the level being loaded
	// can happen to arrive on the same total the last one left behind. Forcing a
	// mismatch makes manualUpdate re-sync the page on the very next frame instead
	// of leaving the new level's manual open at the old level's step.
	manualProgress = -1;
}

// Progress across sessions
//
// levelBuilds already holds every level's build, and the main loop keeps the
// open level folded into it every frame, so the whole of the player's progress
// is that one array. Saving it is therefore a straight write of the array, and
// the save module never has to learn what a part is.
//
// What it does have to be told is when that array stops meaning what it meant.
// The layout id below folds together everything that decides the shape of the
// bytes, so raising a part limit or adding a field to partState makes an older
// file fail its check and be discarded, rather than be read as if the fields had
// not moved.
static unsigned int progressLayoutId(void)
{
	return (unsigned int)sizeof(levelBuildState) * 1000003u
	     ^ (unsigned int)sizeof(partState)       *   65599u
	     ^ (unsigned int)LEVEL_SAVE_COUNT        *      131u
	     ^ (unsigned int)MESH_MAX_PARTS;
}

// Item 32 back-compat.
//
// Adding colourOverride to partState changes its size, which changes
// progressLayoutId(), which makes a save written before this item shipped
// fail the layout check above - save.c's own design note calls that
// "refused, not migrated", and that stays true of save.c itself. What
// follows is a second, older shape handed to the same generic blob reader,
// plus the one-time copy that upgrades it, so a pre-paint save still loads
// instead of being discarded. save.c and save.h are untouched by this.
//
// Mirrored byte-for-byte against partState/levelBuildState as they stood
// right before colourOverride was added. Nothing in the live game ever
// touches these types outside progressLoad()/migrateFromV1().
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
	float poseDeg;
} partStateV1;

typedef struct
{
	bool visited;
	partStateV1 parts[MESH_MAX_PARTS];
	int cutCount, filedCount, builtCount;
	float runnerLift, boxOpen;
	bool boxOpening;
	int manualPage;
	int currentRunner;
} levelBuildStateV1;

static levelBuildStateV1 legacyBuilds[LEVEL_SAVE_COUNT];

static unsigned int progressLayoutIdV1(void)
{
	return (unsigned int)sizeof(levelBuildStateV1) * 1000003u
	     ^ (unsigned int)sizeof(partStateV1)       *   65599u
	     ^ (unsigned int)LEVEL_SAVE_COUNT          *      131u
	     ^ (unsigned int)MESH_MAX_PARTS;
}

// Field-by-field, not a memcpy: the two structs are laid out the same today
// but nothing enforces that going forward, and a memcpy would silently start
// reading garbage the day they drift.
static void migrateFromV1(void)
{
	for (int l = 0; l < LEVEL_SAVE_COUNT; l++)
	{
		levelBuildState* dstLevel = &levelBuilds[l];
		const levelBuildStateV1* srcLevel = &legacyBuilds[l];
		dstLevel->visited       = srcLevel->visited;
		dstLevel->cutCount      = srcLevel->cutCount;
		dstLevel->filedCount    = srcLevel->filedCount;
		dstLevel->builtCount    = srcLevel->builtCount;
		dstLevel->runnerLift    = srcLevel->runnerLift;
		dstLevel->boxOpen       = srcLevel->boxOpen;
		dstLevel->boxOpening    = srcLevel->boxOpening;
		dstLevel->manualPage    = srcLevel->manualPage;
		dstLevel->currentRunner = srcLevel->currentRunner;
		for (int p = 0; p < MESH_MAX_PARTS; p++)
		{
			partState* dst = &dstLevel->parts[p];
			const partStateV1* src = &srcLevel->parts[p];
			dst->cut     = src->cut;
			dst->seated  = src->seated;
			dst->moving  = src->moving;
			dst->startMs = src->startMs;
			for (int a = 0; a < 3; a++)
			{
				dst->from[a]   = src->from[a];
				dst->target[a] = src->target[a];
				dst->offset[a] = src->offset[a];
			}
			dst->filed   = src->filed;
			dst->smooth  = src->smooth;
			dst->poseDeg = src->poseDeg;
			// A pre-item-32 save recorded no paint choice. Spelled out
			// rather than relied on: PART_COLOUR_NONE is 0, the same value
			// dst already holds coming out of BSS/memset, but that stops
			// being true the day PART_COLOUR_NONE stops being 0.
			dst->colourOverride = PART_COLOUR_NONE;
		}
	}
}

// A part saved mid-flight has to be put down before the state is used again.
// The animation is driven from startMs, a reading of the console clock, and that
// reading means nothing on the next boot - left alone, a part cut a moment before
// quitting would come back either frozen halfway to the mat or snapped back to
// the runner it is no longer attached to. Landing it on its target is what the
// animation was going to do anyway, just without the travel.
static void settleSavedMoves(void)
{
	for (int l = 0; l < LEVEL_SAVE_COUNT; l++)
	{
		levelBuildState* save = &levelBuilds[l];
		if (!save->visited) continue;
		for (int p = 0; p < MESH_MAX_PARTS; p++)
		{
			partState* st = &save->parts[p];
			if (!st->moving) continue;
			for (int a = 0; a < 3; a++) { st->offset[a] = st->target[a]; st->from[a] = st->target[a]; }
			st->moving  = false;
			st->startMs = 0;
		}
		// Same argument for the box lid: boxOpening is a lid caught part way, and
		// there is no reason to make the player watch it finish opening again.
		if (save->boxOpening) { save->boxOpen = 1.0f; save->boxOpening = false; }
	}
}

// Answers whether a save was actually read. At boot nothing looks at that - a
// console with no file just starts fresh - but the pause menu's Load row has to
// be able to say "there is nothing on the card" rather than silently leaving the
// build exactly as it was and looking like a dead button.
static bool progressLoad(void)
{
	if (saveReadBlob(levelBuilds, sizeof(levelBuilds), progressLayoutId()))
	{
		settleSavedMoves();
		int visited = 0;
		for (int l = 0; l < LEVEL_SAVE_COUNT; l++) if (levelBuilds[l].visited) visited++;
		printf("SAVE: loaded, %d level%s started\n", visited, visited == 1 ? "" : "s");
		return true;
	}

	// Might not be corrupt - might just predate item 32's colourOverride
	// field. Try the shape saves had before that, and migrate up if it
	// matches.
	if (saveReadBlob(legacyBuilds, sizeof(legacyBuilds), progressLayoutIdV1()))
	{
		migrateFromV1();
		settleSavedMoves();
		int visited = 0;
		for (int l = 0; l < LEVEL_SAVE_COUNT; l++) if (levelBuilds[l].visited) visited++;
		printf("SAVE: loaded pre-paint save, upgraded, %d level%s started\n", visited, visited == 1 ? "" : "s");
		return true;
	}

	printf("SAVE: %s - starting fresh\n", saveLastReason());
	return false;
}

// Returns false if the card would not take it, so the caller can say so on
// screen. A save that fails quietly is the one failure the player cannot
// recover from - they find out by losing the build.
static bool progressSave(void)
{
	return saveWriteBlob(levelBuilds, sizeof(levelBuilds), progressLayoutId());
}

// A kit is built when every one of its parts has been fitted.
//
// The count is taken from the kit tables rather than from MESH_MAX_PARTS, which
// is the ceiling the state array is sized to and not the size of any actual kit.
// visited is checked first so that an untouched slot - all zeroes, which is also
// what a kit of no parts would look like - cannot come out as finished.
static bool levelIsBuilt(int level)
{
	if (level < 1 || level > LEVEL_SAVE_COUNT) return false;
	const levelBuildState* save = &levelBuilds[level - 1];
	int parts = meshKitPartCount(level);
	return save->visited && parts > 0 && save->builtCount >= parts;
}

// Hands the front end the whole picture at once. Called wherever the save could
// have moved on - at boot, and on the way back out to the level select - rather
// than from the one place a part is seated, so there is no path that finishes a
// kit without the grid hearing about it.
static void publishCompletion(void)
{
	for (int level = 1; level <= LEVEL_SAVE_COUNT; level++)
		titleSetBuilt(level, levelIsBuilt(level));
}

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

// pickSocket and pickGhostSocket moved to picking.c with the rest of the
// picking code; picking.h declares them for the tap-resolution code below.

// socketParentReady - "is the piece this socket hangs off already on the stand"
// - is gone. It gated the ghost, the ghost's hit box, the hint dot and seatPart
// itself, which together made the kit buildable in exactly one order. Sockets
// still record a parent (meshSocket.parent); nothing reads it to refuse a fit
// any more.

// Writes the top screen's line about the selected part: where it goes, and what
// still has to happen to it first. Called when the selection changes and when
// filing finishes - never every frame, because a refusal message has to stay up
// long enough to be read.
static void describeSelection(void)
{
	if (selectedPart < 0)
	{
		snprintf(seatMsg, sizeof(seatMsg), "%s", STR(STR_C_SEAT_NONE));
		return;
	}

	const partState* st = &partStates[selectedPart];
	int sock = socketForPart(selectedPart);
	const char* where = (sock >= 0) ? meshSockets()[sock].name : "nowhere yet";

	if (!st->cut)         snprintf(seatMsg, sizeof(seatMsg), "%s", STR(STR_C_SEAT_ON_RUNNER));
	// The offer of a second tap is the only place taking a part off is taught, so
	// it says so here rather than reading "fitted" and stopping.
	else if (st->seated)  snprintf(seatMsg, sizeof(seatMsg), "%s", STR(STR_C_SEAT_FITTED_TAP_REMOVE));
	else if (!st->smooth) snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_FILE_FIRST), where);
	else                  snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_TAP_SOCKET), where);
}

// Snips a part off the runner and puts it down in the patch of bare wood mesh.c
// reserved for it. The home belongs to the part, not to the cut order, so a part
// always lands in the same place and no two parts ever share a spot.
static void cutPart(int index, u64 now)
{
	const meshPart* p = &meshParts()[index];
	partState* st = &partStates[index];
	float home[2];
	meshLooseSlot(index, home);

	st->cut     = true;
	st->moving  = true;
	st->startMs = now;
	// The runner is rotated and translated onto the front-right bench.  Starting
	// from zero used its packed local coordinates as world coordinates, which
	// visibly teleported a fresh cut to the room origin and pulled focus there.
	runnerPartLooseOffset(p, st->from);
	for (int a = 0; a < 3; a++) st->offset[a] = st->from[a];

	// Line the part's centre up with its home, and stand it on the wood.
	st->target[0] = home[0] - (p->min[0] + p->max[0]) * 0.5f;
	st->target[1] = meshLooseTopY() - p->min[1];
	st->target[2] = home[1] - (p->min[2] + p->max[2]) * 0.5f;

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
	if (index < 0 || index >= meshPartCount()) return false;
	return partStates[index].cut && partStates[index].filed < 1.0f;
}

// Rubs the file across the selected part. Distance travelled is what counts,
// not direction, so back-and-forth strokes add up the way real sanding does.
static void fileStroke(int index, float travelPx)
{
	// Every caller today picks the index out of a hit test that already validated
	// it, so this is a tripwire for the next one that does not: partStates is a
	// fixed MESH_MAX_PARTS array and a stray index writes straight through it.
	if (index < 0 || index >= meshPartCount()) return;

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
	// Same tripwire as fileStroke: the socket index indexes the socket table and
	// s->part then indexes partStates, so both have to be in range before either
	// is dereferenced.
	if (socketIndex < 0 || socketIndex >= meshSocketCount()) return false;

	const meshSocket* s = &meshSockets()[socketIndex];
	if (s->part < 0 || s->part >= meshPartCount()) return false;

	if (partStates[s->part].seated)
	{
		snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_ALREADY_FITTED), s->name);
		return false;
	}
	if (selectedPart != s->part)
	{
		snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_TAKES), s->name, meshParts()[s->part].name);
		return false;
	}

	// A part used to be refused until the piece it hangs off was on the stand, so
	// the model had to build up from the hips the way the manual reads. That is
	// how a real kit is built, not how one is played with: a player who wants the
	// arms on first was told no by a game that already knew exactly where the arm
	// went. Any filed piece goes on whenever it is offered now.

	if (!partStates[selectedPart].cut)
	{
		snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_SNIP_FIRST), s->name);
		return false;
	}
	if (!partStates[selectedPart].smooth)
	{
		snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_FILE_FIRST), s->name);
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

	// Finishing level 1 used to put the tutorial away for good, here, at the
	// moment the first kit was finished. That is what made the breathing dot a
	// level-1-only feature: the dot is drawn only while the sheet is up, so the
	// last part of the first kit switched off the marker for the other nineteen
	// and there was no sign anywhere that it had happened. The sheet stays up on
	// every level now, and the only thing that takes it down is the player
	// turning it off themselves on the Options page.
	snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_FITTED), s->name);
	return true;
}

// Takes a fitted part back off the model and slides it home to its slot on the
// mat, where it sits as a cut, filed, loose piece again - exactly what it was
// the moment before it was seated.
//
// Until this existed a mis-aimed fit was permanent for the session: the only way
// out of a part clicked into the wrong place was to leave the level and lose the
// build. Nothing is destroyed by taking a part off, so there is no reason for it
// to be a one-way door.
//
// It refuses while another fitted part is hanging off this one. Sockets are a
// parent chain - an arm is fitted to a shoulder - so pulling a piece out from
// under its children would leave them floating in mid-air with nothing under
// them. Taking the children off first is the same order a real kit comes apart
// in, and the refusal says which piece is in the way.
static bool unseatPart(int index, u64 now)
{
	if (index < 0 || index >= meshPartCount()) return false;

	partState* st = &partStates[index];
	if (!st->seated) return false;

	const meshSocket* sockets = meshSockets();
	for (int i = 0; i < meshSocketCount(); i++)
	{
		if (sockets[i].parent < 0) continue;
		if (sockets[sockets[i].parent].part != index) continue;
		if (partStates[sockets[i].part].seated)
		{
			snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_HOLDS),
				meshParts()[index].name, sockets[i].name);
			return false;
		}
	}

	const meshPart* p = &meshParts()[index];
	float home[2];
	meshLooseSlot(index, home);

	// The same landing spot a fresh cut is sent to, worked out the same way, so a
	// part that has been on and off the model ends up exactly where it would have
	// been had it never left the mat.
	for (int a = 0; a < 3; a++) st->from[a] = st->offset[a];
	st->target[0] = home[0] - (p->min[0] + p->max[0]) * 0.5f;
	st->target[1] = meshLooseTopY() - p->min[1];
	st->target[2] = home[1] - (p->min[2] + p->max[2]) * 0.5f;

	st->seated  = false;
	st->moving  = true;
	st->startMs = now;
	if (builtCount > 0) builtCount--;
	snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_SEAT_TAKEN_OFF), p->name);
	return true;
}

// Capture-only deterministic state audit. It drives the same cut/file/seat
// helpers used by play, including runner advancement, the nub gate, and
// out-of-order seating.
#if TEST_AUDIT_ALL_KITS
static bool runGameplayAuditAllKits(void)
{
	bool all=true; int passed=0;
	for (int level=1; level<=MESH_KIT_LEVELS; level++) {
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
		// An unfiled part still has its nub and still cannot go on.
		selectedPart=0; before=builtCount; if(seatPart(0,now) || builtCount!=before) ok=false;
		// A filed one can, in whatever order the player likes. This used to assert
		// the opposite - socket 1 refused until socket 0 was on the stand - and that
		// parent gate is gone, so what is checked here is that the second socket
		// really does take its piece with the first still empty. Taken straight back
		// off again so the ordered pass below starts from a bare stand.
		if (meshSocketCount()>1) {
			selectedPart=meshSockets()[1].part;
			fileStroke(selectedPart,FILE_TRAVEL_PX);
			before=builtCount;
			if(!seatPart(1,now) || builtCount!=before+1) ok=false;
			updateCuts(now+CUT_ANIM_MS+1);
			if(!unseatPart(meshSockets()[1].part,now) || builtCount!=before) ok=false;
			updateCuts(now+CUT_ANIM_MS+1);
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

// True while the frame currently being worked still has a part attached to it.
static bool runnerStillHolding(void)
{
	for (int i = 0; i < meshPartCount(); i++)
		if (!partStates[i].cut && meshParts()[i].runner == currentRunner) return true;
	return false;
}

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
	// Same pivot the opening framing uses, from the same place, so the ease
	// holds the view it was given instead of hauling it somewhere else.
	float want[3];
	float wantAmt;
	benchPivot(want, &wantAmt);

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
		} else if (runnerStillHolding()) {
			// Snipping a part off is not a reason to leave the frame. While the
			// frame still holds anything the work is here, so the camera stays
			// on it and the freed part just drops away at the edge of the shot.
			// It is only worth crossing the bench once the frame is empty.
			want[0]=meshRunnerX(); want[2]=meshRunnerZ(); wantAmt=0.55f;
		} else if (st->smooth) {
			// Filed and waiting to go on. The work has crossed the bench: what
			// the player has to reach for now is the amber ghost standing in its
			// hole, not the piece in their hand. Pivoting on the loose pile put
			// the stand 296 px away on a 320 px screen - the top screen said "put
			// it where it glows" while the thing that glows was off the edge - so
			// the view follows the step over to the stand instead.
			// Weighted to the stand rather than parked on it: pivoting on the
			// stand alone pushed the loose pile off the other edge, and then a
			// player who wanted a different piece instead had nothing to tap.
			// This holds both, with the ghost the more comfortably framed of
			// the two because it is the one the step is asking for.
			want[0]=meshStandX()*0.80f + meshLooseX()*0.20f;
			want[2]=meshStandZ()*0.80f + meshLooseZ()*0.20f;
			wantAmt=0.72f;
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

	// The Circle Pad slide rides on top of whatever the game has picked to look
	// at, so letting go of the pad does not fight the automatic framing - the
	// view keeps the offset and carries on following the work.
	want[0] += camPanX;
	want[2] += camPanZ;

	// Clamped against the desk, and the correction is taken back out of the pan
	// itself. Without that, holding the pad against the end of the bench winds
	// up an offset that then has to be unwound before the view moves back at
	// all, which reads as the control having died.
	if (want[0] < PAN_MIN_X) { camPanX += PAN_MIN_X - want[0]; want[0] = PAN_MIN_X; }
	if (want[0] > PAN_MAX_X) { camPanX -= want[0] - PAN_MAX_X; want[0] = PAN_MAX_X; }
	if (want[2] < PAN_MIN_Z) { camPanZ += PAN_MIN_Z - want[2]; want[2] = PAN_MIN_Z; }
	if (want[2] > PAN_MAX_Z) { camPanZ -= want[2] - PAN_MAX_Z; want[2] = PAN_MAX_Z; }

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

#if TEST_CAMERA_PAN_AUDIT
// Only remaining main.c use of a zero offset into projectBox - picking.c
// keeps its own private copy for its own call sites.
static const float noOffset[3] = { 0.0f, 0.0f, 0.0f };

// Where a marker sitting still on the mat lands across the screen, measured
// through the same matrix and projection the drawn frame and the picking use.
// Reading the pan offset back instead would only restate the arithmetic that
// produced it, and would agree with a sideways slide that ran the wrong way.
static float panMarkerScreenX(bool* visible)
{
	float mn[3] = { 0.165f, MAT_TOP,        3.94f };
	float mx[3] = { 0.265f, MAT_TOP + 0.1f, 4.04f };
	float pad[3] = { 0.0f, 0.0f, 0.0f };
	float minX, minY, maxX, maxY, nearW;
	C3D_Mtx view, mvp;
	buildModelView(&view);
	Mtx_Multiply(&mvp, &pickProjection, &view);
	*visible = projectBox(&mvp, mn, mx, noOffset, pad, &minX, &minY, &maxX, &maxY, &nearW);
	return (minX + maxX) * 0.5f;
}

// A hard right lean on the Circle Pad, driven exactly as the input path drives it.
static void slidePadRight(int frames)
{
	for (int f = 0; f < frames; ++f) {
		C3D_Mtx yaw;
		Mtx_Identity(&yaw);
		Mtx_RotateY(&yaw, angleY, true);
		camPanX += yaw.r[0].x * 110 * CIRCLE_PAN_SPEED;
		camPanZ += yaw.r[0].z * 110 * CIRCLE_PAN_SPEED;
		updateFocus();
	}
}

static void runCameraPanAudit(void)
{
	// Four bench angles, including the two the yaw sign would disagree on, so a
	// slide that ran backwards on half of them cannot pass by averaging out.
	static const float yaws[4] = { 0.0f, 1.5707963f, 3.4915927f, 4.712389f };
	bool ok = true;

	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = runnerLift = 0.0f; boxOpening = false; selectedPart = -1;

	for (int i = 0; i < 4; ++i) {
		bool seenBefore, seenAfter;
		float before, after;
		frameWorkbenchCamera();
		angleY = yaws[i];
		for (int f = 0; f < 60; ++f) updateFocus();
		before = panMarkerScreenX(&seenBefore);
		slidePadRight(45);
		for (int f = 0; f < 30; ++f) updateFocus();
		after = panMarkerScreenX(&seenAfter);
		// Push right, see further right - so what was already on the bench has to
		// travel left across the screen to make room for it.
		bool pass = seenBefore && seenAfter && after < before - 2.0f;
		printf("PAN yaw=%d x %d->%d %s\n", (int)(yaws[i] * 100.0f),
			(int)before, (int)after, pass ? "OK" : "FAIL");
		if (!pass) ok = false;
	}

	// The end of the bench is a wall, not a cliff: leaning on the pad forever
	// has to stop the view at the desk rather than sail off it.
	frameWorkbenchCamera();
	slidePadRight(1200);
	for (int f = 0; f < 60; ++f) updateFocus();
	bool clamped = focus[0] >= PAN_MIN_X - 0.05f && focus[0] <= PAN_MAX_X + 0.05f
	            && focus[2] >= PAN_MIN_Z - 0.05f && focus[2] <= PAN_MAX_Z + 0.05f;
	printf("PAN clamp focus=(%d,%d) %s\n",
		(int)(focus[0] * 100.0f), (int)(focus[2] * 100.0f), clamped ? "OK" : "FAIL");
	if (!clamped) ok = false;

	printf("PAN AUDIT %s\n", ok ? "PASS" : "FAIL");
	frameWorkbenchCamera();
}
#else
static void runCameraPanAudit(void) { }
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

#if TEST_PAINT_AUDIT || TEST_SAVELOAD_AUDIT || TEST_SAVELOAD_TOUR
// Where these probes park the player's real save while they run.
//
// Everything else in debug.h is kept away from the card by TEST_ANY_HARNESS,
// but these two cannot be: what they test IS the write/read path, so gating
// the writes would delete the test. Between them they write save.bin dozens of
// times with fabricated content, and on 2026-08-18 a different harness doing
// much less than that cost a real player their twenty kits. So the file is
// copied aside first, byte for byte, and copied back at the end - the audit
// gets a card of its own and the save that was there is the save that is there
// afterwards.
#define AUDIT_SAVE_BACKUP SAVE_PATH ".auditbak"

// Straight byte copy. Deliberately not a rename: a rename would leave the
// player with no save at all if the console lost power mid-audit, whereas a
// copy leaves two.
static bool auditCopyFile(const char* from, const char* to)
{
	FILE* in = fopen(from, "rb");
	if (!in) return false;               // nothing to copy is not a failure here
	FILE* out = fopen(to, "wb");
	if (!out) { fclose(in); return false; }
	char buf[1024];
	size_t n;
	bool ok = true;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
		if (fwrite(buf, 1, n, out) != n) { ok = false; break; }
	if (ferror(in)) ok = false;
	fclose(in);
	if (fclose(out) != 0) ok = false;
	return ok;
}
#endif

#if TEST_PAINT_AUDIT
static void runPaintAudit(void)
{
	bool ok = true; u64 now = osGetTime();

	// Before anything is written: get the player's file out of the line of fire.
	// "no file" is the normal answer on a console that has never saved, and is
	// not a failure - it just means there is nothing to put back at the end.
	bool stashed = auditCopyFile(SAVE_PATH, AUDIT_SAVE_BACKUP);
	printf("PAINT stashed real save: %s\n", stashed ? "yes" : "no file to stash");

	// 1) The cycle itself: the exact expression the X-button handler uses,
	// run five times, must walk 1,2,3,4,1 and never land back on
	// PART_COLOUR_NONE once it has left it.
	{
		unsigned char v = PART_COLOUR_NONE;
		const unsigned char want[5] = { 1, 2, 3, 4, 1 };
		bool cycleOk = true;
		for (int i = 0; i < 5; i++) {
			v = (v % 4) + 1;
			if (v != want[i]) cycleOk = false;
		}
		printf("PAINT cycle 1,2,3,4,1 %s\n", cycleOk ? "OK" : "FAIL");
		if (!cycleOk) ok = false;
	}

	// 2) Palette mapping: colourOverride-1 must resolve through
	// partMaterialFor() to the same four material pointers the render path
	// and the pre-item-32 parts[i].colour path already agree on.
	{
		bool mapOk =
			partMaterialFor(0) == &material &&
			partMaterialFor(1) == &materialPartGreen &&
			partMaterialFor(2) == &materialPartRed &&
			partMaterialFor(3) == &materialPartYellow;
		printf("PAINT palette map %s\n", mapOk ? "OK" : "FAIL");
		if (!mapOk) ok = false;
	}

	// 3) The gate: refused before the kit is finished, allowed once it is -
	// same builtCount >= meshPartCount() condition the X handler uses,
	// checked against a kit actually cut, filed and seated for real, the
	// same way runLevel1WorkspaceAudit proves the workspace bounds.
	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = 1.0f; boxOpening = false; runnerLift = 0.0f; selectedPart = -1;
	bool gateClosedOk = !(builtCount >= meshPartCount());
	for (int i = 0; i < meshPartCount(); ++i) {
		cutPart(i, now);
		updateCuts(now + CUT_ANIM_MS + 1);
		fileStroke(i, FILE_TRAVEL_PX);
	}
	for (int s = 0; s < meshSocketCount(); ++s) {
		// seatPart only ever fits whatever is currently "in hand" - see
		// handleTap()'s inHand check - so the part has to be selected first,
		// the same way a real tap on its loose piece would select it before
		// a tap on the socket seats it.
		selectedPart = meshSockets()[s].part;
		if (!seatPart(s, now)) { printf("PAINT seat %d failed\n", s); ok = false; }
	}
	updateCuts(now + CUT_ANIM_MS + 1);
	bool gateOpenOk = builtCount >= meshPartCount();
	printf("PAINT gate closed-before/open-after %s/%s\n",
		gateClosedOk ? "OK" : "FAIL", gateOpenOk ? "OK" : "FAIL");
	if (!gateClosedOk || !gateOpenOk) ok = false;

	// 4) Set an override the way saveCurrentLevel() would find it already
	// folded in (colourOverride lives in partState, saved as part of the
	// per-level parts[] block, not separately), then prove it round-trips
	// through a real save/load at the current (post-item-32) layout.
	memset(levelBuilds, 0, sizeof(levelBuilds));
	levelBuilds[0].visited = true;
	levelBuilds[0].parts[0].colourOverride = 2; // materialPartRed
	if (meshPartCount() > 1) levelBuilds[0].parts[1].colourOverride = 4; // materialPartYellow
	bool wroteOk = progressSave();
	memset(levelBuilds, 0, sizeof(levelBuilds));
	progressLoad();
	bool roundTripOk = wroteOk &&
		levelBuilds[0].visited == true &&
		levelBuilds[0].parts[0].colourOverride == 2 &&
		(meshPartCount() <= 1 || levelBuilds[0].parts[1].colourOverride == 4);
	printf("PAINT save round-trip %s\n", roundTripOk ? "OK" : "FAIL");
	if (!roundTripOk) ok = false;

	// 5) A save written before item 32 has no colourOverride field at all.
	// Hand-build one in that older shape, write it under the older layout
	// id (bypassing progressSave(), which only ever writes the current
	// shape), then confirm progressLoad() falls back to it, migrates every
	// field, and lands PART_COLOUR_NONE for the field that never existed.
	memset(legacyBuilds, 0, sizeof(legacyBuilds));
	legacyBuilds[0].visited = true;
	legacyBuilds[0].cutCount = 3;
	legacyBuilds[0].builtCount = 2;
	legacyBuilds[0].parts[0].cut = true;
	legacyBuilds[0].parts[0].seated = true;
	legacyBuilds[0].parts[0].filed = 1.0f;
	legacyBuilds[0].parts[0].poseDeg = 12.5f;
	legacyBuilds[0].parts[0].offset[0] = 1.25f;
	bool legacyWroteOk = saveWriteBlob(legacyBuilds, sizeof(legacyBuilds), progressLayoutIdV1());
	memset(levelBuilds, 0, sizeof(levelBuilds));
	memset(partStates, 0, sizeof(partStates));
	progressLoad();
	bool migrateOk = legacyWroteOk &&
		levelBuilds[0].visited == true &&
		levelBuilds[0].cutCount == 3 &&
		levelBuilds[0].builtCount == 2 &&
		levelBuilds[0].parts[0].seated == true &&
		levelBuilds[0].parts[0].poseDeg == 12.5f &&
		levelBuilds[0].parts[0].offset[0] == 1.25f &&
		levelBuilds[0].parts[0].colourOverride == PART_COLOUR_NONE;
	printf("PAINT legacy-save migrate %s\n", migrateOk ? "OK" : "FAIL");
	if (!migrateOk) ok = false;

	printf("PAINT AUDIT %s\n", ok ? "PASS" : "FAIL");

	// Leave the card the way this probe found it, rather than with either
	// test's data, and put live state back to neutral like the other audits
	// above do. Found with a save on it: that exact file goes back and the
	// copy is dropped. Found with none: the one this probe made is removed,
	// so a console that had never saved has never saved afterwards either.
	//
	// A restore that fails leaves the copy on the card on purpose and says
	// where it is - the player can be told to rename it back, which is not
	// possible once it has been deleted.
	if (stashed)
	{
		bool restored = auditCopyFile(AUDIT_SAVE_BACKUP, SAVE_PATH);
		if (restored) remove(AUDIT_SAVE_BACKUP);
		printf("PAINT restored real save: %s\n",
			restored ? "OK" : "FAIL - copy kept at " AUDIT_SAVE_BACKUP);
		// RAM follows the card back, so the rest of the boot is not running on
		// the last thing the audit happened to leave in levelBuilds.
		progressLoad();
	}
	else
	{
		memset(levelBuilds, 0, sizeof(levelBuilds));
		remove(SAVE_PATH);
	}
	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = runnerLift = 0.0f; boxOpening = false; selectedPart = -1;
	frameWorkbenchCamera();
}
#else
static void runPaintAudit(void) { }
#endif

#if TEST_HINT_AUDIT
// Is the breathing dot somewhere a tap actually works?
//
// The claim worth testing is not "hintPoint returned a number" but "the dot is
// on the piece this step is talking about". So each step is staged, hintPoint is
// asked where its dot goes, and that exact point is then fed back through the
// pick function the player's stylus would go through. If the pick answers with
// the piece the step meant, the dot is on it - by the game's own definition of
// on, rather than by a second opinion written here that could be wrong in the
// same direction.
//
// Level 1, part 0 throughout: the first piece of the first kit is the one a new
// player meets, which is the whole audience for the tutorial.
// Where a step's target actually lands on the bottom screen, printed next to
// its pass/fail. "no point" on its own cannot tell "the target is behind the
// camera" from "it is on screen but its centre is not", and those two want
// opposite fixes. Kept short: the 3DS console is 50 columns and a wrapped line
// is unreadable in a screenshot.
static void hintRect(const char* tag, const C3D_Mtx* view, const float min[3],
	const float max[3], const float offset[3])
{
	C3D_Mtx mvp;
	float pad[3] = { 0.0f, 0.0f, 0.0f };
	float aX, aY, bX, bY, w;
	Mtx_Multiply(&mvp, &pickProjection, view);
	int vis = projectBox(&mvp, min, max, offset, pad, &aX, &aY, &bX, &bY, &w) ? 1 : 0;
	printf("R %-4s v%d x%d..%d y%d..%d m%d,%d\n", tag, vis,
		(int)aX, (int)bX, (int)aY, (int)bY,
		(int)((aX + bX) * 0.5f), (int)((aY + bY) * 0.5f));
}

// How one step came out. The three-way split matters: "no dot at all" and "a
// dot on the wrong thing" are different bugs with opposite fixes, and a single
// pass/fail hides which one a kit has.
#define HINT_NO_DOT 0
#define HINT_WRONG  1
#define HINT_OK     2

// One kit, all six checks, no printing except the optional rectangle dump.
// Returns the six results packed two bits at a time, lowest pair first.
static unsigned hintAuditKit(int level, bool detail)
{
	unsigned res = 0;
	int step = 0;
	u64 now = osGetTime();
	C3D_Mtx mv, runnerMv;
	float x = 0.0f, y = 0.0f;
	const float zero3[3] = { 0.0f, 0.0f, 0.0f };

	#define HINT_PUT(v) do { res |= ((unsigned)(v)) << (step * 2); step++; } while (0)

	sceneLoadKit(level);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = 0.0f; boxOpening = false; runnerLift = 0.0f; selectedPart = -1;
	frameWorkbenchCamera();
	for (int f = 0; f < 60; f++) updateFocus();

	// Step 0 - the closed kit box, before anything else exists to point at.
	buildModelView(&mv);
	if (detail)
	{
		float bmin[3], bmax[3];
		meshKitBoxBounds(bmin, bmax);
		hintRect("box", &mv, bmin, bmax, zero3);
	}
	if (!hintPoint(&mv, 0, &x, &y))          HINT_PUT(HINT_NO_DOT);
	else if (!pickKitBox(&mv, (int)x, (int)y)) HINT_PUT(HINT_WRONG);
	else                                     HINT_PUT(HINT_OK);

	// Step 1 - a gate on the frame, box open, nothing cut yet. The gate the dot
	// is meant to be on is the first uncut part of the runner on the bench,
	// which is not part 0 on every kit.
	boxOpen = 1.0f;
	frameRunnerCamera();
	for (int f = 0; f < 60; f++) updateFocus();
	buildModelView(&mv);
	// The frame is drawn - and picked, see handleTap - through buildRunnerView,
	// not the bare bench view.
	buildRunnerView(&runnerMv, &mv);
	int wantGate = -1;
	for (int i = 0; i < meshPartCount(); i++)
		if (!partStates[i].cut && meshParts()[i].runner == currentRunner) { wantGate = i; break; }
	if (detail && wantGate >= 0)
		hintRect("gate", &runnerMv, meshParts()[wantGate].gateMin,
			meshParts()[wantGate].gateMax, zero3);
	if (!hintPoint(&mv, 1, &x, &y))                       HINT_PUT(HINT_NO_DOT);
	else if (pickGate(&runnerMv, (int)x, (int)y) != wantGate) HINT_PUT(HINT_WRONG);
	else                                                  HINT_PUT(HINT_OK);

	// Step 2 - the loose piece being filed. The whole kit comes off the frame
	// first, because that is the only state the game ever shows this page in:
	// beginnerAction() will not leave step 1 until cutCount reaches the part
	// count. It also matters for where the camera is - updateFocus deliberately
	// stays on the frame while it still holds anything.
	for (int i = 0; i < meshPartCount(); i++)
	{
		if (partStates[i].cut) continue;
		currentRunner = meshParts()[i].runner;
		cutPart(i, now);
	}
	currentRunner = 0;
	updateCuts(now + CUT_ANIM_MS + 1);
	selectedPart = 0;
	for (int f = 0; f < 60; f++) updateFocus();
	buildModelView(&mv);
	if (detail)
		hintRect("part", &mv, meshParts()[0].min, meshParts()[0].max, partStates[0].offset);
	if (!hintPoint(&mv, 2, &x, &y))                 HINT_PUT(HINT_NO_DOT);
	else if (pickPart(&mv, (int)x, (int)y) != 0)    HINT_PUT(HINT_WRONG);
	else                                            HINT_PUT(HINT_OK);

	// Step 3 - the hole it goes in, once it is smooth.
	fileStroke(0, FILE_TRAVEL_PX);
	for (int f = 0; f < 60; f++) updateFocus();
	buildModelView(&mv);
	int want = socketForPart(0);
	if (detail)
	{
		if (want >= 0)
			hintRect("sock", &mv, meshSockets()[want].min, meshSockets()[want].max, zero3);
		float sMin[3] = { meshStandX() - 0.2f, MAT_TOP,         meshStandZ() - 0.2f };
		float sMax[3] = { meshStandX() + 0.2f, MAT_TOP + 0.40f, meshStandZ() + 0.2f };
		float lMin[3] = { meshLooseX() - 0.2f, MAT_TOP,         meshLooseZ() - 0.2f };
		float lMax[3] = { meshLooseX() + 0.2f, MAT_TOP + 0.40f, meshLooseZ() + 0.2f };
		hintRect("stnd", &mv, sMin, sMax, zero3);
		hintRect("lose", &mv, lMin, lMax, zero3);
	}
	if (!hintPoint(&mv, 3, &x, &y)) HINT_PUT(HINT_NO_DOT);
	else
	{
		// Either pick counts. The ghost box is the generous one the player is
		// actually aiming at; the plain socket pick is the fallback.
		int ghost = pickGhostSocket(&mv, (int)x, (int)y);
		int plain = pickSocket(&mv, (int)x, (int)y);
		HINT_PUT((want >= 0 && (ghost == want || plain == want)) ? HINT_OK : HINT_WRONG);
	}

	// Out of order. Every piece off the frame and filed, nothing fitted, and the
	// piece in hand is one that hangs off another - exactly what the old parent
	// rule refused. Both halves have to work for the kit to be buildable in any
	// order: the generous ghost box has to be there to be tapped, and the seat
	// itself has to go through.
	//
	// pickGhostSocket stands in for "the amber ghost is drawn": sceneRender draws
	// the ghost for the same part, under the same test, so the two cannot
	// disagree. Whether it looks right is a screenshot's job, not this one's.
	{
		for (int i = 0; i < meshPartCount(); i++)
			if (!partStates[i].smooth) fileStroke(i, FILE_TRAVEL_PX);

		// The first piece that hangs off another one. Shallowest in the tree, so
		// it is the one a player reaching past the base part meets first.
		int child = -1, childSock = -1;
		for (int i = 0; i < meshPartCount(); i++)
		{
			int s = socketForPart(i);
			if (s >= 0 && meshSockets()[s].parent >= 0) { child = i; childSock = s; break; }
		}

		if (child < 0) HINT_PUT(HINT_OK);   // single-socket kit: nothing to order
		else
		{
			selectedPart = child;
			for (int f = 0; f < 60; f++) updateFocus();
			buildModelView(&mv);
			if (detail)
				hintRect("ooo", &mv, meshSockets()[childSock].min,
					meshSockets()[childSock].max, zero3);

			// Aimed at the middle of the hole itself, not at wherever the hint dot
			// ended up: whether the dot can be shown depends on the camera having
			// the stand in shot, and it is the ghost this is asking about.
			C3D_Mtx mvp;
			float gMinX, gMinY, gMaxX, gMaxY, gW, gPad[3] = { 0.0f, 0.0f, 0.0f };
			Mtx_Multiply(&mvp, &pickProjection, &mv);
			int ghost = -1;
			if (projectBox(&mvp, meshSockets()[childSock].min, meshSockets()[childSock].max,
					zero3, gPad, &gMinX, &gMinY, &gMaxX, &gMaxY, &gW))
				ghost = pickGhostSocket(&mv, (int)((gMinX + gMaxX) * 0.5f),
					(int)((gMinY + gMaxY) * 0.5f));

			bool seat = seatPart(childSock, now);   // last: it changes state
			if (ghost != childSock)  HINT_PUT(HINT_NO_DOT);
			else if (!seat)          HINT_PUT(HINT_WRONG);
			else                     HINT_PUT(HINT_OK);
		}
	}

	// A dot must not appear where the tutorial is not asking for anything. Step
	// 3 with the kit already finished has nowhere left to point, and a marker
	// parked on the last hole would be telling the player to redo work.
	selectedPart = -1;
	for (int i = 0; i < meshPartCount(); i++) partStates[i].seated = true;
	buildModelView(&mv);
	HINT_PUT(hintPoint(&mv, 3, &x, &y) ? HINT_NO_DOT : HINT_OK);

	#undef HINT_PUT
	return res;
}

static void runHintAudit(void)
{
	// Every kit, not just the first. The tutorial sheet is on every level, so a
	// dot that only lands on kit 1 is a dot that is broken for nineteen of the
	// twenty kits in the game - and kit 1 is the least representative one there
	// is: fewest parts, one runner, shallowest socket tree.
	//
	// One line per kit, six digits, one per step in order (box, gate, file,
	// socket, out-of-order, silent-when-done): 2 passed, 1 landed on the wrong
	// piece, 0 produced no dot at all. The 3DS console is 50 columns and 30
	// rows, so twenty kits only fit if each is one short line.
	bool all = true;
	int firstBad = 0;

	for (int level = 1; level <= MESH_KIT_LEVELS; level++)
	{
		unsigned r = hintAuditKit(level, false);
		bool ok = true;
		char digits[8];
		for (int s = 0; s < 6; s++)
		{
			int v = (int)((r >> (s * 2)) & 3u);
			digits[s] = (char)('0' + v);
			if (v != HINT_OK) ok = false;
		}
		digits[6] = '\0';
		printf("HINT k%-2d %s %s\n", level, digits, ok ? "OK" : "FAIL");
		if (!ok && !firstBad) firstBad = level;
		if (!ok) all = false;
	}

	// The rectangles for one failing kit, so the numbers behind the first FAIL
	// are on the same screenshot. Only one, because each dump is five more rows.
	if (firstBad)
	{
		printf("HINT detail k%d\n", firstBad);
		hintAuditKit(firstBad, true);
	}

	printf("HINT AUDIT %s\n", all ? "PASS" : "FAIL");

	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = runnerLift = 0.0f; boxOpening = false; selectedPart = -1;
	frameWorkbenchCamera();
}
#else
static void runHintAudit(void) { }
#endif

// Normals do not survive a non-uniform scale on the modelView matrix - squash a
// box on one axis and its side faces end up pointing off the surface, so the
// shading no longer matches the shape being drawn. The shader multiplies the
// object-space normal by this before transforming it, and the reciprocal SQUARES
// are what makes that come out exactly right: the matrix contributes one factor
// of the scale back, leaving the 1/scale the normal actually wants.
static void setNormalScale(float sx, float sy, float sz)
{
	// A zero on any axis would flatten the part to nothing anyway; leaving that
	// axis alone keeps an infinity out of the normal rather than turning the whole
	// vector into NaN once it is normalized.
	if (sx == 0.0f) sx = 1.0f;
	if (sy == 0.0f) sy = 1.0f;
	if (sz == 0.0f) sz = 1.0f;
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_nrmScale,
		1.0f / (sx * sx), 1.0f / (sy * sy), 1.0f / (sz * sz), 0.0f);
}

// Item 44: the fixed scenery draws in sceneRender - the ones with no per-frame
// transform and no visibility test - as a table instead of one hand-written
// material-then-DrawArrays pair per piece. firstVertex/vertexCount are the
// mesh module's own per-piece accessor pairs, called through the table
// instead of by name.
typedef struct {
	const C3D_Mtx* material;
	int (*firstVertex)(void);
	int (*vertexCount)(void);
} sceneDrawEntry;

static const sceneDrawEntry SCENE_STATIC_DRAWS[] = {
	{ &materialDesk,       meshRoomBedWoodFirstVertex,  meshRoomBedWoodVertexCount  },
	{ &materialDesk,       meshRoomShelfFirstVertex,    meshRoomShelfVertexCount    },
	{ &materialKitBox,     meshRoomMattressFirstVertex, meshRoomMattressVertexCount },
	{ &materialKitBox,     meshRoomPillowFirstVertex,   meshRoomPillowVertexCount   },
	{ &materialPartGreen,  meshRoomBlanketFirstVertex,  meshRoomBlanketVertexCount  },
	// A dark wood door reads as a deliberate opening rather than a black gap.
	{ &materialDesk,       meshRoomDoorFirstVertex,     meshRoomDoorVertexCount     },
	{ &materialKitArtwork, meshRoomKnobFirstVertex,     meshRoomKnobVertexCount     },
	{ &materialKitBox,     meshRoomTrimFirstVertex,     meshRoomTrimVertexCount     },
	{ &materialWindow,     meshRoomSkyFirstVertex,      meshRoomSkyVertexCount      },
	{ &materialMat,        meshRoomGroundFirstVertex,   meshRoomGroundVertexCount   },
	{ &materialDesk,       meshDeskFirstVertex,         meshDeskVertexCount         },
	{ &materialMat,        meshMatFirstVertex,          meshMatVertexCount          },
	{ &materialGrid,       meshGridFirstVertex,         meshGridVertexCount         },
	{ &materialStand,      meshStandFirstVertex,        meshStandVertexCount        },
};

// Item 36: every draw below used to be a direct C3D_DrawArrays(GPU_TRIANGLES,
// first, count) into vbo_data. The scene is one shared, deduplicated vertex
// buffer now, so a draw is a slice of idx_data instead - first and count keep
// the exact meaning they always had (see the meshIndices() comment in
// mesh.h), only the call underneath changed. One place to get that
// pointer-into-linear-memory arithmetic right rather than eleven.
static void drawRange(int first, int count)
{
	C3D_DrawElements(GPU_TRIANGLES, count, C3D_UNSIGNED_SHORT, (const unsigned short*)idx_data + first);
}

static void sceneRender(const C3D_Mtx* modelView)
{
	// Every draw below indexes the same shared vertex buffer through the same
	// shared index buffer. If either could not be allocated there is nothing
	// to read from, and the ranges the mesh accessors report would run off the
	// end of whatever the buffer pointers happen to be.
	if (!vbo_data || !idx_data) return;

	// Uniforms that are the same for every draw
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &projection);
	// The shader wants the direction the light TRAVELS, in view space, and gives
	// a surface max(0, -dot(lightVec, normal)). Straight down the camera axis
	// would leave every upward-facing surface - the desk, the mat - unlit, so the
	// lamp is set up and to the left of the viewer, the way a desk lamp sits.
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightVec,     -0.20f, -0.55f, -0.81f, 0.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightHalfVec, -0.20f, -0.55f, -0.81f, 0.0f);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_lightClr,     1.0f, 1.0f,  1.0f, 1.0f);
	// Every draw in the scene is rigid, so the shader's normal correction stays at
	// identity for the whole frame. It is still set explicitly rather than left to
	// whatever the uniform happened to hold last.
	setNormalScale(1.0f, 1.0f, 1.0f);

	// Scenery first: the desk, the mat and its grid, then the runner with its
	// leftover gate stubs. None of it ever moves, so it all goes out with the
	// plain scene transform - only the material changes between draws.
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, modelView);

	// The shell is still dropped a surface at a time once the camera has gone
	// behind it, but the four walls and the ceiling sit in one contiguous run
	// of the vertex buffer - mesh.c builds them back to back, in this exact
	// order, all under this one material - so any unbroken stretch that is
	// fully visible (almost always all five, since the camera is clamped
	// inside the room) goes out as a single draw instead of one call per
	// surface. A hidden surface still breaks the run on its own; that costs
	// one extra draw per gap, never one draw per surface.
	float camera[3];
	cameraWorldPosition(modelView, camera);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialRoom);
	{
		const int shellFirst[5] = {
			meshRoomWallRearFirstVertex(),  meshRoomWallLeftFirstVertex(),
			meshRoomWallRightFirstVertex(), meshRoomWallFrontFirstVertex(),
			meshRoomCeilingFirstVertex()
		};
		const int shellCount[5] = {
			meshRoomWallRearVertexCount(),  meshRoomWallLeftVertexCount(),
			meshRoomWallRightVertexCount(), meshRoomWallFrontVertexCount(),
			meshRoomCeilingVertexCount()
		};
		const int shellSurface[5] = { SURF_REAR, SURF_LEFT, SURF_RIGHT, SURF_FRONT, SURF_CEILING };
		int runFirst = -1, runCount = 0;
		for (int i = 0; i < 5; i++)
		{
			bool visible = shouldDrawRoomSurface(shellSurface[i], camera);
			if (visible)
			{
				if (runFirst < 0) runFirst = shellFirst[i];
				runCount += shellCount[i];
			}
			if ((!visible || i == 4) && runFirst >= 0)
			{
				drawRange(runFirst, runCount);
				runFirst = -1;
				runCount = 0;
			}
		}
	}
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialDesk);
	if (shouldDrawRoomSurface(SURF_FLOOR, camera))
		drawRange(meshRoomFloorFirstVertex(), meshRoomFloorVertexCount());

	// The rest of the fixed scenery - none of it moves and none of it needs a
	// visibility test - is one draw per row of this table rather than one
	// hand-written material-then-DrawArrays pair per piece (item 44). Each row
	// names its own material even where it repeats the row above, so a row can
	// be reordered or dropped without silently inheriting the wrong one.
	for (int i = 0; i < (int)(sizeof(SCENE_STATIC_DRAWS) / sizeof(SCENE_STATIC_DRAWS[0])); i++)
	{
		const sceneDrawEntry* e = &SCENE_STATIC_DRAWS[i];
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, e->material);
		drawRange(e->firstVertex(), e->vertexCount());
	}

	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitBox);
	float boxClear = boxStage(0.48f, 0.70f);
	if (boxClear < 1.0f)
	{
		C3D_Mtx trayMv = *modelView;
		Mtx_Translate(&trayMv, -3.40f * boxClear, 0.0f, 0.0f, true);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &trayMv);
		drawRange(meshKitTrayFirstVertex(), meshKitTrayVertexCount());
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, modelView);
	}
	if (boxOpen < 1.0f)
	{
		C3D_Mtx lidMv = *modelView;
		Mtx_Translate(&lidMv, 0.0f, 1.55f * boxStage(0.0f, 0.16f), 0.0f, true);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &lidMv);
		drawRange(meshKitLidFirstVertex(), meshKitLidVertexCount());
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material, &materialKitArtwork);
		drawRange(meshKitArtFirstVertex(), meshKitArtVertexCount());
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
		drawRange(meshRunnerFirstVertex(), meshRunnerBaseVertexCount());
		// Only gates belonging to the active runner stay attached to its frame.
		for (int i=0; i<meshPartCount(); i++)
			if (meshParts()[i].runner == currentRunner && !partStates[i].cut)
				drawRange(meshRunnerStubFirstVertex(i), meshRunnerStubVertexCount(i));
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

		// Item 35: posing. Walk from this part up to the root collecting the
		// chain it hangs off, then sweep it root-first - a part fitted under
		// two posed joints has to turn with the outer one before the inner
		// one turns it further, the same order a real linkage would. Most
		// parts have no joint anywhere in their chain and every poseDeg
		// along it reads 0, so this is a handful of lookups that rotate
		// nothing.
		{
			int chainParts[MESH_MAX_PARTS];
			int chainLen = 0;
			int curPart = i;
			while (curPart >= 0 && chainLen < MESH_MAX_PARTS)
			{
				chainParts[chainLen++] = curPart;
				int sock = socketForPart(curPart);
				if (sock < 0) break;
				int parentSock = meshSockets()[sock].parent;
				curPart = (parentSock >= 0) ? meshSockets()[parentSock].part : -1;
			}
			for (int c = chainLen - 1; c >= 0; c--)
			{
				const meshJoint* j = meshJointForPart(chainParts[c]);
				if (!j) continue;
				float deg = partStates[chainParts[c]].poseDeg;
				if (deg == 0.0f) continue;
				float rad = deg * POSE_DEG_TO_RAD;
				const meshPart* jp = &parts[chainParts[c]];
				float px = jp->bodyCentre[0] + j->pivotOffset[0];
				float py = jp->bodyCentre[1] + j->pivotOffset[1];
				float pz = jp->bodyCentre[2] + j->pivotOffset[2];
				Mtx_Translate(&mv, px, py, pz, true);
				if (j->axis == JOINT_AXIS_X)      Mtx_RotateX(&mv, rad, true);
				else if (j->axis == JOINT_AXIS_Y) Mtx_RotateY(&mv, rad, true);
				else                               Mtx_RotateZ(&mv, rad, true);
				Mtx_Translate(&mv, -px, -py, -pz, true);
			}
		}

		if (i == selectedPart && osGetTime() < filingAnimUntil)
		{
			// A small side-to-side scrub makes an active file stroke visible
			// without changing the part's true location or its filing progress.
			//
			// This used to be a square wave - it snapped between -0.035 and
			// +0.035 every 18ms, so the part jumped 0.070 across in a single
			// frame roughly 28 times a second and read as being shaken about the
			// table rather than rubbed. A sine at 0.010 amplitude covers the same
			// ground in a continuous sweep at about 14Hz: still obvious that
			// something is happening, no longer violent. The lift comes down with
			// it, since a part that vaults 0.018 into the air on touch-down was
			// part of the same effect.
			float scrub = sinf((float)osGetTime() * 0.088f) * 0.010f;
			Mtx_Translate(&mv, scrub, 0.006f, 0.0f, true);
		}

		// Parts are packed on the runner at their authored size, so there is one
		// mesh and one transform for a part whether it is still on the frame or cut
		// free - nothing shrinks going in and nothing grows coming out.
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &mv);
		C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_material,
			i == selectedPart ? &materialSelected :
			i == hoverPart    ? &materialHover    :
			partMaterialFor(st->colourOverride != PART_COLOUR_NONE ? st->colourOverride - 1 : parts[i].colour));

		// Body and nub are drawn apart so filing can sink the nub into the body
		// on its own. A nub that is all the way in is not drawn at all - the
		// faces would z-fight with the body they are buried in.
		int nubVerts  = parts[i].nubVertexCount;
		int bodyFirst = parts[i].firstVertex + nubVerts;
		drawRange(bodyFirst, parts[i].vertexCount - nubVerts);

		if (st->filed < 1.0f)
		{
			if (st->cut || st->filed > 0.0f)
			{
				C3D_Mtx nubMv = mv;
				Mtx_Translate(&nubMv, 0.0f, parts[i].nubSink * st->filed, 0.0f, true);
				C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_modelView, &nubMv);
			}
			drawRange(parts[i].firstVertex, nubVerts);
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
	// It shows for whatever is held, base piece or not. It used to stay hidden
	// until the piece that one hangs off was already on the stand, so selecting
	// an arm before its shoulder gave nothing at all - no amber box, and no way
	// of seeing where the piece went. The kit builds in any order now, so the
	// ghost is always there to be aimed at.
	// Held, and ready to go on. pickGhostSocket refuses a tap unless the piece
	// has been cut off the frame and filed smooth, so drawing the amber box for
	// a piece that fails either test glows a target that cannot be hit: the
	// player aims at it, taps, and the model does not move. That is worst on a
	// first kit, because cutPart selects the piece the instant it is snipped -
	// so the very first cut lit a socket the game would then refuse.
	int ghostPart = selectedPart;
	if (ghostPart >= 0 && !partStates[ghostPart].seated &&
	    partStates[ghostPart].cut && partStates[ghostPart].smooth)
	{
		int sock = socketForPart(ghostPart);
		if (sock >= 0)
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
			drawRange(p->firstVertex + nubVerts, p->vertexCount - nubVerts);
		}
	}
}

static void sceneExit(void)
{
	// Both screens' targets are made in main but torn down here, so the colour
	// and depth buffers behind them go back with the rest of the scene rather
	// than being left to the process teardown.
	if (beginnerTarget) { C3D_RenderTargetDelete(beginnerTarget); beginnerTarget = NULL; }
	if (benchTarget)    { C3D_RenderTargetDelete(benchTarget);    benchTarget    = NULL; }
	// sceneInit can now fail before any of these exist - main calls this on that
	// path to give back whatever did get made, so each one has to tolerate never
	// having been set up.
	if (vbo_data)      { linearFree(vbo_data); vbo_data = NULL; }
	if (idx_data)      { linearFree(idx_data); idx_data = NULL; }
	shaderProgramFree(&program);
	if (vshader_dvlb)  { DVLB_Free(vshader_dvlb); vshader_dvlb = NULL; }
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

	m->vramTotal   = memoryStatusVramTotalBytes();
	m->vramUsed    = memoryStatusVramUsedBytes();

	// The app figure is the resident image and stack plus what the two
	// allocators are handing out - see memory_status.c, which owns it. The
	// heap and linear rows above are the allocators alone, so the app total
	// is deliberately larger than their sum rather than equal to it. VRAM is
	// separate silicon and is in none of them.
	//
	// The ceiling is fixed at the retail Original 3DS 64 MB rather than at
	// whatever the running console hands out, so the bar always answers the
	// only question worth asking: does this still fit the machine we target?
	// Measured, same binary, Azahar 2126.0:
	//   New 3DS mode  126752 KB   (124 MB region)
	//   Old 3DS mode   98080 KB   ( 96 MB - a DEV unit memory mode)
	// Even the emulator's Old 3DS mode is a third too generous and can never
	// make this bar go red, so the fixed number is the only honest one.
	m->appUsed     = memoryStatusAppUsedBytes();
	m->appTotal    = memoryStatusAppTotalBytes();
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
		printf("%-18s %s\n", STR(lines[i].key), STR(lines[i].action));
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

// Level 1 has no console walkthrough. It draws its illustrated beginner sheet
// on the top screen with citro2d instead, so printStaticInfo bails out before
// reaching here.
//
// Every level after the first: the same three jobs in one line, then the button
// list. Eight rows of buttons and four of prose leaves rows 15 to 17 blank,
// which is deliberate - it keeps the manual clear of the live block below.
static void printBuildGuide(void)
{
	printf("\n");
	printf("%s", STR(STR_C_MANUAL_GUIDE));
	printf("\n");
	printControls();
}

// Declared ahead of its definition down with the live block itself, because
// this is the one place that wipes the console the block is caching.
static void liveBlockInvalidate(void);

// Whether the illustrated beginner sheet owns the top screen right now.
//
// Four places need this answer and they must all give the same one: the two
// console writers stand down when the sheet is up, the render loop draws it,
// and the top-screen buffer swap has to go the other way when it is not. They
// used to each spell out "level 1", which was fine while that was the whole
// condition. It is not any more - Options can switch the sheet off - so the
// question is asked in one place and the setting can only ever be honoured by
// all four at once.
// Whether the citro2d top screen owns GFX_TOP at all, in either of its two
// forms - the tutorial sheet, or the plain card that replaces it when Options
// has the tutorial switched off. Both are drawn on beginnerTarget, so either
// way the two console writers have to stand down and the top-screen buffer swap
// has to go the other way.
//
// This used to be level 1 only, and every level after it got a wall of console
// text instead. The tutorial now runs on all twenty, so the only thing left that
// can hand the top screen back to the console is photo mode, which takes the
// screen down to a caption on purpose.
static bool beginnerTopShowing(void)
{
	return !photoMode;
}

// Whether what is on that screen is the tutorial rather than the plain card.
// Only the draw call needs to tell them apart; everything else only cares that
// citro2d has the screen.
static bool beginnerSheetShowing(void)
{
	return beginnerTopShowing() && settingsShowTutorial();
}

static void printStaticInfo(bool isNew3DS)
{
	// The screen is about to be cleared, so nothing the live block believes is
	// on it will be. Before the level-1 return, because level 1 hands the top
	// screen to its beginner sheet - the block's rows do not survive that
	// either, and the level after it would inherit a cache describing them.
	liveBlockInvalidate();

	// Photo mode takes the top screen down to a caption. Ahead of the level-1
	// return so the beginner sheet clears too - it is the largest thing on the
	// screen and leaving it up would defeat the point of the mode.
	if (photoMode)
	{
		printf("\x1b[2J\x1b[1;1H");
		printf("%s\n", meshKitName());
		printf(RULE_LINE);
		printf("%s", STR(STR_C_PHOTO_HEADER));
		printf("%s", STR(STR_C_PHOTO_NEXT));
		printf("%s", STR(STR_C_PHOTO_BACK));
		return;
	}

	// The illustrated top screen owns the display on every level now, so the
	// console manual below is reached only in photo mode, where the caption
	// above has already returned. The rows are kept rather than deleted: photo
	// mode is still a console page and shares this function's setup.
	if (beginnerTopShowing()) return;
	// The hardware line has gone with the rest of the debug block. The flag
	// stays in the signature because every caller has it to hand and the New
	// 3DS extras will want somewhere to announce themselves later.
	(void)isNew3DS;

	printf("\x1b[2J\x1b[1;1H");
	printf(STR(STR_C_MANUAL_HEADER), titleLevel());
	printf(RULE_LINE);

	printBuildGuide();
}

// Repainting the top screen after the pause menu closes, a frame late and twice.
//
// The top screen is single-buffered - gfxSetDoubleBuffering(GFX_TOP, false) in
// main - and it has two writers: this CPU text console, and the citro3d target
// the pause menu draws on. C3D_FrameEnd does not perform the display transfer
// that pushes the menu onto that one buffer; it queues it. So a console repaint
// issued on the same frame the menu closes can be writing that memory while the
// GPU is still transferring into it, and what lands is half menu, half console,
// with torn columns where the two met. It stays that way, because with one
// buffer there is no clean copy to present next frame - the player has to
// restart the game. Real hardware has a genuine asynchronous transfer engine so
// it shows there; an emulator completes the transfer inside the same step and
// the window never opens, which is why this was never reproducible here.
//
// Rather than reach for a sync primitive, the repaint is simply not done on the
// transition frame. It is queued, and performed on the following frames - by
// which point the menu's transfer cannot still be in flight.
//
// The obvious alternative is gspWaitForPPF(), which blocks on the transfer
// engine's completion interrupt. It is not used, and deliberately: that macro is
// gspWaitForEvent(GSPGPU_EVENT_PPF, false), which waits on the event and only
// returns early if it happens to be signalled at that moment. If the transfer
// has already completed and something else consumed the event, it blocks until
// the NEXT transfer - and on a frame where nothing is queued to the top screen
// there is no next transfer. Trading a corrupt screen for a hang is not a fix,
// least of all for a fault that only appears on hardware this bench cannot
// reproduce.
//
// TOP_REPAINT_FRAMES is the margin instead. Four frames is 67 ms at 60 Hz
// against a 400x240 RGB565 transfer of about 192 KB, which the PPF engine
// finishes inside a single frame; the extra frames cost console writes nobody
// can see, and the failure being covered is a screen that stays broken for the
// rest of the session.
#define TOP_REPAINT_FRAMES 4
typedef enum { TOP_REPAINT_NONE = 0, TOP_REPAINT_LEVEL, TOP_REPAINT_TITLE } topRepaintKind;
static topRepaintKind topRepaintWhat  = TOP_REPAINT_NONE;
static int            topRepaintFrames = 0;

static void queueTopRepaint(topRepaintKind what)
{
	topRepaintWhat   = what;
	topRepaintFrames = TOP_REPAINT_FRAMES;
}

// Called at the top of the frame, before anything can open the menu again, so
// the repaint never shares a frame with the transfer it is waiting out.
static void serviceTopRepaint(bool isNew3DS, bool menuIsOpen)
{
	if (topRepaintFrames <= 0) return;

	// Re-opening the menu makes the repaint both pointless and unsafe: the menu
	// is about to own that buffer again.
	if (menuIsOpen)
	{
		topRepaintFrames = 0;
		topRepaintWhat   = TOP_REPAINT_NONE;
		return;
	}

	topRepaintFrames--;
	if (topRepaintWhat == TOP_REPAINT_LEVEL) printStaticInfo(isNew3DS);
	else if (topRepaintWhat == TOP_REPAINT_TITLE) titlePrintTop(isNew3DS);
	if (topRepaintFrames == 0) topRepaintWhat = TOP_REPAINT_NONE;
}

// ---------------------------------------------------------------------------
// Opening a level
//
// A level that has just opened is deaf to the stylus for the same reason the
// front end is (title.c: settle / seenUp). The tap that chose the level lands
// on the same glass the bench uses, and the player's finger is often still on
// its way up when the kit appears - so without a settle window the release can
// carry straight through and snip a part before the kit has even been looked
// at. That is the "fresh boot already showing Cut : 1 of 4" fault.
// ---------------------------------------------------------------------------
#define BENCH_SETTLE_FRAMES 30
static int  benchSettle = 0;
static bool benchSeenUp = false;

// Both ways into a level go through here: the level-select tile and the
// straight-to-level test builds. One function rather than two copies of the
// same five lines, because two copies of the camera pivot is exactly how the
// opening view ended up inside the kit box - they drift, and nothing tells you.
//
// titleCaptureStartLevel is called on both paths even though the tile has
// already set the level. It is idempotent, and it is what guarantees that the
// front end's idea of the level (which the manual header prints) and the kit
// actually loaded can never disagree.
static void enterLevel(int level, bool isNew3DS)
{
	titleCaptureStartLevel(level);
	sceneLoadKit(level);
	loadLevelState(level);
	frameWorkbenchCamera();
	printStaticInfo(isNew3DS);

	benchSettle = BENCH_SETTLE_FRAMES;
	benchSeenUp = false;
}

// True once the settle window has run out AND the glass has been seen clear on
// one frame. A touch that was already down when the level opened satisfies
// neither, so it cannot act on the kit. Decrements, so call it once a frame.
static bool benchReady(u32 kHeld)
{
	if (benchSettle > 0)
	{
		benchSettle--;
		return false;
	}
	if (!benchSeenUp)
	{
		if (kHeld & KEY_TOUCH) return false;
		benchSeenUp = true;
	}
	return true;
}

// The live block is thirteen rows of fifty columns, and it was sent in full
// every frame. The console has no dirty tracking - every character handed to it
// is a glyph blitted into the top framebuffer by the CPU - so that was 650
// blits a frame to redraw text that, most frames, had not changed by one
// character: the step, the part names and the counters only move when the
// player does something, and the frame counter only reloads once a second.
//
// So each row is kept as it was last written and compared before being sent.
// A row that matches costs a strcmp and nothing else; a still frame sends
// nothing at all. Rows are addressed absolutely rather than run together with
// newlines, because once a row can be skipped the cursor is no longer wherever
// the previous row left it.
#define LIVE_ROWS 13
static char liveCache[LIVE_ROWS][50];
static int  liveRowNext = 0;

// Anything that paints over the console - entering a level, the pause menu
// closing - has to call this. The cache describes characters that are on the
// screen, and after a repaint they are not, so without it the block would go on
// skipping rows whose text has been wiped. '\1' is the marker because no row
// the block formats can begin with it.
static void liveBlockInvalidate(void)
{
	for (int i = 0; i < LIVE_ROWS; i++) { liveCache[i][0] = '\1'; liveCache[i][1] = '\0'; }
}

// Every row of the live block goes through here. Each row is padded out to the
// console's width and cut at it: a line that got shorter has to wipe the tail
// the last frame left behind, and a line that got longer must not wrap into the
// row below and shunt the whole block down one.
static void liveRow(const char* fmt, ...)
{
	char buf[80];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	char row[50];
	snprintf(row, sizeof(row), "%-49.49s", buf);

	int i = liveRowNext++;
	if (i >= LIVE_ROWS) return;
	if (!strcmp(row, liveCache[i])) return;

	memcpy(liveCache[i], row, sizeof(row));
	printf("\x1b[%d;1H%s", LIVE_ROW + i, row);
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
	// No "fit the piece it hangs off first" line any more - nothing waits on its
	// parent, so a filed piece is always ready for the stand.
	return "tap the amber ghost on the stand";
}

// Rows LIVE_ROW to 30: the manual page, then the state of the bench under it.
static void printLiveBlock(int fps, const memUsage* m)
{
	// The caption printStaticInfo left behind is the whole of the top screen in
	// photo mode, and the live rows would print straight over it.
	if (photoMode) return;
	// citro2d has the top screen whenever the illustrated page is up, tutorial
	// or plain card. Printing these rows would be a second writer on a
	// single-buffered screen.
	if (beginnerTopShowing()) return;
	liveRowNext = 0;

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

	// Row 30. It goes through liveRow like the rest now, which also settles the
	// old hazard here: rows are placed absolutely and none of them ends in a
	// newline, so the bottom row can no longer scroll the console and drag the
	// whole manual up by one. u32 is a long on this ABI, so both figures are
	// cast down for printf.
	liveRow("FPS %3d    App RAM %3u%% - %u of %u KB O3DS",
		fps, pctOf(m->appUsed, m->appTotal),
		(unsigned int)(m->appUsed / 1024), (unsigned int)(m->appTotal / 1024));
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

// Save and Load sit directly under Resume, above the two rows that lead to other
// pages and the one that leaves. The game already writes the card by itself - on
// the way out of a level and on the way out of the game - so these are not what
// makes progress survive; they are the two things a player cannot do without
// leaving the bench. Save is a checkpoint taken where they want it, and Load is
// the way back to it when the last twenty minutes went wrong.
static const char* const menuItems[] = { "Resume", "Save", "Load", "Controls", "Options", "Quit" };
#define MENU_ITEM_COUNT 6
#define MENU_ROW_SAVE 1
#define MENU_ROW_LOAD 2

// Shared row geometry: the pause menu and the Options page both lay out a
// list of selectable rows at the same indent and width, one directly below
// the other, so a hand-edit to one row list cannot silently drift out of
// line with the other's.
#define MENU_ROW_X       64.0f
#define MENU_ROW_WIDTH  272.0f
#define MENU_ROW_STEP    31.0f
// The pause menu's own step and row height. It used to share MENU_ROW_STEP with
// the Options page, and could while it had four rows; six of them at 31 apart
// runs off the bottom of a 240px screen and through the footer hint. Options
// still has three rows and keeps the roomier spacing - the two lists no longer
// line up with each other, which is the price of the pause menu holding twice as
// much as the settings page.
// Measured against a real 240px bottom screen rather than reasoned about: at
// top 76 / step 23 the sixth row ran from 191 to 212 and the Save/Load result
// line at 208 was drawn through it. These numbers put the last row's bottom at
// 202, the result line at 206, and the footer hint at its fixed 221.
#define MENU_MAIN_ROW_TOP    72.0f
#define MENU_MAIN_ROW_STEP   22.0f
#define MENU_MAIN_ROW_HEIGHT 20.0f
// Y of the Save/Load result line, directly under the last row.
#define MENU_MAIN_NOTE_Y    206.0f
// Y of the bottom hint line ("D-PAD MOVE...", "B BACK TO PAUSE MENU"), the
// same on all three console-menu pages so the hint sits in a fixed place
// while the page above it changes.
#define MENU_FOOTER_Y   221.0f

static menuPage menuOn     = PAGE_NONE;
static int      menuCursor = 0;

// The result of the last Save or Load, shown under the rows until the menu is
// closed. Cleared on open rather than on a timer: a message that disappears
// while the player is still reading it is worse than one that stays.
static char menuNote[48] = "";
// Load has been chosen once and is waiting for a second press to go through.
// Cleared by moving the cursor, by leaving the page, and by the load itself, so
// it can never be left armed for a press made minutes later about something
// else.
static bool loadArmed = false;

// Write the card now, from where the player is standing.
//
// saveCurrentLevel() first even though the frame loop has already run it this
// frame: it costs a memcpy, and it means this row means "the build as it is on
// screen" no matter what the frame loop is doing when the row is pressed.
static void menuSaveNow(void)
{
	saveCurrentLevel();
	if (progressSave()) snprintf(menuNote, sizeof(menuNote), "SAVED - %s", meshKitName());
	else                snprintf(menuNote, sizeof(menuNote), "SAVE FAILED - THE CARD REFUSED IT");
	loadArmed = false;
}

// Put the card's version back on the bench, discarding whatever has happened
// since it was written. Asks twice: the first press arms, the second goes.
//
// The whole array is re-read, not just this level, because that is what is on
// the card - a load that restored one level and left the other nineteen holding
// unsaved work would leave the game in a state no save file has ever described.
static void menuLoadNow(void)
{
	if (!loadArmed)
	{
		loadArmed = true;
		snprintf(menuNote, sizeof(menuNote), "LOAD LOSES UNSAVED WORK - A AGAIN TO GO ON");
		return;
	}
	loadArmed = false;

	int level = loadedLevel;
	if (!progressLoad())
	{
		// A read that got past the header and then failed zeroes the array on the
		// way out - see saveReadPath in save.c, where all-zero is deliberately the
		// fresh-install state. That is right at boot and wrong here, so the open
		// build is folded straight back into its slot and the player keeps what is
		// on their bench either way.
		saveCurrentLevel();
		snprintf(menuNote, sizeof(menuNote), "NO SAVE TO LOAD (%s) - BUILD KEPT", saveLastReason());
		return;
	}

	loadLevelState(level);
	frameWorkbenchCamera();
	benchSettle = BENCH_SETTLE_FRAMES;
	snprintf(menuNote, sizeof(menuNote), "LOADED - %s", meshKitName());
}

#if TEST_SAVELOAD_AUDIT
#include <sys/stat.h>
#include <unistd.h>

// Do the pause menu's Save and Load rows work on every kit in the game?
//
// The check those rows shipped with was one level deep: level 1, saved once,
// loaded once, read off the screen. The other nineteen kits were untested, and
// so were the two answers a hand test cannot reach - a Load with nothing on the
// card, and a Save the card refuses. This drives menuSaveNow()/menuLoadNow()
// themselves, per level, on a bench that has really been cut, filed and seated,
// and compares what comes back with what went in byte for byte rather than by
// eye.
static void runSaveLoadAudit(void)
{
	bool ok = true;
	u64 now = osGetTime();

	// Same protection the paint audit uses, for the same reason: this writes
	// save.bin more than twenty times with fabricated builds.
	bool stashed = auditCopyFile(SAVE_PATH, AUDIT_SAVE_BACKUP);
	printf("SL stashed real save: %s\n", stashed ? "yes" : "no file to stash");
	remove(SAVE_PATH);
	memset(levelBuilds, 0, sizeof(levelBuilds));

	// One character per level, so twenty results fit on one console line:
	// P pass, s save row refused, l load row found nothing, m came back changed.
	char marks[LEVEL_SAVE_COUNT + 1];
	// Static rather than automatic: a kit's worth of partState is thousands of
	// bytes and this runs on the main thread's stack.
	static partState expect[MESH_MAX_PARTS];
	int passed = 0;

	for (int level = 1; level <= LEVEL_SAVE_COUNT; level++)
	{
		sceneLoadKit(level);
		loadLevelState(level);
		boxOpen = 1.0f; boxOpening = false; runnerLift = 0.0f; selectedPart = -1;

		// Real work on the bench rather than a hand-filled struct: cut the whole
		// runner, file a part, seat a part - so what goes onto the card is the
		// shape playing produces, including the fields only those paths touch.
		int parts = meshPartCount();
		for (int i = 0; i < parts; ++i)
		{
			cutPart(i, now);
			updateCuts(now + CUT_ANIM_MS + 1);
		}
		if (parts > 0) fileStroke(0, FILE_TRAVEL_PX);
		if (meshSocketCount() > 0)
		{
			selectedPart = meshSockets()[0].part;
			seatPart(0, now);
			updateCuts(now + CUT_ANIM_MS + 1);
		}

		memcpy(expect, partStates, sizeof(expect));
		int expCut = cutCount, expFiled = filedCount, expBuilt = builtCount;
		int expRunner = currentRunner;

		menuSaveNow();
		bool saveOk = strncmp(menuNote, "SAVED", 5) == 0;

		// Wipe every trace from RAM - the open bench and the whole array behind
		// it - so anything that comes back has come back off the card.
		memset(partStates, 0, sizeof(partStates));
		memset(levelBuilds, 0, sizeof(levelBuilds));
		cutCount = filedCount = builtCount = 0; currentRunner = 0;

		menuLoadNow();   // first press arms
		menuLoadNow();   // second press goes
		bool loadOk = strncmp(menuNote, "LOADED", 6) == 0;
		bool matchOk = memcmp(partStates, expect, sizeof(expect)) == 0 &&
			cutCount == expCut && filedCount == expFiled &&
			builtCount == expBuilt && currentRunner == expRunner;

		marks[level - 1] = saveOk ? (loadOk ? (matchOk ? 'P' : 'm') : 'l') : 's';
		if (saveOk && loadOk && matchOk) passed++;
		else { ok = false; printf("SL L%02d %s\n", level, menuNote); }
	}
	marks[LEVEL_SAVE_COUNT] = '\0';
	printf("SL %s\n", marks);
	printf("SL round trip %d/%d\n", passed, LEVEL_SAVE_COUNT);

	// Every level saved above is still on the card at the end, not just the last
	// one: each Save writes the whole array, so the twentieth load must bring
	// back all twenty.
	int visited = 0;
	for (int l = 0; l < LEVEL_SAVE_COUNT; l++) if (levelBuilds[l].visited) visited++;
	printf("SL all levels on card %d/%d %s\n", visited, LEVEL_SAVE_COUNT,
		visited == LEVEL_SAVE_COUNT ? "OK" : "FAIL");
	if (visited != LEVEL_SAVE_COUNT) ok = false;

	// Load with nothing on the card. The row has to say so AND leave the bench
	// standing - the failure being guarded against is save.c zeroing the array
	// on a failed read and the player watching their build disappear.
	{
		remove(SAVE_PATH);
		static partState keep[MESH_MAX_PARTS];
		memcpy(keep, partStates, sizeof(keep));
		int keepCut = cutCount;
		menuLoadNow(); menuLoadNow();
		bool emptyOk = strncmp(menuNote, "NO SAVE TO LOAD", 15) == 0 &&
			memcmp(partStates, keep, sizeof(keep)) == 0 && cutCount == keepCut;
		printf("SL empty card %s\n", emptyOk ? "OK" : "FAIL");
		if (!emptyOk) { ok = false; printf("SL   note: %s\n", menuNote); }
	}

	// A save the card will not take. Nothing in the game can unplug an SD card,
	// so the block is a directory standing where save.bin goes: fopen(..,"wb")
	// cannot open it, which is the same "cannot open for write" saveWritePath
	// reports for a full, missing or write-protected card.
	{
		remove(SAVE_PATH);
		bool blocked = mkdir(SAVE_PATH, 0777) == 0;
		menuSaveNow();
		bool refusedOk = blocked && strncmp(menuNote, "SAVE FAILED", 11) == 0;
		printf("SL refused save %s\n",
			refusedOk ? "OK" : (blocked ? "FAIL" : "FAIL - could not block the path"));
		if (!refusedOk) { ok = false; printf("SL   note: %s\n", menuNote); }
		if (blocked) rmdir(SAVE_PATH);
	}

	// A card that stops taking bytes half way through the write - the failure the
	// directory trick above cannot reach, because that one never gets past fopen.
	// Two things have to hold afterwards: the row says the save failed, and the
	// torn file that is now sitting on the card cannot come back as half a build.
	// save.c zeroes the blob on a read that fails after the header, so what is
	// being proved on the load side is that menuLoadNow puts the bench back rather
	// than handing the player twenty empty kits.
	{
		remove(SAVE_PATH);
		sceneLoadKit(1);
		loadLevelState(1);
		boxOpen = 1.0f; boxOpening = false; selectedPart = -1;
		u64 t = osGetTime();
		if (meshPartCount() > 0) { cutPart(0, t); updateCuts(t + CUT_ANIM_MS + 1); }

		static partState keep[MESH_MAX_PARTS];
		memcpy(keep, partStates, sizeof(keep));
		int keepCut = cutCount;

		saveTestTearNextWrite = true;
		menuSaveNow();
		bool tornSaveOk = strncmp(menuNote, "SAVE FAILED", 11) == 0;

		menuLoadNow(); menuLoadNow();
		bool tornLoadOk = strncmp(menuNote, "NO SAVE TO LOAD", 15) == 0 &&
			memcmp(partStates, keep, sizeof(keep)) == 0 && cutCount == keepCut;

		printf("SL torn write %s, bench kept %s\n",
			tornSaveOk ? "OK" : "FAIL", tornLoadOk ? "OK" : "FAIL");
		if (!tornSaveOk || !tornLoadOk) { ok = false; printf("SL   note: %s\n", menuNote); }
	}

	printf("SAVELOAD AUDIT %s\n", ok ? "PASS" : "FAIL");

	// Leave the card as found, exactly as the paint audit does.
	if (stashed)
	{
		bool restored = auditCopyFile(AUDIT_SAVE_BACKUP, SAVE_PATH);
		if (restored) remove(AUDIT_SAVE_BACKUP);
		printf("SL restored real save: %s\n",
			restored ? "OK" : "FAIL - copy kept at " AUDIT_SAVE_BACKUP);
		progressLoad();
	}
	else
	{
		memset(levelBuilds, 0, sizeof(levelBuilds));
		remove(SAVE_PATH);
	}
	sceneLoadKit(1);
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = runnerLift = 0.0f; boxOpening = false; selectedPart = -1;
	menuNote[0] = '\0'; loadArmed = false;
	frameWorkbenchCamera();
}
#else
static void runSaveLoadAudit(void) { }
#endif

// Options has three rows now rather than the one it opened with, so it needs a
// cursor of its own. Kept separate from menuCursor so backing out of Options and
// into it again does not move the pause menu's selection under the player.
static const char* const optionRows[] = { "MASTER VOLUME", "SHOW TUTORIAL", "SWAP START / SELECT" };
#define OPTION_ROW_COUNT 3
static int optionsCursor = 0;

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
	C2D_DrawRectSolid(BEGINNER_PANEL_X, 66.0f, 0.0f, BEGINNER_PANEL_WIDTH, BEGINNER_PANEL_BORDER, PAPER_BLUE);

	if (menuOn == PAGE_MAIN)
	{
		for (int i = 0; i < MENU_ITEM_COUNT; i++)
		{
			float y = MENU_MAIN_ROW_TOP + i * MENU_MAIN_ROW_STEP;
			bool selected = i == menuCursor;
			C2D_DrawRectSolid(MENU_ROW_X, y, 0.0f, MENU_ROW_WIDTH, MENU_MAIN_ROW_HEIGHT,
				selected ? C2D_Color32(0xE8, 0xF1, 0xF8, 0xFF) : PAPER_WHITE);
			C2D_DrawRectSolid(MENU_ROW_X, y, 0.0f, 4.0f, MENU_MAIN_ROW_HEIGHT, selected ? PAPER_BLUE : PAPER_LINE);
			C2D_DrawRectSolid(MENU_ROW_X, y, 0.0f, MENU_ROW_WIDTH, 2.0f, selected ? PAPER_BLUE : PAPER_LINE);
			beginnerLabel(menuItems[i], 82.0f, y + 4.0f, 0.42f, PAPER_INK);
			// Load asks twice. It throws away everything built since the last save,
			// and one stray press of A on a menu the player opened to do something
			// else should not be able to do that. The row says so itself rather than
			// putting a warning in the footer, because the footer is not where
			// somebody about to press A is looking.
			const char* tag = selected ? "SELECT" : NULL;
			if (i == MENU_ROW_LOAD && loadArmed) tag = "A AGAIN";
			if (tag) beginnerLabel(tag, i == MENU_ROW_LOAD && loadArmed ? 248.0f : 258.0f,
				y + 5.0f, 0.27f, PAPER_BLUE);
		}
		// What the last Save or Load actually did, on the page the button that did
		// it lives on. Without it the only difference a successful save makes is
		// nothing at all happening, which reads exactly like a row that is not
		// wired up.
		if (menuNote[0]) beginnerLabel(menuNote, 66.0f, MENU_MAIN_NOTE_Y, 0.30f, PAPER_INK);
		beginnerLabel("D-PAD  MOVE     A  SELECT     B  RESUME", 62.0f, MENU_FOOTER_Y, 0.31f, PAPER_BLUE);
	}
	else if (menuOn == PAGE_CONTROLS)
	{
		// Eight rows, in the same order the console manual lists them. Three of
		// these - the manual paging, the view reset and closing the game - used
		// to be missing here, so the page told a player five of the eight
		// buttons that do something. The rows sit closer together to make room.
		const char* action[] = { "TAP", "RUB", "DRAG", "D-PAD", "L / R", "A", "SELECT", "START" };
		const char* meaning[] = { "choose, snip, or fit", "file a loose part smooth", "turn the workbench", "page the manual", "zoom the view", "reset the view", "pause the build", "close the game" };
		for (int i = 0; i < 8; i++)
		{
			float y = 76.0f + i * 17.0f;
			C2D_DrawRectSolid(63.0f, y, 0.0f, 77.0f, 15.0f, C2D_Color32(0xE8, 0xF1, 0xF8, 0xFF));
			// The last two rows are the pause and close bindings, and Options can
			// trade them. Only the button name moves; what it does stays put.
			beginnerLabel(action[controlKeyRow(i, 8)], 76.0f, y + 2.0f, 0.32f, PAPER_BLUE);
			beginnerLabel(meaning[i], 153.0f, y + 2.0f, 0.32f, PAPER_INK);
		}
		beginnerLabel("B  BACK TO PAUSE MENU", 111.0f, MENU_FOOTER_Y, 0.34f, PAPER_BLUE);
	}
	else
	{
		char ram[18];
		snprintf(ram, sizeof(ram), STR(STR_C_STATUS_RAM), memoryStatusAppPercent());
		// A dedicated low-poly paper panel makes Options read as a real page,
		// rather than a few values floating on the guide background.
		C2D_DrawRectSolid(54.0f, 76.0f, 0.0f, 292.0f, 125.0f, C2D_Color32(0xF4,0xF7,0xF1,0xFF));
		C2D_DrawRectSolid(54.0f, 76.0f, 0.0f, 292.0f, 3.0f, PAPER_BLUE);
		C2D_DrawRectSolid(54.0f, 198.0f, 0.0f, 292.0f, 3.0f, PAPER_BLUE);
		C2D_DrawRectSolid(245.0f, 29.0f, 0.0f, 88.0f, 22.0f, C2D_Color32(0xE8,0xF1,0xF8,0xFF));
		beginnerLabel(ram, 255.0f, 32.0f, 0.32f, PAPER_BLUE);
		beginnerLabel("SETTINGS", 76.0f, 84.0f, 0.32f, PAPER_BLUE);

		// Three rows on one page, drawn the same way the pause menu draws its
		// items so the cursor means the same thing in both places. Each row shows
		// its value on the right, because a toggle that only reads "SHOW TUTORIAL"
		// tells a player what it is about and not what it is currently doing.
		for (int i = 0; i < OPTION_ROW_COUNT; i++)
		{
			float y = 102.0f + i * MENU_ROW_STEP;
			bool selected = i == optionsCursor;
			C2D_DrawRectSolid(MENU_ROW_X, y, 0.0f, MENU_ROW_WIDTH, 26.0f,
				selected ? C2D_Color32(0xE8, 0xF1, 0xF8, 0xFF) : PAPER_WHITE);
			C2D_DrawRectSolid(MENU_ROW_X, y, 0.0f, 4.0f, 26.0f, selected ? PAPER_BLUE : PAPER_LINE);
			beginnerLabel(optionRows[i], 78.0f, y + 6.0f, 0.38f, PAPER_INK);

			if (i == 0)
			{
				// The bar is the readout; the percentage beside it is for the
				// player who wants the number rather than the shape.
				int lit = settingsVolume() / VOLUME_STEP;
				for (int c = 0; c < VOLUME_CELLS; c++)
					C2D_DrawRectSolid(190.0f + c * 11.0f, y + 7.0f, 0.0f, 8.0f, 12.0f,
						c < lit ? PAPER_BLUE : PAPER_LINE);
				char volume[12]; snprintf(volume, sizeof(volume), "%d%%", settingsVolume());
				beginnerLabel(volume, 305.0f, y + 6.0f, 0.38f, PAPER_BLUE);
			}
			else
			{
				bool on = i == 1 ? settingsShowTutorial() : settingsSwapStartSelect();
				beginnerLabel(on ? "ON" : "OFF", 296.0f, y + 6.0f, 0.38f, PAPER_BLUE);
			}
		}

		beginnerLabel("D-PAD  MOVE       LEFT / RIGHT  CHANGE", 76.0f, 204.0f, 0.31f, PAPER_INK);
		beginnerLabel("B  BACK TO PAUSE MENU", 111.0f, MENU_FOOTER_Y, 0.34f, PAPER_BLUE);
	}

	C2D_Flush();
}

// The frame's key words used to pass through applyStartSelectSwap here, which
// traded those two bits and nothing else. Item 37 replaced it with the general
// case: controlsTranslate in controls.c maps all twelve remappable buttons onto
// the twelve logical actions, and the START/SELECT swap is now one particular
// remap rather than a second, separate mechanism that could disagree with it.
//
// The reason for translating once, here, rather than at each of the places that
// go on to test a bit is unchanged and is the whole point of the design: every
// downstream test still reads the same KEY_A / KEY_B / KEY_SELECT it always
// did, so a button check added later cannot forget that remapping exists.

// Runs the menu for one frame. Returns true if Quit was chosen. There is no
// redraw to schedule here: the paused top screen is a citro2d paper sheet that
// the render loop repaints every frame, so this only has to move the state on.
static bool menuInput(u32 kDown, bool isNew3DS)
{
	// One click for every press this menu acts on. Done once at the top rather
	// than on each branch below, because a menu that is silent on some of its
	// buttons reads as a menu that missed the press.
	if (kDown & (KEY_A | KEY_B | KEY_SELECT | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT))
		audioPlay(SND_UI);

	if (menuOn == PAGE_OPTIONS)
	{
		// The one page with something on it to change. settingsVolumeStep does
		// the clamping, and it is the same level the front end's Options page
		// sets - there is only one of it.
		if (kDown & KEY_DUP)
			optionsCursor = (optionsCursor + OPTION_ROW_COUNT - 1) % OPTION_ROW_COUNT;
		if (kDown & KEY_DDOWN)
			optionsCursor = (optionsCursor + 1) % OPTION_ROW_COUNT;

		// A toggles as well as left/right, because a two-state row has nothing
		// for a direction to mean. It no longer backs out of the page: that is
		// what B is for, and what the footer has always said.
		int step = (kDown & KEY_DRIGHT) ? 1 : (kDown & KEY_DLEFT) ? -1 : 0;
		if (step || (kDown & KEY_A))
		{
			if (optionsCursor == 0) { if (step) settingsVolumeStep(step * VOLUME_STEP); }
			else if (optionsCursor == 1) settingsSetShowTutorial(!settingsShowTutorial());
			else settingsSetSwapStartSelect(!settingsSwapStartSelect());
		}
		if (kDown & (KEY_B | KEY_SELECT)) menuOn = PAGE_MAIN;
	}
	else if (menuOn != PAGE_MAIN)
	{
		// The Controls page only reads; anything that means "done" goes back.
		if (kDown & (KEY_A | KEY_B | KEY_SELECT)) menuOn = PAGE_MAIN;
	}
	else
	{
		if (kDown & (KEY_DUP | KEY_DDOWN))
		{
			int step = (kDown & KEY_DUP) ? MENU_ITEM_COUNT - 1 : 1;
			menuCursor = (menuCursor + step) % MENU_ITEM_COUNT;
			// Moving off the row disarms it. Holding the confirm across a cursor
			// move would mean a player who changed their mind, went elsewhere and
			// came back could wipe their build with what they thought was a first
			// press.
			loadArmed = false;
		}
		if (kDown & (KEY_B | KEY_SELECT))
		{
			menuOn = PAGE_NONE;
			loadArmed = false;
			queueTopRepaint(TOP_REPAINT_LEVEL);
		}
		else if (kDown & KEY_A)
		{
			switch (menuCursor)
			{
				case 0: menuOn = PAGE_NONE; queueTopRepaint(TOP_REPAINT_LEVEL); break;
				case MENU_ROW_SAVE: menuSaveNow(); break;
				case MENU_ROW_LOAD: menuLoadNow(); break;
				case 3: menuOn = PAGE_CONTROLS; break;
				case 4: menuOn = PAGE_OPTIONS; optionsCursor = 0; break;
				case 5: return true;
			}
		}
	}

	return false;
}

// Puts whatever startup failure was just printed in front of the player, then
// holds it long enough to read.
//
// Console output only lands in the top screen's framebuffer - something has to
// present it. Every one of these failures happens before the first frame is
// ever drawn, so without this the app prints its reason and exits on a black
// screen, which is exactly what it was trying not to do. Booting the failure
// path in the emulator is how that was found: the message was being written and
// never shown.
static void showConsoleAndPause(void)
{
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
	// Roughly three seconds at 60Hz. Long enough to read two lines, short enough
	// that a player who cannot act on it is not stuck staring at it.
	for (int f = 0; f < 180; f++) gspWaitForVBlank();
}

// --- main() split (item 43) -------------------------------------------
//
// main() used to be one function running from boot to shutdown. It is split
// here into init / one-frame update / one-frame render / shutdown, called in
// that shape from main() itself below.
//
// The frame loop's own locals (lastTouch, tapPending, modelView, the fps/mem
// counters and so on) move out to file scope so all four functions can see
// them, the same way the rest of the session state already living at file
// scope here does (camDist, angleX, focus, menuOn, selectedPart, hoverPart).
static bool isNew3DS;
static touchPosition lastTouch;
static u64  touchStartMs;
static int  touchMoved;
static bool tapPending;
static bool filingStroke;
static int  lastTapX = -1, lastTapY = -1;
static u64  fpsTick, memTick;
static int  fpsFrames, fps;
static memUsage mem;
// Kept across frames so the stylus can be tested against the view that was
// actually on screen when it landed, before this frame moves anything.
static C3D_Mtx modelView;
// Set in gameUpdateFrame and read back in gameRenderFrame: showTitle can
// change mid-frame (the front end's own Quit returns here), so render has to
// see the same value update just decided rather than recomputing it.
static bool showTitle;
static bool screenshotWanted;

static int  gameInit(void);
static void handleTap(u64 now);
static bool gameUpdateFrame(void);
static void gameRenderFrame(void);
static void gameShutdown(void);

static int gameInit(void)
{
	// Measure VRAM before the framebuffers take a bite out of it.
	memoryStatusMeasureVram();

	// Initialize graphics. The console takes the top screen; the bottom screen
	// belongs to the 3D view so the stylus lands on the model itself.
	gfxInitDefault();
	gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
	consoleInit(GFX_TOP, NULL);
	// The top screen changes owner between the legacy console and Level 1's
	// rendered sheet. One stable framebuffer prevents either renderer from
	// presenting the other's stale back buffer.
	gfxSetDoubleBuffering(GFX_TOP, false);

	// Sound is optional by design: a console with no dumped DSP firmware plays
	// nothing and everything else works exactly as before. Said out loud on the
	// console either way, so a silent bench is explained rather than a mystery.
	if (!audioInit()) printf("AUDIO OFF: %s\n", audioReason());

	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

	// Item 34: build/upload the shared atlas before the first sceneBind()
	// (inside sceneInit(), below) tries to bind it. A failure here is not
	// fatal - textureAtlasGet() then returns NULL, sceneBind() skips the
	// C3D_TexBind, and TexEnv is still sourcing GPU_TEXTURE0, which reads
	// back the border colour (default transparent black) - see the
	// texturing report for why this is left as a known, honestly-reported
	// gap rather than papered over with a REPLACE fallback path.
	if (!textureAtlasInit()) printf("TEXTURE ATLAS INIT FAILED\n");

	benchTarget = C3D_RenderTargetCreate(BOTTOM_H, BOTTOM_W, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	if (!benchTarget)
	{
		// Nothing below can run without it: every frame clears and draws on this
		// target, and C3D_FrameDrawOn would be handed NULL. Better to say so on
		// the console that is already up than to fault on the first frame.
		printf("BOTTOM RENDER TARGET FAILED\n");
		C3D_Fini(); gfxExit();
		return 1;
	}
	C3D_RenderTargetSetOutput(benchTarget, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

	if (!sceneInit())
	{
		// The bench is the app: with no vertex buffer there is no kit to cut, so
		// stopping here on the console that is already up says why, rather than
		// leaving the player on a blank bottom screen with working menus.
		printf("SCENE INIT FAILED\n");
		showConsoleAndPause();
		sceneExit(); C3D_Fini(); gfxExit();
		return 1;
	}
	frameWorkbenchCamera();

	// citro2d draws the front end - it is the only thing here with a font. 1024
	// objects rather than the default 4096, because that number sizes a linear
	// vertex buffer and the busiest page is the Controls list at roughly thirty
	// quads and three hundred glyphs.
	C2D_Init(1024);
	beginnerTarget = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
	if (!beginnerTarget)
	{
		// Level 1's sheet and every paused menu are drawn on this, and all three
		// draw paths clear it without asking. There is no console fallback for
		// the menus, so carrying on would only defer the fault.
		printf("TOP RENDER TARGET FAILED\n");
		C2D_Fini(); sceneExit(); C3D_Fini(); gfxExit();
		return 1;
	}
	C3D_RenderTargetSetOutput(beginnerTarget, GFX_TOP, GFX_LEFT, TOP_TRANSFER_FLAGS);
	beginnerText = C2D_TextBufNew(4096);
	// The updater brings up its own services - sockets, AM and the RomFs its
	// certificate bundle lives in. A console with no network, or a 3DSX build
	// with no reach into AM, fails here and the Options page greys the button
	// out; nothing else in the game cares either way, so this is not fatal.
	if (!updaterInit()) printf("UPDATER OFF: no network or title services\n");

	titleInit();

	// Read before the audits, because the straight-to-level test builds below
	// enter a level for real and enterLevel prints the manual header, which
	// takes it. It was read further down when nothing up here needed it.
	APT_CheckNew3DS(&isNew3DS);

	// Before anything enters a level, so a test build or the front end both find
	// the card's progress already in place rather than an empty array. Settings
	// come first for the same reason: the Options page caches the volume it last
	// printed, and it must not cache the default over a level read off the card.
	settingsLoad();
	progressLoad();
	// The grid is drawn before the player can enter anything, so the ticks off
	// the card have to be in place before the first frame, not on the way back
	// from a level.
	publishCompletion();

	runCameraIdleAudit();
	runCameraPanAudit();
	runCeilingAudit();
	runLevel1WorkspaceAudit();
	runPaintAudit();
	runHintAudit();
	runSaveLoadAudit();
	#if TEST_SAVELOAD_AUDIT
	// Same hold the other console-output audits use: the frame loop paints over
	// the top screen within one frame, and this output has to survive long
	// enough to be captured.
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
	for (int f = 0; f < 900; f++) gspWaitForVBlank(); // ~15s at 60Hz
	#endif
	#if TEST_HINT_AUDIT
	// Same hold TEST_PAINT_AUDIT uses: the frame loop paints over the top screen
	// immediately, and this output has to survive long enough to be captured.
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
	for (int f = 0; f < 900; f++) gspWaitForVBlank(); // ~15s at 60Hz
	#endif
	#if TEST_PAINT_AUDIT
	// The normal frame loop draws over the top screen (options/manual/etc)
	// within the first frame, same as any other console-output audit - hold
	// what runPaintAudit() just printed on screen long enough to screenshot
	// it. Longer than showConsoleAndPause()'s 3s (that one is sized for a
	// player reading a real failure message, not for lining up an external
	// capture against emulator boot time), and only this probe's own hold -
	// showConsoleAndPause() itself is untouched.
	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();
	for (int f = 0; f < 900; f++) gspWaitForVBlank(); // ~15s at 60Hz
	#endif
	#if TEST_AUDIT_ALL_KITS
	meshAuditAllKits();
	runGameplayAuditAllKits();
	#endif
	#if TEST_COLLISION_AUDIT
	meshCollisionAuditAllKits();
	#endif
	#if TEST_CAPTURE_LEVEL > 0
	enterLevel(TEST_CAPTURE_LEVEL, isNew3DS);
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
	#if TEST_POSE_PART > 0
	// sceneRender reads poseDeg unconditionally, so this alone is enough to
	// capture a posed joint. Deliberately NOT setting selectedPart: that also
	// drives updateFocus()'s selection-follow camera, which - since the
	// TEST_CAPTURE_ASSEMBLED block above marks every part seated=false to
	// dodge Azahar's completion-update 3D skip - resolves to the loose-part
	// rest spot instead of the stand, aiming the still at a wall corner.
	poseMode = true;
	partStates[TEST_POSE_PART - 1].poseDeg = (float)(TEST_POSE_DEG);
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

	// The front end writes its own top screen from here on. The workbench does
	// not get the console until Play is tapped, so nothing about the kit - no
	// manual page, no part name - can appear behind a menu.
	#if !TEST_AUDIT_ALL_KITS && !TEST_CAMERA_IDLE_AUDIT && !TEST_CAMERA_PAN_AUDIT && !TEST_LEVEL1_WORKSPACE_AUDIT && !TEST_CEILING_AUDIT && !TEST_COLLISION_AUDIT
	titlePrintTop(isNew3DS);
	#endif

	fpsTick = osGetTime();
	memTick = fpsTick;
	readMemUsage(&mem);

	buildModelView(&modelView);

	return 0;
}

// Extracted from the frame loop (item 45): resolves whatever the stylus tap
// detected this frame landed on - kit box, ghost socket, gate, part or
// socket - against the view that was on screen when the tap was read.
// A no-op when nothing is actually pending, so gameUpdateFrame can call it
// unconditionally.
static void handleTap(u64 now)
{
	if (!tapPending) return;

	lastTapX = (int)lastTouch.px;
	lastTapY = (int)lastTouch.py;

	// Posing outranks everything else a tap could mean. It can only be on
	// once the kit is fully built (see gameUpdateFrame), by which point
	// cutting, filing and fitting are all finished with, so there is nothing
	// below this branch a pose-mode tap should ever be allowed to reach -
	// in particular never the second-tap-unseats rule, which would let
	// posing quietly take the model apart.
	if (poseMode)
	{
		int part = pickPart(&modelView, lastTapX, lastTapY);
		if (part >= 0 && partStates[part].seated && meshJointForPart(part))
		{
			selectedPart = part;
			snprintf(seatMsg, sizeof(seatMsg), STR(STR_C_STATUS_POSE_HINT), meshParts()[part].name);
		}
		else
		{
			selectedPart = -1;
			snprintf(seatMsg, sizeof(seatMsg), "%s", STR(STR_C_STATUS_TAP_MOVES));
		}
		tapPending = false;
		return;
	}

	// The ghost outranks everything. When a part is snipped, filed and
	// waiting, the game has drawn the player a target and anywhere on or
	// near it means "put it there" - even over a part already fitted next
	// to it, which the part-before-socket rule below would otherwise
	// re-select instead. It can only fire for the part in hand, so it
	// never takes a tap that was meant for anything else.
	if (boxOpen < 1.0f)
	{
		if (pickKitBox(&modelView, lastTapX, lastTapY)) { boxOpening = true; audioPlay(SND_BOX); frameRunnerCamera(); }
		else { selectedPart = -1; describeSelection(); }
		tapPending = false;
	}

	int ghost = tapPending ? pickGhostSocket(&modelView, lastTapX, lastTapY) : -1;
	C3D_Mtx runnerView;
	buildRunnerView(&runnerView, &modelView);
	int gate  = (ghost >= 0) ? -1 : (tapPending ? pickGate(&runnerView, lastTapX, lastTapY) : -1);

	if (tapPending && ghost >= 0)
	{
		audioPlay(seatPart(ghost, now) ? SND_CLICK : SND_REFUSE);
	}
	// Gates win ties: a tap that could be either is a cut, because that
	// is the only thing you can do to a part still on the frame.
	else if (tapPending && gate >= 0)
	{
		cutPart(gate, now);
		audioPlay(SND_SNIP);
		selectedPart = gate;
		describeSelection();
	}
	else if (tapPending)
	{
		// Parts before sockets: a fitted part still has its socket around
		// it, and tapping it should look at the part rather than answer
		// that the socket is full.
		//
		// With one exception, and it is the one that used to lose work.
		// When a part is in hand, a tap that lands on both a socket and a
		// part already fitted beside it means "put it here" - the player
		// is trying to set the piece down, not admire its neighbour.
		// Re-selecting the neighbour silently drops what they were
		// holding, and the piece has to be found and picked up again. So
		// while something is in hand, a socket beats an already-fitted
		// part. A loose part still wins, because tapping one of those is
		// how you change your mind about which piece you are holding.
		//
		// seatPart refuses safely - it prints why and leaves the
		// selection alone - so aiming at the wrong socket costs nothing.
		int part = pickPart(&modelView, lastTapX, lastTapY);
		int sock = pickSocket(&modelView, lastTapX, lastTapY);
		bool inHand = selectedPart >= 0 &&
			partStates[selectedPart].cut &&
			!partStates[selectedPart].seated;

		// The sounds hang off the player's taps rather than off the
		// functions themselves, so the audits below - which cut and seat
		// several hundred parts before the first frame - stay silent.
		if (sock >= 0 && inHand && (part < 0 || partStates[part].seated))
		{
			audioPlay(seatPart(sock, now) ? SND_CLICK : SND_REFUSE);
		}
		else if (part >= 0)
		{
			// Taking a part back off is a second tap on a part that is
			// already selected, never the first. A single tap on a fitted
			// piece is how you look at it, and a build that came apart
			// because the player tapped to read a label would be worse
			// than the mis-fit this is here to undo. The first tap selects
			// and the status line offers the second one.
			//
			// Not while something is in hand: that tap means "put this
			// down", which the branch above has already dealt with.
			if (!inHand && part == selectedPart && partStates[part].seated)
			{
				audioPlay(unseatPart(part, now) ? SND_UNSEAT : SND_REFUSE);
			}
			else
			{
				selectedPart = part;
				describeSelection();
			}
		}
		else if (sock >= 0)
		{
			audioPlay(seatPart(sock, now) ? SND_CLICK : SND_REFUSE);
		}
		else
		{
			selectedPart = -1;
			describeSelection();
		}
	}

	tapPending = false;
}

// One iteration of the frame loop's input/state half: reads pads and stylus,
// drives the front end and pause menu, moves the camera, resolves any tap,
// and refreshes the fps/mem readout. Returns true when the game should
// close (START, or Quit from the front end's own title page).
#if TEST_HINT_TOUR
// ---------------------------------------------------------------------------
// The dot tour
//
// TEST_HINT_AUDIT answers "does the dot land on the right piece" in numbers, by
// feeding the dot's own position back through the pick functions. It cannot
// answer "is the dot on screen", because it never renders anything - which is
// exactly how a dot that was being switched off entirely got a clean twenty-kit
// pass. This walks the same eighty states in front of the camera and writes the
// bottom screen out at each one, so the answer is a picture rather than a claim.
//
// One state per two frames: the first sets it up, the second asks for the shot,
// because screenshotCapture reads the framebuffer after C3D_FrameEnd and a state
// applied this frame is only in there once this frame has been presented.
// ---------------------------------------------------------------------------
#define TOUR_STEPS 4

static void hintTourSetup(int level, int step)
{
	u64 now = osGetTime();

	// The tutorial sheet is what the dot is drawn under, and the switch that
	// governs it is persisted - a console whose save has it off would produce
	// eighty empty shots and look like a fault in the dot.
	settingsSetShowTutorial(true);

	// The real level-entry path, so the kit, the saved build and the opening
	// camera are all set the way a player tapping the tile would get them.
	enterLevel(level, isNew3DS);

	// Then wound straight back to a kit still in its box, whatever the save had
	// to say: the tour is about the four steps, not about where this console
	// happens to have got to.
	memset(partStates, 0, sizeof(partStates));
	cutCount = filedCount = builtCount = currentRunner = 0;
	boxOpen = 0.0f; boxOpening = false; runnerLift = 0.0f; selectedPart = -1;
	frameWorkbenchCamera();

	if (step >= 1) { boxOpen = 1.0f; frameRunnerCamera(); }
	if (step >= 2)
	{
		// The whole kit off the frame, because that is the only state the sheet
		// ever shows the filing page in - beginnerAction will not leave step 1
		// until cutCount reaches the part count. updateCuts also hands the camera
		// over to the assembly framing here, same as it does in play.
		for (int i = 0; i < meshPartCount(); i++)
		{
			currentRunner = meshParts()[i].runner;
			cutPart(i, now);
		}
		// Left on the LAST frame, not wound back to the first. A kit with two
		// moulded frames in the box (kits 14 up) hands the player the second one
		// the moment the first is empty, and that hand-over clears the held piece
		// and swings the camera back to the frame - so a tour that cut everything
		// and then claimed to be on frame 1 got the hand-over run on it a frame
		// later, ending up looking at an empty frame with the loose pile off the
		// side of the screen. Real play only ever reaches "all cut" on the last
		// frame, which is what this is.
		currentRunner = meshKitRunnerCount() - 1;
		updateCuts(now + CUT_ANIM_MS + 1);
		selectedPart = 0;
	}
	if (step >= 3) fileStroke(0, FILE_TRAVEL_PX);

	// The focus pivot eases towards its target a frame at a time. Run flat out
	// here so the shot is of a settled view rather than of one still sliding.
	for (int f = 0; f < 90; f++) updateFocus();

	// A line per shot, next to the shots, so a picture with no dot in it can be
	// told apart from a picture whose dot is somewhere the frame does not reach.
	{
		C3D_Mtx mv;
		float hx = 0.0f, hy = 0.0f;
		buildModelView(&mv);
		bool got = hintPoint(&mv, beginnerAction(), &hx, &hy);
		FILE* log = fopen("sdmc:/modelkit/tour.txt", "a");
		if (log)
		{
			fprintf(log, "k%-2d s%d act%d parts%d cut%d filed%d dot%d %.0f,%.0f\n",
				level, step, beginnerAction(), meshPartCount(), cutCount, filedCount,
				got ? 1 : 0, hx, hy);
			fclose(log);
		}
	}
}

// True when the last shot has been written and the game should close.
static bool hintTourFrame(void)
{
	static int index = 0;   // 0 .. MESH_KIT_LEVELS*TOUR_STEPS - 1
	static bool shoot = false;

	if (index >= MESH_KIT_LEVELS * TOUR_STEPS) return true;

	if (!shoot)
	{
		hintTourSetup(index / TOUR_STEPS + 1, index % TOUR_STEPS);
		shoot = true;
	}
	else
	{
		screenshotWanted = true;
		shoot = false;
		index++;
	}
	return false;
}
#endif

#if TEST_SAVELOAD_TOUR
// ---------------------------------------------------------------------------
// The Save/Load tour
//
// TEST_SAVELOAD_AUDIT answers "do the rows work on all twenty kits" in numbers.
// It cannot answer "does what the player reads fit on the sheet", because it
// renders nothing - and the note carries a kit name, so it is the one part of
// the pause menu whose width is different on every level. This walks the twenty
// levels with the menu open and a real note under it, two seconds each, so the
// answer is twenty pictures rather than a calculation about string lengths.
//
// Two seconds because the capture running outside takes a frame a second: any
// window shorter than that could fall between two shots and leave a level with
// no picture, which would read as a level that was checked.
// ---------------------------------------------------------------------------
#define SL_TOUR_HOLD_FRAMES 120   // ~2s at 60Hz

static bool saveLoadTourFrame(void)
{
	static int  level  = 0;   // 0 = nothing set up yet
	static int  frames = 0;
	static bool stashed = false;

	if (level == 0)
	{
		// This writes the card once per level, so the same protection the audits
		// use goes on before the first one.
		stashed = auditCopyFile(SAVE_PATH, AUDIT_SAVE_BACKUP);
		level = 1;
	}

	if (frames == 0)
	{
		// The real entry path, so the kit, its name and the bench are set the way
		// tapping the tile would set them.
		enterLevel(level, isNew3DS);
		u64 now = osGetTime();
		boxOpen = 1.0f; boxOpening = false; runnerLift = 0.0f; selectedPart = -1;
		if (meshPartCount() > 0) { cutPart(0, now); updateCuts(now + CUT_ANIM_MS + 1); }

		// The note is produced by the rows themselves rather than typed out here:
		// a hand-written copy of the format string would prove nothing about what
		// the game actually prints. Load last, because "LOADED - <kit>" is one
		// character longer than "SAVED - <kit>" and it is the long one that has to
		// fit.
		menuOn = PAGE_MAIN;
		menuCursor = MENU_ROW_LOAD;
		menuSaveNow();
		menuLoadNow();   // arms
		menuLoadNow();   // goes
		menuOn = PAGE_MAIN;
	}

	frames++;
	if (frames >= SL_TOUR_HOLD_FRAMES)
	{
		frames = 0;
		level++;
		if (level > MESH_KIT_LEVELS)
		{
			// Card back as found, then close the game - same contract as the audits.
			if (stashed)
			{
				if (auditCopyFile(AUDIT_SAVE_BACKUP, SAVE_PATH)) remove(AUDIT_SAVE_BACKUP);
			}
			else remove(SAVE_PATH);
			return true;
		}
	}
	return false;
}
#endif

static bool gameUpdateFrame(void)
{
	hidScanInput();

	u32 kDown = controlsTranslate(hidKeysDown());
	u32 kHeld = controlsTranslate(hidKeysHeld());
	u32 kUp   = controlsTranslate(hidKeysUp());
	if (kDown & KEY_START)
		return true; // close the game

	// Set below, on the bench only, and acted on once this frame's render
	// has actually landed in the framebuffer - see the capture call after
	// C3D_FrameEnd in gameRenderFrame.
	screenshotWanted = false;

	#if TEST_HINT_TOUR
	// Before the front end gets a look in: the tour drives the bench itself and
	// closes the game when it runs out of states.
	if (hintTourFrame()) return true;
	#endif

	#if TEST_SAVELOAD_TOUR
	if (saveLoadTourFrame()) return true;
	#endif

	// Any top-screen console repaint the pause menu left owing. Done here, at
		// the top of a later frame, so it can never share a frame with the menu's
		// own display transfer into the same single buffer.
		serviceTopRepaint(isNew3DS, menuOn != PAGE_NONE);

		// The front end owns the game from boot until Play is tapped. Held as the
		// state at the top of the frame rather than re-read later, so the release
		// that fires Play cannot also land on the bench behind it.
	const bool inTitle = (TEST_AUDIT_ALL_KITS || TEST_COLLISION_AUDIT) ? false : titleActive();

		if (inTitle)
		{
			titleAction act = titleInput(kDown, kHeld, kUp);
			if (act == TITLE_QUIT)
				return true; // Quit
			// An update just installed. Ending the loop is the whole action: the
			// updater has already pointed the chainloader back at this title, so
			// closing is what starts the new build.
			if (act == TITLE_RELAUNCH)
				return true;
			if (act == TITLE_PLAY)
			{
				enterLevel(titleLevel(), isNew3DS);
			}
			else {
			#if !TEST_AUDIT_ALL_KITS && !TEST_CAMERA_IDLE_AUDIT && !TEST_CAMERA_PAN_AUDIT && !TEST_LEVEL1_WORKSPACE_AUDIT
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
				menuOn       = PAGE_MAIN;
				menuCursor   = 0;
				menuNote[0]  = '\0';    // last visit's SAVED/LOADED line is stale now
				loadArmed    = false;   // and so is a Load that was left half-confirmed
				tapPending   = false;   // drop any tap that was mid-flight
				filingStroke = false;   // and any file stroke that was mid-rub
				hoverPart    = -1;      // and the highlight that was offering it
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
			// And out to the card while we are at it. Quitting to the level select
			// is the one moment the player has plainly finished with a build, so
			// it is the natural checkpoint - without it the only write is on the
			// way out of the game, and a console that runs its battery flat on the
			// level select would lose the lot.
			progressSave();
			// saveCurrentLevel has just folded the open build back into
			// levelBuilds, so this is the first moment the array can say the
			// kit just left is finished - and it is the moment before the
			// grid that has to show it comes back on screen.
			publishCompletion();
			menuOn = PAGE_NONE;
			titleReturnToLevels();
			#if !TEST_AUDIT_ALL_KITS && !TEST_CAMERA_IDLE_AUDIT && !TEST_CAMERA_PAN_AUDIT && !TEST_LEVEL1_WORKSPACE_AUDIT
			queueTopRepaint(TOP_REPAINT_TITLE);
			#endif
		}

		// Re-read, because the front end can have come back this very frame.
		// inTitle above is deliberately the state at the top of the frame, so the
		// release that fires Play cannot also land on the bench behind it; this
		// one is what the screen shows and what freezes the bench. Without it the
		// A press that chose Quit would fall straight through and reset the
		// bench camera on its way out.
		showTitle = (TEST_AUDIT_ALL_KITS || TEST_COLLISION_AUDIT) ? false : titleActive();
		const bool paused = inTitle || showTitle || (menuOn != PAGE_NONE);

		if (!paused)
		{
			if (kDown & KEY_A)
			{
				frameWorkbenchCamera();
			}

			// D-Pad Up is otherwise idle on the bench - it only means anything
			// inside the pause menu, which this branch is never in - so it is
			// free for a screenshot without displacing any binding on the
			// Controls page.
			if (kDown & KEY_DUP) screenshotWanted = true;

			// Item 35: posing. Only once every part is on the model - while
			// the build is still in progress a tap has to mean cut/file/fit,
			// and letting posing in earlier would mean deciding what a tap on
			// a half-built kit is for. Refused rather than silently ignored,
			// same as any other gate on this bench, so the player learns why
			// nothing happened instead of just pressing it again.
			if (kDown & KEY_DDOWN)
			{
				if (builtCount >= meshPartCount())
				{
					poseMode = !poseMode;
					if (!poseMode) selectedPart = -1;
					audioPlay(SND_CLICK);
				}
				else
				{
					audioPlay(SND_REFUSE);
				}
			}

			// Stylus: a small quick touch acts on the kit, anything longer turns
			// the bench. Nothing rotates until the touch passes the slop, so a
			// tap is never mistaken for a drag.
			//
			// The level has to have settled first. While it has not, anything the
			// stylus left mid-flight is dropped rather than held, so nothing fires
			// the instant the window closes either.
			if (!benchReady(kHeld))
			{
				tapPending   = false;
				filingStroke = false;
				hoverPart    = -1;
				touchMoved   = 0;
			}
			else if (kDown & KEY_TOUCH)
			{
				hidTouchRead(&lastTouch);
				touchStartMs = osGetTime();
				touchMoved   = 0;

				// One pick answers both questions asked on contact, rather than
				// two rays down the same line: what is under the stylus, and
				// whether that is the selected part.
				int under = pickPart(&modelView, (int)lastTouch.px, (int)lastTouch.py);

				// Rubbing a selected part files it; rubbing anywhere else turns
				// the bench. Decided once, on contact, against the view that was
				// on screen at the time - so the camera can never swing out from
				// under a stroke that has already started.
				filingStroke = canFile(selectedPart) && under == selectedPart;

				// Shown from the moment of contact, because that is already the
				// position the release will be tested at.
				hoverPart = under;
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
					{
						fileStroke(selectedPart, (float)(abs(dx) + abs(dy)));
						// A rub is sampled every frame it moves. Retriggering the
						// rasp on each of those would be a buzz, so it is only
						// allowed to start again once the last one is nearly out.
						audioPlayThrottled(SND_FILE, 170);
					}
					else
					{
						angleY += dx * 0.012f;
						angleX += dy * 0.012f;
					}
				}
				lastTouch = touch;

				// The highlight is a promise about what letting go would do, so
				// it is dropped the instant that stops being true - on exactly
				// the two conditions the release itself is tested against.
				if (touchMoved > TAP_SLOP_PX || (osGetTime() - touchStartMs) > TAP_MAX_MS)
					hoverPart = -1;
			}
			else if (kUp & KEY_TOUCH)
			{
				hoverPart = -1;

				// The release coordinates are unreliable on hardware, so the tap
				// is tested at the last position read while the stylus was down.
				if (touchMoved <= TAP_SLOP_PX && (osGetTime() - touchStartMs) <= TAP_MAX_MS)
					tapPending = true;
				// The stroke ends with the stylus. Left set, it would be inherited
				// by a later drag that never went through the contact test above -
				// pausing mid-stroke and resuming still holding the stylus does
				// exactly that, and would file whatever is selected by then.
				filingStroke = false;
			}

			// Circle Pad: left and right slide the view along the bench, up and down
			// still tilt it, and stylus drag still turns it. Its neutral hardware
			// noise is ignored, and taking the deadzone back out of the response
			// stops a tiny resting lean from creeping the view sideways on its own.
			circlePosition circle;
			hidCircleRead(&circle);
			if (circle.dx < -CIRCLE_ORBIT_DEADZONE || circle.dx > CIRCLE_ORBIT_DEADZONE) {
				int dx = circle.dx - (circle.dx > 0 ? CIRCLE_ORBIT_DEADZONE : -CIRCLE_ORBIT_DEADZONE);
				// Slide along the camera's own right rather than along world x, so
				// pushing right always moves the view right across the screen
				// whichever way the bench has been turned to face.
				C3D_Mtx yaw;
				Mtx_Identity(&yaw);
				Mtx_RotateY(&yaw, angleY, true);
				// The world direction that comes out as screen-right is the first
				// ROW of the yaw matrix, not its first column - Mtx_RotateY builds
				// the transpose of the textbook form. Reading the column instead
				// sends the slide backwards whenever the bench is turned near a
				// quarter turn, where the z term is the one doing the work. At the
				// default angle the x term dominates and it looks perfectly
				// correct, so only testing several angles catches it.
				// The pitch applied above the yaw leaves the x axis alone, so it
				// cannot change the answer.
				camPanX += yaw.r[0].x * dx * CIRCLE_PAN_SPEED;
				camPanZ += yaw.r[0].z * dx * CIRCLE_PAN_SPEED;
			}
			if (circle.dy < -CIRCLE_ORBIT_DEADZONE || circle.dy > CIRCLE_ORBIT_DEADZONE) {
				int dy = circle.dy - (circle.dy > 0 ? CIRCLE_ORBIT_DEADZONE : -CIRCLE_ORBIT_DEADZONE);
				angleX -= dy * 0.00025f;
			}

			// Shoulder buttons zoom - or, with a poseable part selected while
			// posing, turn its joint instead. The same two buttons rather
			// than a third control, the same way X only ever means something
			// once Y has turned photo mode on: L/R already means "the thing
			// I am focused on right now", and a selected, posing-mode part is
			// exactly that.
			const meshJoint* posingJoint = (poseMode && selectedPart >= 0) ? meshJointForPart(selectedPart) : NULL;
			if (posingJoint)
			{
				partState* pst = &partStates[selectedPart];
				if (kHeld & KEY_L) pst->poseDeg -= POSE_DEG_PER_FRAME;
				if (kHeld & KEY_R) pst->poseDeg += POSE_DEG_PER_FRAME;
				if (pst->poseDeg < posingJoint->minDeg) pst->poseDeg = posingJoint->minDeg;
				if (pst->poseDeg > posingJoint->maxDeg) pst->poseDeg = posingJoint->maxDeg;
			}

			// The near limit closes in as the camera locks onto a part, so a
			// selected part can be brought right up to the glass; dropping
			// the selection walks the limit back out and takes the camera
			// with it, instead of snapping.
			float nearLimit = CAM_NEAR_LIMIT + (CAM_NEAR_FOCUS - CAM_NEAR_LIMIT) * focusAmt;
			if (!posingJoint)
			{
				if (kHeld & KEY_L) camDist += 0.04f;
				if (kHeld & KEY_R) camDist -= 0.04f;
			}

			// Paging the manual by hand. Wraps both ways, and takes the manual
			// off auto-follow until the next thing gets snipped, filed or fitted.
			// Paging by hand is a one-way door without this. manualUpdate only
			// takes the manual back onto the job in hand when something gets
			// snipped, filed or fitted - so a player who paged forward to see
			// what was coming was then reading step 9 while working step 3, and
			// the only way back was to do some work blind. B rejoins the build.
			// Nothing to recompute here: clearing the hold is the whole of it,
			// because manualUpdate re-syncs the page later this same frame.
			if (kDown & KEY_B) manualHeld = false;

			// Photo mode. Y toggles it, X walks the framings while it is on.
			// Leaving it puts the camera back on the bench rather than wherever
			// the last shot was pointed, so the build carries on from a view the
			// player can work in. Both edges repaint the top screen, because the
			// caption and the manual overwrite each other.
			if (kDown & KEY_Y)
			{
				photoMode = !photoMode;
				if (photoMode) { photoShot = 0; framePhotoCamera(); }
				else            frameWorkbenchCamera();
				queueTopRepaint(TOP_REPAINT_LEVEL);
			}
			if (photoMode && (kDown & KEY_X))
			{
				photoShot = (photoShot + 1) % PHOTO_SHOT_COUNT;
				framePhotoCamera();
			}

			// Item 32: paint. X repaints the selected part once the kit is
			// finished - the same builtCount >= meshPartCount() gate item 35
			// uses for pose mode, so a build still in progress cannot be
			// colour-cycled out from under the player mid-fit. Free to claim
			// while photo mode is off, on the same "X only ever means
			// something once something else has already claimed the moment"
			// reuse the L/R comment above describes for posing.
			if (!photoMode && (kDown & KEY_X))
			{
				if (builtCount >= meshPartCount() && selectedPart >= 0)
				{
					partState* pst = &partStates[selectedPart];
					// Palette index is colourOverride - 1 (see picking.h);
					// this walks 1,2,3,4,1,... so PART_COLOUR_NONE (0) is
					// only ever the pre-first-press state, never landed on
					// again once the player has cycled once.
					pst->colourOverride = (pst->colourOverride % 4) + 1;
					audioPlay(SND_CLICK);
				}
				else
				{
					audioPlay(SND_REFUSE);
				}
			}

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
		// Not while a verification flag is on: the state a harness has staged is
		// not a build anybody made, and this is the line that would fold it into
		// the array the card is written from. See TEST_ANY_HARNESS in debug.h.
		if (!showTitle && !TEST_ANY_HARNESS) saveCurrentLevel();
		// Settings are written the moment they change rather than at exit, so a
		// console that runs flat with the Options page open still remembers what
		// was set on it. Costs a flag test on the frames where nothing moved.
		// Silenced under a verification flag for the same reason the line above
		// is: the tutorial switch a harness forces on for a screenshot is not a
		// setting the player chose.
		if (!TEST_ANY_HARNESS) settingsFlush();
		// Both Options pages set the same master volume, so pulling it into the
		// DSP here rather than at either of them means neither can forget to.
		audioUpdateVolume();

		u64 now = osGetTime();
		if (!paused)
		{
			updateCuts(now);
			updateFocus();
		}

		// One transform for both the hit test and the frame it is tested
		// against, so a tap always hits what is on screen.
		buildModelView(&modelView);

		handleTap(now);

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

		return false;
}

// The frame loop's render half: draws the front end or the bench (plus
// whatever the bench has open - pause menu, beginner sheet, or the guide's
// stand-in target for a hide-guide capture) and presents both screens.
static void gameRenderFrame(void)
{
	// Two renderers share this one target, and each one leaves the GPU set up
	// for itself, so whichever draws re-binds its own state first -
	// sceneBind() here, C2D_Prepare() inside titleDraw().
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		if (showTitle)
		{
			titleDraw(benchTarget);
		}
		else
		{
			C3D_RenderTargetClear(benchTarget, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
			C3D_FrameDrawOn(benchTarget);
			sceneBind();
			sceneRender(&modelView);

			// The breathing dot, drawn on the bench itself. The sheet on the top
			// screen says what to do; this says where, on the actual piece, which
			// is the half a diagram of a generic box cannot tell anybody.
			//
			// Only while the sheet is up: with the tutorial switched off the player
			// has asked not to be led by the hand, and a dot is still leading them
			// by the hand. Not under the pause menu either, which is not a moment
			// anybody is aiming the stylus at the bench.
			//
			// After sceneRender so it sits on top of the kit rather than inside it,
			// and drawn with citro2d - which is exactly why sceneBind() above has to
			// run again every frame before the bench does.
			#if TEST_HINT_TOUR
			// Logged from the draw path rather than from the tour's own setup: the
			// question a shot with no dot in it raises is which of these three tests
			// failed, and they only mean anything on the frame the shot is actually
			// taken from. screenshotWanted is still up here - it is consumed after
			// C3D_FrameEnd - so this is exactly that frame.
			if (screenshotWanted)
			{
				float lx = 0.0f, ly = 0.0f;
				bool got = hintPoint(&modelView, beginnerAction(), &lx, &ly);
				FILE* log = fopen("sdmc:/modelkit/tour.txt", "a");
				if (log)
				{
					fprintf(log, "   draw menu%d sheet%d act%d dot%d %.0f,%.0f\n",
						(int)menuOn, beginnerSheetShowing() ? 1 : 0,
						beginnerAction(), got ? 1 : 0, lx, ly);
					fclose(log);
				}
			}
			#endif
			if (!TEST_CAPTURE_HIDE_GUIDE && menuOn == PAGE_NONE && beginnerSheetShowing())
			{
				float hintX, hintY;
				if (hintPoint(&modelView, beginnerAction(), &hintX, &hintY))
				{
					C2D_Prepare();
					C2D_SceneBegin(benchTarget);
					hintDraw(hintX, hintY);
					C2D_Flush();
				}
			}
			if (menuOn != PAGE_NONE) drawInLevelMenu();
			else if (!TEST_AUDIT_ALL_KITS && !TEST_COLLISION_AUDIT && !TEST_CAPTURE_HIDE_GUIDE && beginnerTopShowing())
			{
				if (beginnerSheetShowing()) drawBeginnerSheet();
				else                        drawTopIdleCard();
			}
			else if (TEST_CAPTURE_HIDE_GUIDE)
			{
				// Keep the top target participating in this frame so a guide-free
				// capture still presents the bottom 3D target on every emulator.
				C3D_RenderTargetClear(beginnerTarget, C3D_CLEAR_ALL, 0xFBFAF6FF, 0);
				C3D_FrameDrawOn(beginnerTarget);
			}
		}
	C3D_FrameEnd(0);

	// Grabbed here rather than in gameUpdateFrame: the bottom screen's
	// framebuffer only holds this frame's render once C3D_FrameEnd has
	// actually queued the transfer into it, and by the next frame's
	// hidScanInput it would be the frame after that which was on screen when
	// the player pressed the button.
	if (screenshotWanted) { if (screenshotCapture()) audioPlay(SND_UI); }

	// The top screen has no citro3d render target - it belongs to the
	// console - so C3D_FrameEnd never swaps it. Left unswapped, NEITHER
	// screen presents and the whole app goes black. Swapping it by hand is
	// what makes a bottom-screen-only layout work at all.
	if (TEST_AUDIT_ALL_KITS || TEST_COLLISION_AUDIT || TEST_CEILING_AUDIT || TEST_CAPTURE_HIDE_GUIDE || !(!showTitle && (beginnerTopShowing() || menuOn != PAGE_NONE)))
		gfxScreenSwapBuffers(GFX_TOP, false);
}

// Every way out of the frame loop lands here - START, the front end's Quit,
// and closing the app from the HOME menu, which is what ends aptMainLoop.
static void gameShutdown(void)
{
	// One write covers all three exits - unless this is a verification build, in
	// which case there is no write at all and nothing on the card is touched.
	// See TEST_ANY_HARNESS in debug.h for why.
	bool progressWritten = TEST_ANY_HARNESS ? true : progressSave();
	// settingsFlush answers true when there was nothing to write, so the latched
	// flag is what actually reports a settings write that was refused - including
	// one that happened mid-session and has long since been cleared.
	if (!TEST_ANY_HARNESS) settingsFlush();
	if (!progressWritten || settingsSaveFailed())
	{
		if (!progressWritten)
		{
			printf("SAVE FAILED: %s\n", saveLastReason());
			printf("progress was not written to %s\n", SAVE_PATH);
		}
		if (settingsSaveFailed())
			printf("SETTINGS NOT SAVED to %s\n", SETTINGS_PATH);
		showConsoleAndPause();
	}

	audioExit();
	titleExit();
	updaterExit();
	C2D_TextBufDelete(beginnerText);
	C2D_Fini();
	sceneExit();
	C3D_Fini();
	gfxExit();
}

int main(void)
{
	int rc = gameInit();
	if (rc) return rc;

	while (aptMainLoop())
	{
		if (gameUpdateFrame()) break;
		gameRenderFrame();
	}

	gameShutdown();
	return 0;
}
