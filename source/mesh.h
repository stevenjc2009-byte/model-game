// Geometry for the model kit.
//
// Everything is built out of boxes for now. Real part shapes come later; the
// point of this file is that main.c never touches vertex data directly.

#pragma once

typedef struct { float position[3]; float normal[3]; } vertex;

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
// bedroom shell. Vertex data is 864 bytes a box, so the ceiling itself costs
// 324 KB of the 65312 KB an Original 3DS gives us - the headroom is close to
// free.
#define MESH_MAX_BOXES    384
#define MESH_MAX_PARTS     64
#define MESH_MAX_SOCKETS   64

// The surface a part rests on once it is put down: a hair above the mat's
// printed grid lines, so a part never sinks into one. Shared, because the
// build stand stands on it too and the two have to agree.
#define MAT_TOP (-1.317f)

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
	// Scale applied about bodyCentre while the body is still attached to its
	// packed runner. The stored mesh and the assembly sockets stay at the full
	// authored dimensions; only the draw and runnerMin/runnerMax shrink.
	float runnerScale[3];
	float nubFullOffset[3];
	float min[3];
	float max[3];
	// The same bounds at the packed size actually drawn on the runner, so a tap
	// on a shrunken part cannot reach past it into a neighbouring runner cell.
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

// Builds the workbench - desk, cutting mat, runner frame, and the parts with
// their gates - into an internal buffer. Call once before uploading the VBO.
void meshBuildKit(int level);
bool meshValidateKits(void);
bool meshAuditAllKits(void);
// Strict collision audit: face contact is allowed, any positive 3D volume
// intersection in the fully cut loose layout or assembled layout is a fail.
bool meshCollisionAuditAllKits(void);
const char* meshKitName(void);
int meshKitRunnerCount(void);
float meshBenchX(void);
float meshBenchZ(void);
float meshLooseX(void);
float meshLooseZ(void);
float meshStandX(void);
float meshStandZ(void);
float meshRunnerX(void);
float meshRunnerZ(void);

const vertex* meshVertices(void);
int meshVertexCount(void);

// How much of the box budget the built scene used, and how much there was.
// Reported on screen: addBox drops a box it has no room for and says nothing,
// so without these a kit that overflows just quietly loses parts.
int meshBoxCount(void);
int meshBoxCapacity(void);

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

// The closed kit box behind the mat. The tray and lid stay separate so the
// lid can later animate without rebuilding the mesh.
int meshKitTrayFirstVertex(void);
int meshKitTrayVertexCount(void);
int meshKitLidFirstVertex(void);
int meshKitLidVertexCount(void);
int meshKitArtFirstVertex(void);
int meshKitArtVertexCount(void);
int meshKitSpareFirstVertex(void);
int meshKitSpareVertexCount(void);
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
