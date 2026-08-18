// Geometry for the model kit.
//
// Everything is built out of boxes for now. Real part shapes come later; the
// point of this file is that main.c never touches vertex data directly.

#pragma once

// uv is the texture coordinate into the one shared atlas (texture_atlas.h).
// Most vertices keep it at {0,0}, which samples the atlas's reserved white
// texel - MODULATE against that leaves the lit vertex colour unchanged, so
// adding this field cannot regress a part nobody has textured. See
// applyAtlasUV() in mesh.c for the vertices that get a real value.
typedef struct { float position[3]; float normal[3]; float uv[2]; } vertex;

// How big a kit is allowed to get. These live in the header rather than in
// mesh.c because main.c keeps a state record per part and has to size it off
// the same number - two separate limits that happen to match today is a buffer
// overrun waiting for the day someone raises only one of them.
//
// The box budget is what actually caps a kit. A box is 36 vertices and the
// budget is really a vertex budget spent in those units, so a part costs a stub
// plus a nub plus a body and the body is only one box if it is box-shaped: a
// wedge is cheaper, a cylinder is nearly three boxes and a star is over three.
// 384 leaves room for the 60-part kit the last level is meant to be, plus the
// bedroom shell. Vertex data is 1152 bytes a box (uv added the vertex's two
// extra floats, item 34), so the ceiling itself costs 432 KB of the 65536 KB
// an Original 3DS gives us - the headroom is still close to free. That budget
// is OLD3DS_APP_BUDGET in memory_status.h, which owns the figure; the 65312
// this comment used to quote was the old guess that tried to subtract a
// pre-main() overhead off the ceiling, and it was wrong.
#define MESH_MAX_BOXES    384
#define MESH_MAX_PARTS     64
// The longest an authored part name may be, not counting the terminator. The
// workbench composes messages like "<socket> needs <part> first" out of two of
// them into a fixed buffer, so an over-long name would not overflow anything -
// snprintf truncates - but it would quietly cut the sentence off mid-word with
// nothing said. meshValidateKits checks every authored part name against this,
// and main.c sizes that buffer from it, so the two cannot drift. The longest
// part name authored today is "triangular stand", at 16.
#define MESH_NAME_MAX      20
// Kit names are held to their own, looser limit: they are never composed into
// those two-name messages, only printed on their own line in the manual header,
// so the tight limit does not apply to them. The longest today is "Rescue
// Helicopter", at 17.
#define MESH_KIT_NAME_MAX  24
// How many levels the kit tables define, and the only place that number is
// written down. It was spelled out as a bare 20 in the level loops in mesh.c, in
// main.c's all-kits audit, in main.c's per-level save array and in the front
// end's level grid - five copies of one fact, with nothing making them agree.
#define MESH_KIT_LEVELS    20
// A kit is authored with at most four runners, and ships in one standard box per
// runner, stacked on the desk.
#define MESH_MAX_RUNNERS    4
#define MESH_MAX_SOCKETS   64

// The surface a part rests on once it is put down: a hair above the mat's
// printed grid lines, so a part never sinks into one. Shared, because the
// build stand stands on it too and the two have to agree.
#define MAT_TOP (-1.317f)

// The inside faces of the room shell. main.c stops drawing a surface once the
// camera has crossed it, and stops the camera crossing the solid furniture, so
// both files have to agree to the last decimal on where each surface is. These
// are the interior faces, not the slab centres: the floor's top, the ceiling's
// underside, and the room side of each of the four walls.
#define ROOM_FLOOR_TOP     (-4.06f)
#define ROOM_CEILING_UNDER   2.32f
#define ROOM_WALL_LEFT     (-7.34f)
#define ROOM_WALL_RIGHT      7.34f
#define ROOM_WALL_REAR     (-6.34f)
#define ROOM_WALL_FRONT      6.34f

// One part of the kit.
//
// The vertex range covers the part body AND the nub - the stub of gate left
// behind when you snip it off the runner - because the two travel together once
// the part is cut free. The other half of the gate stays on the runner and is
// drawn with it.
//
// Three sets of bounds, all in model space: the part at its full authored size,
// which a tap selects once it has been cut free; the same part at the smaller
// size it is drawn at while still packed on the runner, which is what a tap
// selects until then; and the gate, which a tap snips.
//
// The nub is the first nubVertexCount vertices of the range, so filing can draw
// it apart from the body. Filing sinks it into the body rather than scaling it
// - a squashed box would wreck its face normals. nubSink is the Y travel that
// buries it exactly, signed because some nubs hang under their part; nubDrop is
// how far the part settles as the nub wears away, and is zero for a nub that
// points up and never rested on the mat.
//
// bodyCentre is the middle of the part body alone, with the nub left out. It is
// what a socket lines up against: by the time a part is fitted the nub is filed
// away, so the whole-part centre would seat it crooked.
typedef struct
{
	const char* name;
	int   firstVertex;
	int   vertexCount;
	int   nubVertexCount;
	float nubSink;
	float nubDrop;
	float bodyCentre[3];
	float min[3];
	float max[3];
	// The same bounds taken about the packed runner position. Parts are packed
	// at their authored size, so this is the full silhouette - a tap on a part
	// still on the frame covers exactly what is drawn there.
	float runnerMin[3];
	float runnerMax[3];
	float gateMin[3];
	float gateMax[3];
	unsigned char colour;
	unsigned char runner;
} meshPart;

// A place on the build stand that one part clicks into. pos is where that
// part's bodyCentre ends up; min/max is the part-sized box a tap has to land in
// to fit it, before the caller's own slop is added.
//
// parent is the socket this one physically hangs off, so nothing can be fitted
// into thin air: an arm needs its shoulder on the stand first. Only the first
// piece of the kit has no parent.
typedef struct
{
	const char* name;
	int   part;
	int   parent;
	float pos[3];
	float min[3];
	float max[3];
} meshSocket;

// Which bench-space axis a joint turns about. Kept to the three cardinal
// axes rather than an arbitrary vector: every joint authored so far - an arm
// swinging forward, a wing flapping up, a boom raising - turns about one of
// them, and Mtx_RotateX/Y/Z is what the rest of the file already uses for
// every other rotation, so a joint asks for the same three letters instead of
// pulling in a general axis-angle rotation the codebase has never needed.
typedef enum { JOINT_AXIS_X, JOINT_AXIS_Y, JOINT_AXIS_Z } meshJointAxis;

// A part that can be posed once the kit is fully built, and the hinge it
// turns on. Authored, not guessed - a part with no entry here is rigid and
// stays exactly where it was fitted no matter how far posing pushes on its
// parent; a part that is itself somebody else's child follows that parent's
// rotation for free through the same socket-parent chain assembly already
// uses (see buildSockets), with no entry of its own required.
//
// pivotOffset is a delta from the part's own bodyCentre, in bench space -
// not an absolute position, so the hinge travels with the kit's placement on
// the bench for free and does not have to be re-authored if that ever moves.
// It marks where the part actually joins its parent (a shoulder, a boom's
// root), not the part's own middle - rotating about the middle would spin it
// in place rather than swing it.
typedef struct
{
	int           part;
	float         pivotOffset[3];
	meshJointAxis axis;
	float         minDeg;
	float         maxDeg;
} meshJoint;

// The joint authored for a given part in the currently built kit, or NULL if
// that part is rigid. Answers against whatever meshBuildKit last built, the
// same way meshParts()/meshSockets() do.
const meshJoint* meshJointForPart(int part);

// Builds the workbench - desk, cutting mat, runner frame, and the parts with
// their gates - into an internal buffer. Call once before uploading the VBO.
void meshBuildKit(int level);
bool meshValidateKits(void);
bool meshAuditAllKits(void);
// Strict collision audit: face contact is allowed, any positive 3D volume
// intersection in the fully cut loose layout or assembled layout is a fail.
bool meshCollisionAuditAllKits(void);
const char* meshKitName(void);
// How many parts a level's kit holds, without building it. The front end draws
// the level grid before any kit is loaded, so it cannot use meshPartCount() -
// and it must not keep its own copy of the number, which is what let the grid
// advertise counts the kits did not have. Returns 0 for a level outside 1-20.
int meshKitPartCount(int level);
int meshKitRunnerCount(void);
float meshBenchX(void);
float meshBenchZ(void);
float meshLooseX(void);
float meshLooseZ(void);
// The middle of the cutting mat - the middle of the working half of the desk,
// and what the opening camera is aimed at.
float meshMatX(void);
float meshMatZ(void);
float meshStandX(void);
float meshStandZ(void);
float meshRunnerX(void);
float meshRunnerZ(void);
// Where a given cut part rests once it is off the frame, and the height of the
// bare wood it rests on.  Keyed on the part, not on how many have been cut, so
// two parts can never be put down in the same place whatever order they go in.
void meshLooseSlot(int part, float out[2]);
float meshLooseTopY(void);

// The deduplicated (position,normal) pairs - the VBO the scene actually
// binds. Smaller than the draw order below because a shared quad corner is
// stored once no matter how many triangles reuse it (item 36).
const vertex* meshVertices(void);
int meshVertexCount(void);

// The triangle draw order: one index into meshVertices() per vertex the scene
// would have drawn before indexing existed. Every First/Count accessor below
// - meshDeskFirstVertex and the rest - is an offset and a count into THIS
// array, not into meshVertices(), so a draw call reads
// meshIndices() + firstVertex, vertexCount indices long, through
// C3D_DrawElements. The naming keeps "vertex" because every caller already
// thinks in those terms; only the buffer the offset resolves against changed.
const unsigned short* meshIndices(void);
int meshIndexCount(void);

// How much of the box budget the built scene used, and how much there was.
// Reported on screen: addBox drops a box it has no room for and says nothing,
// so without these a kit that overflows just quietly loses parts.
int meshBoxCount(void);
int meshBoxCapacity(void);
// How many pieces the last build had to throw away for want of room. Zero on a
// kit that fits; anything else means the model on screen is missing geometry.
int meshDroppedCount(void);

const meshPart* meshParts(void);
int meshPartCount(void);

const meshSocket* meshSockets(void);
int meshSocketCount(void);

// The bench the kit is built on: the wooden desk with its apron and legs, the
// self-healing cutting mat laid on it, and the mat's printed grid. Drawn in
// three ranges because each is a different colour. Scenery: never moves,
// never picked.
int meshDeskFirstVertex(void);
int meshDeskVertexCount(void);
int meshMatFirstVertex(void);
int meshMatVertexCount(void);
int meshGridFirstVertex(void);
int meshGridVertexCount(void);

// The runner: the four rails plus the runner half of every gate. Stays put when
// a part is cut, so the snipped gate stubs stay behind on the frame.
int meshRunnerFirstVertex(void);
int meshRunnerVertexCount(void);
int meshRunnerBaseVertexCount(void);
int meshRunnerStubFirstVertex(int part);
int meshRunnerStubVertexCount(int part);

// The build stand: the plinth sitting on the mat and the post rising off it,
// which the finished parts hang on. Scenery, like the desk - the sockets are
// what gets picked, not this.
int meshStandFirstVertex(void);
int meshStandVertexCount(void);

// The closed kit boxes behind the mat - one per runner, stacked. The tray and
// lid stay separate so the lid can later animate without rebuilding the mesh,
// and only the top box carries a printed panel because the lids underneath are
// covered by the box above.
int meshKitTrayFirstVertex(void);
int meshKitTrayVertexCount(void);
int meshKitLidFirstVertex(void);
int meshKitLidVertexCount(void);
int meshKitArtFirstVertex(void);
int meshKitArtVertexCount(void);
void meshKitBoxBounds(float min[3], float max[3]);
void meshKitBoxedOffset(float out[3]);

// Bedroom scenery surrounding the workbench. Never picked.
int meshRoomFirstVertex(void);
int meshRoomVertexCount(void);
int meshRoomCeilingFirstVertex(void); int meshRoomCeilingVertexCount(void);
int meshRoomFloorFirstVertex(void); int meshRoomFloorVertexCount(void);
int meshRoomBedWoodFirstVertex(void); int meshRoomBedWoodVertexCount(void);
int meshRoomMattressFirstVertex(void); int meshRoomMattressVertexCount(void);
int meshRoomBlanketFirstVertex(void); int meshRoomBlanketVertexCount(void);
int meshRoomPillowFirstVertex(void); int meshRoomPillowVertexCount(void);
int meshRoomDoorFirstVertex(void); int meshRoomDoorVertexCount(void);
int meshRoomKnobFirstVertex(void); int meshRoomKnobVertexCount(void);
int meshRoomShelfFirstVertex(void); int meshRoomShelfVertexCount(void);
int meshRoomTrimFirstVertex(void); int meshRoomTrimVertexCount(void);
int meshRoomSkyFirstVertex(void); int meshRoomSkyVertexCount(void);
int meshRoomGroundFirstVertex(void); int meshRoomGroundVertexCount(void);
// The four walls, separately, so each can be hidden on its own once the camera
// has gone behind it - the ceiling already worked this way and now they all do.
int meshRoomWallRearFirstVertex(void); int meshRoomWallRearVertexCount(void);
int meshRoomWallFrontFirstVertex(void); int meshRoomWallFrontVertexCount(void);
int meshRoomWallLeftFirstVertex(void); int meshRoomWallLeftVertexCount(void);
int meshRoomWallRightFirstVertex(void); int meshRoomWallRightVertexCount(void);

// The solid volumes the camera is not allowed inside: the desk box by box, the
// build stand, the kit box stack, the bed and the two shelf boards. Given as
// obstacles rather than draw ranges, because what stops a camera is the shape of
// the furniture, not how it is coloured. Indices 0..meshBlockerCount()-1.
int meshBlockerCount(void);
void meshBlockerBounds(int index, float min[3], float max[3]);
// Which entry is the kit box stack, so it can be dropped once the box is open
// and no longer drawn.
int meshBlockerKitBoxIndex(void);
