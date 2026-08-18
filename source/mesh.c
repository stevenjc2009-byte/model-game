// Sprue geometry.
//
// A sprue is the plastic frame a kit arrives on: a rectangular runner with the
// parts held inside it by thin gates. Cutting the gates is the first thing you
// do when you build a kit, so the frame is the first thing this game draws.
//
// Every gate is modelled as two boxes end to end. Snipping happens in the
// middle, the way a real nipper cut does: the outer half stays on the runner as
// a stub, the inner half stays on the part as a nub. Nothing is created or
// destroyed when a part is cut - the part's half simply travels with it.
//
// Boxes only, one shared buffer, a handful of draw calls - the Original 3DS has
// to run this too.

#include <string.h>
#include <math.h>
#include <stdio.h>
#include "mesh.h"
#include "texture_atlas.h"
#include "debug.h"

// Capacity comes from mesh.h so main.c's per-part state cannot drift from it.
#define MAX_BOXES   MESH_MAX_BOXES
#define MAX_PARTS   MESH_MAX_PARTS
#define MAX_SOCKETS MESH_MAX_SOCKETS
#define VERTS_PER_BOX 36

// The workbench, stacked bottom up. The mat sinks into the desk and the grid
// sits a hair proud of the mat, so no two coplanar faces ever fight over depth.
// Desk centre after a 180-degree turn about the former (3.10,2.00) centre.
// Right/front edges land exactly on the room interior faces x=7.34,z=6.34.
// The desk was then widened leftwards, towards the bed, to make room for the
// cut parts: it now runs x -5.24..7.34, stopping 0.28 clear of the bed's right
// face at x=-5.52. Right and front edges are unchanged - it was already flush
// against both of those walls, so left was the only direction left to grow.
#define BENCH_X   1.05f
#define BENCH_Z   3.94f
#define BENCH_W  12.58f
#define BENCH_D   4.80f
#define DESK_Y   (-1.40f)
#define DESK_T     0.10f
// The mat is centred in depth on the desk, and in width it sits in the middle of
// the wood between the cut zone and the kit box with an equal 0.17 margin either
// side of all three zones.  It is NOT on the desk's own centre line at x 1.05,
// and it cannot be: the cut zone needs 2.85 on the left and the box needs 4.62
// on the right, and a desk-centred mat leaves only 4.065 on the right, so the
// box would hang half a unit off the edge into the wall.  Even margins are the
// closest thing to centred that the three zones actually allow.
#define MAT_X      0.165f
#define MAT_Z      3.94f
#define MAT_W      4.55f
#define MAT_D      3.00f
#define MAT_Y    (-1.345f)
#define MAT_T      0.04f
#define GRID_Y   (-1.3225f)
#define GRID_T     0.010f
#define GRID_W     0.03f

// The desk frame under the top: an apron rail flush with the underside, and
// four legs hanging off it. Both are inset from the edge so the top overhangs.
#define APRON_Y  (-1.53f)
#define APRON_T    0.16f
#define LEG_Y    (-2.75f)
#define LEG_H      2.60f
#define LEG_W      0.24f
#define LEG_X      2.56f
#define LEG_Z      2.24f
// Apron and legs sit inset from the desk edge, so the top overhangs them. Both
// are derived from the desk size rather than written out, so widening the desk
// carries the frame with it instead of leaving the legs behind in mid-air.
// The Z inset is the shallower of the two and is unchanged - only the width
// moved.
#define FRAME_X   (BENCH_W*0.5f - 0.24f)
#define FRAME_Z   (BENCH_D*0.5f - 0.16f)
#define APRON_LEN (BENCH_W - 0.40f)

// The build stand: a plinth on the mat with a post rising out of it, standing at
// the front of the bench where nothing can come between it and the stylus. It
// sits in front of every cut-part slot, so a part waiting on the mat can never
// land inside it.
//
// The post is tall and slim because ten parts have to hang off it without their
// tap boxes running into each other - a short post crowds them into the same
// few pixels and the wrong one answers the tap.
// Separate horizontal work zones prevent the runner from sprawling over the
// assembly stand.  Left to right along the widened desk: cut parts on the bare
// wood at the far left, then the mat with the stand on it, then the kit box on
// the bare wood at the right.  None of the three overlaps another.
#define RUNNER_X   4.89f
// The cut-out parts land on bare wood at the far left, clear of the mat and a
// long way from the stand.  Width and depth are the packer's working area: every
// cut part gets a reserved home inside it, worked out when the kit is built, so
// two parts can never be put down on top of one another however they are cut.
// The three zones and the four gaps around them add up to the desk exactly:
// 0.14 + 2.85 + 0.14 + 4.55 + 0.14 + 4.62 + 0.14 = 12.58.  Even margins are why
// the numbers below look arbitrary - they are not, they are that sum solved.
#define LOOSE_X  (-3.675f)
#define LOOSE_Z    3.94f
#define LOOSE_W    2.85f
#define LOOSE_D    2.40f
#define LOOSE_GAP  0.06f   // clear wood between two parts put down side by side
// The display stand keeps its old offset from the middle of the mat, so it sits
// where it always did relative to the cutting surface the model is built on.
#define STAND_X    0.685f
#define STAND_Z    3.94f
// Finished parts sit just in front of the mounting post, rather than centred
// inside it.  All kit-local target Z coordinates remain relative to this.
// The workstation was turned through 180 degrees; camera-facing is now the
// negative-Z side of the post, not the old positive-Z side.
#define ASSEMBLY_FRONT_Z -0.36f
#define PLINTH_W   1.10f
#define PLINTH_T   0.10f
#define PLINTH_D   0.44f
#define POST_W     0.30f
#define POST_H     1.30f
#define POST_D     0.22f
#define POST_TOP   (MAT_TOP + PLINTH_T + POST_H)

// The kit box lies flat on the expanded bare desk behind the mat. Its base and
// lid are separate so the lid can lift straight off like a shoebox top.
// One standard box, the same on every level, and a kit too big for one sprue
// gets another of the same box stacked on it - one box per runner. That is how a
// real kit range works: the manufacturer does not blow up the carton for the big
// models, it ships more of them. It also means the desk footprint never changes,
// so nothing on the desk has to shuffle out of the way level to level.
//
// The standard size is measured off the largest runner frame any of the twenty
// kits produces - 3.90 wide (Sailboat) by 3.20 deep (Desk Fan) - plus a wall
// each side and 0.30 of air, then rounded up with 0.06 to spare. The spare is
// not decoration: sized to the exact requirement, the widest and the deepest
// kits land dead on the limit and single-precision rounding, not the geometry,
// decides which side of it they fall.
#define KIT_BOX_X       4.89f
#define KIT_BOX_W       4.50f
#define KIT_BOX_H       0.42f
#define KIT_BOX_D       3.80f
#define KIT_BOX_T       0.12f
#define KIT_BOX_AIR     0.30f
// Bottom of the lowest box in the stack: the desk's own top face, so the stack
// stands on the wood rather than hovering over it.
#define KIT_BOX_FLOOR   (DESK_Y + DESK_T*0.5f)
// Base, walls and lid of one box, so the next box up starts exactly where this
// one's lid ends and the two meet face to face with nothing overlapping.
#define KIT_BOX_PITCH   (KIT_BOX_T + KIT_BOX_H + KIT_BOX_T)
// Pinning the back edge against the wall rather than the centre means the box
// can only ever reach forwards onto desk we know is empty.
#define KIT_BOX_BACK_Z  6.30f

static vertex meshData[MAX_BOXES * VERTS_PER_BOX];
static int meshCount;

// Item 36: the scene is authored above as an unindexed triangle list - every
// addXxx emitter above writes two CCW triangles per quad face, 6 vertices,
// with the corner shared by both triangles duplicated in the array (a box
// alone repeats 12 of its 36 verts this way). Draw ranges (deskFirst/Count and
// the rest below) are all offsets and counts into that emission order, so
// meshCount and every First/Count pair keep meaning exactly what they always
// meant - "position N in draw order" - and nothing above this comment changes.
//
// What changes is what gets uploaded. meshDedupBuild() runs once, after a kit
// is fully built, and turns meshData[0..meshCount) into two arrays: dedupData,
// the unique (position,normal) pairs, and meshIndex[0..meshCount), one index
// per original vertex, in the same draw order as before. Feeding meshIndex
// through C3D_DrawElements with dedupData bound as the vertex buffer
// reproduces the identical triangle sequence and winding - the index list IS
// the old vertex order, just indirected - so every First/Count pair above
// still addresses the right slice, now of the index array rather than the
// vertex array.
//
// A hash table rather than an O(n^2) scan: MESH_INDEX_MAX exists so a kit
// built from all 384 boxes cannot overflow either array, and at that size a
// linear "have I seen this vertex before" scan is 190 million memcmp calls on
// hardware clocked at 268MHz. The table only has to answer "is there already
// a vertex with this exact bit pattern", so a bucket of exact-hash matches
// checked with memcmp is enough - it never has to be a good hash, only a fast
// one that spreads real kit geometry, which FNV-1a over the raw struct bytes
// does.
#define MESH_INDEX_MAX (MAX_BOXES * VERTS_PER_BOX)
#define DEDUP_BUCKETS  16384  // power of 2, comfortably above MESH_INDEX_MAX so chains stay short
static vertex dedupData[MESH_INDEX_MAX];
static unsigned short meshIndex[MESH_INDEX_MAX];
static int dedupCount;
static int dedupBucketHead[DEDUP_BUCKETS];
static int dedupNext[MESH_INDEX_MAX];

static unsigned int hashVertex(const vertex* v)
{
	const unsigned char* p = (const unsigned char*)v;
	unsigned int h = 2166136261u;
	for (size_t i = 0; i < sizeof(vertex); i++) { h ^= p[i]; h *= 16777619u; }
	return h;
}

// Rebuilds dedupData/meshIndex from meshData. Must run after every emitter
// for the kit has finished writing meshData - meshBuildKit calls it last.
static void meshDedupBuild(void)
{
	dedupCount = 0;
	for (int b = 0; b < DEDUP_BUCKETS; b++) dedupBucketHead[b] = -1;

	int total = meshCount;
	if (total > MESH_INDEX_MAX) total = MESH_INDEX_MAX; // meshRoomFor already enforces this; belt and braces
	for (int i = 0; i < total; i++)
	{
		const vertex* v = &meshData[i];
		unsigned int h = hashVertex(v) & (DEDUP_BUCKETS - 1);
		int found = -1;
		for (int b = dedupBucketHead[h]; b >= 0; b = dedupNext[b])
		{
			if (memcmp(&dedupData[b], v, sizeof(vertex)) == 0) { found = b; break; }
		}
		if (found < 0)
		{
			found = dedupCount;
			dedupData[dedupCount] = *v;
			dedupNext[dedupCount] = dedupBucketHead[h];
			dedupBucketHead[h] = dedupCount;
			dedupCount++;
		}
		meshIndex[i] = (unsigned short)found;
	}
}

// How many pieces of geometry were thrown away this build because the vertex
// store was full.
//
// Every emitter already refused to write past the end of meshData, which is the
// right thing to do - it is the only thing standing between a kit that outgrows
// the store and a buffer overrun. But it did it silently, so a kit too big for
// the store did not fail anything; it just came out with holes in it, and the
// audit's "did it fit" clause was checking an invariant the guards had already
// made impossible to break. Counting the refusals is what gives that clause
// something real to test.
static int meshDropped;

// Whether there is room for one more piece, and a record of it if there is not.
static bool meshRoomFor(int verts)
{
	if (meshCount + verts <= (int)(sizeof(meshData)/sizeof(meshData[0]))) return true;
	meshDropped++;
	return false;
}

static meshPart parts[MAX_PARTS];
static int partCount;

// Item 35: which parts of which level can be posed once built, and the hinge
// each one turns on. One row per articulated part - most parts never appear
// here at all, and stay rigid.
//
// pivotOffset is measured off the authored target/size the kit table already
// gives each part: half the part's own length back toward whatever it hangs
// off, along whichever axis that part is drawn long on. A part rotates about
// where it joins its parent, not about its own middle.
typedef struct { int level; int part; float pivotOffset[3]; meshJointAxis axis; float minDeg; float maxDeg; } jointDef;
static const jointDef jointDefs[] =
{
	// Friendly Robot (L6): arm L/R hang off the torso, rod along Y, length
	// .65 - half .325 back up toward the shoulder. Forward swing about X.
	{ 6,  4, { 0.0f, 0.325f, 0.0f }, JOINT_AXIS_X, -10.0f, 70.0f }, // arm L
	{ 6,  5, { 0.0f, 0.325f, 0.0f }, JOINT_AXIS_X, -10.0f, 70.0f }, // arm R
	// Excavator (L12): boom hangs off the cab, rod along X, length 1.0 -
	// half .5 back toward the cab. Raise/lower about Z. arm and bucket hang
	// off the boom in turn and follow it with no entry of their own.
	//
	// Capped at 12 rather than the wider lift a real excavator has: past
	// ~15 the trailing arm+bucket swing up and tuck in behind the cab
	// instead of staying out in front of it - clean at rest and at 10,
	// visibly behind the cab by 20 (checked by screenshot, not guessed).
	{ 12, 2, { -0.5f, 0.0f, 0.0f }, JOINT_AXIS_Z, -10.0f, 12.0f }, // boom
	// Dragon (L13): wings hang off the body, wedge spanning Z, half-span
	// .55 back toward the body. Flap up/down about X.
	{ 13, 3, { 0.0f, 0.0f, -0.55f }, JOINT_AXIS_X, -10.0f, 50.0f }, // wing L
	{ 13, 4, { 0.0f, 0.0f,  0.55f }, JOINT_AXIS_X, -10.0f, 50.0f }, // wing R
	// Large Mech (L20): arm L/R hang off the shoulders, rod along Y, length
	// .7 - half .35 back up toward the shoulder. Forward swing about X. The
	// shield hangs off arm L and follows it with no entry of its own - the
	// one part in the game whose parent is itself an articulated part.
	{ 20, 4, { 0.0f, 0.35f, 0.0f }, JOINT_AXIS_X, -10.0f, 70.0f }, // arm L
	{ 20, 5, { 0.0f, 0.35f, 0.0f }, JOINT_AXIS_X, -10.0f, 70.0f }, // arm R
};
#define JOINT_DEF_COUNT (int)(sizeof(jointDefs) / sizeof(jointDefs[0]))

// Sparse, indexed by part: which of the current level's parts have a joint,
// and what it is. Rebuilt whenever meshBuildKit runs, the same as every other
// per-level table in this file.
static meshJoint activeJoints[MAX_PARTS];
static bool activeHasJoint[MAX_PARTS];

static void buildJoints(int level)
{
	for (int i = 0; i < MAX_PARTS; i++) activeHasJoint[i] = false;
	for (int k = 0; k < JOINT_DEF_COUNT; k++)
	{
		if (jointDefs[k].level != level) continue;
		int p = jointDefs[k].part;
		if (p < 0 || p >= MAX_PARTS) continue;
		activeJoints[p].part         = p;
		activeJoints[p].pivotOffset[0] = jointDefs[k].pivotOffset[0];
		activeJoints[p].pivotOffset[1] = jointDefs[k].pivotOffset[1];
		activeJoints[p].pivotOffset[2] = jointDefs[k].pivotOffset[2];
		activeJoints[p].axis         = jointDefs[k].axis;
		activeJoints[p].minDeg       = jointDefs[k].minDeg;
		activeJoints[p].maxDeg       = jointDefs[k].maxDeg;
		activeHasJoint[p] = true;
	}
}

const meshJoint* meshJointForPart(int part)
{
	if (part < 0 || part >= MAX_PARTS || !activeHasJoint[part]) return NULL;
	return &activeJoints[part];
}

static meshSocket sockets[MAX_SOCKETS];
static int socketCount;

static int deskFirst, deskCount;
static int matFirst, matCount;
static int gridFirst, gridCount;
static int runnerFirst, runnerCount;
static int runnerBaseCount;
static int stubFirst[MAX_PARTS], stubCount[MAX_PARTS];
// Kept with the active mesh so the whole-kit audit can fail on a bad packed
// frame rather than merely leaving a diagnostic in the console.
static bool activePackedValid;
static int standFirst, standCount;
static int kitTrayFirst, kitTrayCount;
static int kitLidFirst, kitLidCount;
static int kitArtFirst, kitArtCount;
static int roomFirst, roomCount, roomCeilingFirst, roomCeilingCount, roomFloorFirst, roomFloorCount;
static int roomBedWoodFirst, roomBedWoodCount, roomMattressFirst, roomMattressCount;
static int roomBlanketFirst, roomBlanketCount, roomPillowFirst, roomPillowCount;
static int roomDoorFirst, roomDoorCount, roomKnobFirst, roomKnobCount;
static int roomShelfFirst, roomShelfCount, roomTrimFirst, roomTrimCount;
static int roomSkyFirst, roomSkyCount, roomGroundFirst, roomGroundCount;
static int roomWallRearFirst, roomWallRearCount, roomWallFrontFirst, roomWallFrontCount;
static int roomWallLeftFirst, roomWallLeftCount, roomWallRightFirst, roomWallRightCount;
static int activeLevel = 1;
static int activeRunners = 1;
static const char* activeKitName = "Star Trophy";
// Where each cut part goes once it is off the frame, worked out per kit rather
// than handed out in cut order, so the same part always lands in the same place.
static float looseHome[MAX_PARTS][2];
static bool activeLooseValid;

// Sprue packing. Parts sit on the frame at the size they will be when they come
// off it - nothing shrinks on the runner and nothing grows once it is cut. So
// the frame cannot be a fixed grid of identical cells; every cell is exactly as
// wide as the part in it, and the frame is measured to fit whatever the kit
// authored. These are the clearances that measurement is built from.
#define RUN_GAP   0.10f  // clear air between neighbouring parts in one lane
#define RUN_SIDE  0.12f  // clear air between an end part and a side rail
#define RUN_GATE  0.18f  // rail face to part face: holds the stub then the nub
#define RUN_MID   0.16f  // clear band between the two lanes, holds the cross-bar
#define RUN_BAR   0.12f  // rail thickness

// The measured frame for the kit currently built. Defaults are the fixed frame
// the game shipped with, so anything asking before the first meshBuildKit still
// gets a sane answer.
static float runnerFrameW = 3.60f;   // outer width, side rail to side rail
static float runnerFrameH = 2.08f;   // outer height, top rail to bottom rail
static float runnerInnerH = 1.84f;   // rail inner face to rail inner face
static float runnerBarY  = -0.10f;   // centre of the cross-bar, in the lane gap

// A kit with more than one runner needs a bigger box to hold them, and any kit
// needs a box the measured frame actually fits inside. The geometry and the tap
// bounds both go through here, because a box drawn wider than it can be touched
// leaves a dead strip down each side that ignores the stylus.
static void kitBoxOuterSize(float* w, float* d, float* h)
{
	// Fixed, on every level. It does NOT grow to the kit: growing it is what put
	// a different-sized box on the desk each level and pushed things off the far
	// edge. If a frame ever will not fit, kitBoxFits says so out loud rather than
	// quietly resizing the box out from under the rest of the layout.
	*w = KIT_BOX_W;
	*d = KIT_BOX_D;
	*h = KIT_BOX_H;
}

// Whether the runner currently measured actually goes in the standard box. The
// runner lies flat inside, so the frame's width is the box's width and the
// frame's height is the box's depth.
static bool kitBoxFits(void)
{
	// The same tolerance strictAabbOverlap uses, and for the same reason: the
	// frame width is accumulated from a chain of single-precision part sizes, so
	// a frame that fits exactly can land a rounding hair over the limit and be
	// reported as not fitting. KIT_BOX_AIR is 0.30 of real clearance either way -
	// two thousandths on top of it cannot let a frame through that does not fit.
	const float eps = .002f;
	return runnerFrameW + 2.0f*KIT_BOX_T + KIT_BOX_AIR <= KIT_BOX_W + eps
	    && runnerFrameH + 2.0f*KIT_BOX_T + KIT_BOX_AIR <= KIT_BOX_D + eps;
}

// How many boxes the kit ships in: one per runner, stacked. A model with more
// parts than one sprue holds needs somewhere for the other sprues to live, and
// that somewhere is another copy of the same box.
static int kitBoxCount(void)
{
	int n = activeRunners;
	if (n < 1) n = 1;
	if (n > MESH_MAX_RUNNERS) n = MESH_MAX_RUNNERS;
	return n;
}

// Where the box actually sits, given the size the current kit forced it to be.
// The back edge is pinned against the wall side of the desk, so a deeper kit
// grows forwards onto empty desk instead of backwards through the wall.
static float kitBoxCentreZ(void)
{
	float w, d, h;
	kitBoxOuterSize(&w, &d, &h);
	return KIT_BOX_BACK_Z - (d + KIT_BOX_T)*0.5f;
}

typedef enum { SH_BOX, SH_CYLINDER, SH_CONE, SH_WEDGE, SH_PLATE, SH_ROD, SH_STAR, SH_GRILL, SH_FAN_BLADE,
	SH_DOME, SH_TUBE, SH_CURVE, SH_BELL, SH_WHEEL } partShape;
typedef enum { AXIS_X, AXIS_Y, AXIS_Z } partAxis;
typedef struct { const char* name; partShape shape; partAxis axis; float size[3]; float target[3]; float spin; signed char parent; unsigned char colour; unsigned char runner; } kitPartDef;
typedef struct { const char* name; unsigned char runners; kitPartDef parts[10]; } kitDef;
#define P(N,S,A,SX,SY,SZ,TX,TY,TZ,PA,C,R) {N,S,A,{SX,SY,SZ},{TX,TY,TZ},0.0f,PA,C,R}
#define PR(N,S,A,SX,SY,SZ,TX,TY,TZ,SPIN,PA,C,R) {N,S,A,{SX,SY,SZ},{TX,TY,TZ},SPIN,PA,C,R}
// A mirrored L/R pair: two parts identical in every field but the sign of one
// target coordinate, which is how most kits author a wing, wheel, leg or arm
// either side of the model's centreline. Each expands to two P(...) records,
// so a PAIR fills two of a kit's ten part slots from one authored line and the
// two can never drift out of mirror by a typo in the second copy's number.
// PAIR_Z mirrors through target Z, the axis most kits are built along; PAIR_X
// mirrors through target X, for the kits authored the other way round. Both
// take the first-authored side's own signed target - PAIR_Z the positive Z,
// PAIR_X the positive X - and derive the second side by negating it.
// KIT takes its part list as __VA_ARGS__ rather than ten named parameters
// exactly so that a PAIR, which occupies one syntactic argument but expands to
// two comma-separated records, can take the place of two of them.
#define PAIR_Z(NL,NR,S,A,SX,SY,SZ,TX,TY,TZ,PA,C,R) \
	P(NL,S,A,SX,SY,SZ,TX,TY, (TZ),PA,C,R), \
	P(NR,S,A,SX,SY,SZ,TX,TY,-(TZ),PA,C,R)
#define PAIR_X(NL,NR,S,A,SX,SY,SZ,TX,TY,TZ,PA,C,R) \
	P(NL,S,A,SX,SY,SZ,-(TX),TY,TZ,PA,C,R), \
	P(NR,S,A,SX,SY,SZ, (TX),TY,TZ,PA,C,R)
// Variadic so one PAIR_Z/PAIR_X line can stand for two parts. That costs the
// compile-time arity check KIT used to give when it took ten fixed arguments:
// a kit written with nine parts now zero-fills the tenth instead of failing to
// build. It is caught on the next launch instead, by the partCount test in
// validateKitDef, which prints the kit and the count it found.
//
// Counting them here again is not available. An attempt to route KIT through a
// fixed-arity KIT_ROW failed because macro arguments are expanded before they
// are substituted: each P has already become a brace-enclosed record by the
// time the inner macro sees it, and the preprocessor does not treat braces as
// protecting the commas inside them the way parentheses do. Ten parts arrived
// as 132 arguments. Restoring the check means giving PAIR up.
#define KIT(N,R,...) {N,R,{__VA_ARGS__}}
// Every kit's name lives in its own kitDef below, so there is no separate name
// table to keep in step with them.
static const kitDef kitDefsMid[6] = {
KIT("Submarine",2,P("hull",SH_CYLINDER,AXIS_X,1.5,.6,.6,0,-.3,0,-1,3,0),P("tower",SH_BOX,AXIS_Y,.4,.35,.4,0,.15,0,0,0,0),P("periscope",SH_ROD,AXIS_Y,.08,.55,.08,0,.55,0,1,1,0),P("tail fin",SH_WEDGE,AXIS_X,.4,.4,.08,.7,-.2,0,0,3,1),P("propeller",SH_CYLINDER,AXIS_X,.3,.08,.3,.93,-.3,0,3,1,1),PAIR_Z("plane L","plane R",SH_PLATE,AXIS_Z,.5,.12,.05,.15,-.3,.38,0,3,1),P("window",SH_CYLINDER,AXIS_Z,.14,.05,.14,-.25,-.18,.3,0,1,1),P("keel",SH_PLATE,AXIS_Y,.8,.05,.18,0,-.62,0,0,3,1),P("stand",SH_BOX,AXIS_Y,.7,.1,.5,0,-.75,0,0,0,1)),
KIT("Propeller Plane",2,P("hub",SH_CYLINDER,AXIS_X,.25,.1,.25,-.85,-.1,0,-1,3,0),P("fuselage",SH_CYLINDER,AXIS_X,1.5,.4,.4,0,-.15,0,0,0,0),PAIR_Z("wing L","wing R",SH_WEDGE,AXIS_Y,.8,.12,1.4,0,-.15,.55,1,2,0),P("tail",SH_PLATE,AXIS_Z,.45,.15,.65,.65,.0,0,1,2,1),P("rudder",SH_WEDGE,AXIS_Y,.12,.45,.35,.80,.22,0,4,2,1),PAIR_Z("wheel L","wheel R",SH_WHEEL,AXIS_X,.12,.25,.25,-.2,-.65,.3,1,3,1),P("cockpit",SH_DOME,AXIS_Y,.4,.25,.3,.15,.15,0,1,1,1),P("spinner",SH_CONE,AXIS_X,.25,.25,.25,-1.05,-.1,0,0,3,1)),
// Low side-profile racer: wide front wing, low wedge body, raised cockpit,
// four corner wheels and a clearly separate rear spoiler.
//
// The nose is a blade rather than a cone. Every cone here tapers towards its
// own +axis and the car faces -X, so a cone at the front could only ever be
// widest at the tip - and the chassis wedge has thinned to a knife edge by the
// time it reaches x -0.925, so the cone's point had nothing to bury itself in
// and hung in the air. A flat blade from the chassis tip out to the front wing
// is both what an open-wheel nose actually is and something that overlaps real
// solid at both ends.
KIT("Race Car",2,P("chassis",SH_WEDGE,AXIS_Y,1.85,.38,.72,0,-.48,0,-1,2,0),P("cockpit",SH_BOX,AXIS_Y,.62,.28,.48,.18,-.12,0,0,0,0),P("front wing",SH_PLATE,AXIS_Y,.20,.08,1.08,-1.04,-.67,0,0,0,0),P("rear spoiler",SH_PLATE,AXIS_Y,.20,.15,1.02,.98,.04,0,1,0,1),PAIR_Z("wheel FL","wheel FR",SH_WHEEL,AXIS_X,.18,.38,.38,-.52,-.76,.44,0,3,1),PAIR_Z("wheel RL","wheel RR",SH_WHEEL,AXIS_X,.18,.38,.38,.58,-.76,.44,0,3,1),P("nose",SH_PLATE,AXIS_Y,.45,.14,.30,-1.00,-.62,0,0,2,1),P("rear diffuser",SH_PLATE,AXIS_Y,.30,.07,.76,.96,-.67,0,0,3,1)),
KIT("Excavator",2,P("track base",SH_BOX,AXIS_Y,1.2,.35,.7,0,-.65,0,-1,3,0),P("cab",SH_BOX,AXIS_Y,.55,.55,.55,-.2,-.1,0,0,2,0),P("boom",SH_ROD,AXIS_X,1.0,.15,.15,.55,.15,0,1,3,0),P("arm",SH_ROD,AXIS_Y,.15,.75,.15,.95,-.25,0,2,3,1),P("bucket",SH_WEDGE,AXIS_X,.45,.35,.5,1.1,-.65,0,3,2,1),PAIR_Z("track L","track R",SH_BOX,AXIS_Y,1.1,.3,.18,0,-.85,.35,0,3,1),P("hydraulic",SH_ROD,AXIS_X,.7,.07,.07,.55,-.05,.18,2,0,1),P("weight",SH_BOX,AXIS_Y,.4,.4,.5,-.70,-.35,0,0,3,1),P("pivot",SH_CYLINDER,AXIS_Y,.22,.15,.22,.0,-.28,0,0,0,1)),
KIT("Dragon",2,P("body",SH_CYLINDER,AXIS_X,1.25,.55,.55,0,-.25,0,-1,1,0),P("head",SH_WEDGE,AXIS_X,.55,.45,.45,-.75,.0,0,0,1,0),P("tail",SH_CONE,AXIS_X,.75,.3,.3,.85,-.3,0,0,1,0),PAIR_Z("wing L","wing R",SH_WEDGE,AXIS_Y,.75,.15,1.1,0,.15,.58,0,3,1),PAIR_Z("leg FL","leg FR",SH_ROD,AXIS_Y,.16,.55,.16,-.35,-.68,.24,0,1,1),PAIR_Z("leg RL","leg RR",SH_ROD,AXIS_Y,.16,.55,.16,.35,-.68,.24,0,1,1),P("horn",SH_CONE,AXIS_Y,.14,.3,.14,-.85,.35,0,1,3,1)),
KIT("Castle Tower",3,P("base",SH_CYLINDER,AXIS_Y,.9,.35,.9,0,-.75,0,-1,0,0),P("ring",SH_CYLINDER,AXIS_Y,.75,.35,.75,0,-.42,0,0,0,0),P("tower",SH_CYLINDER,AXIS_Y,.62,.75,.62,0,.1,0,1,0,0),P("battlement",SH_TUBE,AXIS_Y,.9,.18,.9,0,.58,0,2,3,1),P("door",SH_PLATE,AXIS_Z,.25,.42,.05,0,-.55,.45,0,2,1),P("window",SH_PLATE,AXIS_Z,.16,.2,.05,.25,.12,.32,2,1,1),P("mast",SH_ROD,AXIS_Y,.07,.65,.07,0,1.0,0,3,3,1),P("flag",SH_PLATE,AXIS_Z,.4,.22,.04,.2,1.2,0,6,2,2),P("roof",SH_CONE,AXIS_Y,.7,.35,.7,0,.95,0,3,2,2),P("trim",SH_PLATE,AXIS_Y,.7,.05,.7,0,-.22,0,1,3,2)),
};
// Explicit authored kit data. P records a real part's geometry, assembled
// target (kit-local), dependency, colour and runner. Compact macro notation
// keeps the 200 records readable on a 3DS-sized codebase.
// Sized to what is actually authored. It used to be declared [20], which left
// twelve zeroed entries that nothing indexes - a kit added past the eighth would
// have looked present and drawn nothing.
static const kitDef kitDefs[8] = {
KIT("Star Trophy",1, P("star",SH_STAR,AXIS_Z,.68,.68,.12,0,.63,.05,-1,3,0),P("cup",SH_CONE,AXIS_Y,.48,.38,.20,0,.10,0,0,3,0),P("stem",SH_ROD,AXIS_Y,.12,.42,.12,0,-.30,0,1,0,0),P("base",SH_CYLINDER,AXIS_Y,.58,.14,.58,0,-.58,0,2,3,0),PAIR_X("point","point",SH_PLATE,AXIS_Z,.12,.10,.04,.28,.53,.00,0,3,0),P("badge",SH_PLATE,AXIS_Z,.22,.14,.04,0,.10,.19,1,0,0),P("rim",SH_CYLINDER,AXIS_Y,.50,.02,.50,0,.27,0,1,3,0),P("foot",SH_BOX,AXIS_Y,.42,.09,.28,0,-.70,0,3,0,0),P("shine",SH_PLATE,AXIS_Z,.08,.16,.03,.20,.16,.22,1,3,0)),
KIT("Small Cactus",1, P("pot",SH_CONE,AXIS_Y,.62,.48,.62,0,-.7,0,-1,2,0),P("soil",SH_CYLINDER,AXIS_Y,.48,.1,.48,0,-.43,0,0,3,0),P("trunk",SH_CYLINDER,AXIS_Y,.26,1.0,.26,0,.05,0,1,1,0),P("left arm",SH_ROD,AXIS_X,.6,.16,.16,-.36,.12,0,2,1,0),P("right arm",SH_ROD,AXIS_X,.6,.16,.16,.36,.28,0,2,1,0),P("flower",SH_CONE,AXIS_Y,.22,.22,.22,0,.62,0,2,3,0),PAIR_X("rib","rib",SH_PLATE,AXIS_Z,.05,.7,.04,.13,.05,.14,2,1,0),P("base",SH_BOX,AXIS_Y,.5,.1,.5,0,-.98,0,0,2,0),P("tag",SH_PLATE,AXIS_Z,.22,.12,.04,.28,-.72,.3,0,3,0)),
KIT("Sailboat",1, P("hull",SH_WEDGE,AXIS_Y,1.5,.45,.5,0,-.55,0,-1,1,0),P("keel",SH_ROD,AXIS_Y,.12,.6,.12,0,-.85,0,0,3,0),P("mast",SH_ROD,AXIS_Y,.1,1.6,.1,0,.35,0,0,2,0),P("main sail",SH_WEDGE,AXIS_Y,.75,1.0,.05,.38,.48,0,2,0,0),P("front sail",SH_WEDGE,AXIS_Y,.55,.7,.05,-.38,.28,0,2,0,0),P("rudder",SH_PLATE,AXIS_Z,.22,.4,.05,-.78,-.48,0,0,2,0),P("deck",SH_PLATE,AXIS_Y,1.1,.08,.42,0,-.25,0,0,2,0),P("flag",SH_PLATE,AXIS_Z,.35,.2,.04,0,1.15,0,2,3,0),P("boom",SH_ROD,AXIS_X,.9,.08,.08,.25,.0,0,2,0,0),P("anchor",SH_ROD,AXIS_Y,.12,.32,.12,.7,-.65,0,0,3,0)),
// Nose cone, both side fins and the rear fin ask to sit exactly on the tube's
// surface rather than inside it - the cone's base on the body's top face, each
// fin root on the 0.25 radius. resolveAssemblyTargets pulls each of them back to
// the join minimum it insists on, so what the model actually shows is a 0.080
// seam instead of the 0.200 the cone was buried by and the 0.105/0.145 the fins
// were. Zero is not reachable here by design; asking for it is what gets the
// seam down to the floor.
//
// The flame and the stand are the one pair here that had to be re-authored
// rather than nudged. Both used to hang off the nozzle, and the resolver above
// is why that could never work: the stand is thin, so its join minimum is only
// 0.042, and a child is pulled to within parentHalf + childHalf - join of its
// parent whatever the table asks for. That put the stand at y -1.068 no matter
// what was written for it, while a 0.45 flame reached down to -1.405 - the
// stand ended up buried inside the flame by 0.120 with the flame's tip out the
// underside. Shortening the flame alone cannot fix it: to clear a stand pinned
// at -1.068 the flame would have to be under 0.04 tall. So the stand now hangs
// off the flame (parent 8) instead of the nozzle, and the flame is shortened to
// 0.28 so the resolver seats the two exactly at the 0.042 join. The cost is one
// step of build order - the flame must be fitted before the base plate.
KIT("Rocket",1, P("nose cone",SH_CONE,AXIS_Y,.48,.7,.48,0,1.05,0,-1,2,0),P("rocket body",SH_CYLINDER,AXIS_Y,.5,1.4,.5,0,.0,0,0,0,0),PAIR_X("fin left","fin right",SH_WEDGE,AXIS_X,.55,.35,.08,.525,-.48,0,1,2,0),P("fin rear",SH_WEDGE,AXIS_Z,.08,.35,.55,0,-.48,-.525,1,2,0),P("nozzle",SH_BELL,AXIS_Y,.36,.35,.36,0,-.875,0,1,3,0),P("window",SH_CYLINDER,AXIS_Z,.16,.05,.16,0,.18,.25,1,1,0),P("band",SH_CYLINDER,AXIS_Y,.54,.12,.54,0,-.22,0,1,2,0),P("flame",SH_CONE,AXIS_Y,.22,.28,.22,0,-1.18,0,5,3,0),P("stand",SH_BOX,AXIS_Y,.8,.12,.6,0,-1.28,0,8,0,0)),
// Table fan reads from its silhouette: a circular guard in front of a motor,
// three radial blades, a narrow upright neck, and a weighted desk base.
// Two runners: the fan body and rear guard first, then the front guard and
// three separately cut blades.  This matches how a real two-shell fan cage
// encloses its rotor during final assembly.
KIT("Desk Fan",2, P("base",SH_BOX,AXIS_Y,1.18,.18,.72,0,-.88,0,-1,0,0),PAIR_X("button left","button right",SH_CYLINDER,AXIS_Y,.15,.08,.15,.17,-.72,-.28,0,3,0),P("triangular stand",SH_WEDGE,AXIS_Y,.62,.65,.45,0,-.45,.15,0,1,0),P("motor and hub",SH_CYLINDER,AXIS_Z,.46,.46,.46,0,.10,-.02,3,0,0),P("back grill",SH_GRILL,AXIS_Z,1.22,1.22,.12,0,.12,-.20,4,0,0),P("front grill",SH_GRILL,AXIS_Z,1.22,1.22,.12,0,.12,-.53,5,0,1),PR("blade one",SH_FAN_BLADE,AXIS_Z,.48,.32,.10,0,.10,-.36,0.00f,4,1,1),PR("blade two",SH_FAN_BLADE,AXIS_Z,.48,.32,.10,0,.10,-.36,2.0944f,4,1,1),PR("blade three",SH_FAN_BLADE,AXIS_Z,.48,.32,.10,0,.10,-.36,4.1888f,4,1,1)),
KIT("Friendly Robot",1, P("head",SH_BOX,AXIS_Y,.6,.5,.4,0,.55,0,-1,3,0),P("face",SH_PLATE,AXIS_Z,.4,.18,.04,0,.55,.23,0,1,0),P("torso",SH_BOX,AXIS_Y,.8,.7,.35,0,.0,0,0,0,0),P("hip",SH_BOX,AXIS_Y,.5,.18,.3,0,-.43,0,2,1,0),PAIR_X("arm L","arm R",SH_ROD,AXIS_Y,.16,.65,.16,.52,-.02,0,2,1,0),PAIR_X("leg L","leg R",SH_ROD,AXIS_Y,.22,.7,.22,.22,-.78,0,3,3,0),P("antenna",SH_ROD,AXIS_Y,.08,.35,.08,0,.95,0,0,2,0),P("pack",SH_BOX,AXIS_Y,.45,.5,.18,0,.0,-.28,2,0,0)),
KIT("Space Rover",1, P("chassis",SH_WEDGE,AXIS_Y,1.3,.35,.7,0,-.45,0,-1,0,0),P("cabin",SH_BOX,AXIS_Y,.6,.45,.5,0,-.05,0,0,3,0),P("antenna",SH_ROD,AXIS_Y,.06,.55,.06,0,.42,0,1,2,0),PAIR_X("wheel FL","wheel FR",SH_WHEEL,AXIS_X,.18,.36,.36,.5,-.72,.32,0,3,0),PAIR_X("wheel RL","wheel RR",SH_WHEEL,AXIS_X,.18,.36,.36,.5,-.72,-.32,0,3,0),P("bumper",SH_ROD,AXIS_X,1.1,.1,.1,0,-.48,.42,0,2,0),P("camera",SH_CYLINDER,AXIS_Z,.14,.1,.14,.25,.08,.3,1,1,0),P("panel",SH_PLATE,AXIS_Y,.6,.04,.4,-.35,-.05,.15,1,3,0)),
KIT("Fire Engine",2, P("cab",SH_BOX,AXIS_Y,.65,.6,.55,-.35,-.1,0,-1,2,0),P("body",SH_BOX,AXIS_Y,1.0,.55,.6,.35,-.2,0,0,2,0),P("ladder",SH_ROD,AXIS_X,1.0,.1,.1,.55,.3,0,1,0,1),PAIR_X("wheel","wheel",SH_WHEEL,AXIS_X,.16,.3,.3,.48,-.7,.32,0,3,1),PAIR_X("wheel","wheel",SH_WHEEL,AXIS_X,.16,.3,.3,.48,-.7,-.32,0,3,1),P("light",SH_PLATE,AXIS_Y,.3,.05,.18,-.35,.25,.25,0,1,1),P("hose",SH_CYLINDER,AXIS_Z,.22,.08,.22,.45,.0,.34,1,3,1),P("bumper",SH_ROD,AXIS_X,1.2,.1,.1,.35,-.45,.05,1,0,1)),
};

// Late kits: compact, multi-runner authored silhouettes. Their records remain
// explicit even where a repeated limb/wheel uses the same dimensions.
//
// Three joins here were authored against the box a part occupies rather than
// against the solid it actually draws inside it, which is why they looked
// unattached on the bench:
//
//   Pirate bow    - was an AXIS_X wedge, so its full face was at the front and
//                   it tapered backwards to a line 0.38 above the hull's own
//                   tapering tip. It is now an AXIS_Y wedge sharing the hull's
//                   base plane exactly, a smaller prow of the same shape.
//   Loco coupler  - the rod keeps only min(size[1],size[2]) as its diameter, so
//                   the drawn bar was 0.08 across in a 0.32 box and met the
//                   tender only at one bottom corner. Moved up into the body.
//   Heli skids    - same rule: 0.50 of authored height was thrown away and the
//                   drawn 0.10 bar hung clear under the cabin. Re-authored at
//                   0.16 square so the box and the bar are the same object.
static const kitDef kitDefsLate[6] = {
	KIT("Pirate Ship",3,P("hull",SH_WEDGE,AXIS_Y,1.6,.5,.6,0,-.55,0,-1,2,0),P("deck",SH_PLATE,AXIS_Y,1.25,.08,.5,0,-.28,0,0,2,0),P("mast front",SH_ROD,AXIS_Y,.1,1.3,.1,-.35,.38,0,1,3,1),P("mast rear",SH_ROD,AXIS_Y,.1,1.15,.1,.38,.3,0,1,3,1),P("sail front",SH_WEDGE,AXIS_Y,.55,.7,.05,-.1,.55,0,2,0,1),P("sail rear",SH_WEDGE,AXIS_Y,.5,.62,.05,.62,.48,0,3,0,1),P("bow",SH_WEDGE,AXIS_Y,.5,.35,.45,-.85,-.625,0,0,2,2),P("rudder",SH_PLATE,AXIS_Z,.2,.4,.05,.82,-.45,0,0,2,2),P("cannon",SH_ROD,AXIS_X,.42,.12,.12,.1,-.22,.22,1,3,2),P("anchor",SH_ROD,AXIS_Y,.12,.35,.12,-.65,-.7,0,0,3,2)),
	KIT("Steam Locomotive",3,P("boiler",SH_CYLINDER,AXIS_X,1.25,.55,.55,0,-.25,0,-1,3,0),P("cab",SH_BOX,AXIS_Y,.55,.65,.6,.55,.0,0,0,2,0),P("chimney",SH_TUBE,AXIS_Y,.3,.55,.3,-.38,.28,0,0,3,0),PAIR_Z("wheel FL","wheel FR",SH_WHEEL,AXIS_X,.16,.34,.34,-.45,-.75,.35,0,2,1),PAIR_Z("wheel RL","wheel RR",SH_WHEEL,AXIS_X,.16,.34,.34,.3,-.75,.35,0,2,1),P("tender",SH_BOX,AXIS_Y,.6,.5,.55,1.0,-.35,0,1,3,2),P("lamp",SH_CYLINDER,AXIS_Z,.14,.08,.14,-.72,-.2,.28,0,1,2),P("coupler",SH_ROD,AXIS_X,.32,.08,.08,1.35,-.42,0,7,3,2)),
	KIT("Spacecraft",4,P("core",SH_CYLINDER,AXIS_X,1.3,.5,.5,0,-.15,0,-1,0,0),P("nose",SH_CONE,AXIS_X,.5,.5,.5,-.85,-.15,0,0,0,0),PAIR_Z("pod L","pod R",SH_CYLINDER,AXIS_X,.65,.3,.3,.05,-.18,.5,0,1,1),PAIR_Z("wing L","wing R",SH_WEDGE,AXIS_Z,.7,.1,.95,.18,-.15,.72,0,2,1),PAIR_Z("engine L","engine R",SH_CONE,AXIS_X,.3,.28,.3,.78,-.18,.3,0,3,2),P("canopy",SH_DOME,AXIS_Y,.42,.25,.3,-.15,.2,0,0,1,3),P("antenna",SH_ROD,AXIS_Y,.06,.45,.06,.2,.45,0,8,3,3)),
	KIT("Dinosaur",4,P("body",SH_CYLINDER,AXIS_X,1.3,.55,.55,0,-.3,0,-1,1,0),P("head",SH_WEDGE,AXIS_X,.55,.45,.45,-.8,-.05,0,0,1,0),P("tail",SH_CONE,AXIS_X,.85,.3,.3,.9,-.35,0,0,1,1),PAIR_Z("leg FL","leg FR",SH_ROD,AXIS_Y,.18,.65,.18,-.38,-.75,.28,0,3,1),P("leg RL",SH_ROD,AXIS_Y,.18,.65,.18,.35,-.75,.28,0,3,2),P("leg RR",SH_ROD,AXIS_Y,.18,.65,.18,.35,-.75,-.28,0,3,2),P("plate L",SH_WEDGE,AXIS_Y,.25,.35,.12,-.1,.25,.22,0,2,2),P("plate R",SH_WEDGE,AXIS_Y,.25,.35,.12,.25,.25,-.22,0,2,3),P("horn",SH_CONE,AXIS_X,.16,.2,.16,-1.05,.15,0,1,3,3)),
	KIT("Rescue Helicopter",4,P("cabin",SH_BOX,AXIS_Y,.8,.5,.55,-.25,-.15,0,-1,2,0),P("nose",SH_WEDGE,AXIS_X,.45,.35,.45,-.75,-.18,0,0,2,0),P("tail boom",SH_ROD,AXIS_X,1.25,.16,.16,.65,-.1,0,0,1,1),P("tail rotor",SH_CYLINDER,AXIS_X,.32,.06,.32,1.25,-.08,0,2,3,1),PAIR_Z("skid L","skid R",SH_ROD,AXIS_X,.9,.16,.16,-.05,-.41,.24,0,3,2),P("door",SH_PLATE,AXIS_Z,.35,.35,.04,-.1,-.1,.3,0,1,2),P("rotor hub",SH_CYLINDER,AXIS_Y,.2,.12,.2,-.05,.35,0,0,3,3),P("rotor blade",SH_PLATE,AXIS_X,1.5,.05,.12,-.05,.45,0,7,3,3),P("winch",SH_ROD,AXIS_Y,.08,.35,.08,-.35,-.10,.28,0,1,3)),
	KIT("Large Mech",4,P("torso",SH_BOX,AXIS_Y,.8,.7,.4,0,.0,0,-1,1,0),P("head",SH_BOX,AXIS_Y,.45,.4,.35,0,.58,0,0,0,0),PAIR_X("shoulder L","shoulder R",SH_WEDGE,AXIS_X,.45,.3,.35,.55,.22,0,0,2,1),P("arm L",SH_ROD,AXIS_Y,.18,.7,.18,-.62,-.25,0,2,1,2),P("arm R",SH_ROD,AXIS_Y,.18,.7,.18,.62,-.25,0,3,1,2),PAIR_X("leg L","leg R",SH_ROD,AXIS_Y,.25,.85,.25,.25,-.82,0,0,3,3),P("backpack",SH_BOX,AXIS_Y,.55,.5,.2,0,.0,-.32,0,1,3),P("shield",SH_CURVE,AXIS_Z,.45,.7,.08,-.95,-.18,.05,4,2,3))
};

const char* meshKitName(void) { return activeKitName; }
int meshKitRunnerCount(void) { return activeRunners; }
float meshBenchX(void) { return BENCH_X; }
float meshBenchZ(void) { return BENCH_Z; }
float meshLooseX(void) { return LOOSE_X; }
float meshLooseZ(void) { return LOOSE_Z; }
float meshMatX(void) { return MAT_X; }
float meshMatZ(void) { return MAT_Z; }
float meshStandX(void) { return STAND_X; }
float meshStandZ(void) { return STAND_Z; }
float meshRunnerX(void) { return RUNNER_X; }
// The runner lies inside the kit box, so it follows the box rather than sitting
// at a fixed spot the box may have grown away from.
float meshRunnerZ(void) { return kitBoxCentreZ(); }
// The reserved home on the bare wood for one cut part, keyed on the part rather
// than on how many have been cut, so the layout does not depend on cut order.
void meshLooseSlot(int part, float out[2])
{
	if (part < 0 || part >= partCount) { out[0] = LOOSE_X; out[1] = LOOSE_Z; return; }
	out[0] = looseHome[part][0];
	out[1] = looseHome[part][1];
}
// Cut parts now sit on bare wood beside the mat, not on the mat itself.
float meshLooseTopY(void) { return DESK_Y + DESK_T*0.5f; }

// A box as written in the part table: centre then size.
typedef struct { float cx, cy, cz, sx, sy, sz; } boxSpec;

// One part of the kit as authored. The gate is split at the point the nipper
// would cut it, so `stub` and `nub` are the two halves of one gate.
typedef struct
{
	const char* name;
	boxSpec stub; // runner side of the gate - stays on the frame
	boxSpec nub;  // part side of the gate - stays on the part
	boxSpec body;
	// Where the drawn silhouette sits relative to body's centre, and how much
	// room it needs, in x and y.  For every shape but the fan blade this is
	// no offset and body's own size; see shapeSilhouette.
	float silOff[2];
	float silExt[2];
} partSpec;
static partSpec activeSpecs[MAX_PARTS];
static float activeTarget[MAX_PARTS][3];
static int activeParent[MAX_PARTS];

// The fixed sprue data, and there is very little of it left.
//
// There used to be a hand-written table here holding a position and a size for
// each of ten parts, with a long comment describing the sprue those numbers laid
// out - a robot, with an armour plate at the left end and the arms paired up on
// the cross-bar. None of it survives into the game. The packing pass below now
// measures every part off the kit definition and overwrites every position,
// every size and every name before anything is drawn, because parts sit on the
// frame at the true size they will be when they come off it and no two of the
// twenty kits are the same shape. The table was describing a sprue that has not
// been built since, so it has been taken out rather than left to be read as if
// it still meant something.
//
// What genuinely is fixed is below. Every kit is authored with the same number
// of parts, and every gate is one 0.09 cube split in half where the nipper would
// bite: the stub half stays on the frame, the nub half travels with the part. A
// player who learns where to aim on one part aims the same way on all of them.
#define PART_SPEC_COUNT 10
#define GATE_CUBE       0.09f

// Sockets are no longer a fixed table. Every kit authors its own assembly
// targets in its kitDef, and resolveAssemblyTargets turns those into the socket
// list at build time, so the old hand-written robot socketSpecs table is gone.

// Appends an axis-aligned box with per-face normals. Face order and winding
// match the devkitPro cube example, so the default cull mode is correct.
static void addBox(float cx, float cy, float cz, float sx, float sy, float sz)
{
	if (!meshRoomFor(VERTS_PER_BOX)) return;

	const float x0 = cx - sx*0.5f, x1 = cx + sx*0.5f;
	const float y0 = cy - sy*0.5f, y1 = cy + sy*0.5f;
	const float z0 = cz - sz*0.5f, z1 = cz + sz*0.5f;

	const vertex box[VERTS_PER_BOX] =
	{
		// PZ
		{ {x0, y0, z1}, {0.0f, 0.0f, +1.0f} },
		{ {x1, y0, z1}, {0.0f, 0.0f, +1.0f} },
		{ {x1, y1, z1}, {0.0f, 0.0f, +1.0f} },
		{ {x1, y1, z1}, {0.0f, 0.0f, +1.0f} },
		{ {x0, y1, z1}, {0.0f, 0.0f, +1.0f} },
		{ {x0, y0, z1}, {0.0f, 0.0f, +1.0f} },

		// MZ
		{ {x0, y0, z0}, {0.0f, 0.0f, -1.0f} },
		{ {x0, y1, z0}, {0.0f, 0.0f, -1.0f} },
		{ {x1, y1, z0}, {0.0f, 0.0f, -1.0f} },
		{ {x1, y1, z0}, {0.0f, 0.0f, -1.0f} },
		{ {x1, y0, z0}, {0.0f, 0.0f, -1.0f} },
		{ {x0, y0, z0}, {0.0f, 0.0f, -1.0f} },

		// PX
		{ {x1, y0, z0}, {+1.0f, 0.0f, 0.0f} },
		{ {x1, y1, z0}, {+1.0f, 0.0f, 0.0f} },
		{ {x1, y1, z1}, {+1.0f, 0.0f, 0.0f} },
		{ {x1, y1, z1}, {+1.0f, 0.0f, 0.0f} },
		{ {x1, y0, z1}, {+1.0f, 0.0f, 0.0f} },
		{ {x1, y0, z0}, {+1.0f, 0.0f, 0.0f} },

		// MX
		{ {x0, y0, z0}, {-1.0f, 0.0f, 0.0f} },
		{ {x0, y0, z1}, {-1.0f, 0.0f, 0.0f} },
		{ {x0, y1, z1}, {-1.0f, 0.0f, 0.0f} },
		{ {x0, y1, z1}, {-1.0f, 0.0f, 0.0f} },
		{ {x0, y1, z0}, {-1.0f, 0.0f, 0.0f} },
		{ {x0, y0, z0}, {-1.0f, 0.0f, 0.0f} },

		// PY
		{ {x0, y1, z0}, {0.0f, +1.0f, 0.0f} },
		{ {x0, y1, z1}, {0.0f, +1.0f, 0.0f} },
		{ {x1, y1, z1}, {0.0f, +1.0f, 0.0f} },
		{ {x1, y1, z1}, {0.0f, +1.0f, 0.0f} },
		{ {x1, y1, z0}, {0.0f, +1.0f, 0.0f} },
		{ {x0, y1, z0}, {0.0f, +1.0f, 0.0f} },

		// MY
		{ {x0, y0, z0}, {0.0f, -1.0f, 0.0f} },
		{ {x1, y0, z0}, {0.0f, -1.0f, 0.0f} },
		{ {x1, y0, z1}, {0.0f, -1.0f, 0.0f} },
		{ {x1, y0, z1}, {0.0f, -1.0f, 0.0f} },
		{ {x0, y0, z1}, {0.0f, -1.0f, 0.0f} },
		{ {x0, y0, z0}, {0.0f, -1.0f, 0.0f} },
	};

	memcpy(&meshData[meshCount], box, sizeof(box));
	meshCount += VERTS_PER_BOX;
}

static void addBoxSpec(const boxSpec* b)
{
	addBox(b->cx, b->cy, b->cz, b->sx, b->sy, b->sz);
}

// Local coordinates use Y as their long axis.  These helpers rotate both
// positions and normals, rather than faking X/Z versions by swapping sizes.
//
// These must be proper rotations (determinant +1).  A bare axis swap is a
// reflection, and a reflection flips the handedness of every triangle it
// touches, so the primitive would be culled inside-out on X and Z but not on Y.
// AXIS_X turns local +Y onto world +X, AXIS_Z turns local +Y onto world +Z.
static void axisMap(partAxis axis, float cx, float cy, float cz,
	float lx, float ly, float lz, float out[3])
{
	if (axis == AXIS_X) { out[0] = cx + ly; out[1] = cy - lx; out[2] = cz + lz; }
	else if (axis == AXIS_Z) { out[0] = cx + lx; out[1] = cy - lz; out[2] = cz + ly; }
	else { out[0] = cx + lx; out[1] = cy + ly; out[2] = cz + lz; }
}

static void axisNormal(partAxis axis, float lx, float ly, float lz, float out[3])
{
	axisMap(axis, 0.0f, 0.0f, 0.0f, lx, ly, lz, out);
}

static void emitAxisVertex(partAxis axis, float cx, float cy, float cz,
	float x, float y, float z, float nx, float ny, float nz)
{
	if (!meshRoomFor(1)) return;
	axisMap(axis, cx, cy, cz, x, y, z, meshData[meshCount].position);
	axisNormal(axis, nx, ny, nz, meshData[meshCount].normal);
	// meshData is never cleared between rebuilds, only meshCount resets - a
	// stale uv from a previous kit's applyAtlasUV() pass would otherwise leak
	// through. Real values are assigned afterwards, in meshBuildKit.
	meshData[meshCount].uv[0] = meshData[meshCount].uv[1] = 0.0f;
	meshCount++;
}

// Low-poly round primitives keep the kit silhouettes readable at 320x240.
// They are deliberately eight-sided: smooth enough for a rocket nose or wheel,
// cheap enough for an Original 3DS, and still give a conservative box bound for
// the existing stylus picker.
static void addCylinder8Axis(float cx, float cy, float cz, const float s[3], partAxis axis, bool pointed)
{
	float r = axis == AXIS_Y ? fminf(s[0], s[2])*.5f :
		(axis == AXIS_X ? fminf(s[1], s[2])*.5f : fminf(s[0], s[1])*.5f);
	float h = axis == AXIS_Y ? s[1] : (axis == AXIS_X ? s[0] : s[2]);
	if (!meshRoomFor(96)) return;
	// Every triangle below is wound counter-clockwise seen from outside the solid,
	// matching addBox, because the whole scene draws in one VBO under one
	// GPU_CULL_BACK_CCW state and a single odd primitive renders inside-out.
	for (int i = 0; i < 8; i++) {
		float a0 = 6.2831853f*i/8.0f, a1 = 6.2831853f*(i+1)/8.0f;
		float x0 = r*cosf(a0), z0 = r*sinf(a0), x1 = r*cosf(a1), z1 = r*sinf(a1);
		float nx = cosf((a0+a1)*.5f), nz = sinf((a0+a1)*.5f);
		float tx0 = pointed ? 0.0f : x0, tz0 = pointed ? 0.0f : z0;
		float tx1 = pointed ? 0.0f : x1, tz1 = pointed ? 0.0f : z1;
		emitAxisVertex(axis,cx,cy,cz,x0,-h*.5f,z0,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,tx1,h*.5f,tz1,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x1,-h*.5f,z1,nx,0,nz);
		if (!pointed) {
			// Second half of the side quad. A cone's top ring collapses onto the
			// apex, which would make this triangle zero-area, so it is skipped.
			emitAxisVertex(axis,cx,cy,cz,tx1,h*.5f,tz1,nx,0,nz);
			emitAxisVertex(axis,cx,cy,cz,x0,-h*.5f,z0,nx,0,nz);
			emitAxisVertex(axis,cx,cy,cz,tx0,h*.5f,tz0,nx,0,nz);
		}
		// bottom cap; a normal is essential for the fixed-function lighting path.
		emitAxisVertex(axis,cx,cy,cz,0,-h*.5f,0,0,-1,0);
		emitAxisVertex(axis,cx,cy,cz,x0,-h*.5f,z0,0,-1,0);
		emitAxisVertex(axis,cx,cy,cz,x1,-h*.5f,z1,0,-1,0);
		if (!pointed) {
			emitAxisVertex(axis,cx,cy,cz,0,h*.5f,0,0,1,0);
			emitAxisVertex(axis,cx,cy,cz,x1,h*.5f,z1,0,1,0);
			emitAxisVertex(axis,cx,cy,cz,x0,h*.5f,z0,0,1,0);
		}
	}
}

// One quad of a cylindrical wall, wound so its normal points away from the axis
// when ns is +1 and back towards it when ns is -1. The inward case is what makes
// a recess look like a recess rather than a hole: with only outward faces the
// inside of a tread lip is culled away and the mat shows through the wheel.
static void wheelBand(partAxis axis, float cx, float cy, float cz,
	float rr, float a0, float a1, float lo, float hi, float ns)
{
	const float x0 = rr*cosf(a0), z0 = rr*sinf(a0);
	const float x1 = rr*cosf(a1), z1 = rr*sinf(a1);
	const float nx = ns*cosf((a0+a1)*.5f), nz = ns*sinf((a0+a1)*.5f);
	if (ns > 0.0f) {
		emitAxisVertex(axis,cx,cy,cz,x0,lo,z0,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x1,hi,z1,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x1,lo,z1,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x1,hi,z1,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x0,lo,z0,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x0,hi,z0,nx,0,nz);
	} else {
		emitAxisVertex(axis,cx,cy,cz,x1,lo,z1,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x1,hi,z1,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x0,lo,z0,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x0,hi,z0,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x0,lo,z0,nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz,x1,hi,z1,nx,0,nz);
	}
}

// One quad of a flat ring facing along local +Y (ny +1) or -Y (ny -1). A zero
// inner radius collapses it to the middle triangle of a fan, so the same helper
// draws the hub cap and the ring around it without a second code path.
static void wheelRing(partAxis axis, float cx, float cy, float cz,
	float rin, float rout, float a0, float a1, float y, float ny)
{
	const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
	if (ny > 0.0f) {
		emitAxisVertex(axis,cx,cy,cz,rin *c1,y,rin *s1,0,1,0);
		emitAxisVertex(axis,cx,cy,cz,rout*c1,y,rout*s1,0,1,0);
		emitAxisVertex(axis,cx,cy,cz,rout*c0,y,rout*s0,0,1,0);
		if (rin > 0.0f) {
			emitAxisVertex(axis,cx,cy,cz,rin *c1,y,rin *s1,0,1,0);
			emitAxisVertex(axis,cx,cy,cz,rout*c0,y,rout*s0,0,1,0);
			emitAxisVertex(axis,cx,cy,cz,rin *c0,y,rin *s0,0,1,0);
		}
	} else {
		emitAxisVertex(axis,cx,cy,cz,rin *c0,y,rin *s0,0,-1,0);
		emitAxisVertex(axis,cx,cy,cz,rout*c0,y,rout*s0,0,-1,0);
		emitAxisVertex(axis,cx,cy,cz,rout*c1,y,rout*s1,0,-1,0);
		if (rin > 0.0f) {
			emitAxisVertex(axis,cx,cy,cz,rin *c0,y,rin *s0,0,-1,0);
			emitAxisVertex(axis,cx,cy,cz,rout*c1,y,rout*s1,0,-1,0);
			emitAxisVertex(axis,cx,cy,cz,rin *c1,y,rin *s1,0,-1,0);
		}
	}
}

// A tyre instead of a drum: a tread band, a rim face set back behind it on both
// sides, and a hub boss standing back out of that.
//
// A wheel is the most repeated part in the whole kit set - eighteen of them
// across five vehicles - and every one of them came out of the same eight-sided
// cylinder that draws a boiler, a window and a periscope, so a car read as a
// body with four blank bobbins under it. The silhouette is deliberately
// unchanged, because the authored AABB is still the tap bound and still what
// packs on the runner; what changes is the face, where one flat disc of colour
// becomes a ring, a shadowed step and a hub. That step is the detail, and it is
// visible from every angle the build camera can reach, including straight on.
//
// Twelve sides rather than the eight the other round parts use: a wheel is the
// one part the player sees edge-on against a straight chassis line, where an
// octagon reads as an octagon.
#define WHEEL_SEG 12
static void addWheelAxis(float cx, float cy, float cz, const float s[3], partAxis axis)
{
	// Radius and width are read off the authored box exactly as the cylinder
	// emitter reads them, so a recipe can swap SH_CYLINDER for SH_WHEEL without
	// re-authoring any numbers.
	const float r = axis == AXIS_Y ? fminf(s[0], s[2])*.5f :
		(axis == AXIS_X ? fminf(s[1], s[2])*.5f : fminf(s[0], s[1])*.5f);
	const float h = axis == AXIS_Y ? s[1] : (axis == AXIS_X ? s[0] : s[2]);
	const float rh = r*.55f;
	// Capped against both width and radius: a narrow wheel would otherwise be
	// dished until the two recesses met in the middle, and a large-radius one
	// would get a recess too shallow to shade.
	const float dish = fminf(h*.28f, r*.20f);
	if (!meshRoomFor(WHEEL_SEG*6 + 2*(WHEEL_SEG*6*3 + WHEEL_SEG*3))) return;
	for (int i = 0; i < WHEEL_SEG; i++)
	{
		const float a0 = 6.2831853f*i/WHEEL_SEG, a1 = 6.2831853f*(i+1)/WHEEL_SEG;
		wheelBand(axis,cx,cy,cz,r,a0,a1,-h*.5f,h*.5f,1.0f);      // tread
		for (int side = 0; side < 2; side++)
		{
			const float t    = side ? -1.0f : 1.0f;
			const float yOut = t*h*.5f, yIn = t*(h*.5f - dish);
			const float lo   = fminf(yOut, yIn), hi = fmaxf(yOut, yIn);
			wheelBand(axis,cx,cy,cz,r ,a0,a1,lo,hi,-1.0f);  // inside of the tread lip
			wheelRing(axis,cx,cy,cz,rh,r ,a0,a1,yIn ,t);    // rim face, set back
			wheelBand(axis,cx,cy,cz,rh,a0,a1,lo,hi, 1.0f);  // side of the hub boss
			wheelRing(axis,cx,cy,cz,0 ,rh,a0,a1,yOut,t);    // hub cap
		}
	}
}

static void addWedgeAxis(float cx, float cy, float cz, const float s[3], partAxis axis)
{
	// A true triangular prism: rectangular base, ridge along local Z, five faces
	// closed by eight triangles.  Every triangle is wound counter-clockwise seen
	// from outside, to match addBox under the scene-wide GPU_CULL_BACK_CCW state.
	float x=s[0]*.5f, y=s[1]*.5f, z=s[2]*.5f;
	const float p[6][3]={{-x,-y,-z},{x,-y,-z},{x,-y,z},{-x,-y,z},{0,y,-z},{0,y,z}};
	const int ix[24]={
		0,1,2, 0,2,3,   // base
		0,3,5, 0,5,4,   // -X slope
		1,4,5, 1,5,2,   // +X slope
		0,4,1,          // -Z end
		3,2,5};         // +Z end
	// The slopes rise x across and 2y up, so their outward normal is (2y, x, 0);
	// a hardcoded constant would only be right on a part whose width equals its
	// height, and the kit wedges range from a flat hull to a tall sail.
	float sl = sqrtf(4.0f*y*y + x*x);
	float su = sl > 1e-6f ? 2.0f*y/sl : 1.0f;
	float sv = sl > 1e-6f ? x/sl : 0.0f;
	const float n[5][3]={{0,-1,0},{-su,sv,0},{su,sv,0},{0,0,-1},{0,0,1}};
	const int fn[8]={0,0,1,1,2,2,3,4};   // normal index per triangle
	if (!meshRoomFor(24)) return;
	for (int t=0; t<8; t++) for (int j=0;j<3;j++) {
		const float* q=p[ix[t*3+j]];
		const float* m=n[fn[t]];
		emitAxisVertex(axis,cx,cy,cz,q[0],q[1],q[2],m[0],m[1],m[2]);
	}
}

// A moulded panel: a flat slab with its four corners cut off.
//
// Plates used to come out of the plain box emitter, which made a deck, a door,
// a flag and a rotor blade all the same shape as the blocks around them. The
// corner cut is what fixes that, and it is a corner cut rather than an edge
// bevel on purpose - at 320x240 a bevel round a .05 thick plate is under a
// pixel, while a cut corner changes the outline, and the outline is the only
// part of a small piece a player can actually read.
//
// The thin direction is measured rather than taken from the part's axis field,
// because that field says which way the piece points, not which way it is flat:
// the helicopter's rotor blade is authored AXIS_X and is thin in Y. Picking the
// smallest of the three sizes is right for every plate in every kit.
static void addPlate(float cx, float cy, float cz, const float s[3])
{
	int t = 0;
	if (s[1] < s[t]) t = 1;
	if (s[2] < s[t]) t = 2;
	// (u, v, t) is a cyclic permutation of (x, y, z) and so stays right-handed,
	// which is what lets the octagon below be wound once and stay correct on
	// whichever of the three faces the plate happens to lie flat against.
	const int u = (t + 1) % 3, v = (t + 2) % 3;

	const float hu = s[u]*.5f, hv = s[v]*.5f, ht = s[t]*.5f;
	const float c  = .22f * fminf(hu, hv);
	const float centre[3] = { cx, cy, cz };

	const float ring[8][2] = {
		{ -hu+c, -hv   }, {  hu-c, -hv   }, {  hu,   -hv+c }, {  hu,    hv-c },
		{  hu-c,  hv   }, { -hu+c,  hv   }, { -hu,    hv-c }, { -hu,   -hv+c },
	};

	if (!meshRoomFor(84)) return;

	// Local emit: (u, v, t) components in, world x/y/z out.
	// uv zeroed explicitly: meshData is never cleared between rebuilds, only
	// meshCount resets, so a stale value from a previous kit's applyAtlasUV()
	// pass would otherwise leak through.
	#define PLATE_VTX(pu, pv, pt, nu, nv, nt) do {                  \
		float* P_ = meshData[meshCount].position;                   \
		float* N_ = meshData[meshCount].normal;                     \
		P_[u] = centre[u] + (pu); P_[v] = centre[v] + (pv);         \
		P_[t] = centre[t] + (pt);                                   \
		N_[u] = (nu); N_[v] = (nv); N_[t] = (nt);                   \
		meshData[meshCount].uv[0] = meshData[meshCount].uv[1] = 0.0f; \
		meshCount++;                                                \
	} while (0)

	for (int i = 1; i < 7; i++)
	{
		PLATE_VTX(ring[0][0], ring[0][1],  ht, 0, 0, +1);
		PLATE_VTX(ring[i][0], ring[i][1],  ht, 0, 0, +1);
		PLATE_VTX(ring[i+1][0], ring[i+1][1], ht, 0, 0, +1);

		PLATE_VTX(ring[0][0], ring[0][1], -ht, 0, 0, -1);
		PLATE_VTX(ring[i+1][0], ring[i+1][1], -ht, 0, 0, -1);
		PLATE_VTX(ring[i][0], ring[i][1], -ht, 0, 0, -1);
	}

	for (int i = 0; i < 8; i++)
	{
		const float* a = ring[i];
		const float* b = ring[(i+1) & 7];
		float ex = b[0]-a[0], ey = b[1]-a[1];
		float el = sqrtf(ex*ex + ey*ey);
		float nu = el > 1e-6f ?  ey/el : 1.0f;
		float nv = el > 1e-6f ? -ex/el : 0.0f;

		PLATE_VTX(a[0], a[1], -ht, nu, nv, 0);
		PLATE_VTX(b[0], b[1], -ht, nu, nv, 0);
		PLATE_VTX(b[0], b[1],  ht, nu, nv, 0);
		PLATE_VTX(b[0], b[1],  ht, nu, nv, 0);
		PLATE_VTX(a[0], a[1],  ht, nu, nv, 0);
		PLATE_VTX(a[0], a[1], -ht, nu, nv, 0);
	}

	#undef PLATE_VTX
}

// How many sides the grill's ring is built from. Eight is full detail - the
// ring is the single costliest primitive this file emits (more triangles per
// part than any box, cylinder or wedge), and a fan kit draws two of them back
// to back, which is what makes the grill the heaviest geometry in the game.
// A grill that is not a large, dominant feature on screen, or one built once
// the shared vertex store is already filling up, is given fewer sides
// instead: the four crosshair spokes are what actually read as "grill" at
// 320x240, so a hexagon - or, under real pressure, a square - still closes
// into a recognisable guard, it just costs less to get there.
static int fanGrillSides(float outer)
{
	float pressure = (float)meshBoxCount() / (float)meshBoxCapacity();
	if (outer < 0.35f || pressure > 0.75f) return 4;
	if (outer < 0.70f || pressure > 0.45f) return 6;
	return 8;
}

// A lightweight circular fan guard: an octagonal annular rim with a vertical
// and horizontal wire.  Unlike a solid cylinder it leaves the blades visible.
// Fan guards always face along Z in the authored kit data.
static void addFanGrill(float cx, float cy, float cz, const float s[3])
{
	float outer=fminf(s[0],s[1])*.5f, inner=outer-.075f, half=s[2]*.5f;
	if (inner < outer*.65f) inner=outer*.65f;
	// The wall normals below divide by outer and by inner. A zero-width part
	// would make both zero; with outer > 0 the clamp above keeps inner >= .65
	// of it, so this one test guards every divide in the function.
	if (outer <= 0.0f) return;
	int sides = fanGrillSides(outer);
	if (!meshRoomFor(sides*24 + 72)) return;
	for (int i=0;i<sides;i++) {
		float a0=6.2831853f*i/sides, a1=6.2831853f*(i+1)/sides;
		float ox0=outer*cosf(a0), oy0=outer*sinf(a0), ox1=outer*cosf(a1), oy1=outer*sinf(a1);
		float ix0=inner*cosf(a0), iy0=inner*sinf(a0), ix1=inner*cosf(a1), iy1=inner*sinf(a1);
		vertex v[24] = {
			// front annular face
			{{cx+ox0,cy+oy0,cz-half},{0,0,-1}},{{cx+ix1,cy+iy1,cz-half},{0,0,-1}},{{cx+ox1,cy+oy1,cz-half},{0,0,-1}},
			{{cx+ox0,cy+oy0,cz-half},{0,0,-1}},{{cx+ix0,cy+iy0,cz-half},{0,0,-1}},{{cx+ix1,cy+iy1,cz-half},{0,0,-1}},
			// back annular face
			{{cx+ox0,cy+oy0,cz+half},{0,0,1}},{{cx+ox1,cy+oy1,cz+half},{0,0,1}},{{cx+ix1,cy+iy1,cz+half},{0,0,1}},
			{{cx+ox0,cy+oy0,cz+half},{0,0,1}},{{cx+ix1,cy+iy1,cz+half},{0,0,1}},{{cx+ix0,cy+iy0,cz+half},{0,0,1}},
			// outside wall and inside wall
			{{cx+ox0,cy+oy0,cz-half},{ox0/outer,oy0/outer,0}},{{cx+ox1,cy+oy1,cz-half},{ox1/outer,oy1/outer,0}},{{cx+ox1,cy+oy1,cz+half},{ox1/outer,oy1/outer,0}},
			{{cx+ox0,cy+oy0,cz-half},{ox0/outer,oy0/outer,0}},{{cx+ox1,cy+oy1,cz+half},{ox1/outer,oy1/outer,0}},{{cx+ox0,cy+oy0,cz+half},{ox0/outer,oy0/outer,0}},
			{{cx+ix0,cy+iy0,cz-half},{-ix0/inner,-iy0/inner,0}},{{cx+ix1,cy+iy1,cz+half},{-ix1/inner,-iy1/inner,0}},{{cx+ix1,cy+iy1,cz-half},{-ix1/inner,-iy1/inner,0}},
			{{cx+ix0,cy+iy0,cz-half},{-ix0/inner,-iy0/inner,0}},{{cx+ix0,cy+iy0,cz+half},{-ix0/inner,-iy0/inner,0}},{{cx+ix1,cy+iy1,cz+half},{-ix1/inner,-iy1/inner,0}}
		};
		memcpy(&meshData[meshCount],v,sizeof(v)); meshCount+=24;
	}
	// Four simple spokes make the ring read as a protective grill at 3DS scale.
	addBox(cx,cy,cz,outer*1.45f,.055f,s[2]);
	addBox(cx,cy,cz,.055f,outer*1.45f,s[2]);
}

// A tapered, swept fan blade instead of a flat triangular wedge.  The root is
// at the hub and the broad outer end curves in the direction of rotation.  It
// is extruded along Z, so the three authored spin angles form a real rotor.
// The blade outline, spun, in the part's own space.  Split out from the drawing
// so the packer can ask where a blade really reaches without drawing one: the
// outline is rooted at the hub rather than centred on it, so a blade is not the
// box it was authored as, and the three spin angles point it three ways.
static void fanBladeOutline(const float s[3], float spin, float p[6][2])
{
	float l=s[0], w=s[1];
	const float q[6][2]={{-.06f*l,-.14f*w},{.18f*l,-.48f*w},{.78f*l,-.42f*w},{1.00f*l,-.08f*w},{.72f*l,.46f*w},{.16f*l,.34f*w}};
	float c=cosf(spin), sn=sinf(spin);
	for(int i=0;i<6;i++){p[i][0]=q[i][0]*c-q[i][1]*sn; p[i][1]=q[i][0]*sn+q[i][1]*c;}
}

static void addFanBlade(float cx, float cy, float cz, const float s[3], float spin)
{
	float h=s[2]*.5f;
	float p[6][2];
	if (!meshRoomFor(72)) return;
	fanBladeOutline(s, spin, p);
	for(int i=1;i<5;i++) {
		vertex f[6]={
			{{cx+p[0][0],cy+p[0][1],cz-h},{0,0,-1}},{{cx+p[i+1][0],cy+p[i+1][1],cz-h},{0,0,-1}},{{cx+p[i][0],cy+p[i][1],cz-h},{0,0,-1}},
			{{cx+p[0][0],cy+p[0][1],cz+h},{0,0,1}},{{cx+p[i][0],cy+p[i][1],cz+h},{0,0,1}},{{cx+p[i+1][0],cy+p[i+1][1],cz+h},{0,0,1}}
		};
		memcpy(&meshData[meshCount],f,sizeof(f)); meshCount+=6;
	}
	for(int i=0;i<6;i++) {
		int j=(i+1)%6; float ex=p[j][0]-p[i][0], ey=p[j][1]-p[i][1];
		float n=sqrtf(ex*ex+ey*ey);
		if (n <= 0.0f) continue;   // zero-area edge: no side wall to extrude
		float nx=ey/n, ny=-ex/n;
		vertex side[6]={
			{{cx+p[i][0],cy+p[i][1],cz-h},{nx,ny,0}},{{cx+p[j][0],cy+p[j][1],cz-h},{nx,ny,0}},{{cx+p[j][0],cy+p[j][1],cz+h},{nx,ny,0}},
			{{cx+p[i][0],cy+p[i][1],cz-h},{nx,ny,0}},{{cx+p[j][0],cy+p[j][1],cz+h},{nx,ny,0}},{{cx+p[i][0],cy+p[i][1],cz+h},{nx,ny,0}}
		};
		memcpy(&meshData[meshCount],side,sizeof(side)); meshCount+=6;
	}
}

// Five-point emblem, thin along Z so it faces the normal assembled-model
// viewing direction.  This is an actual extruded ten-vertex star, not a wedge.
static void addStar5(float cx, float cy, float cz, const float s[3])
{
	float p[10][2];
	float rx = s[0]*.5f, ry = s[1]*.5f, inner = .43f;
	float half = s[2]*.5f;
	if (!meshRoomFor(120)) return;
	for (int i=0; i<10; i++) {
		float radius = (i & 1) ? inner : 1.0f;
		float a = 1.5707963f + 6.2831853f*(float)i/10.0f;
		p[i][0] = cosf(a)*rx*radius;
		p[i][1] = sinf(a)*ry*radius;
	}
	for (int i=0; i<10; i++) {
		int n=(i+1)%10;
		// Front and back are triangulated fans with opposite winding.
		vertex v[6] = {
			{{cx,cy,cz+half},{0,0,1}}, {{cx+p[i][0],cy+p[i][1],cz+half},{0,0,1}}, {{cx+p[n][0],cy+p[n][1],cz+half},{0,0,1}},
			{{cx,cy,cz-half},{0,0,-1}}, {{cx+p[n][0],cy+p[n][1],cz-half},{0,0,-1}}, {{cx+p[i][0],cy+p[i][1],cz-half},{0,0,-1}}
		};
		memcpy(&meshData[meshCount], v, sizeof(v)); meshCount += 6;
		float ex=p[n][0]-p[i][0], ey=p[n][1]-p[i][1];
		// A star authored with a zero width or height collapses its points onto
		// each other, and dividing by that edge length would put NaN into the
		// vertex buffer - which the GPU draws as scattered stray triangles rather
		// than as nothing.
		float len=sqrtf(ex*ex+ey*ey);
		float nx = len > 1e-6f ? ey/len : 0.0f, ny = len > 1e-6f ? -ex/len : 1.0f;
		vertex edge[6] = {
			{{cx+p[i][0],cy+p[i][1],cz-half},{nx,ny,0}}, {{cx+p[n][0],cy+p[n][1],cz-half},{nx,ny,0}}, {{cx+p[n][0],cy+p[n][1],cz+half},{nx,ny,0}},
			{{cx+p[n][0],cy+p[n][1],cz+half},{nx,ny,0}}, {{cx+p[i][0],cy+p[i][1],cz+half},{nx,ny,0}}, {{cx+p[i][0],cy+p[i][1],cz-half},{nx,ny,0}}
		};
		memcpy(&meshData[meshCount], edge, sizeof(edge)); meshCount += 6;
	}
}

// The three sizes a part authors, seen from inside the primitive.
//
// axisMap turns local +Y onto whichever world axis the part was authored along,
// so an emitter that thinks in local x/y/z has to pick its extents up the same
// way round. The cylinder does this inline and gets away with it because all it
// needs is one radius and one height; the three below each need all three, and
// getting the permutation subtly wrong there would show up as a part that is
// the right shape and the wrong proportions only on X or only on Z.
static void axisExtents(partAxis axis, const float s[3], float out[3])
{
	if (axis == AXIS_X)      { out[0] = s[1]; out[1] = s[0]; out[2] = s[2]; }
	else if (axis == AXIS_Z) { out[0] = s[0]; out[1] = s[2]; out[2] = s[1]; }
	else                     { out[0] = s[0]; out[1] = s[1]; out[2] = s[2]; }
}

// All three primitives below are drawn inside the box they were authored as and
// centred on it, so shapeSilhouette has nothing to say about them and the runner
// packer can go on treating the authored size as the truth. The fan blade
// remains the only shape that reaches outside its own box.

// A half-round shell: flat underneath, curved over the top. Canopies, helmets,
// nose blisters, hub caps.
//
// Eight round and three high, for the same reason the cylinder is eight-sided -
// it has to read at 320x240, not survive a close look. It is a squashed
// hemisphere rather than a true one so that it fills whatever box it is given: a
// wide flat box comes out a blister, a cube comes out a dome.
#define DOME_SEG   8
#define DOME_STACK 3
static void addDome8Axis(float cx, float cy, float cz, const float s[3], partAxis axis)
{
	float e[3];
	axisExtents(axis, s, e);
	const float r = fminf(e[0], e[2])*.5f, h = e[1];
	if (r <= 0.0f || h <= 0.0f) return;
	// Two full bands and one apex fan per segment, plus the flat underside.
	if (!meshRoomFor(DOME_SEG*((DOME_STACK-1)*6 + 3 + 3))) return;

	// The surface is the ellipsoid (x/r)^2 + ((y+h/2)/h)^2 + (z/r)^2 = 1, so the
	// normal is that gradient - not the position. On a squashed dome the two are
	// not the same direction, and using the position would light a blister as if
	// it were a ball.
	#define DOME_VTX(th, ph) do {                                           \
		const float ct_ = cosf(th), st_ = sinf(th);                         \
		const float cp_ = cosf(ph), sp_ = sinf(ph);                         \
		float nx_ = cp_*ct_/r, ny_ = sp_/h, nz_ = cp_*st_/r;                \
		float nl_ = sqrtf(nx_*nx_ + ny_*ny_ + nz_*nz_);                     \
		if (nl_ < 1e-6f) nl_ = 1.0f;                                        \
		emitAxisVertex(axis, cx, cy, cz,                                    \
			r*cp_*ct_, -h*.5f + h*sp_, r*cp_*st_,                           \
			nx_/nl_, ny_/nl_, nz_/nl_);                                     \
	} while (0)

	for (int i = 0; i < DOME_SEG; i++)
	{
		const float t0 = 6.2831853f*i/DOME_SEG, t1 = 6.2831853f*(i+1)/DOME_SEG;
		for (int k = 0; k < DOME_STACK; k++)
		{
			const float p0 = 1.5707963f*k/DOME_STACK;
			const float p1 = 1.5707963f*(k+1)/DOME_STACK;
			// Wound to match addBox and the cylinder: counter-clockwise seen
			// from outside, because the whole scene draws under one
			// GPU_CULL_BACK_CCW state and one odd primitive renders inside-out.
			DOME_VTX(t0, p0); DOME_VTX(t1, p1); DOME_VTX(t1, p0);
			// The top band's upper ring collapses onto the apex, which makes the
			// second triangle zero-area, so it is skipped there.
			if (k < DOME_STACK-1)
			{
				DOME_VTX(t1, p1); DOME_VTX(t0, p0); DOME_VTX(t0, p1);
			}
		}
		// Flat underside. A normal is essential for the fixed-function lighting.
		const float x0 = r*cosf(t0), z0 = r*sinf(t0);
		const float x1 = r*cosf(t1), z1 = r*sinf(t1);
		emitAxisVertex(axis,cx,cy,cz, 0,-h*.5f,0,  0,-1,0);
		emitAxisVertex(axis,cx,cy,cz, x0,-h*.5f,z0, 0,-1,0);
		emitAxisVertex(axis,cx,cy,cz, x1,-h*.5f,z1, 0,-1,0);
	}

	#undef DOME_VTX
}

// A pipe: a cylinder with the middle taken out. Exhausts, collars, barrels,
// hose stubs - anything that should read as open at both ends.
//
// The wall is a fraction of the radius rather than an absolute thickness, so a
// .12 collar and a .6 barrel both still look hollow; an absolute wall closes the
// small one up completely and there is no one value that suits both.
static void addTube8Axis(float cx, float cy, float cz, const float s[3], partAxis axis)
{
	float e[3];
	axisExtents(axis, s, e);
	const float outer = fminf(e[0], e[2])*.5f, h = e[1];
	if (outer <= 0.0f || h <= 0.0f) return;
	const float inner = outer*.62f;
	if (!meshRoomFor(8*24)) return;

	for (int i = 0; i < 8; i++)
	{
		const float a0 = 6.2831853f*i/8.0f, a1 = 6.2831853f*(i+1)/8.0f;
		const float nx = cosf((a0+a1)*.5f), nz = sinf((a0+a1)*.5f);
		const float ox0 = outer*cosf(a0), oz0 = outer*sinf(a0);
		const float ox1 = outer*cosf(a1), oz1 = outer*sinf(a1);
		const float ix0 = inner*cosf(a0), iz0 = inner*sinf(a0);
		const float ix1 = inner*cosf(a1), iz1 = inner*sinf(a1);
		const float lo = -h*.5f, hi = h*.5f;

		// Outside wall, facing out - the cylinder's side, unchanged.
		emitAxisVertex(axis,cx,cy,cz, ox0,lo,oz0, nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz, ox1,hi,oz1, nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz, ox1,lo,oz1, nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz, ox1,hi,oz1, nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz, ox0,lo,oz0, nx,0,nz);
		emitAxisVertex(axis,cx,cy,cz, ox0,hi,oz0, nx,0,nz);

		// Inside wall. Both the normal and the winding flip: the bore is seen
		// from the axis, so its outside is the middle of the part.
		emitAxisVertex(axis,cx,cy,cz, ix0,lo,iz0, -nx,0,-nz);
		emitAxisVertex(axis,cx,cy,cz, ix1,lo,iz1, -nx,0,-nz);
		emitAxisVertex(axis,cx,cy,cz, ix1,hi,iz1, -nx,0,-nz);
		emitAxisVertex(axis,cx,cy,cz, ix1,hi,iz1, -nx,0,-nz);
		emitAxisVertex(axis,cx,cy,cz, ix0,hi,iz0, -nx,0,-nz);
		emitAxisVertex(axis,cx,cy,cz, ix0,lo,iz0, -nx,0,-nz);

		// The two rims, each an annular strip rather than a solid cap.
		emitAxisVertex(axis,cx,cy,cz, ox0,lo,oz0, 0,-1,0);
		emitAxisVertex(axis,cx,cy,cz, ix1,lo,iz1, 0,-1,0);
		emitAxisVertex(axis,cx,cy,cz, ix0,lo,iz0, 0,-1,0);
		emitAxisVertex(axis,cx,cy,cz, ox0,lo,oz0, 0,-1,0);
		emitAxisVertex(axis,cx,cy,cz, ox1,lo,oz1, 0,-1,0);
		emitAxisVertex(axis,cx,cy,cz, ix1,lo,iz1, 0,-1,0);

		emitAxisVertex(axis,cx,cy,cz, ox0,hi,oz0, 0,1,0);
		emitAxisVertex(axis,cx,cy,cz, ix0,hi,iz0, 0,1,0);
		emitAxisVertex(axis,cx,cy,cz, ix1,hi,iz1, 0,1,0);
		emitAxisVertex(axis,cx,cy,cz, ox0,hi,oz0, 0,1,0);
		emitAxisVertex(axis,cx,cy,cz, ix1,hi,iz1, 0,1,0);
		emitAxisVertex(axis,cx,cy,cz, ox1,hi,oz1, 0,1,0);
	}
}

// A rocket engine: a bell, not a cone.
//
// SH_CONE gave the rocket a party hat under its tail - one straight taper, no
// throat, no chamber, and a solid point where the exit should be. What makes an
// engine read as an engine is the profile changing direction: a straight-walled
// combustion chamber at the top, the wall pulled in to a waist at the throat,
// then a skirt that opens out again and keeps opening more gently the wider it
// gets. The mouth is hollow, because a nozzle you cannot see up is a plug.
//
// Authored, like every other shape, by the box it has to fill: the widest ring
// is the exit and takes the full radius, the height is the whole engine from
// the top of the chamber to the lip.
#define BELL_STATIONS 7
static void addBell8Axis(float cx, float cy, float cz, const float s[3], partAxis axis)
{
	float e[3];
	axisExtents(axis, s, e);
	const float R = fminf(e[0], e[2])*.5f, h = e[1];
	if (R <= 0.0f || h <= 0.0f) return;
	// 6 outer bands + 3 inner bands, 8 sides, 6 vertices each, plus the top cap
	// and the annular lip at the mouth.
	if (!meshRoomFor(9*8*6 + 8*3 + 8*6)) return;

	// Fraction of the height down from the top, and fraction of the full radius.
	// The pinch at 0.44 is the throat; everything above it is chamber, and the
	// widening steps below it are what give the skirt its curve.
	static const float prof[BELL_STATIONS][2] = {
		{0.00f, 0.60f}, {0.22f, 0.60f}, {0.34f, 0.40f}, {0.44f, 0.30f},
		{0.62f, 0.56f}, {0.80f, 0.80f}, {1.00f, 1.00f},
	};
	// Where the bore starts. The wall has to have a thickness or the lip is a
	// knife edge, and the inner surface only exists below the throat - above it
	// the chamber is closed off by the cap.
	const int throat = 3;
	const float bore = 0.86f;

	float y[BELL_STATIONS], r[BELL_STATIONS];
	for (int k = 0; k < BELL_STATIONS; k++)
	{
		y[k] = h*.5f - prof[k][0]*h;
		r[k] = R * prof[k][1];
	}

	for (int i = 0; i < 8; i++)
	{
		const float a0 = 6.2831853f*i/8.0f, a1 = 6.2831853f*(i+1)/8.0f;
		const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);

		for (int k = 0; k < BELL_STATIONS-1; k++)
		{
			const float dy = y[k+1] - y[k], dr = r[k+1] - r[k];
			// Perpendicular to the profile in the radial/axial plane, pointing
			// away from the axis. A straight wall gives (1,0); a flaring skirt
			// tilts it upwards, which is what stops the bell reading flat.
			float nr = -dy, ny = dr;
			const float nl = sqrtf(nr*nr + ny*ny);
			if (nl > 0.0f) { nr /= nl; ny /= nl; }
			const float tx0 = r[k]*c0,   tz0 = r[k]*s0,   ty = y[k];
			const float bx0 = r[k+1]*c0, bz0 = r[k+1]*s0, by = y[k+1];
			const float tx1 = r[k]*c1,   tz1 = r[k]*s1;
			const float bx1 = r[k+1]*c1, bz1 = r[k+1]*s1;
			// Wound the same way round as the cylinder's side quad, so the one
			// GPU_CULL_BACK_CCW state that draws the whole scene keeps it solid.
			emitAxisVertex(axis,cx,cy,cz, bx0,by,bz0, nr*c0,ny,nr*s0);
			emitAxisVertex(axis,cx,cy,cz, tx1,ty,tz1, nr*c1,ny,nr*s1);
			emitAxisVertex(axis,cx,cy,cz, bx1,by,bz1, nr*c1,ny,nr*s1);
			emitAxisVertex(axis,cx,cy,cz, tx1,ty,tz1, nr*c1,ny,nr*s1);
			emitAxisVertex(axis,cx,cy,cz, bx0,by,bz0, nr*c0,ny,nr*s0);
			emitAxisVertex(axis,cx,cy,cz, tx0,ty,tz0, nr*c0,ny,nr*s0);
		}

		// The bore, from the throat to the mouth. Same bands mirrored: the
		// normal points back at the axis and the winding flips, because this
		// surface is seen from inside the bell.
		for (int k = throat; k < BELL_STATIONS-1; k++)
		{
			const float dy = y[k+1] - y[k], dr = (r[k+1] - r[k])*bore;
			float nr = -dy, ny = dr;
			const float nl = sqrtf(nr*nr + ny*ny);
			if (nl > 0.0f) { nr /= nl; ny /= nl; }
			const float ir0 = r[k]*bore, ir1 = r[k+1]*bore;
			emitAxisVertex(axis,cx,cy,cz, ir1*c0,y[k+1],ir1*s0, -nr*c0,-ny,-nr*s0);
			emitAxisVertex(axis,cx,cy,cz, ir1*c1,y[k+1],ir1*s1, -nr*c1,-ny,-nr*s1);
			emitAxisVertex(axis,cx,cy,cz, ir0*c1,y[k],  ir0*s1, -nr*c1,-ny,-nr*s1);
			emitAxisVertex(axis,cx,cy,cz, ir0*c1,y[k],  ir0*s1, -nr*c1,-ny,-nr*s1);
			emitAxisVertex(axis,cx,cy,cz, ir0*c0,y[k],  ir0*s0, -nr*c0,-ny,-nr*s0);
			emitAxisVertex(axis,cx,cy,cz, ir1*c0,y[k+1],ir1*s0, -nr*c0,-ny,-nr*s0);
		}

		// The top of the engine, closed flat. This is the face that sits against
		// the rocket body, and it is why the part has a top at all.
		emitAxisVertex(axis,cx,cy,cz, 0,y[0],0, 0,1,0);
		emitAxisVertex(axis,cx,cy,cz, r[0]*c1,y[0],r[0]*s1, 0,1,0);
		emitAxisVertex(axis,cx,cy,cz, r[0]*c0,y[0],r[0]*s0, 0,1,0);

		// The lip: the ring of metal between the outside of the skirt and the
		// bore, so the mouth has a real edge instead of an infinitely thin one.
		{
			const int L = BELL_STATIONS-1;
			const float lo = y[L], oR = r[L], iR = r[L]*bore;
			emitAxisVertex(axis,cx,cy,cz, iR*c0,lo,iR*s0, 0,-1,0);
			emitAxisVertex(axis,cx,cy,cz, oR*c0,lo,oR*s0, 0,-1,0);
			emitAxisVertex(axis,cx,cy,cz, oR*c1,lo,oR*s1, 0,-1,0);
			emitAxisVertex(axis,cx,cy,cz, iR*c0,lo,iR*s0, 0,-1,0);
			emitAxisVertex(axis,cx,cy,cz, oR*c1,lo,oR*s1, 0,-1,0);
			emitAxisVertex(axis,cx,cy,cz, iR*c1,lo,iR*s1, 0,-1,0);
		}
	}
}

// A panel bent over one axis: a section of a cylinder wall with a real
// thickness. Windscreens, mudguards, cowlings, shields, a curved roof.
//
// The flat plate already covers anything sheet-like, and what it cannot do is
// bulge, which is exactly what separates a car's wing from its door. The arc is
// authored the same way as every other shape - by the box it has to fill - so
// the part's own size decides how far it bends: a box as tall as it is wide
// comes out a half round, a shallow one comes out barely bowed.
//
// The part bulges along the axis it was authored with, sits flat against that
// axis at both ends, and is extruded across the remaining one.
#define CURVE_SEG 6
static void addCurvedPanel(float cx, float cy, float cz, const float s[3], partAxis axis)
{
	float e[3];
	axisExtents(axis, s, e);
	const float w = e[0], rise = e[1], dep = e[2];
	if (w <= 0.0f || rise <= 0.0f || dep <= 0.0f) return;
	if (!meshRoomFor(CURVE_SEG*24 + 12)) return;

	// The circle through the two bottom corners and the top of the box. R comes
	// out of that alone, so the caller never has to think about radii - and
	// R - w/2 works out to (w/2 - rise)^2 / 2rise, which is never negative, so
	// the half-angle below can never be handed a value outside asin's domain.
	const float R  = (w*w*.25f + rise*rise) / (2.0f*rise);
	const float a  = asinf((w*.5f)/R);
	const float y0 = rise*.5f - R;          // arc centre, in the box's own frame
	// Thin enough to read as a panel, thick enough to still be there at 320x240,
	// and never more than half the bulge or the inside would poke out the top.
	const float th = fminf(fminf(w, rise)*.16f, rise*.5f);
	const float hz = dep*.5f;

	// A point on the arc at angle t and radius rad, at depth z. Every vertex in
	// this primitive is one of these, so the arc is only written down once.
	#define CURVE_VTX(t, rad, z, nx, ny, nz) \
		emitAxisVertex(axis,cx,cy,cz, (rad)*sinf(t), y0 + (rad)*cosf(t), (z), (nx), (ny), (nz))

	for (int i = 0; i < CURVE_SEG; i++)
	{
		const float t0 = -a + 2.0f*a*i/CURVE_SEG;
		const float t1 = -a + 2.0f*a*(i+1)/CURVE_SEG;
		// Radially outward at each end of the segment, which is also what the
		// outside face points along. Per-vertex rather than per-face, so a
		// six-segment arc shades as a curve instead of as six flats.
		const float o0x = sinf(t0), o0y = cosf(t0);
		const float o1x = sinf(t1), o1y = cosf(t1);

		// Outside of the bend. Wound counter-clockwise seen from out there, to
		// match addBox under the scene-wide GPU_CULL_BACK_CCW state.
		CURVE_VTX(t0,R,-hz, o0x,o0y,0); CURVE_VTX(t1,R, hz, o1x,o1y,0); CURVE_VTX(t1,R,-hz, o1x,o1y,0);
		CURVE_VTX(t0,R,-hz, o0x,o0y,0); CURVE_VTX(t0,R, hz, o0x,o0y,0); CURVE_VTX(t1,R, hz, o1x,o1y,0);
		// Underside: the same arc one thickness in, wound and pointed the other
		// way, because from inside the bend the outside is the middle of the part.
		CURVE_VTX(t0,R-th,-hz, -o0x,-o0y,0); CURVE_VTX(t1,R-th,-hz, -o1x,-o1y,0); CURVE_VTX(t1,R-th, hz, -o1x,-o1y,0);
		CURVE_VTX(t0,R-th,-hz, -o0x,-o0y,0); CURVE_VTX(t1,R-th, hz, -o1x,-o1y,0); CURVE_VTX(t0,R-th, hz, -o0x,-o0y,0);
		// The two flanks, which are what give the panel a visible thickness.
		CURVE_VTX(t0,R,-hz, 0,0,-1); CURVE_VTX(t1,R-th,-hz, 0,0,-1); CURVE_VTX(t0,R-th,-hz, 0,0,-1);
		CURVE_VTX(t0,R,-hz, 0,0,-1); CURVE_VTX(t1,R,-hz, 0,0,-1);    CURVE_VTX(t1,R-th,-hz, 0,0,-1);
		CURVE_VTX(t0,R, hz, 0,0, 1); CURVE_VTX(t0,R-th, hz, 0,0, 1); CURVE_VTX(t1,R-th, hz, 0,0, 1);
		CURVE_VTX(t0,R, hz, 0,0, 1); CURVE_VTX(t1,R-th, hz, 0,0, 1); CURVE_VTX(t1,R, hz, 0,0, 1);
	}

	// The two ends of the arc, where the panel is cut square. Each faces along
	// the tangent there, away from the rest of the arc.
	{
		const float ex = cosf(a), ey = sinf(a);
		CURVE_VTX(-a,R,-hz, -ex,-ey,0); CURVE_VTX(-a,R-th,-hz, -ex,-ey,0); CURVE_VTX(-a,R-th, hz, -ex,-ey,0);
		CURVE_VTX(-a,R,-hz, -ex,-ey,0); CURVE_VTX(-a,R-th, hz, -ex,-ey,0); CURVE_VTX(-a,R, hz, -ex,-ey,0);
		CURVE_VTX( a,R,-hz,  ex,-ey,0); CURVE_VTX( a,R-th, hz,  ex,-ey,0); CURVE_VTX( a,R-th,-hz,  ex,-ey,0);
		CURVE_VTX( a,R,-hz,  ex,-ey,0); CURVE_VTX( a,R, hz,  ex,-ey,0);    CURVE_VTX( a,R-th, hz,  ex,-ey,0);
	}

	#undef CURVE_VTX
}

// Grows a min/max pair to contain a box.
static void expandBounds(float min[3], float max[3], const boxSpec* b)
{
	const float c[3] = { b->cx, b->cy, b->cz };
	const float s[3] = { b->sx, b->sy, b->sz };

	for (int a = 0; a < 3; a++)
	{
		float lo = c[a] - s[a]*0.5f, hi = c[a] + s[a]*0.5f;
		if (lo < min[a]) min[a] = lo;
		if (hi > max[a]) max[a] = hi;
	}
}

static void setBounds(float min[3], float max[3], const boxSpec* b)
{
	min[0] = min[1] = min[2] =  1e9f;
	max[0] = max[1] = max[2] = -1e9f;
	expandBounds(min, max, b);
}

// Pull only disconnected child axes back toward their parent.  This preserves
// the authored left/right/up/down sign while guaranteeing that each explicit
// assembly chain has a small physical join rather than floating pieces.
static void resolveAssemblyTargets(const kitDef* def)
{
	for (int i=0; i<PART_SPEC_COUNT; i++) {
		int parent = def->parts[i].parent;
		if (parent < 0 || parent >= i) continue;
		for (int a=0; a<3; a++) {
			float childHalf = def->parts[i].size[a]*.5f;
			float parentHalf = def->parts[parent].size[a]*.5f;
			// Guarantee a real contact area, not an edge/corner touch.  The clamp
			// only pulls a disconnected axis inward and preserves its sign.
			float join = fminf(.08f, fminf(def->parts[i].size[a],def->parts[parent].size[a])*.35f);
			float limit = parentHalf + childHalf - join;
			float delta = activeTarget[i][a] - activeTarget[parent][a];
			if (fabsf(delta) > limit)
				activeTarget[i][a] = activeTarget[parent][a] + (delta < 0.0f ? -limit : limit);
		}
	}
}

static bool validateKitDef(const kitDef* def, int level)
{
	bool ok = def && def->name && def->runners > 0 && def->runners <= 4;
	if (ok && strlen(def->name) > MESH_KIT_NAME_MAX) {
		printf("NAME TOO LONG L%d: kit \"%s\" (%u > %d)\n",
			level, def->name, (unsigned)strlen(def->name), MESH_KIT_NAME_MAX);
		ok=false;
	}
	float t[PART_SPEC_COUNT][3];
	for (int i=0; i<PART_SPEC_COUNT; i++) {
		const kitPartDef* p=&def->parts[i];
		for (int a=0;a<3;a++) t[i][a]=p->target[a];
		if (!p->name || p->size[0] <= 0.0f || p->size[1] <= 0.0f || p->size[2] <= 0.0f || p->runner >= def->runners ||
			(i == 0 ? p->parent != -1 : (p->parent < 0 || p->parent >= i))) ok=false;
		// The workbench builds sentences out of two of these at a time, into a
		// buffer sized from MESH_NAME_MAX. Catching an over-long name here is the
		// difference between a startup failure that names the offender and a
		// player reading a message that stops mid-word.
		else if (strlen(p->name) > MESH_NAME_MAX) {
			printf("NAME TOO LONG L%d: part \"%s\" (%u > %d)\n",
				level, p->name, (unsigned)strlen(p->name), MESH_NAME_MAX);
			ok=false;
		}
	}
	for (int r=0; r<def->runners; r++) {
		int count=0; for (int i=0;i<PART_SPEC_COUNT;i++) if (def->parts[i].runner==r) count++;
		if (!count) { printf("RUNNER VALIDATION FAILED L%d R%d: no parts\n",level,r); ok=false; }
	}
	// Mirror the live resolver, then assert every authored child touches parent.
	for (int i=0;i<PART_SPEC_COUNT;i++) if (def->parts[i].parent >= 0 && def->parts[i].parent < i) {
		int q=def->parts[i].parent;
		int deepAxes=0; bool childOK=true;
		for (int a=0;a<3;a++) {
			float need=fminf(.08f,fminf(def->parts[i].size[a],def->parts[q].size[a])*.35f);
			float lim=(def->parts[i].size[a]+def->parts[q].size[a])*.5f-need;
			float d=t[i][a]-t[q][a]; if (fabsf(d)>lim) t[i][a]=t[q][a]+(d<0?-lim:lim);
			float overlap=(def->parts[i].size[a]+def->parts[q].size[a])*.5f-fabsf(t[i][a]-t[q][a]);
			// Thin plates and sails legitimately have a smaller contact thickness;
			// require the resolver's size-relative inset, with a tiny FP allowance.
			if (overlap + .001f < need) childOK=false;
			// A proper moulded join has area, not just a corner touch.  Demand
			// substantial overlap in two axes after the resolver has pulled it in.
			if (overlap >= fminf(.08f, fminf(def->parts[i].size[a],def->parts[q].size[a])*.35f)) deepAxes++;
		}
		if (!childOK || deepAxes < 2) { printf("JOIN VALIDATION FAILED L%d %s -> %s\n",level,def->parts[i].name,def->parts[q].name); ok=false; }
	}
	if (!ok) printf("KIT VALIDATION FAILED: L%d\n", level);
	return ok;
}

static bool validatePackedRunner(const kitDef* def, int level)
{
	bool ok=true;
	for (int i=0;i<PART_SPEC_COUNT;i++) {
		const boxSpec* b=&activeSpecs[i].body; const boxSpec* n=&activeSpecs[i].nub; const boxSpec* s=&activeSpecs[i].stub;
		float bd=b->sz;
		// Everything below is about where the part can be seen, not where it is
		// drawn from, so it is tested against the silhouette box. For all but the
		// fan blade the two are the same box.
		float bcx=b->cx+activeSpecs[i].silOff[0], bcy=b->cy+activeSpecs[i].silOff[1];
		float bw=activeSpecs[i].silExt[0], bh=activeSpecs[i].silExt[1];
		// Bodies stay inside the inner face of the rails. The frame is measured to
		// fit the kit rather than fixed, so the envelope comes from the numbers the
		// packer just worked out, not from literals that would go stale the moment a
		// kit authored a wider part. The envelope is only two-dimensional on purpose:
		// the rails are RUN_BAR deep and every part is meant to stand proud of that
		// plane in z, so there is nothing to bound depth against.
		float railX=runnerFrameW*.5f-RUN_BAR, railY=runnerInnerH*.5f;
		if (bcx-bw*.5f < -railX || bcx+bw*.5f > railX || bcy-bh*.5f < -railY || bcy+bh*.5f > railY) {
			printf("PACK VALIDATION FAILED L%d %s R%d: outside rails\n",level,def->parts[i].name,def->parts[i].runner); ok=false;
		}
		// Gate chain is continuous: stub/nub meet and nub meets the body's AABB.
		// The nub is tested on all three axes - checking only x and y let a nub sit
		// clear of the body in depth and still pass as attached.
		if (fabsf(s->cx-n->cx)>.001f || fabsf(s->cz-n->cz)>.001f || fabsf(s->cy-n->cy)>.10f ||
			fabsf(n->cx-bcx)>bw*.5f+.05f || fabsf(n->cy-bcy)>bh*.5f+.05f || fabsf(n->cz-b->cz)>bd*.5f+.05f) {
			printf("GATE VALIDATION FAILED L%d %s R%d\n",level,def->parts[i].name,def->parts[i].runner); ok=false;
		}
		for(int j=0;j<i;j++) if(def->parts[j].runner==def->parts[i].runner) {
			const boxSpec* q=&activeSpecs[j].body;
			float qcx=q->cx+activeSpecs[j].silOff[0], qcy=q->cy+activeSpecs[j].silOff[1];
			float qw=activeSpecs[j].silExt[0], qh=activeSpecs[j].silExt[1];
			if (fabsf(bcx-qcx) < (bw+qw)*.5f-.015f && fabsf(bcy-qcy) < (bh+qh)*.5f-.015f)
				{ printf("PACK OVERLAP L%d R%d %s/%s\n",level,def->parts[i].runner,def->parts[i].name,def->parts[j].name); ok=false; }
		}
	}
	return ok;
}

// Which table a level's kit lives in. The twenty are held in three arrays
// rather than one because they were authored in three passes.
static const kitDef* kitDefFor(int level)
{
	if (level < 1 || level > MESH_KIT_LEVELS) return NULL;
	return level <= 8  ? &kitDefs[level-1]
	     : level <= 14 ? &kitDefsMid[level-9]
	                   : &kitDefsLate[level-15];
}

// How many parts the level's kit actually holds, counted off that kit's own
// table rather than returned as a constant.
//
// It used to return PART_SPEC_COUNT for any level in range, which gave the
// right answer for the wrong reason: it was reporting the size of the array the
// kits are stored in, not the number of parts in the kit the player is about to
// open. Those two agree today because every kit is authored full, and a kit
// that was not would still be reported as ten - the front end's level grid and
// its size-scale colours would go on advertising a number the kit did not have,
// which is exactly the drift this function exists to prevent. Counting the
// entries makes the answer come from the kit.
int meshKitPartCount(int level)
{
	const kitDef* def = kitDefFor(level);
	if (!def) return 0;
	int count = 0;
	for (int i = 0; i < PART_SPEC_COUNT; i++) if (def->parts[i].name) count++;
	return count;
}

bool meshValidateKits(void)
{
	bool ok=true;
	for (int level=1; level<=MESH_KIT_LEVELS; level++) {
		const kitDef* d = level <= 8 ? &kitDefs[level-1] : (level <= 14 ? &kitDefsMid[level-9] : &kitDefsLate[level-15]);
		ok &= validateKitDef(d, level);
	}
	printf(ok ? "KIT VALIDATION: 20 DEFINITIONS OK\n" : "KIT VALIDATION: ERROR\n");
	return ok;
}

bool meshAuditAllKits(void)
{
	bool ok=true; int passed=0; int peak=0, peakLevel=0;
	for (int level=1; level<=MESH_KIT_LEVELS; level++) {
		const kitDef* def = level <= 8 ? &kitDefs[level-1] : (level <= 14 ? &kitDefsMid[level-9] : &kitDefsLate[level-15]);
		meshBuildKit(level);
		// "Did it fit" is asked of the drop counter, not of meshCount. meshCount
		// cannot exceed the store - the emitters make sure of that - so testing it
		// was testing nothing. What can go wrong is geometry being dropped to keep
		// it under, and that is what this now catches.
		bool row = validateKitDef(def,level) && partCount == PART_SPEC_COUNT && meshCount > 0 &&
			meshDroppedCount() == 0 && runnerBaseCount > 0 && activePackedValid;
		for (int i=0;i<partCount;i++) {
			if (parts[i].firstVertex < 0 || parts[i].vertexCount <= 0 || parts[i].firstVertex+parts[i].vertexCount > meshCount ||
				stubCount[i] <= 0 || stubFirst[i]+stubCount[i] > meshCount) row=false;
		}
		if (meshBoxCount() > peak) { peak = meshBoxCount(); peakLevel = level; }
		if(!row) printf("GEOMETRY AUDIT L%02d FAIL parts=%d runners=%d verts=%d dropped=%d\n",level,partCount,activeRunners,meshCount,meshDroppedCount());
		if(row) passed++; else ok=false;
	}
	printf("BOX BUDGET peak %d/%d at L%02d\n", peak, meshBoxCapacity(), peakLevel);
	printf("GEOMETRY AUDIT %d/20 %s\n",passed,ok?"OK":"FAIL");
	meshBuildKit(1);
	return ok;
}

static bool strictAabbOverlap(const float amin[3], const float amax[3], const float bmin[3], const float bmax[3])
{
	// A small tolerance allows parts to meet exactly at a face without being
	// labelled a collision by float rounding.  All three dimensions must have
	// positive volume for a collision.
	const float eps=.002f;
	for (int a=0;a<3;a++) if (fminf(amax[a],bmax[a])-fmaxf(amin[a],bmin[a]) <= eps) return false;
	return true;
}

// A world point turned back into one part's own local frame - the inverse of
// axisMap, and the only way to ask a rotated primitive whether it contains a
// point without rebuilding its triangles.
static void axisUnmap(partAxis axis, const float c[3], const float w[3], float out[3])
{
	const float dx = w[0]-c[0], dy = w[1]-c[1], dz = w[2]-c[2];
	if (axis == AXIS_X)      { out[0] = -dy; out[1] =  dx; out[2] =  dz; }
	else if (axis == AXIS_Z) { out[0] =  dx; out[1] =  dz; out[2] = -dy; }
	else                     { out[0] =  dx; out[1] =  dy; out[2] =  dz; }
}

// Is this world point inside the solid the part actually draws?
//
// Not inside its authored box - inside the solid. The two are the same thing
// for a box and a plate and nothing else: a wedge tapers to a zero-height edge,
// a cone tapers to a point, and the round emitters take a single radius from
// min(size) and throw the larger cross size away. Every part that has ever
// looked unattached on this bench was a part joined where its parent's box was
// still there and its parent's solid was not.
//
// Shapes whose emitter reads its dimensions through axisExtents are authored in
// world sizes, so they are unpermuted here to match. The wedge is authored in
// local sizes, because addWedgeAxis takes size[] straight as its local box.
// Anything not modelled exactly falls through to its box, which can only ever
// report a join that is not there - never hide one that is.
static bool solidContains(const kitPartDef* p, const float c[3], const float w[3])
{
	const float* s = p->size;
	float l[3];

	switch (p->shape)
	{
	case SH_CYLINDER: case SH_ROD: case SH_CONE: case SH_WHEEL:
	{
		const float r = p->axis == AXIS_Y ? fminf(s[0], s[2])*.5f :
			(p->axis == AXIS_X ? fminf(s[1], s[2])*.5f : fminf(s[0], s[1])*.5f);
		const float h = p->axis == AXIS_Y ? s[1] : (p->axis == AXIS_X ? s[0] : s[2]);
		axisUnmap(p->axis, c, w, l);
		if (fabsf(l[1]) > h*.5f) return false;
		// The cone's radius runs from r at its base to nothing at the apex.
		const float rr = p->shape == SH_CONE ? r*(h*.5f - l[1])/h : r;
		return l[0]*l[0] + l[2]*l[2] <= rr*rr;
	}
	case SH_WEDGE:
	{
		axisUnmap(p->axis, c, w, l);
		if (fabsf(l[1]) > s[1]*.5f || fabsf(l[2]) > s[2]*.5f) return false;
		return fabsf(l[0]) <= s[0]*.5f*(s[1]*.5f - l[1])/s[1];
	}
	case SH_DOME: case SH_TUBE: case SH_BELL:
	{
		float e[3]; axisExtents(p->axis, s, e);
		const float r = fminf(e[0], e[2])*.5f, h = e[1];
		axisUnmap(p->axis, c, w, l);
		if (fabsf(l[1]) > h*.5f) return false;
		const float d2 = l[0]*l[0] + l[2]*l[2];
		if (p->shape == SH_TUBE) return d2 <= r*r && d2 >= r*.62f*r*.62f;
		if (p->shape == SH_BELL) return d2 <= r*r;
		{
			const float t = (l[1] + h*.5f)/h;   // 0 at the flat underside, 1 at the apex
			return d2 <= r*r*(1.0f - t*t);
		}
	}
	default:
		// Box, plate, star, grill, blade, curved panel: bounded by the box they
		// were authored as, which is the honest answer for the first two and a
		// safe over-estimate for the rest.
		for (int a = 0; a < 3; a++) if (fabsf(w[a]-c[a]) > s[a]*.5f) return false;
		return true;
	}
}

// The exact world box a part's solid occupies - the box the grid below samples.
//
// This used to take max(size, axisExtents(size)) per axis as a safe superset,
// and the safety cost it its accuracy: the helicopter rotor blade is a plate
// 0.05 thick on an AXIS_X record, so the superset widened that axis to 1.5 and a
// fourteen-step grid stepped 0.107 at a time straight over a 0.05 solid, finding
// nothing inside the part at all. Only the wedge authors its size in local
// space; every other shape here is already world-aligned, because the round
// emitters and axisExtents both pre-compensate for the rotation.
static void solidBounds(const kitPartDef* p, const float c[3], float lo[3], float hi[3])
{
	float e[3];
	if (p->shape == SH_WEDGE) axisExtents(p->axis, p->size, e);
	else                      { e[0]=p->size[0]; e[1]=p->size[1]; e[2]=p->size[2]; }
	for (int a = 0; a < 3; a++) { lo[a] = c[a]-e[a]*.5f; hi[a] = c[a]+e[a]*.5f; }
}

// How many of a part's own grid samples land inside another part.
//
// The grid is laid over a's exact box and only the points that are really inside
// a's solid are offered to b, so a taper or a bore on either side is honoured
// rather than assumed away. An AABB test cannot answer this: the two boxes of a
// cone tip meeting a knife-edged wedge overlap perfectly while the drawn solids
// miss each other by a wide margin, which is exactly how four parts came to be
// floating with the box audit reporting nothing.
#define JOIN_GRID 14
static int solidJoinSamples(const kitPartDef* a, const float ca[3],
                            const kitPartDef* b, const float cb[3])
{
	float alo[3],ahi[3],blo[3],bhi[3];
	solidBounds(a,ca,alo,ahi);
	solidBounds(b,cb,blo,bhi);
	// Boxes that do not even touch cannot have solids that do, and skipping them
	// here keeps twenty levels of ten parts against nine neighbours affordable.
	for (int a2=0;a2<3;a2++) if (alo[a2] > bhi[a2] || blo[a2] > ahi[a2]) return 0;

	int shared = 0;
	for (int i=0;i<JOIN_GRID;i++) for (int j=0;j<JOIN_GRID;j++) for (int k=0;k<JOIN_GRID;k++) {
		const float w[3] = {
			alo[0] + (i+.5f)*(ahi[0]-alo[0])/JOIN_GRID,
			alo[1] + (j+.5f)*(ahi[1]-alo[1])/JOIN_GRID,
			alo[2] + (k+.5f)*(ahi[2]-alo[2])/JOIN_GRID };
		if (solidContains(a,ca,w) && solidContains(b,cb,w)) shared++;
	}
	return shared;
}

// A part counts as attached when at least one of its own grid samples lands
// inside another part. One sample out of a 14x14x14 grid is the floor because
// the question being asked is binary - is this thing sitting in mid-air - and
// any real join buries far more than one cell. Asking for a percentage instead
// would be asking how deep the join is, which is a different question and one
// the kits have no agreed answer to: a wheel meets a body across a sliver and a
// fin is half swallowed, and both are correct.
#define JOIN_MIN_SAMPLES 1

bool meshCollisionAuditAllKits(void)
{
	bool all=true;
	for (int level=1; level<=MESH_KIT_LEVELS; level++) {
		meshBuildKit(level);
		int looseHits=0, assembledHits=0;
		// This asks the same question the live cut path answers: every part is put
		// down at the home the packer reserved for it, resting on the bare wood,
		// and bounded by the same full-size min/max the game uses.  It says
		// nothing about parts still on the runner - those are drawn and picked at
		// runnerMin/runnerMax, and validatePackedRunner is what checks them.
		float woodTop = meshLooseTopY();
		for (int i=0;i<partCount;i++) {
			float amin[3],amax[3],home[2];
			meshLooseSlot(i,home);
			float off[3]={home[0]-(parts[i].min[0]+parts[i].max[0])*.5f, woodTop-parts[i].min[1], home[1]-(parts[i].min[2]+parts[i].max[2])*.5f};
			for(int a=0;a<3;a++){amin[a]=parts[i].min[a]+off[a];amax[a]=parts[i].max[a]+off[a];}
			for(int j=0;j<i;j++) {
				float bmin[3],bmax[3],bhome[2];
				meshLooseSlot(j,bhome);
				float boff[3]={bhome[0]-(parts[j].min[0]+parts[j].max[0])*.5f, woodTop-parts[j].min[1], bhome[1]-(parts[j].min[2]+parts[j].max[2])*.5f};
				for(int a=0;a<3;a++){bmin[a]=parts[j].min[a]+boff[a];bmax[a]=parts[j].max[a]+boff[a];}
				if(strictAabbOverlap(amin,amax,bmin,bmax)) looseHits++;
			}
		}
		for(int i=0;i<socketCount;i++) for(int j=0;j<i;j++)
			if(strictAabbOverlap(sockets[i].min,sockets[i].max,sockets[j].min,sockets[j].max)) assembledHits++;
		// ASM above is reported, not judged. An assembled model is meant to
		// interpenetrate - every real join shows up in that count - so failing on
		// it made the audit unpassable by construction and therefore meaningless.
		// What is worth failing on is the opposite question, asked of the solids
		// rather than the boxes: is every part touching something?
		//
		// Touching ANYTHING, not touching its parent. The parent field is a build
		// order dependency and not a statement about geometry - the castle tower's
		// mast is authored off the battlement and physically stands buried in the
		// roof cone a quarter of a unit away from it, and the first version of this
		// audit failed it for that. Twelve of the twenty levels failed that way,
		// none of them for anything a player could see. A part that shares material
		// with any other part in the kit is attached to the model; a part that
		// shares material with none of them is the thing that looks wrong.
		const kitDef* def = level <= 8 ? &kitDefs[level-1] :
			(level <= 14 ? &kitDefsMid[level-9] : &kitDefsLate[level-15]);
		int floatHits=0;
		for (int i=0;i<PART_SPEC_COUNT;i++) {
			int best=0, bestJ=-1;
			for (int j=0;j<PART_SPEC_COUNT && best<JOIN_MIN_SAMPLES;j++) {
				if (j==i) continue;
				const int n = solidJoinSamples(&def->parts[i], activeTarget[i],
				                               &def->parts[j], activeTarget[j]);
				if (n > best) { best=n; bestJ=j; }
			}
			if (best >= JOIN_MIN_SAMPLES) continue;
			(void)bestJ;
			printf("  ! %-12.12s touches nothing\n", def->parts[i].name);
			floatHits++;
		}
		bool pass=looseHits==0 && floatHits==0;
		// Kept deliberately short: the 3DS console is 50 columns wide and this
		// audit must leave all twenty rows readable on screen.
		printf("L%02d %-14.14s CUT=%02d ASM=%02d JOIN=%02d %s\n",
			level,activeKitName,looseHits,assembledHits,floatHits,pass?"OK":"FAIL");
		if(!pass) all=false;
	}
	printf("COLLISION AUDIT %s\n",all?"20/20 OK":"FAIL");
	meshBuildKit(1);
	return all;
}

// How much room a part's drawn silhouette needs in x and y, and where that room
// sits relative to the point the part is drawn about.
//
// Almost every shape is drawn inside its authored box and centred on it, so the
// honest answer is "no offset, the authored size" and the packer can go on
// treating the table as the truth.  The fan blade is the exception: its outline
// starts at the hub and sweeps outward, so it is off-centre before it is spun,
// and spinning it then throws it in a different direction for each blade.
// Packed as if it were a centred box, a blade reached a quarter of a unit past
// the box the packer had reserved for it - through its neighbours and out over
// the rails.
static void shapeSilhouette(partShape shape, const float size[3], float spin,
                            float offset[2], float extent[2])
{
	offset[0] = offset[1] = 0.0f;
	extent[0] = size[0];
	extent[1] = size[1];
	if (shape != SH_FAN_BLADE) return;

	float p[6][2];
	fanBladeOutline(size, spin, p);
	float lo[2] = { p[0][0], p[0][1] }, hi[2] = { p[0][0], p[0][1] };
	for (int i = 1; i < 6; i++)
		for (int a = 0; a < 2; a++)
		{
			if (p[i][a] < lo[a]) lo[a] = p[i][a];
			if (p[i][a] > hi[a]) hi[a] = p[i][a];
		}
	for (int a = 0; a < 2; a++)
	{
		offset[a] = (lo[a] + hi[a]) * 0.5f;
		extent[a] = hi[a] - lo[a];
	}
}

// meshBuildKit is built from the pieces below, one focused stage per function:
// runner layout, then the loose-part homes, then the bedroom shell and bench
// furniture, then the runner frame and its parts, then the sockets. Split out
// so each stage can be read and changed on its own; meshBuildKit itself is
// just the fixed order they run in.
static void packRunnerLayout(const kitDef* def, int level)
{
	// Pack the runner at true size, in two passes.
	//
	// Pass one measures. Every part in a runner goes into one of two lanes - the
	// first half hangs down off the top rail, the rest stands up off the bottom
	// rail - and a lane is as wide as its parts laid end to end with RUN_GAP
	// between them. Only one runner is on the desk at a time (main.c
	// currentRunner) but the frame is built once and never rebuilt, so it has to
	// fit the widest and tallest runner in the kit; every runner is then centred
	// inside that one frame.
	//
	// Two lanes, so a runner holds as many parts as the widest lane will take
	// rather than a fixed cell count. PART_SPEC_COUNT bounds the arrays below.
	_Static_assert(PART_SPEC_COUNT <= 10, "lane index arrays are sized for one kit's parts");
	int runnerOf[PART_SPEC_COUNT], laneOf[PART_SPEC_COUNT], laneSlot[PART_SPEC_COUNT];
	float laneW[MESH_MAX_RUNNERS][2] = {{0}}, laneH[MESH_MAX_RUNNERS][2] = {{0}};
	int laneN[MESH_MAX_RUNNERS][2] = {{0}};
	// A bad authored runner index fails validateKitDef; fold it onto runner 0 here
	// so the packing arrays below are never indexed out of range in the meantime.
	for (int i = 0; i < PART_SPEC_COUNT; i++)
	{
		int r = def->parts[i].runner;
		runnerOf[i] = (r >= 0 && r < MESH_MAX_RUNNERS) ? r : 0;
		// Worked out once, up front, so measuring and placing cannot disagree
		// about how big a part is.
		shapeSilhouette(def->parts[i].shape, def->parts[i].size, def->parts[i].spin,
		                activeSpecs[i].silOff, activeSpecs[i].silExt);
	}
	for (int r = 0; r < MESH_MAX_RUNNERS; r++)
	{
		int count = 0;
		for (int i = 0; i < PART_SPEC_COUNT; i++) if (runnerOf[i] == r) count++;
		int half = (count + 1) / 2, seen = 0;
		for (int i = 0; i < PART_SPEC_COUNT; i++) if (runnerOf[i] == r)
		{
			int lane = seen < half ? 0 : 1;
			laneOf[i] = lane;
			laneSlot[i] = laneN[r][lane]++;
			// One RUN_GAP per part except the first, so a lane of one is just the
			// part itself.
			laneW[r][lane] += activeSpecs[i].silExt[0] + (laneSlot[i] ? RUN_GAP : 0.0f);
			laneH[r][lane] = fmaxf(laneH[r][lane], activeSpecs[i].silExt[1]);
			seen++;
		}
	}
	float contentW = 0.0f, topLaneH = 0.0f, botLaneH = 0.0f;
	for (int r = 0; r < MESH_MAX_RUNNERS; r++)
	{
		contentW = fmaxf(contentW, fmaxf(laneW[r][0], laneW[r][1]));
		topLaneH = fmaxf(topLaneH, laneH[r][0]);
		botLaneH = fmaxf(botLaneH, laneH[r][1]);
	}
	runnerInnerH = RUN_GATE + topLaneH + RUN_MID + botLaneH + RUN_GATE;
	runnerFrameW = contentW + 2.0f*RUN_SIDE + 2.0f*RUN_BAR;
	runnerFrameH = runnerInnerH + 2.0f*RUN_BAR;
	float topFace = runnerInnerH*0.5f, botFace = -runnerInnerH*0.5f;
	// The cross-bar lives in the clear band between the lanes, so it can never
	// run into a part however tall the parts in either lane turn out to be.
	runnerBarY = topFace - RUN_GATE - topLaneH - RUN_MID*0.5f;

	// Pass two places. Each lane is centred in the frame and walked left to
	// right, giving every part a cell exactly its own width.
	float cursor[MESH_MAX_RUNNERS][2];
	for (int r = 0; r < MESH_MAX_RUNNERS; r++)
		for (int k = 0; k < 2; k++) cursor[r][k] = -laneW[r][k]*0.5f;
	for (int i = 0; i < PART_SPEC_COUNT; i++)
	{
		activeSpecs[i].name = def->parts[i].name;
		float fullW=def->parts[i].size[0], fullH=def->parts[i].size[1], fullD=def->parts[i].size[2];
		// Gates are sized to the part they hold, the way a real sprue does it - a
		// hull gets a chunky one, an antenna gets a thread. Only the cross-section
		// varies. The height stays GATE_CUBE because three other things are
		// measured off it: the stub and nub are placed at a hardcoded 0.045 and
		// 0.135 from the rail face, filing buries the nub by exactly nub.sy, and
		// the part then settles by the same amount. Widening a gate changes how it
		// looks on the frame; making it taller would change how long every part
		// takes to file and how far it drops, which is a different question than
		// the one being answered here.
		//
		// The taper is off the part's narrower footprint and capped under it, so a
		// gate is never wider than the piece hanging from it - which is what the
		// thin plates need, and they are the ones a fixed cube looked worst on.
		float gateSpan = fminf(fullW, fullD);
		float gate = 0.05f + 0.085f * fminf(1.0f, gateSpan / 0.75f);
		gate = fminf(gate, gateSpan * 0.9f);
		activeSpecs[i].stub.sx = activeSpecs[i].stub.sz = gate;
		activeSpecs[i].nub.sx  = activeSpecs[i].nub.sz  = gate;
		activeSpecs[i].stub.sy = activeSpecs[i].nub.sy  = GATE_CUBE;
		// The cell is sized and placed by what gets drawn, then the part is drawn
		// about a point offset back from the middle of that cell - so a shape that
		// is not centred on its own origin still ends up centred in its cell.
		float cellW=activeSpecs[i].silExt[0], cellH=activeSpecs[i].silExt[1];
		int r = runnerOf[i], lane = laneOf[i];
		if (laneSlot[i]) cursor[r][lane] += RUN_GAP;
		float x = cursor[r][lane] + cellW*0.5f;
		cursor[r][lane] += cellW;
		// Upper lane hangs from the top rail; lower lane rises from the bottom.
		float y = lane==0 ? topFace - RUN_GATE - cellH*0.5f
		                  : botFace + RUN_GATE + cellH*0.5f;
		// Depth is flush-backed, not centred. Local +z is downwards once the sprue
		// is laid flat on the desk (main.c buildRunnerView), so centring a thick
		// part on the frame plane would push half of it through the desk top.
		// Lining its back face up with the back of the rails keeps every part on
		// the desk and lets thick ones stand proud, the way a moulded sprue does.
		//
		// This governs the part while it is still on the runner and nothing else.
		// Where a part ends up once it is cut and fitted is authored, not derived:
		// it comes from target[2] below by way of resolveAssemblyTargets, which
		// never reads this. Changing the rule here moves nothing on the model.
		float z = -(fullD - RUN_BAR)*0.5f;
		activeSpecs[i].body.cx=x-activeSpecs[i].silOff[0];
		activeSpecs[i].body.cy=y-activeSpecs[i].silOff[1];
		activeSpecs[i].body.cz=z;
		activeSpecs[i].body.sx=fullW; activeSpecs[i].body.sy=fullH; activeSpecs[i].body.sz=fullD;
		// The gate hangs off the middle of the cell, which is the middle of what
		// can actually be seen, rather than off the drawing origin.
		activeSpecs[i].stub.cx=x; activeSpecs[i].nub.cx=x;
		activeSpecs[i].stub.cz=activeSpecs[i].nub.cz=0.0f;
		// Stub grows out of the rail face, nub grows out of the stub, part face is
		// RUN_GATE from the rail - the same chain the gate validator checks.
		if (lane==0) {
			activeSpecs[i].stub.cy=topFace-0.045f; activeSpecs[i].nub.cy=topFace-0.135f;
		} else {
			activeSpecs[i].stub.cy=botFace+0.045f; activeSpecs[i].nub.cy=botFace+0.135f;
		}
		activeTarget[i][0] = STAND_X + def->parts[i].target[0]; activeTarget[i][1] = def->parts[i].target[1]; activeTarget[i][2] = STAND_Z + ASSEMBLY_FRONT_Z + def->parts[i].target[2]; activeParent[i]=def->parts[i].parent;
	}
	activePackedValid = validatePackedRunner(def, level);
	if (!activePackedValid) printf("PACK VALIDATION ERROR L%d\n",level);
	// The box no longer stretches to whatever the frame turned out to be, so a
	// frame that outgrows it has to be heard about rather than silently swallowed.
	if (!kitBoxFits()) printf("BOX TOO SMALL L%d frm %.2fx%.2f\n",level,runnerFrameW,runnerFrameH);
	resolveAssemblyTargets(def);
}

static void packLooseHomes(const kitDef* def, int level)
{
	// Reserve every cut part a home on the bare wood left of the mat.  Deepest
	// first into shelves the width of the zone, then the whole block is centred
	// on the zone.  A part's footprint is its drawn silhouette: the body widened
	// by the gate nub that travels with it, which is why a part thinner than the
	// gate still reserves the gate's width.
	{
		int order[PART_SPEC_COUNT];
		float fw[PART_SPEC_COUNT], fd[PART_SPEC_COUNT];
		for (int i = 0; i < PART_SPEC_COUNT; i++)
		{
			order[i] = i;
			fw[i] = fmaxf(activeSpecs[i].silExt[0], 0.09f);
			// Depth is not symmetric: the part is placed by its body centre but
			// the nub sits behind it, so a shallow part reaches further back.
			float d = def->parts[i].size[2];
			fd[i] = 2.0f * fmaxf(d*0.5f, 0.045f - (d - RUN_BAR)*0.5f);
		}
		for (int a = 1; a < PART_SPEC_COUNT; a++)
		{
			int k = order[a], b = a - 1;
			while (b >= 0 && fd[order[b]] < fd[k]) { order[b+1] = order[b]; b--; }
			order[b+1] = k;
		}
		float penX = 0.0f, penZ = 0.0f, rowD = 0.0f, blockW = 0.0f;
		for (int k = 0; k < PART_SPEC_COUNT; k++)
		{
			int i = order[k];
			if (penX > 0.0f && penX + LOOSE_GAP + fw[i] > LOOSE_W)
			{
				penZ += rowD + LOOSE_GAP; penX = 0.0f; rowD = 0.0f;
			}
			if (penX > 0.0f) penX += LOOSE_GAP;
			looseHome[i][0] = penX + fw[i]*0.5f;
			looseHome[i][1] = penZ + fd[i]*0.5f;
			penX += fw[i];
			if (penX > blockW) blockW = penX;
			if (fd[i] > rowD) rowD = fd[i];
		}
		float blockD = penZ + rowD;
		float ox = LOOSE_X - blockW*0.5f, oz = LOOSE_Z - blockD*0.5f;
		for (int i = 0; i < PART_SPEC_COUNT; i++) { looseHome[i][0] += ox; looseHome[i][1] += oz; }
		activeLooseValid = (blockW <= LOOSE_W + 0.001f) && (blockD <= LOOSE_D + 0.001f);
		if (!activeLooseValid) printf("LOOSE PACK OVERFLOW L%d %.2fx%.2f\n", level, blockW, blockD);
	}
}

static void buildBedroomShell(void)
{
	// Bedroom shell: exact room bounds and six non-overlapping back-wall pieces.
	roomFloorFirst=meshCount; addBox(0,-4.12f,0,15,.12f,13); roomFloorCount=meshCount-roomFloorFirst;
	roomFirst=meshCount;
	// Each wall is its own draw range as well as part of the shell range, for the
	// same reason the ceiling already was: main.c hides whichever wall the camera
	// has gone behind, and it can only do that if it can draw them separately.
	// The boxes are not reordered to achieve it - they already sit rear, left,
	// right, front, so this is only a matter of noting where each group starts.
	roomWallRearFirst=meshCount;
	// Door [-5.60,-3.60] x [-4.06,.80], window [.60,3.90] x [-1.80,1.20].
	addBox(-6.55f,-.87f,-6.42f,1.90f,6.38f,.16f); // full height left of door
	addBox(-1.50f,-.87f,-6.42f,4.20f,6.38f,.16f); // between door/window: [-3.60,.60]
	addBox( 5.70f,-.87f,-6.42f,3.60f,6.38f,.16f); // right of window: [3.90,7.50]
	addBox(-4.60f,1.56f,-6.42f,2.00f,1.52f,.16f); // header above door
	addBox( 2.25f,-2.93f,-6.42f,3.30f,2.26f,.16f); // wall below window
	addBox( 2.25f,1.76f,-6.42f,3.30f,1.12f,.16f); // wall above window
	roomWallRearCount=meshCount-roomWallRearFirst;
	roomWallLeftFirst=meshCount;  addBox(-7.42f,-.87f,0,.16f,6.38f,13); roomWallLeftCount=meshCount-roomWallLeftFirst;
	roomWallRightFirst=meshCount; addBox( 7.42f,-.87f,0,.16f,6.38f,13); roomWallRightCount=meshCount-roomWallRightFirst;
	// The front wall has no opening: a single joined panel closes the former
	// dark seam between the bed and desk.  Door/window remain on the rear wall.
	roomWallFrontFirst=meshCount;
	addBox(0,-.87f,6.42f,15.0f,6.38f,.16f);
	roomWallFrontCount=meshCount-roomWallFrontFirst;
	// Keep the ceiling as its own draw range.  main.c can then hide only this
	// slab when a high orbit would otherwise put the camera above the room.
	roomCount=meshCount-roomFirst;
	roomCeilingFirst=meshCount;
	addBox(0,2.40f,0,15,.16f,13);
	roomCeilingCount=meshCount-roomCeilingFirst;
	// A proper single bed runs lengthwise along the left/shelf wall.  Its long
	// side stays inset from the wall face, while the headboard meets the front
	// wall; this keeps the rear door and the desk corner completely clear.
	// Bed left edge = -6.43 - 1.82/2 = -7.34: exactly the left-wall interior
	// face.  Headboard far edge = 6.26 + .16/2 = 6.34: front-wall interior.
	roomBedWoodFirst=meshCount; addBox(-6.43f,-3.52f,3.655f,1.82f,.36f,5.05f); addBox(-6.43f,-2.55f,6.26f,1.82f,1.38f,.16f); roomBedWoodCount=meshCount-roomBedWoodFirst;
	roomMattressFirst=meshCount; addBox(-6.43f,-3.20f,3.65f,1.68f,.30f,4.88f); roomMattressCount=meshCount-roomMattressFirst;
	roomBlanketFirst=meshCount; addBox(-6.43f,-2.98f,2.72f,1.48f,.15f,2.52f); roomBlanketCount=meshCount-roomBlanketFirst;
	roomPillowFirst=meshCount; addBox(-6.82f,-2.96f,5.42f,.58f,.16f,.62f); addBox(-6.04f,-2.96f,5.42f,.58f,.16f,.62f); roomPillowCount=meshCount-roomPillowFirst;
	roomShelfFirst=meshCount; addBox(-6.98f,-.55f,-2.20f,.72f,.16f,2.30f); addBox(-6.98f,-1.35f,-2.20f,.72f,.16f,2.30f); roomShelfCount=meshCount-roomShelfFirst;
	roomDoorFirst=meshCount; addBox(-4.60f,-1.63f,-6.30f,2.18f,4.86f,.12f); addBox(-4.60f,-1.63f,-6.20f,1.86f,4.68f,.12f); addBox(-4.60f,-.65f,-6.12f,1.38f,.70f,.06f); addBox(-4.60f,-2.20f,-6.12f,1.38f,1.00f,.06f); roomDoorCount=meshCount-roomDoorFirst;
	roomKnobFirst=meshCount; addBox(-3.86f,-1.55f,-6.05f,.13f,.13f,.12f); roomKnobCount=meshCount-roomKnobFirst;
	// Window trim sits fully within the wall aperture; only sky/ground beyond
	// it are visible, with no opaque backing slab or edge seam.
	roomTrimFirst=meshCount; addBox(2.25f,1.10f,-6.34f,3.30f,.14f,.12f); addBox(2.25f,-1.70f,-6.34f,3.30f,.14f,.20f); addBox(.70f,-.30f,-6.34f,.14f,2.66f,.12f); addBox(3.80f,-.30f,-6.34f,.14f,2.66f,.12f); addBox(2.25f,-.30f,-6.35f,.08f,2.52f,.06f); roomTrimCount=meshCount-roomTrimFirst;
	roomSkyFirst=meshCount; addBox(2.25f,.10f,-7.20f,3.18f,2.20f,.06f); roomSkyCount=meshCount-roomSkyFirst;
	roomGroundFirst=meshCount; addBox(2.25f,-1.48f,-7.18f,3.18f,.42f,.08f); roomGroundCount=meshCount-roomGroundFirst;
}

static void buildDesk(void)
{
	// The bench: a wooden desk with a cutting mat laid on it. Everything is a
	// plain slab - at 320x240 the silhouette and the colour do all the work, and
	// wood grain would not survive the resolution.
	//
	// Top, apron and legs share one range because they share one material, so
	// the whole desk still costs a single draw.
	deskFirst = meshCount;
	addBox(BENCH_X, DESK_Y, BENCH_Z, BENCH_W, DESK_T, BENCH_D);

	addBox(BENCH_X, APRON_Y, BENCH_Z+FRAME_Z, APRON_LEN, APRON_T, 0.14f);
	addBox(BENCH_X, APRON_Y, BENCH_Z-FRAME_Z, APRON_LEN, APRON_T, 0.14f);
	addBox(BENCH_X+FRAME_X, APRON_Y, BENCH_Z, 0.14f, APRON_T, 2.0f*FRAME_Z);
	addBox(BENCH_X-FRAME_X, APRON_Y, BENCH_Z, 0.14f, APRON_T, 2.0f*FRAME_Z);

	addBox(BENCH_X-FRAME_X, LEG_Y, BENCH_Z-FRAME_Z, LEG_W, LEG_H, LEG_W);
	addBox(BENCH_X+FRAME_X, LEG_Y, BENCH_Z-FRAME_Z, LEG_W, LEG_H, LEG_W);
	addBox(BENCH_X-FRAME_X, LEG_Y, BENCH_Z+FRAME_Z, LEG_W, LEG_H, LEG_W);
	addBox(BENCH_X+FRAME_X, LEG_Y, BENCH_Z+FRAME_Z, LEG_W, LEG_H, LEG_W);
	deskCount = meshCount - deskFirst;
}

static void buildMat(void)
{
	matFirst = meshCount;
	addBox(MAT_X, MAT_Y, MAT_Z, MAT_W, MAT_T, MAT_D);
	matCount = meshCount - matFirst;
}

static void buildGrid(void)
{
	// The mat's printed grid, deliberately coarse. A fine grid turns to mush on
	// a 320-pixel screen; squares this size still read as squares.
	gridFirst = meshCount;
	{
		static const float lineX[] = { -1.6f, -0.8f, 0.0f, 0.8f, 1.6f };
		static const float lineZ[] = { -0.70f, 0.10f, 0.90f };

		for (int i = 0; i < (int)(sizeof(lineX)/sizeof(lineX[0])); i++)
		addBox(MAT_X+lineX[i], GRID_Y, MAT_Z, GRID_W, GRID_T, MAT_D-0.04f);

		for (int i = 0; i < (int)(sizeof(lineZ)/sizeof(lineZ[0])); i++)
		addBox(MAT_X, GRID_Y, MAT_Z+lineZ[i]-0.10f, MAT_W-0.04f, GRID_T, GRID_W);
	}
	gridCount = meshCount - gridFirst;
}

static void buildStand(void)
{
	// The stand the kit is built on. Two boxes: a plinth resting on the mat and
	// a post standing in it, which the finished parts hang off.
	standFirst = meshCount;
	addBox(STAND_X, MAT_TOP + PLINTH_T*0.5f,          STAND_Z, PLINTH_W, PLINTH_T, PLINTH_D);
	addBox(STAND_X, MAT_TOP + PLINTH_T + POST_H*0.5f, STAND_Z, POST_W,   POST_H,   POST_D);
	standCount = meshCount - standFirst;
}

static void buildKitBoxes(void)
{
	// The kit boxes: one flat shoebox per runner, stacked squarely on the desk.
	// Each is a shallow base, four walls and a separate lid resting on top, and
	// the lid of one is exactly where the base of the next begins - they meet
	// face to face rather than sinking into one another.
	//
	// Drawn as three passes rather than one loop of three because each pass is a
	// different colour and every draw range has to be contiguous.
	float boxW, boxD, boxH;
	kitBoxOuterSize(&boxW, &boxD, &boxH);
	float boxZ = kitBoxCentreZ();
	int boxes = kitBoxCount();

	kitTrayFirst = meshCount;
	for (int b = 0; b < boxes; b++)
	{
		float floorY = KIT_BOX_FLOOR + b*KIT_BOX_PITCH;
		float wallY  = floorY + KIT_BOX_T + boxH*0.5f;
		addBox(KIT_BOX_X, floorY + KIT_BOX_T*0.5f, boxZ, boxW, KIT_BOX_T, boxD); // base
		addBox(KIT_BOX_X - boxW*0.5f + KIT_BOX_T*0.5f, wallY, boxZ, KIT_BOX_T, boxH, boxD); // left
		addBox(KIT_BOX_X + boxW*0.5f - KIT_BOX_T*0.5f, wallY, boxZ, KIT_BOX_T, boxH, boxD); // right
		addBox(KIT_BOX_X, wallY, boxZ - boxD*0.5f + KIT_BOX_T*0.5f, boxW, boxH, KIT_BOX_T); // back
		addBox(KIT_BOX_X, wallY, boxZ + boxD*0.5f - KIT_BOX_T*0.5f, boxW, boxH, KIT_BOX_T); // front
	}
	kitTrayCount = meshCount - kitTrayFirst;

	kitLidFirst = meshCount;
	for (int b = 0; b < boxes; b++)
	{
		float lidY = KIT_BOX_FLOOR + b*KIT_BOX_PITCH + KIT_BOX_T + boxH + KIT_BOX_T*0.5f;
		addBox(KIT_BOX_X, lidY, boxZ, boxW + KIT_BOX_T, KIT_BOX_T, boxD + KIT_BOX_T);
	}
	kitLidCount = meshCount - kitLidFirst;

	// A raised, centred print panel on the lid: simple artwork that remains crisp
	// at 3DS resolution, rather than a texture the game cannot afford. Only the
	// top box in the stack gets one - on any box underneath it the panel would be
	// inside the box sitting on it, which is the one thing that must never happen.
	kitArtFirst = meshCount;
	{
		float topLid = KIT_BOX_FLOOR + (boxes - 1)*KIT_BOX_PITCH + KIT_BOX_PITCH;
		addBox(KIT_BOX_X, topLid + 0.0125f, boxZ - 0.10f, 1.65f, 0.025f, 0.82f);
	}
	kitArtCount = meshCount - kitArtFirst;
}

static void buildRunnerFrame(void)
{
	// The runner: frame first, then the runner-side half of every gate. All of
	// it is drawn as one range and none of it ever moves.
	//
	// A real sprue is not a hollow rectangle with the parts floating loose in
	// the middle of it - that is the giveaway on a counterfeit one. It is a thin
	// outer frame braced by inner bars, and the parts hang off whichever bar is
	// nearest. So: four rails and one cross-bar splitting the frame into an upper
	// and a lower lane. The cross-bar is thinner than the outer frame, the way
	// the moulded ones are.
	//
	// Every number here comes from the packing pass above rather than being fixed,
	// because parts are laid out at their true size and the frame has to be
	// whatever fits them. The cross-bar sits in the clear RUN_MID band the packer
	// reserved between the lanes, so it can never cut through a part however tall
	// the parts turn out to be.
	//
	// There is no inner upright. One frame is emitted per kit but only one runner
	// is on the desk at a time, and with true-size parts each runner puts its
	// parts at different x - so no fixed upright can be guaranteed to miss them
	// all, and a bar through the middle of a part is worse than a slightly plainer
	// frame.
	//
	// Bar faces line up with the gate stubs exactly - every stub is 0.09 tall and
	// reaches 0.045 from its own centre to the face it grew out of, which is the
	// inner face of the top rail for the upper lane and of the bottom rail for the
	// lower one. The side rails run the full height so the corners are closed, and
	// the cross-bar runs RUN_BAR/2 into them at each end.
	runnerFirst = meshCount;
	float railY = runnerInnerH*0.5f + RUN_BAR*0.5f;
	float railX = runnerFrameW*0.5f - RUN_BAR*0.5f;
	addBox( 0.00f,  railY, 0.0f, runnerFrameW, RUN_BAR, RUN_BAR);              // top rail
	addBox( 0.00f, -railY, 0.0f, runnerFrameW, RUN_BAR, RUN_BAR);              // bottom rail
	addBox(-railX,  0.00f, 0.0f, RUN_BAR, runnerFrameH, RUN_BAR);              // left rail
	addBox( railX,  0.00f, 0.0f, RUN_BAR, runnerFrameH, RUN_BAR);              // right rail
	addBox( 0.00f, runnerBarY, 0.0f, runnerFrameW - RUN_BAR, 0.10f, RUN_BAR);  // cross-bar

	runnerBaseCount = meshCount - runnerFirst;
	for (int i = 0; i < PART_SPEC_COUNT; i++) {
		stubFirst[i] = meshCount;
		addBoxSpec(&activeSpecs[i].stub);
		stubCount[i] = meshCount - stubFirst[i];
	}

	runnerCount = meshCount - runnerFirst;
}

static void buildParts(const kitDef* def)
{
	// The parts. Nub first so a part's range starts at the cut face.
	for (int i = 0; i < PART_SPEC_COUNT && partCount < MAX_PARTS; i++)
	{
		const partSpec* spec = &activeSpecs[i];
		meshPart* p = &parts[partCount++];

		p->name        = spec->name;
		p->firstVertex = meshCount;
		addBoxSpec(&spec->nub);
		p->nubVertexCount = meshCount - p->firstVertex;
		// Signature pieces make the kit read as its named subject rather than a
		// recoloured block set. The AABB authored in the recipe remains the tap
		// bound, so the new silhouettes stay just as easy to select.
		partShape shape = def ? def->parts[i].shape : SH_BOX;
		partAxis axis = def ? def->parts[i].axis : AXIS_Y;
		float shapeSize[3] = { spec->body.sx, spec->body.sy, spec->body.sz };
		if (shape == SH_CYLINDER) addCylinder8Axis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis,false);
		else if (shape == SH_CONE) addCylinder8Axis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis,true);
		else if (shape == SH_WEDGE) addWedgeAxis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis);
		// A rod is a round bar, so it is the cylinder emitter under another name.
		// The separate enum earns its keep in the recipes, where SH_ROD says leg
		// or mast or antenna and SH_CYLINDER says boiler or wheel.
		else if (shape == SH_ROD) addCylinder8Axis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis,false);
		else if (shape == SH_PLATE) addPlate(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize);
		else if (shape == SH_STAR) addStar5(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize);
		else if (shape == SH_GRILL) addFanGrill(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize);
		else if (shape == SH_FAN_BLADE) addFanBlade(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,def->parts[i].spin);
		else if (shape == SH_DOME) addDome8Axis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis);
		else if (shape == SH_TUBE) addTube8Axis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis);
		else if (shape == SH_CURVE) addCurvedPanel(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis);
		else if (shape == SH_BELL) addBell8Axis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis);
		else if (shape == SH_WHEEL) addWheelAxis(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,axis);
		else addBoxSpec(&spec->body);
		p->vertexCount = meshCount - p->firstVertex;

		p->bodyCentre[0] = spec->body.cx;
		p->bodyCentre[1] = spec->body.cy;
		p->bodyCentre[2] = spec->body.cz;

		// Filing buries the nub in the body one nub-height of travel. A nub
		// below its part is also what the part is resting on, so as it wears
		// the part has to settle by the same amount to stay on the mat.
		if (spec->nub.cy > spec->body.cy)
		{
			p->nubSink = -spec->nub.sy;
			p->nubDrop =  0.0f;
		}
		else
		{
			p->nubSink =  spec->nub.sy;
			p->nubDrop =  spec->nub.sy;
		}
		p->colour = def->parts[i].colour;
		p->runner = def->parts[i].runner;

		// Selecting tests the whole travelling piece, nub included - the nub is
		// part of the part now, and sanding it off comes later.
		// Sized off the silhouette, not the drawing box: a tap box that does not
		// cover what is on screen is a part you can see and cannot pick up.
		setBounds(p->min, p->max, &spec->body);
		const float silCentre[2] = { spec->body.cx + spec->silOff[0],
		                             spec->body.cy + spec->silOff[1] };
		for (int a = 0; a < 2; a++)
		{
			p->min[a] = silCentre[a] - spec->silExt[a]*.5f;
			p->max[a] = silCentre[a] + spec->silExt[a]*.5f;
		}
		p->min[0]=fminf(p->min[0],spec->nub.cx-spec->nub.sx*.5f); p->max[0]=fmaxf(p->max[0],spec->nub.cx+spec->nub.sx*.5f);
		p->min[1]=fminf(p->min[1],spec->nub.cy-spec->nub.sy*.5f); p->max[1]=fmaxf(p->max[1],spec->nub.cy+spec->nub.sy*.5f);

		// A part on the runner is drawn at exactly the size it will be once cut, so
		// its packed tap box is simply the body box taken about the packed position.
		// Nothing is scaled on the way in or out any more, which is what makes the
		// two sets of bounds the same shape.
		const float bodySize[3] = { spec->silExt[0], spec->silExt[1], spec->body.sz };
		const float bodyOff[3]  = { spec->silOff[0], spec->silOff[1], 0.0f };
		for (int a = 0; a < 3; a++)
		{
			float half = bodySize[a] * .5f;
			p->runnerMin[a] = p->bodyCentre[a] + bodyOff[a] - half;
			p->runnerMax[a] = p->bodyCentre[a] + bodyOff[a] + half;
		}
		p->runnerMin[0]=fminf(p->runnerMin[0],spec->nub.cx-spec->nub.sx*.5f); p->runnerMax[0]=fmaxf(p->runnerMax[0],spec->nub.cx+spec->nub.sx*.5f);
		p->runnerMin[1]=fminf(p->runnerMin[1],spec->nub.cy-spec->nub.sy*.5f); p->runnerMax[1]=fmaxf(p->runnerMax[1],spec->nub.cy+spec->nub.sy*.5f);
		p->runnerMin[2]=fminf(p->runnerMin[2],spec->nub.cz-spec->nub.sz*.5f); p->runnerMax[2]=fmaxf(p->runnerMax[2],spec->nub.cz+spec->nub.sz*.5f);

		// Snipping tests the runner-side stub only, so a tap on the gate cuts
		// rather than selects.
		setBounds(p->gateMin, p->gateMax, &spec->stub);
	}
}

static void buildSockets(void)
{
	// The sockets. A socket's tap box is the part that belongs in it, sitting
	// where it will end up - so aiming at a socket is aiming at the shape you
	// are about to see there.
	//
	// A spec whose part is missing is dropped, which shifts everything after it
	// down, so the parent indices written in the table above cannot be copied
	// straight across. specToSocket remembers where each spec actually landed
	// (-1 if it was dropped) and the parents are looked up through it. A part
	// whose parent was dropped keeps no parent at all rather than an index into
	// the wrong socket.
	socketCount = 0;
	for (int i = 0; i < partCount && socketCount < MAX_SOCKETS; i++)
	{
		const boxSpec* body = &activeSpecs[i].body;
		const float size[3] = { body->sx, body->sy, body->sz };
		meshSocket* s = &sockets[socketCount++];

		s->name = activeSpecs[i].name;
		s->part = i;
		s->parent = activeParent[i];

		for (int a = 0; a < 3; a++)
		{
			float shifted = activeTarget[i][a];
			s->pos[a] = shifted;
			s->min[a] = shifted - size[a]*0.5f;
			s->max[a] = shifted + size[a]*0.5f;
		}
	}
}

// Item 34: assigns real atlas UVs to vertices [first, first+count), which
// must already have position+normal written (called after the emitter that
// wrote them, before meshDedupBuild). Projects each vertex's position onto
// the plane implied by its own dominant normal axis, normalizes that by the
// range's own bounding box so the UV always lands in the full [0,1] the
// caller then remaps into the atlas sub-rectangle, tiles by repeatU/repeatV
// first so a small region pattern (grain, tread, panel lines) reads at a
// believable texel scale instead of one pattern stretched blurrily across a
// whole part.
static void applyAtlasUV(int first, int count, float u0, float v0, float u1, float v1, float repeatU, float repeatV)
{
	if (count <= 0) return;
	float mn[3] = {1e30f,1e30f,1e30f}, mx[3] = {-1e30f,-1e30f,-1e30f};
	for (int i = first; i < first+count; i++)
		for (int k = 0; k < 3; k++)
		{
			float p = meshData[i].position[k];
			if (p < mn[k]) mn[k] = p;
			if (p > mx[k]) mx[k] = p;
		}
	float sz[3] = { mx[0]-mn[0], mx[1]-mn[1], mx[2]-mn[2] };
	for (int i = first; i < first+count; i++)
	{
		const float* n = meshData[i].normal;
		const float* p = meshData[i].position;
		float ax = fabsf(n[0]), ay = fabsf(n[1]), az = fabsf(n[2]);
		int ua, va;
		if (ax >= ay && ax >= az)      { ua = 1; va = 2; } // facing X: project onto Y/Z
		else if (ay >= ax && ay >= az) { ua = 0; va = 2; } // facing Y: project onto X/Z
		else                            { ua = 0; va = 1; } // facing Z: project onto X/Y

		float lu = sz[ua] > 1e-6f ? (p[ua]-mn[ua]) / sz[ua] : 0.0f;
		float lv = sz[va] > 1e-6f ? (p[va]-mn[va]) / sz[va] : 0.0f;
		lu *= repeatU; lv *= repeatV;
		lu -= floorf(lu); lv -= floorf(lv); // wrap the tiling CPU-side; atlas wrap stays CLAMP

		meshData[i].uv[0] = u0 + lu * (u1 - u0);
		meshData[i].uv[1] = v0 + lv * (v1 - v0);
	}
}

// Item 34: the one texturing pass, run once per kit build after every part's
// position/normal is final and before meshDedupBuild folds duplicates. Kept
// as its own function, called from meshBuildKit alongside the other build*
// stages, rather than threaded through every emitter - the emitters only
// ever write the {0,0} default (see emitAxisVertex/PLATE_VTX), and this is
// the one place that overwrites it with something real.
static void applyAtlasTexturing(void)
{
#if TEST_ATLAS_GRADIENT
	// Swizzle verification only: the atlas is one full-size gradient (see
	// texture_atlas.c), mapped across the kit-art panel - the largest flat
	// on-screen surface - at full [0,1] range so any tearing is obvious.
	applyAtlasUV(kitArtFirst, kitArtCount, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f);
	return;
#endif
	// Kit-box print panel: subtle grain, one pass over the panel's own extent.
	applyAtlasUV(kitArtFirst, kitArtCount,
		ATLAS_REGION_GRAIN_U0, ATLAS_REGION_GRAIN_V0, ATLAS_REGION_GRAIN_U1, ATLAS_REGION_GRAIN_V1, 1.0f, 1.0f);

	// Runner frame: the same grain, moulded-plastic surface.
	applyAtlasUV(runnerFirst, runnerBaseCount,
		ATLAS_REGION_GRAIN_U0, ATLAS_REGION_GRAIN_V0, ATLAS_REGION_GRAIN_U1, ATLAS_REGION_GRAIN_V1, 2.0f, 2.0f);

	// Curated by-name parts: tread on the Excavator's tracks, panel lines on
	// the named hull/body-style parts across every kit. Only these two
	// families get texture - everything else stays flat, which is the
	// deliberate art direction the task called for.
	for (int i = 0; i < partCount; i++)
	{
		const char* nm = parts[i].name;
		bool tread = strcmp(nm, "track L") == 0 || strcmp(nm, "track R") == 0;
		bool panel = strcmp(nm, "hull") == 0 || strcmp(nm, "chassis") == 0 ||
		             strcmp(nm, "cabin") == 0 || strcmp(nm, "cab") == 0 ||
		             strcmp(nm, "boiler") == 0 || strcmp(nm, "core") == 0 ||
		             strcmp(nm, "torso") == 0 || strcmp(nm, "fuselage") == 0 ||
		             strcmp(nm, "body") == 0;
		if (tread)
			applyAtlasUV(parts[i].firstVertex, parts[i].vertexCount,
				ATLAS_REGION_TREAD_U0, ATLAS_REGION_TREAD_V0, ATLAS_REGION_TREAD_U1, ATLAS_REGION_TREAD_V1, 2.0f, 2.0f);
		else if (panel)
			applyAtlasUV(parts[i].firstVertex, parts[i].vertexCount,
				ATLAS_REGION_PANEL_U0, ATLAS_REGION_PANEL_V0, ATLAS_REGION_PANEL_U1, ATLAS_REGION_PANEL_V1, 2.0f, 2.0f);
	}
}

void meshBuildKit(int level)
{
	if (level < 1) level = 1;
	if (level > 20) level = 20;
	activeLevel = level;
	const kitDef* def = level <= 8 ? &kitDefs[level - 1] : (level <= 14 ? &kitDefsMid[level - 9] : &kitDefsLate[level - 15]);
	activeKitName = def->name;
	activeRunners = def->runners;

	packRunnerLayout(def, level);
	packLooseHomes(def, level);

	meshCount = 0;
	meshDropped = 0;
	partCount = 0;

	buildBedroomShell();
	buildDesk();
	buildMat();
	buildGrid();
	buildStand();
	buildKitBoxes();
	buildRunnerFrame();
	buildParts(def);
	buildSockets();
	buildJoints(level);
	applyAtlasTexturing();

	meshDedupBuild();
}

// The unique-vertex buffer meshDedupBuild produced, meant for the VBO now
// rather than the raw emission order - see the Item 36 block above.
const vertex* meshVertices(void) { return dedupData; }
int meshVertexCount(void)        { return dedupCount; }
// The index list every First/Count draw-range pair below is measured in - the
// same units meshVertexCount() used to report before indexing existed.
const unsigned short* meshIndices(void) { return meshIndex; }
int meshIndexCount(void)                { return meshCount; }
const meshPart* meshParts(void)  { return parts; }
int meshPartCount(void)          { return partCount; }

// Reported against meshBoxCapacity, so both have to be in the same unit: whole
// boxes worth of the vertex buffer. Round up, because the round shapes cost a
// fraction of a box each (a cylinder is 96 vertices, a star 120) and flooring
// would report headroom that is not there on the run that overflows.
int meshBoxCount(void)    { return (meshCount + VERTS_PER_BOX - 1) / VERTS_PER_BOX; }
int meshBoxCapacity(void) { return MAX_BOXES; }
int meshDroppedCount(void) { return meshDropped; }

const meshSocket* meshSockets(void) { return sockets; }
int meshSocketCount(void)           { return socketCount; }

int meshDeskFirstVertex(void)   { return deskFirst; }
int meshDeskVertexCount(void)   { return deskCount; }
int meshMatFirstVertex(void)    { return matFirst; }
int meshMatVertexCount(void)    { return matCount; }
int meshGridFirstVertex(void)   { return gridFirst; }
int meshGridVertexCount(void)   { return gridCount; }

int meshRunnerFirstVertex(void) { return runnerFirst; }
int meshRunnerVertexCount(void) { return runnerCount; }
int meshRunnerBaseVertexCount(void) { return runnerBaseCount; }
int meshRunnerStubFirstVertex(int part) { return part >= 0 && part < partCount ? stubFirst[part] : 0; }
int meshRunnerStubVertexCount(int part) { return part >= 0 && part < partCount ? stubCount[part] : 0; }

int meshStandFirstVertex(void)  { return standFirst; }
int meshStandVertexCount(void)  { return standCount; }

int meshKitTrayFirstVertex(void) { return kitTrayFirst; }
int meshKitTrayVertexCount(void) { return kitTrayCount; }
int meshKitLidFirstVertex(void)  { return kitLidFirst; }
int meshKitLidVertexCount(void)  { return kitLidCount; }
int meshKitArtFirstVertex(void)  { return kitArtFirst; }
int meshKitArtVertexCount(void)  { return kitArtCount; }

int meshRoomFirstVertex(void)       { return roomFirst; }
int meshRoomVertexCount(void)       { return roomCount; }
int meshRoomCeilingFirstVertex(void){return roomCeilingFirst;} int meshRoomCeilingVertexCount(void){return roomCeilingCount;}
int meshRoomFloorFirstVertex(void){return roomFloorFirst;} int meshRoomFloorVertexCount(void){return roomFloorCount;}
int meshRoomBedWoodFirstVertex(void){return roomBedWoodFirst;} int meshRoomBedWoodVertexCount(void){return roomBedWoodCount;}
int meshRoomMattressFirstVertex(void){return roomMattressFirst;} int meshRoomMattressVertexCount(void){return roomMattressCount;}
int meshRoomBlanketFirstVertex(void){return roomBlanketFirst;} int meshRoomBlanketVertexCount(void){return roomBlanketCount;}
int meshRoomPillowFirstVertex(void){return roomPillowFirst;} int meshRoomPillowVertexCount(void){return roomPillowCount;}
int meshRoomDoorFirstVertex(void){return roomDoorFirst;} int meshRoomDoorVertexCount(void){return roomDoorCount;}
int meshRoomKnobFirstVertex(void){return roomKnobFirst;} int meshRoomKnobVertexCount(void){return roomKnobCount;}
int meshRoomShelfFirstVertex(void){return roomShelfFirst;} int meshRoomShelfVertexCount(void){return roomShelfCount;}
int meshRoomTrimFirstVertex(void){return roomTrimFirst;} int meshRoomTrimVertexCount(void){return roomTrimCount;}
int meshRoomSkyFirstVertex(void){return roomSkyFirst;} int meshRoomSkyVertexCount(void){return roomSkyCount;}
int meshRoomGroundFirstVertex(void){return roomGroundFirst;} int meshRoomGroundVertexCount(void){return roomGroundCount;}
int meshRoomWallRearFirstVertex(void){return roomWallRearFirst;} int meshRoomWallRearVertexCount(void){return roomWallRearCount;}
int meshRoomWallFrontFirstVertex(void){return roomWallFrontFirst;} int meshRoomWallFrontVertexCount(void){return roomWallFrontCount;}
int meshRoomWallLeftFirstVertex(void){return roomWallLeftFirst;} int meshRoomWallLeftVertexCount(void){return roomWallLeftCount;}
int meshRoomWallRightFirstVertex(void){return roomWallRightFirst;} int meshRoomWallRightVertexCount(void){return roomWallRightCount;}

// The solid things in the room a camera must not be able to get inside.
//
// It lives here rather than in main.c because every number in it is a number
// this file already owns; a copy over there would be a second set of furniture
// dimensions to keep in step, and they would not stay in step.
//
// Three things in the room are deliberately NOT on the list, and all three are
// off it for the same reason: they are what the camera is looking at, so making
// them solid does not keep the camera out of anything, it just pins the camera
// on top of the model.
//
//   The desk top. The point the camera looks at rests 0.27 above the desk
//   surface, so a camera tilted down is aimed straight at that surface. Making
//   it solid collapsed the view on 22660 of the 74880 reachable poses - more
//   than half of the downward half of the pitch range. Looking up at the model
//   from a little under the desk plane is a view a model viewer is supposed to
//   have.
//
//   The build stand's plinth, for the same reason one step smaller: it is the
//   pad the model stands on, dead centre of every framing. 7480 more poses.
//
//   The stand's post, which the model actually hangs off. There is no distance
//   at which a camera pointed at the model is not near the post.
//
// What is left is the furniture round the edges of the room - the bed, the two
// shelf boards - plus the kit box stack, which is the one thing on the desk the
// camera has no business being inside. Each is one enclosing volume, because
// each is either solid through or a closed box; splitting them would add work
// without changing an answer. The stack is one obstacle however many boxes are
// in it.
// Named so the count comes off the end of the list rather than being written out
// twice: adding a blocker here bumps what meshBlockerCount() reports, and the
// switch below shouts if the matching case was forgotten. It used to be a bare
// literal 4 with nothing tying it to the cases at all.
enum
{
	BLOCKER_KIT_BOX = 0,
	BLOCKER_BED,
	BLOCKER_SHELF_UPPER,
	BLOCKER_SHELF_LOWER,
	BLOCKER_COUNT
};

int meshBlockerCount(void) { return BLOCKER_COUNT; }

// Which entry is the kit box stack. The stack is only there while the box is
// still shut - once it opens, the tray and lid stop being drawn and the runner
// takes their place - so the caller has to be able to drop this one on its own.
int meshBlockerKitBoxIndex(void) { return BLOCKER_KIT_BOX; }

// Fills min/max from a centre-and-size box, the way addBox is written, so the
// blocker list can be read straight off the emission code above it.
static void blockerFromBox(float min[3], float max[3],
                           float cx, float cy, float cz, float sx, float sy, float sz)
{
	min[0] = cx - sx*0.5f; max[0] = cx + sx*0.5f;
	min[1] = cy - sy*0.5f; max[1] = cy + sy*0.5f;
	min[2] = cz - sz*0.5f; max[2] = cz + sz*0.5f;
}

void meshBlockerBounds(int index, float min[3], float max[3])
{
	switch (index)
	{
	case BLOCKER_KIT_BOX: // The whole kit box stack, however many boxes high.
		meshKitBoxBounds(min, max);
		return;
	case BLOCKER_BED: // The bed: frame, headboard, mattress and bedding as one block.
		min[0] = -7.34f; max[0] = -5.52f;
		min[1] = -3.70f; max[1] = -1.86f;
		min[2] =  1.155f; max[2] = 6.34f;
		return;
	case BLOCKER_SHELF_UPPER:
		blockerFromBox(min,max, -6.98f, -0.55f, -2.20f, 0.72f, 0.16f, 2.30f); return;
	case BLOCKER_SHELF_LOWER:
		blockerFromBox(min,max, -6.98f, -1.35f, -2.20f, 0.72f, 0.16f, 2.30f); return;
	default:
		// An empty box at the origin is invisible to the camera tests, so a
		// blocker added to the enum without a case here would quietly stop
		// blocking anything. Say so instead.
		printf("BLOCKER %d HAS NO BOUNDS\n", index);
		min[0] = min[1] = min[2] = 0.0f;
		max[0] = max[1] = max[2] = 0.0f;
		return;
	}
}

void meshKitBoxBounds(float min[3], float max[3])
{
	// The lid overhangs the tray, so the tap box is measured off the lid or the
	// overhanging strip down each side would be drawn but not tappable. The whole
	// stack answers the tap, not just the top box - a player aiming at the pile
	// should not have to find the one box that opens.
	float boxW, boxD, boxH;
	kitBoxOuterSize(&boxW, &boxD, &boxH);
	min[0] = KIT_BOX_X - (boxW + KIT_BOX_T)*0.5f;
	min[1] = KIT_BOX_FLOOR;
	min[2] = kitBoxCentreZ() - (boxD + KIT_BOX_T)*0.5f;
	max[0] = KIT_BOX_X + (boxW + KIT_BOX_T)*0.5f;
	max[1] = KIT_BOX_FLOOR + kitBoxCount()*KIT_BOX_PITCH;
	max[2] = kitBoxCentreZ() + (boxD + KIT_BOX_T)*0.5f;
}

void meshKitBoxedOffset(float out[3])
{
	out[0] = KIT_BOX_X;
	// Final flat-runner landing: the same rear-desk position the shoebox used,
	// resting just above the desk surface rather than over the cutting mat.
	out[1] = -1.45f;
	out[2] = kitBoxCentreZ();
}
