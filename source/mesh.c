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

// Capacity comes from mesh.h so main.c's per-part state cannot drift from it.
#define MAX_BOXES   MESH_MAX_BOXES
#define MAX_PARTS   MESH_MAX_PARTS
#define MAX_SOCKETS MESH_MAX_SOCKETS
#define VERTS_PER_BOX 36

// The workbench, stacked bottom up. The mat sinks into the desk and the grid
// sits a hair proud of the mat, so no two coplanar faces ever fight over depth.
// Desk centre after a 180-degree turn about the former (3.10,2.00) centre.
// Right/front edges land exactly on the room interior faces x=7.34,z=6.34.
#define BENCH_X   3.64f
#define BENCH_Z   3.94f
#define DESK_Y   (-1.40f)
#define DESK_T     0.10f
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

// The build stand: a plinth on the mat with a post rising out of it, standing at
// the front of the bench where nothing can come between it and the stylus. It
// sits in front of every cut-part slot, so a part waiting on the mat can never
// land inside it.
//
// The post is tall and slim because ten parts have to hang off it without their
// tap boxes running into each other - a short post crowds them into the same
// few pixels and the wrong one answers the tap.
// Separate horizontal work zones prevent the runner from sprawling over the
// assembly stand: runner/box left-rear, loose parts centre, stand right-front.
#define RUNNER_X   5.19f
#define RUNNER_Z   5.13f
#define LOOSE_X    3.29f
#define LOOSE_Z    3.66f
// The display stand is exactly at the tabletop centre, derived from the desk
// transform rather than a hand-tuned offset.
#define STAND_X    BENCH_X
#define STAND_Z    BENCH_Z
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
#define KIT_BOX_X       RUNNER_X
#define KIT_BOX_Y      (-1.18f)
#define KIT_BOX_Z       RUNNER_Z
#define KIT_BOX_W       4.10f
#define KIT_BOX_H       0.42f
#define KIT_BOX_D       2.42f
#define KIT_BOX_T       0.12f

// The fourth runner's side box lives on the widened left desk wing.  This is
// derived from the desk's left edge (-3.70 from BENCH_X) and its 1.68 width,
// leaving it just clear of the mat's left edge while fully supported.
#define SPARE_BOX_W     1.68f
#define SPARE_BOX_D     1.62f
#define SPARE_BOX_X     6.48f
#define SPARE_BOX_Z     3.19f

static vertex meshData[MAX_BOXES * VERTS_PER_BOX];
static int meshCount;

static meshPart parts[MAX_PARTS];
static int partCount;

static meshSocket sockets[MAX_SOCKETS];
static int socketCount;

static int deskFirst, deskCount;
static int matFirst, matCount;
static int gridFirst, gridCount;
static int runnerFirst, runnerCount;
static int runnerBaseCount;
static int stubFirst[MAX_PARTS], stubCount[MAX_PARTS];
static float runnerDisplayScale[MAX_PARTS][3];
// Kept with the active mesh so the whole-kit audit can fail on a bad packed
// frame rather than merely leaving a diagnostic in the console.
static bool activePackedValid;
static int standFirst, standCount;
static int kitTrayFirst, kitTrayCount;
static int kitLidFirst, kitLidCount;
static int kitArtFirst, kitArtCount;
static int kitSpareFirst, kitSpareCount;
static int roomFirst, roomCount, roomCeilingFirst, roomCeilingCount, roomFloorFirst, roomFloorCount;
static int roomBedWoodFirst, roomBedWoodCount, roomMattressFirst, roomMattressCount;
static int roomBlanketFirst, roomBlanketCount, roomPillowFirst, roomPillowCount;
static int roomDoorFirst, roomDoorCount, roomKnobFirst, roomKnobCount;
static int roomShelfFirst, roomShelfCount, roomTrimFirst, roomTrimCount;
static int roomSkyFirst, roomSkyCount, roomGroundFirst, roomGroundCount;
static int activeLevel = 1;
static int activeRunners = 1;
static const char* activeKitName = "Star Trophy";

// A kit with more than one runner needs a bigger box to hold them. The geometry
// and the tap bounds both go through here, because a box drawn wider than it can
// be touched leaves a dead strip down each side that ignores the stylus.
static void kitBoxOuterSize(float* w, float* d, float* h)
{
	bool big = activeRunners >= 2;
	*w = KIT_BOX_W + (big ? 0.42f : 0.0f);
	*d = KIT_BOX_D + (big ? 0.24f : 0.0f);
	*h = KIT_BOX_H + (big ? 0.12f : 0.0f);
}

typedef enum { SH_BOX, SH_CYLINDER, SH_CONE, SH_WEDGE, SH_PLATE, SH_ROD, SH_STAR, SH_GRILL, SH_FAN_BLADE } partShape;
typedef enum { AXIS_X, AXIS_Y, AXIS_Z } partAxis;
typedef struct { const char* name; partShape shape; partAxis axis; float size[3]; float target[3]; float spin; signed char parent; unsigned char colour; unsigned char runner; } kitPartDef;
typedef struct { const char* name; unsigned char runners; kitPartDef parts[10]; } kitDef;
#define P(N,S,A,SX,SY,SZ,TX,TY,TZ,PA,C,R) {N,S,A,{SX,SY,SZ},{TX,TY,TZ},0.0f,PA,C,R}
#define PR(N,S,A,SX,SY,SZ,TX,TY,TZ,SPIN,PA,C,R) {N,S,A,{SX,SY,SZ},{TX,TY,TZ},SPIN,PA,C,R}
#define KIT(N,R,A,B,C,D,E,F,G,H,I,J) {N,R,{A,B,C,D,E,F,G,H,I,J}}
static const char* const kitNames[20] __attribute__((unused)) = {
	"Star Trophy", "Small Cactus", "Sailboat", "Rocket", "Desk Fan", "Friendly Robot",
	"Space Rover", "Fire Engine", "Submarine", "Propeller Plane", "Race Car", "Excavator",
	"Dragon", "Castle Tower", "Pirate Ship", "Steam Locomotive", "Spacecraft", "Dinosaur",
	"Rescue Helicopter", "Large Mech"
};
static const kitDef kitDefsMid[6] = {
KIT("Submarine",2,P("hull",SH_CYLINDER,AXIS_X,1.5,.6,.6,0,-.3,0,-1,3,0),P("tower",SH_BOX,AXIS_Y,.4,.35,.4,0,.15,0,0,0,0),P("periscope",SH_ROD,AXIS_Y,.08,.55,.08,0,.55,0,1,1,0),P("tail fin",SH_WEDGE,AXIS_X,.4,.4,.08,.7,-.2,0,0,3,1),P("propeller",SH_CYLINDER,AXIS_X,.3,.08,.3,.88,-.3,0,3,1,1),P("plane L",SH_PLATE,AXIS_Z,.5,.12,.05,.15,-.3,.38,0,3,1),P("plane R",SH_PLATE,AXIS_Z,.5,.12,.05,.15,-.3,-.38,0,3,1),P("window",SH_CYLINDER,AXIS_Z,.14,.05,.14,-.25,-.18,.3,0,1,1),P("keel",SH_PLATE,AXIS_Y,.8,.05,.18,0,-.62,0,0,3,1),P("stand",SH_BOX,AXIS_Y,.7,.1,.5,0,-.75,0,0,0,1)),
KIT("Propeller Plane",2,P("hub",SH_CYLINDER,AXIS_X,.25,.1,.25,-.85,-.1,0,-1,3,0),P("fuselage",SH_CYLINDER,AXIS_X,1.5,.4,.4,0,-.15,0,0,0,0),P("wing L",SH_WEDGE,AXIS_Z,.8,.12,1.4,0,-.15,.55,1,2,0),P("wing R",SH_WEDGE,AXIS_Z,.8,.12,1.4,0,-.15,-.55,1,2,0),P("tail",SH_PLATE,AXIS_Z,.45,.15,.65,.65,.0,0,1,2,1),P("rudder",SH_WEDGE,AXIS_Y,.12,.45,.35,.68,.22,0,4,2,1),P("wheel L",SH_CYLINDER,AXIS_X,.25,.12,.25,-.2,-.65,.3,1,3,1),P("wheel R",SH_CYLINDER,AXIS_X,.25,.12,.25,-.2,-.65,-.3,1,3,1),P("cockpit",SH_BOX,AXIS_Y,.4,.25,.3,.15,.15,0,1,1,1),P("spinner",SH_CONE,AXIS_X,.25,.25,.25,-1.05,-.1,0,0,3,1)),
// Low side-profile racer: wide front wing, low wedge body, raised cockpit,
// four corner wheels and a clearly separate rear spoiler.
KIT("Race Car",2,P("chassis",SH_WEDGE,AXIS_Y,1.85,.38,.72,0,-.48,0,-1,2,0),P("cockpit",SH_BOX,AXIS_Y,.62,.28,.48,.18,-.12,0,0,0,0),P("front wing",SH_PLATE,AXIS_Y,.20,.08,1.08,-1.04,-.67,0,0,0,0),P("rear spoiler",SH_PLATE,AXIS_Y,.20,.15,1.02,.98,.04,0,1,0,1),P("wheel FL",SH_CYLINDER,AXIS_X,.38,.18,.38,-.52,-.76,.44,0,3,1),P("wheel FR",SH_CYLINDER,AXIS_X,.38,.18,.38,-.52,-.76,-.44,0,3,1),P("wheel RL",SH_CYLINDER,AXIS_X,.38,.18,.38,.58,-.76,.44,0,3,1),P("wheel RR",SH_CYLINDER,AXIS_X,.38,.18,.38,.58,-.76,-.44,0,3,1),P("nose",SH_CONE,AXIS_X,.48,.26,.40,-1.10,-.47,0,0,2,1),P("rear diffuser",SH_PLATE,AXIS_Y,.30,.07,.76,.96,-.67,0,3,3,1)),
KIT("Excavator",2,P("track base",SH_BOX,AXIS_Y,1.2,.35,.7,0,-.65,0,-1,3,0),P("cab",SH_BOX,AXIS_Y,.55,.55,.55,-.2,-.1,0,0,2,0),P("boom",SH_ROD,AXIS_X,1.0,.15,.15,.55,.15,0,1,3,0),P("arm",SH_ROD,AXIS_Y,.15,.75,.15,.95,-.25,0,2,3,1),P("bucket",SH_WEDGE,AXIS_X,.45,.35,.5,1.1,-.65,0,3,2,1),P("track L",SH_BOX,AXIS_Y,1.1,.3,.18,0,-.85,.35,0,3,1),P("track R",SH_BOX,AXIS_Y,1.1,.3,.18,0,-.85,-.35,0,3,1),P("hydraulic",SH_ROD,AXIS_X,.7,.07,.07,.55,-.05,.18,2,0,1),P("weight",SH_BOX,AXIS_Y,.4,.4,.5,-.55,-.35,0,0,3,1),P("pivot",SH_CYLINDER,AXIS_Y,.22,.15,.22,.0,-.28,0,0,0,1)),
KIT("Dragon",2,P("body",SH_CYLINDER,AXIS_X,1.25,.55,.55,0,-.25,0,-1,1,0),P("head",SH_WEDGE,AXIS_X,.55,.45,.45,-.75,.0,0,0,1,0),P("tail",SH_CONE,AXIS_X,.75,.3,.3,.85,-.3,0,0,1,0),P("wing L",SH_WEDGE,AXIS_Z,.75,.15,1.1,0,.15,.58,0,3,1),P("wing R",SH_WEDGE,AXIS_Z,.75,.15,1.1,0,.15,-.58,0,3,1),P("leg FL",SH_ROD,AXIS_Y,.16,.55,.16,-.35,-.68,.24,0,1,1),P("leg FR",SH_ROD,AXIS_Y,.16,.55,.16,-.35,-.68,-.24,0,1,1),P("leg RL",SH_ROD,AXIS_Y,.16,.55,.16,.35,-.68,.24,0,1,1),P("leg RR",SH_ROD,AXIS_Y,.16,.55,.16,.35,-.68,-.24,0,1,1),P("horn",SH_CONE,AXIS_Y,.14,.3,.14,-.85,.35,0,1,3,1)),
KIT("Castle Tower",3,P("base",SH_CYLINDER,AXIS_Y,.9,.35,.9,0,-.75,0,-1,0,0),P("ring",SH_CYLINDER,AXIS_Y,.75,.35,.75,0,-.42,0,0,0,0),P("tower",SH_CYLINDER,AXIS_Y,.62,.75,.62,0,.1,0,1,0,0),P("battlement",SH_BOX,AXIS_Y,.9,.18,.9,0,.58,0,2,3,1),P("door",SH_PLATE,AXIS_Z,.25,.42,.05,0,-.55,.45,0,2,1),P("window",SH_PLATE,AXIS_Z,.16,.2,.05,.25,.12,.32,2,1,1),P("mast",SH_ROD,AXIS_Y,.07,.65,.07,0,1.0,0,3,3,1),P("flag",SH_PLATE,AXIS_Z,.4,.22,.04,.2,1.2,0,6,2,2),P("roof",SH_CONE,AXIS_Y,.7,.35,.7,0,.95,0,3,2,2),P("trim",SH_PLATE,AXIS_Y,.7,.05,.7,0,-.22,0,1,3,2)),
};
// Kit cards are the authoritative recipe index: each entry declares its palette
// and the meaningful silhouette roles that populate the shared gate layout.
// Legacy card data is retained only for names while explicit kitDef migration is
// in progress; gameplay does not read its inferred shape field.
typedef struct { const char* roles[10]; unsigned char runners; unsigned char palette[4]; partShape shapes[10]; } kitRecipe;
static const kitRecipe recipes[20] __attribute__((unused)) = {
	{{"star","cup","stem","base","point","point","point","rim","foot","badge"},1,{3,0,3,0}},
	{{"pot","soil","cactus trunk","left arm","right arm","flower","rib","rib","base","tag"},1,{1,2,1,2}},
	{{"hull","keel","mast","main sail","front sail","rudder","deck","flag","boom","anchor"},1,{1,0,2,0}},
	{{"nose cone","rocket body","fin left","fin right","fin rear","nozzle","window","band","flame","stand"},1,{0,2,1,0}},
	{{"fan hub","guard","blade one","blade two","blade three","motor","neck","base","switch","foot"},1,{1,0,3,1}},
	{{"head","face panel","torso","hip","left arm","right arm","left leg","right leg","antenna","backpack"},1,{3,1,0,2}},
	{{"rover chassis","cabin","antenna","wheel FL","wheel FR","wheel RL","wheel RR","bumper","camera","solar panel"},1,{0,3,2,1}},
	{{"cab","engine body","ladder","wheel FL","wheel FR","wheel RL","wheel RR","light bar","hose","bumper"},2,{2,0,3,1}},
	{{"sub hull","tower","periscope","tail fin","propeller","port plane","starboard plane","window","keel","stand"},2,{3,0,1,2}},
	{{"prop hub","fuselage","wing left","wing right","tail","rudder","wheel left","wheel right","cockpit","spinner"},2,{0,2,1,3}},
	{{"car body","cockpit","front wing","rear wing","wheel FL","wheel FR","wheel RL","wheel RR","nose","diffuser"},2,{2,3,0,1}},
	{{"track base","cab","boom","arm","bucket","track left","track right","hydraulic","counterweight","pivot"},2,{3,2,1,0}},
	{{"dragon body","head","tail taper","wing left","wing right","leg FL","leg FR","leg RL","leg RR","horn"},2,{1,3,0,2}},
	{{"tower base","tower ring","tower top","battlement","door","window","flag mast","flag","roof","stone trim"},3,{0,1,3,2}},
	{{"hull","deck","mast front","mast rear","sail front","sail rear","bow","rudder","cannon","anchor"},3,{2,0,1,3}},
	{{"boiler","cab","chimney","wheel FL","wheel FR","wheel RL","wheel RR","tender","lamp","coupler"},3,{3,2,0,1}},
	{{"core body","nose cone","pod left","pod right","wing left","wing right","engine left","engine right","canopy","antenna"},4,{0,1,3,2}},
	{{"dino body","head","tail taper","leg FL","leg FR","leg RL","leg RR","plate one","plate two","horn"},4,{1,3,2,0}},
	{{"cabin","rotor hub","tail boom","tail rotor","skid left","skid right","door","nose","winch","light"},4,{2,0,1,3}},
	{{"mech torso","head","shoulder left","shoulder right","arm left","arm right","leg left","leg right","backpack","shield"},4,{1,0,2,3}}
};

// Explicit authored kit data. P records a real part's geometry, assembled
// target (kit-local), dependency, colour and runner. Compact macro notation
// keeps the 200 records readable on a 3DS-sized codebase.
static const kitDef kitDefs[20] = {
KIT("Star Trophy",1, P("star",SH_STAR,AXIS_Z,.68,.68,.12,0,.63,.05,-1,3,0),P("cup",SH_CONE,AXIS_Y,.48,.38,.20,0,.10,0,0,3,0),P("stem",SH_ROD,AXIS_Y,.12,.42,.12,0,-.30,0,1,0,0),P("base",SH_CYLINDER,AXIS_Y,.58,.14,.58,0,-.58,0,2,3,0),P("point",SH_PLATE,AXIS_Z,.12,.10,.04,-.28,.53,.00,0,3,0),P("point",SH_PLATE,AXIS_Z,.12,.10,.04,.28,.53,.00,0,3,0),P("badge",SH_PLATE,AXIS_Z,.22,.14,.04,0,.10,.19,1,0,0),P("rim",SH_CYLINDER,AXIS_Y,.50,.06,.50,0,.27,0,1,3,0),P("foot",SH_BOX,AXIS_Y,.42,.09,.28,0,-.70,0,3,0,0),P("shine",SH_PLATE,AXIS_Z,.08,.16,.03,.12,.16,.22,1,3,0)),
KIT("Small Cactus",1, P("pot",SH_CONE,AXIS_Y,.62,.48,.62,0,-.7,0,-1,2,0),P("soil",SH_CYLINDER,AXIS_Y,.48,.1,.48,0,-.43,0,0,3,0),P("trunk",SH_CYLINDER,AXIS_Y,.26,1.0,.26,0,.05,0,1,1,0),P("left arm",SH_ROD,AXIS_X,.6,.16,.16,-.36,.12,0,2,1,0),P("right arm",SH_ROD,AXIS_X,.6,.16,.16,.36,.28,0,2,1,0),P("flower",SH_CONE,AXIS_Y,.22,.22,.22,0,.62,0,2,3,0),P("rib",SH_PLATE,AXIS_Z,.05,.7,.04,-.13,.05,.14,2,1,0),P("rib",SH_PLATE,AXIS_Z,.05,.7,.04,.13,.05,.14,2,1,0),P("base",SH_BOX,AXIS_Y,.5,.1,.5,0,-.98,0,0,2,0),P("tag",SH_PLATE,AXIS_Z,.22,.12,.04,.28,-.72,.3,0,3,0)),
KIT("Sailboat",1, P("hull",SH_WEDGE,AXIS_X,1.5,.45,.5,0,-.55,0,-1,1,0),P("keel",SH_ROD,AXIS_Y,.12,.6,.12,0,-.85,0,0,3,0),P("mast",SH_ROD,AXIS_Y,.1,1.6,.1,0,.35,0,0,2,0),P("main sail",SH_WEDGE,AXIS_X,.75,1.0,.05,.38,.48,0,2,0,0),P("front sail",SH_WEDGE,AXIS_X,.55,.7,.05,-.38,.28,0,2,0,0),P("rudder",SH_PLATE,AXIS_Z,.22,.4,.05,-.78,-.48,0,0,2,0),P("deck",SH_PLATE,AXIS_Y,1.1,.08,.42,0,-.25,0,0,2,0),P("flag",SH_PLATE,AXIS_Z,.35,.2,.04,0,1.15,0,2,3,0),P("boom",SH_ROD,AXIS_X,.9,.08,.08,.25,.0,0,2,0,0),P("anchor",SH_ROD,AXIS_Y,.12,.32,.12,.7,-.65,0,0,3,0)),
KIT("Rocket",1, P("nose cone",SH_CONE,AXIS_Y,.48,.7,.48,0,.85,0,-1,2,0),P("rocket body",SH_CYLINDER,AXIS_Y,.5,1.4,.5,0,.0,0,0,0,0),P("fin left",SH_WEDGE,AXIS_X,.55,.35,.08,-.42,-.48,0,1,2,0),P("fin right",SH_WEDGE,AXIS_X,.55,.35,.08,.42,-.48,0,1,2,0),P("fin rear",SH_WEDGE,AXIS_Z,.08,.35,.55,0,-.48,-.38,1,2,0),P("nozzle",SH_CONE,AXIS_Y,.36,.35,.36,0,-.85,0,1,3,0),P("window",SH_CYLINDER,AXIS_Z,.16,.05,.16,0,.18,.25,1,1,0),P("band",SH_CYLINDER,AXIS_Y,.54,.12,.54,0,-.22,0,1,2,0),P("flame",SH_CONE,AXIS_Y,.22,.45,.22,0,-1.18,0,5,3,0),P("stand",SH_BOX,AXIS_Y,.8,.12,.6,0,-1.28,0,5,0,0)),
// Table fan reads from its silhouette: a circular guard in front of a motor,
// three radial blades, a narrow upright neck, and a weighted desk base.
// Two runners: the fan body and rear guard first, then the front guard and
// three separately cut blades.  This matches how a real two-shell fan cage
// encloses its rotor during final assembly.
KIT("Desk Fan",2, P("base",SH_BOX,AXIS_Y,1.18,.18,.72,0,-.88,0,-1,0,0),P("button left",SH_CYLINDER,AXIS_Y,.15,.08,.15,-.17,-.72,-.28,0,3,0),P("button right",SH_CYLINDER,AXIS_Y,.15,.08,.15,.17,-.72,-.28,0,3,0),P("triangular stand",SH_WEDGE,AXIS_Y,.62,.65,.45,0,-.45,.15,0,1,0),P("motor and hub",SH_CYLINDER,AXIS_Z,.46,.46,.46,0,.10,-.02,3,0,0),P("back grill",SH_GRILL,AXIS_Z,1.22,1.22,.12,0,.12,-.20,4,0,0),P("front grill",SH_GRILL,AXIS_Z,1.22,1.22,.12,0,.12,-.53,5,0,1),PR("blade one",SH_FAN_BLADE,AXIS_Z,.48,.32,.10,0,.10,-.36,0.00f,4,1,1),PR("blade two",SH_FAN_BLADE,AXIS_Z,.48,.32,.10,0,.10,-.36,2.0944f,4,1,1),PR("blade three",SH_FAN_BLADE,AXIS_Z,.48,.32,.10,0,.10,-.36,4.1888f,4,1,1)),
KIT("Friendly Robot",1, P("head",SH_BOX,AXIS_Y,.6,.5,.4,0,.55,0,-1,3,0),P("face",SH_PLATE,AXIS_Z,.4,.18,.04,0,.55,.23,0,1,0),P("torso",SH_BOX,AXIS_Y,.8,.7,.35,0,.0,0,0,0,0),P("hip",SH_BOX,AXIS_Y,.5,.18,.3,0,-.43,0,2,1,0),P("arm L",SH_ROD,AXIS_Y,.16,.65,.16,-.52,-.02,0,2,1,0),P("arm R",SH_ROD,AXIS_Y,.16,.65,.16,.52,-.02,0,2,1,0),P("leg L",SH_ROD,AXIS_Y,.22,.7,.22,-.22,-.78,0,3,3,0),P("leg R",SH_ROD,AXIS_Y,.22,.7,.22,.22,-.78,0,3,3,0),P("antenna",SH_ROD,AXIS_Y,.08,.35,.08,0,.95,0,0,2,0),P("pack",SH_BOX,AXIS_Y,.45,.5,.18,0,.0,-.28,2,0,0)),
KIT("Space Rover",1, P("chassis",SH_WEDGE,AXIS_X,1.3,.35,.7,0,-.45,0,-1,0,0),P("cabin",SH_BOX,AXIS_Y,.6,.45,.5,0,-.05,0,0,3,0),P("antenna",SH_ROD,AXIS_Y,.06,.55,.06,0,.42,0,1,2,0),P("wheel FL",SH_CYLINDER,AXIS_X,.36,.18,.36,-.5,-.72,.32,0,3,0),P("wheel FR",SH_CYLINDER,AXIS_X,.36,.18,.36,.5,-.72,.32,0,3,0),P("wheel RL",SH_CYLINDER,AXIS_X,.36,.18,.36,-.5,-.72,-.32,0,3,0),P("wheel RR",SH_CYLINDER,AXIS_X,.36,.18,.36,.5,-.72,-.32,0,3,0),P("bumper",SH_ROD,AXIS_X,1.1,.1,.1,0,-.55,.42,0,2,0),P("camera",SH_CYLINDER,AXIS_Z,.14,.1,.14,.25,.08,.3,1,1,0),P("panel",SH_PLATE,AXIS_Y,.6,.04,.4,-.35,-.05,.15,1,3,0)),
KIT("Fire Engine",2, P("cab",SH_BOX,AXIS_Y,.65,.6,.55,-.35,-.1,0,-1,2,0),P("body",SH_BOX,AXIS_Y,1.0,.55,.6,.35,-.2,0,0,2,0),P("ladder",SH_ROD,AXIS_X,1.2,.1,.1,.3,.3,0,1,0,1),P("wheel",SH_CYLINDER,AXIS_X,.3,.16,.3,-.48,-.7,.32,0,3,1),P("wheel",SH_CYLINDER,AXIS_X,.3,.16,.3,.48,-.7,.32,0,3,1),P("wheel",SH_CYLINDER,AXIS_X,.3,.16,.3,-.48,-.7,-.32,0,3,1),P("wheel",SH_CYLINDER,AXIS_X,.3,.16,.3,.48,-.7,-.32,0,3,1),P("light",SH_PLATE,AXIS_Y,.3,.05,.18,-.35,.25,.25,0,1,1),P("hose",SH_CYLINDER,AXIS_Z,.22,.08,.22,.45,.0,.34,1,3,1),P("bumper",SH_ROD,AXIS_X,1.2,.1,.1,.35,-.45,.25,1,0,1)),
};

// Late kits: compact, multi-runner authored silhouettes. Their records remain
// explicit even where a repeated limb/wheel uses the same dimensions.
static const kitDef kitDefsLate[6] = {
	KIT("Pirate Ship",3,P("hull",SH_WEDGE,AXIS_X,1.6,.5,.6,0,-.55,0,-1,2,0),P("deck",SH_PLATE,AXIS_Y,1.25,.08,.5,0,-.28,0,0,2,0),P("mast front",SH_ROD,AXIS_Y,.1,1.3,.1,-.35,.38,0,1,3,1),P("mast rear",SH_ROD,AXIS_Y,.1,1.15,.1,.38,.3,0,1,3,1),P("sail front",SH_WEDGE,AXIS_X,.55,.7,.05,-.1,.55,0,2,0,1),P("sail rear",SH_WEDGE,AXIS_X,.5,.62,.05,.62,.48,0,3,0,1),P("bow",SH_WEDGE,AXIS_X,.4,.25,.4,-.9,-.4,0,0,2,2),P("rudder",SH_PLATE,AXIS_Z,.2,.4,.05,.82,-.45,0,0,2,2),P("cannon",SH_ROD,AXIS_X,.42,.12,.12,.1,-.27,.22,1,3,2),P("anchor",SH_ROD,AXIS_Y,.12,.35,.12,-.65,-.7,0,0,3,2)),
	KIT("Steam Locomotive",3,P("boiler",SH_CYLINDER,AXIS_X,1.25,.55,.55,0,-.25,0,-1,3,0),P("cab",SH_BOX,AXIS_Y,.55,.65,.6,.55,.0,0,0,2,0),P("chimney",SH_CONE,AXIS_Y,.3,.55,.3,-.38,.28,0,0,3,0),P("wheel FL",SH_CYLINDER,AXIS_X,.34,.16,.34,-.45,-.75,.35,0,2,1),P("wheel FR",SH_CYLINDER,AXIS_X,.34,.16,.34,-.45,-.75,-.35,0,2,1),P("wheel RL",SH_CYLINDER,AXIS_X,.34,.16,.34,.3,-.75,.35,0,2,1),P("wheel RR",SH_CYLINDER,AXIS_X,.34,.16,.34,.3,-.75,-.35,0,2,1),P("tender",SH_BOX,AXIS_Y,.6,.5,.55,1.0,-.35,0,1,3,2),P("lamp",SH_CYLINDER,AXIS_Z,.14,.08,.14,-.72,-.2,.28,0,1,2),P("coupler",SH_ROD,AXIS_X,.32,.08,.08,1.42,-.6,0,7,3,2)),
	KIT("Spacecraft",4,P("core",SH_CYLINDER,AXIS_X,1.3,.5,.5,0,-.15,0,-1,0,0),P("nose",SH_CONE,AXIS_X,.5,.5,.5,-.85,-.15,0,0,0,0),P("pod L",SH_CYLINDER,AXIS_X,.65,.3,.3,.05,-.18,.5,0,1,1),P("pod R",SH_CYLINDER,AXIS_X,.65,.3,.3,.05,-.18,-.5,0,1,1),P("wing L",SH_WEDGE,AXIS_Z,.7,.1,.95,.18,-.15,.72,0,2,1),P("wing R",SH_WEDGE,AXIS_Z,.7,.1,.95,.18,-.15,-.72,0,2,1),P("engine L",SH_CONE,AXIS_X,.3,.28,.3,.78,-.18,.3,0,3,2),P("engine R",SH_CONE,AXIS_X,.3,.28,.3,.78,-.18,-.3,0,3,2),P("canopy",SH_BOX,AXIS_Y,.42,.25,.3,-.15,.2,0,0,1,3),P("antenna",SH_ROD,AXIS_Y,.06,.45,.06,.2,.45,0,8,3,3)),
	KIT("Dinosaur",4,P("body",SH_CYLINDER,AXIS_X,1.3,.55,.55,0,-.3,0,-1,1,0),P("head",SH_WEDGE,AXIS_X,.55,.45,.45,-.8,-.05,0,0,1,0),P("tail",SH_CONE,AXIS_X,.85,.3,.3,.9,-.35,0,0,1,1),P("leg FL",SH_ROD,AXIS_Y,.18,.65,.18,-.38,-.75,.28,0,3,1),P("leg FR",SH_ROD,AXIS_Y,.18,.65,.18,-.38,-.75,-.28,0,3,1),P("leg RL",SH_ROD,AXIS_Y,.18,.65,.18,.35,-.75,.28,0,3,2),P("leg RR",SH_ROD,AXIS_Y,.18,.65,.18,.35,-.75,-.28,0,3,2),P("plate L",SH_WEDGE,AXIS_Y,.25,.35,.12,-.1,.25,.22,0,2,2),P("plate R",SH_WEDGE,AXIS_Y,.25,.35,.12,.25,.25,-.22,0,2,3),P("horn",SH_CONE,AXIS_X,.16,.2,.16,-1.05,.15,0,1,3,3)),
	KIT("Rescue Helicopter",4,P("cabin",SH_BOX,AXIS_Y,.8,.5,.55,-.25,-.15,0,-1,2,0),P("nose",SH_WEDGE,AXIS_X,.45,.35,.45,-.75,-.18,0,0,2,0),P("tail boom",SH_ROD,AXIS_X,1.25,.16,.16,.65,-.1,0,0,1,1),P("tail rotor",SH_CYLINDER,AXIS_X,.32,.06,.32,1.25,-.08,0,2,3,1),P("skid L",SH_ROD,AXIS_X,.9,.50,.1,-.05,-.55,.24,0,3,2),P("skid R",SH_ROD,AXIS_X,.9,.50,.1,-.05,-.55,-.24,0,3,2),P("door",SH_PLATE,AXIS_Z,.35,.35,.04,-.1,-.1,.3,0,1,2),P("rotor hub",SH_CYLINDER,AXIS_Y,.2,.12,.2,-.05,.35,0,0,3,3),P("rotor blade",SH_PLATE,AXIS_X,1.5,.05,.12,-.05,.45,0,7,3,3),P("winch",SH_ROD,AXIS_Y,.08,.35,.08,-.35,-.55,.28,0,1,3)),
	KIT("Large Mech",4,P("torso",SH_BOX,AXIS_Y,.8,.7,.4,0,.0,0,-1,1,0),P("head",SH_BOX,AXIS_Y,.45,.4,.35,0,.58,0,0,0,0),P("shoulder L",SH_WEDGE,AXIS_X,.45,.3,.35,-.55,.22,0,0,2,1),P("shoulder R",SH_WEDGE,AXIS_X,.45,.3,.35,.55,.22,0,0,2,1),P("arm L",SH_ROD,AXIS_Y,.18,.7,.18,-.62,-.25,0,2,1,2),P("arm R",SH_ROD,AXIS_Y,.18,.7,.18,.62,-.25,0,3,1,2),P("leg L",SH_ROD,AXIS_Y,.25,.85,.25,-.25,-.82,0,0,3,3),P("leg R",SH_ROD,AXIS_Y,.25,.85,.25,.25,-.82,0,0,3,3),P("backpack",SH_BOX,AXIS_Y,.55,.5,.2,0,.0,-.32,0,1,3),P("shield",SH_PLATE,AXIS_Z,.45,.7,.08,-.95,-.18,.05,4,2,3))
};

const char* meshKitName(void) { return activeKitName; }
int meshKitRunnerCount(void) { return activeRunners; }
float meshBenchX(void) { return BENCH_X; }
float meshBenchZ(void) { return BENCH_Z; }
float meshLooseX(void) { return LOOSE_X; }
float meshLooseZ(void) { return LOOSE_Z; }
float meshStandX(void) { return STAND_X; }
float meshStandZ(void) { return STAND_Z; }
float meshRunnerX(void) { return RUNNER_X; }
float meshRunnerZ(void) { return RUNNER_Z; }

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
} partSpec;
static partSpec activeSpecs[MAX_PARTS];
static float activeTarget[MAX_PARTS][3];
static int activeParent[MAX_PARTS];

// Runner A. Gate halves meet exactly where the nipper bites, and each gate
// reaches from a bar to the edge of its part with no gap.
//
// The layout copies how a real sprue is moulded rather than how a spreadsheet
// would lay one out. Three things came out of looking at photographs of real
// ones, and all three are here:
//
//   - a gate runs to whatever structure is NEAREST, which is often an inner
//     cross-bar rather than the outer edge. So the frame has four faces parts
//     can hang off, not two, and the ten parts use all four.
//   - packing is irregular. Parts are not on a pitch, the gaps between them are
//     unequal, and the big ones get room while the small ones tuck into what is
//     left. A tidy row of evenly spaced parts is the thing that reads as fake.
//   - mirrored pairs sit side by side. Both pairs here - the legs and the arms -
//     are neighbours, which is also why they are so easy to mix up in real life.
//
// Sizes are untouched from before, and deliberately so: the stand's socket
// positions were derived from these boxes meeting face to face, so a part that
// changed size would come out of the frame fine and then not stack.
//
// Every gate is one 0.09 cube. A player who learns where to aim on one part
// aims the same way on all of them.
//
// The four anchor faces, so the numbers below are checkable rather than magic:
//   top rail underside   y  0.92  ->  stub cy  0.875, nub cy  0.785, part top  0.74
//   cross-bar top        y -0.05  ->  stub cy -0.005, nub cy  0.085, part bot  0.13
//   cross-bar underside  y -0.15  ->  stub cy -0.195, nub cy -0.285, part top -0.33
//   bottom rail top      y -0.92  ->  stub cy -0.875, nub cy -0.785, part bot -0.74
//
// Order is fixed: the socket table below indexes into this array.
static const partSpec partSpecs[] =
{
	// Upper lane, hanging down off the top rail. The widest part on the frame,
	// so it gets the whole left end to itself.
	{ "A1 armour plate",
		{ -1.15f,  0.875f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -1.15f,  0.785f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -1.15f,  0.49f,  0.0f, 0.80f, 0.50f, 0.10f } },

	// Upper lane, standing up off the cross-bar.
	{ "A2 shoulder block",
		{ -0.35f, -0.005f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -0.35f,  0.085f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -0.35f,  0.39f,  0.0f, 0.52f, 0.52f, 0.18f } },

	// Lower lane, hanging down off the cross-bar. Long and thin, so it lies
	// across the left of the lane with the small parts packed in beside it.
	{ "A3 strut",
		{ -1.05f, -0.195f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -1.05f, -0.285f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -1.05f, -0.41f,  0.0f, 0.95f, 0.16f, 0.16f } },

	// Lower lane, standing up off the bottom rail. The smallest body on the
	// frame, tucked under the strut's overhang.
	{ "A4 joint cube",
		{ -0.30f, -0.875f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -0.30f, -0.785f, 0.0f, 0.09f, 0.09f, 0.09f },
		{ -0.30f, -0.56f,  0.0f, 0.36f, 0.36f, 0.22f } },

	// Lower lane, standing up off the bottom rail.
	{ "A5 shoulder pad",
		{  0.25f, -0.875f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  0.25f, -0.785f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  0.25f, -0.48f,  0.0f, 0.52f, 0.52f, 0.18f } },

	// The arms: a mirrored pair, side by side off the cross-bar at the right
	// end of the upper lane.
	{ "A6 left arm",
		{  1.10f, -0.005f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  1.10f,  0.085f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  1.10f,  0.44f,  0.0f, 0.18f, 0.62f, 0.18f } },

	{ "A7 right arm",
		{  1.36f, -0.005f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  1.36f,  0.085f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  1.36f,  0.44f,  0.0f, 0.18f, 0.62f, 0.18f } },

	// The legs: the other mirrored pair, side by side off the top rail in the
	// middle of the upper lane.
	{ "A8 left leg",
		{  0.30f,  0.875f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  0.30f,  0.785f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  0.30f,  0.42f,  0.0f, 0.24f, 0.64f, 0.24f } },

	{ "A9 right leg",
		{  0.62f,  0.875f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  0.62f,  0.785f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  0.62f,  0.42f,  0.0f, 0.24f, 0.64f, 0.24f } },

	// Lower lane, hanging down off the cross-bar past the brace.
	{ "A10 shield",
		{  1.20f, -0.195f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  1.20f, -0.285f, 0.0f, 0.09f, 0.09f, 0.09f },
		{  1.20f, -0.53f,  0.0f, 0.52f, 0.40f, 0.14f } },
};

#define PART_SPEC_COUNT ((int)(sizeof(partSpecs)/sizeof(partSpecs[0])))

// Sockets are no longer a fixed table. Every kit authors its own assembly
// targets in its kitDef, and resolveAssemblyTargets turns those into the socket
// list at build time, so the old hand-written robot socketSpecs table is gone.

// Appends an axis-aligned box with per-face normals. Face order and winding
// match the devkitPro cube example, so the default cull mode is correct.
static void addBox(float cx, float cy, float cz, float sx, float sy, float sz)
{
	if (meshCount + VERTS_PER_BOX > (int)(sizeof(meshData)/sizeof(meshData[0])))
		return;

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

// Low-poly round primitives keep the kit silhouettes readable at 320x240.
// They are deliberately eight-sided: smooth enough for a rocket nose or wheel,
// cheap enough for an Original 3DS, and still give a conservative box bound for
// the existing stylus picker.
static void __attribute__((unused)) addCylinder8(float cx, float cy, float cz, float radius, float height, bool pointed)
{
	if (meshCount + 96 > (int)(sizeof(meshData)/sizeof(meshData[0]))) return;
	for (int i = 0; i < 8; i++)
	{
		float a0 = 6.2831853f * i / 8.0f, a1 = 6.2831853f * (i + 1) / 8.0f;
		float x0 = cx + radius * cosf(a0), z0 = cz + radius * sinf(a0);
		float x1 = cx + radius * cosf(a1), z1 = cz + radius * sinf(a1);
		float nx = cosf((a0 + a1) * 0.5f), nz = sinf((a0 + a1) * 0.5f);
		vertex side[6] = {
			{{x0,cy-height*.5f,z0},{nx,0,nz}}, {{x1,cy-height*.5f,z1},{nx,0,nz}},
			{{pointed?cx:x1,cy+height*.5f,pointed?cz:z1},{nx,0,nz}},
			{{pointed?cx:x1,cy+height*.5f,pointed?cz:z1},{nx,0,nz}},
			{{pointed?cx:x0,cy+height*.5f,pointed?cz:z0},{nx,0,nz}}, {{x0,cy-height*.5f,z0},{nx,0,nz}}};
		memcpy(&meshData[meshCount], side, sizeof(side)); meshCount += 6;
	}
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
	if (meshCount >= (int)(sizeof(meshData)/sizeof(meshData[0]))) return;
	axisMap(axis, cx, cy, cz, x, y, z, meshData[meshCount].position);
	axisNormal(axis, nx, ny, nz, meshData[meshCount].normal);
	meshCount++;
}

static void addCylinder8Axis(float cx, float cy, float cz, const float s[3], partAxis axis, bool pointed)
{
	float r = axis == AXIS_Y ? fminf(s[0], s[2])*.5f :
		(axis == AXIS_X ? fminf(s[1], s[2])*.5f : fminf(s[0], s[1])*.5f);
	float h = axis == AXIS_Y ? s[1] : (axis == AXIS_X ? s[0] : s[2]);
	if (meshCount + 96 > (int)(sizeof(meshData)/sizeof(meshData[0]))) return;
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
	if (meshCount + 24 > (int)(sizeof(meshData)/sizeof(meshData[0]))) return;
	for (int t=0; t<8; t++) for (int j=0;j<3;j++) {
		const float* q=p[ix[t*3+j]];
		const float* m=n[fn[t]];
		emitAxisVertex(axis,cx,cy,cz,q[0],q[1],q[2],m[0],m[1],m[2]);
	}
}

// A lightweight circular fan guard: an octagonal annular rim with a vertical
// and horizontal wire.  Unlike a solid cylinder it leaves the blades visible.
// Fan guards always face along Z in the authored kit data.
static void addFanGrill(float cx, float cy, float cz, const float s[3])
{
	float outer=fminf(s[0],s[1])*.5f, inner=outer-.075f, half=s[2]*.5f;
	if (inner < outer*.65f) inner=outer*.65f;
	if (meshCount + 336 > (int)(sizeof(meshData)/sizeof(meshData[0]))) return;
	for (int i=0;i<8;i++) {
		float a0=6.2831853f*i/8.0f, a1=6.2831853f*(i+1)/8.0f;
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
static void addFanBlade(float cx, float cy, float cz, const float s[3], float spin)
{
	float l=s[0], w=s[1], h=s[2]*.5f;
	const float q[6][2]={{-.06f*l,-.14f*w},{.18f*l,-.48f*w},{.78f*l,-.42f*w},{1.00f*l,-.08f*w},{.72f*l,.46f*w},{.16f*l,.34f*w}};
	float c=cosf(spin), sn=sinf(spin);
	float p[6][2];
	if(meshCount+72>(int)(sizeof(meshData)/sizeof(meshData[0]))) return;
	for(int i=0;i<6;i++){p[i][0]=q[i][0]*c-q[i][1]*sn; p[i][1]=q[i][0]*sn+q[i][1]*c;}
	for(int i=1;i<5;i++) {
		vertex f[6]={
			{{cx+p[0][0],cy+p[0][1],cz-h},{0,0,-1}},{{cx+p[i+1][0],cy+p[i+1][1],cz-h},{0,0,-1}},{{cx+p[i][0],cy+p[i][1],cz-h},{0,0,-1}},
			{{cx+p[0][0],cy+p[0][1],cz+h},{0,0,1}},{{cx+p[i][0],cy+p[i][1],cz+h},{0,0,1}},{{cx+p[i+1][0],cy+p[i+1][1],cz+h},{0,0,1}}
		};
		memcpy(&meshData[meshCount],f,sizeof(f)); meshCount+=6;
	}
	for(int i=0;i<6;i++) {
		int j=(i+1)%6; float ex=p[j][0]-p[i][0], ey=p[j][1]-p[i][1];
		float n=sqrtf(ex*ex+ey*ey); float nx=ey/n, ny=-ex/n;
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
	if (meshCount + 120 > (int)(sizeof(meshData)/sizeof(meshData[0]))) return;
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
		float len=sqrtf(ex*ex+ey*ey); float nx=ey/len, ny=-ex/len;
		vertex edge[6] = {
			{{cx+p[i][0],cy+p[i][1],cz-half},{nx,ny,0}}, {{cx+p[n][0],cy+p[n][1],cz-half},{nx,ny,0}}, {{cx+p[n][0],cy+p[n][1],cz+half},{nx,ny,0}},
			{{cx+p[n][0],cy+p[n][1],cz+half},{nx,ny,0}}, {{cx+p[i][0],cy+p[i][1],cz+half},{nx,ny,0}}, {{cx+p[i][0],cy+p[i][1],cz-half},{nx,ny,0}}
		};
		memcpy(&meshData[meshCount], edge, sizeof(edge)); meshCount += 6;
	}
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
	float t[PART_SPEC_COUNT][3];
	for (int i=0; i<PART_SPEC_COUNT; i++) {
		const kitPartDef* p=&def->parts[i];
		for (int a=0;a<3;a++) t[i][a]=p->target[a];
		if (!p->name || p->size[0] <= 0.0f || p->size[1] <= 0.0f || p->size[2] <= 0.0f || p->runner >= def->runners ||
			(i == 0 ? p->parent != -1 : (p->parent < 0 || p->parent >= i))) ok=false;
	}
	for (int r=0; r<def->runners; r++) {
		int count=0; for (int i=0;i<PART_SPEC_COUNT;i++) if (def->parts[i].runner==r) count++;
		if (!count) { printf("RUNNER VALIDATION FAILED L%d R%d: no parts\n",level,r); ok=false; }
	}
	// Mirror the live resolver, then assert every authored child touches parent.
	for (int i=0;i<PART_SPEC_COUNT;i++) if (def->parts[i].parent >= 0 && def->parts[i].parent < i) {
		int q=def->parts[i].parent;
		int deepAxes=0; bool childOK=true;
		for (int a=0;a<3;a++) { float lim=(def->parts[i].size[a]+def->parts[q].size[a])*.5f-.02f;
			float need=fminf(.08f,fminf(def->parts[i].size[a],def->parts[q].size[a])*.35f);
			lim=(def->parts[i].size[a]+def->parts[q].size[a])*.5f-need;
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
		float bw=b->sx*runnerDisplayScale[i][0], bh=b->sy*runnerDisplayScale[i][1];
		// Bodies stay inside 3.60 x 2.08 inner runner envelope.
		if (b->cx-bw*.5f < -1.68f || b->cx+bw*.5f > 1.68f || b->cy-bh*.5f < -.90f || b->cy+bh*.5f > .90f) {
			printf("PACK VALIDATION FAILED L%d %s R%d: outside rails\n",level,def->parts[i].name,def->parts[i].runner); ok=false;
		}
		// Gate chain is continuous: stub/nub meet and nub meets the body's AABB.
		if (fabsf(s->cx-n->cx)>.001f || fabsf(s->cz-n->cz)>.001f || fabsf(s->cy-n->cy)>.10f ||
			fabsf(n->cx-b->cx)>bw*.5f+.05f || fabsf(n->cy-b->cy)>bh*.5f+.05f) {
			printf("GATE VALIDATION FAILED L%d %s R%d\n",level,def->parts[i].name,def->parts[i].runner); ok=false;
		}
		for(int j=0;j<i;j++) if(def->parts[j].runner==def->parts[i].runner) {
			const boxSpec* q=&activeSpecs[j].body;
			if (fabsf(b->cx-q->cx) < (bw+q->sx*runnerDisplayScale[j][0])*.5f-.015f && fabsf(b->cy-q->cy) < (bh+q->sy*runnerDisplayScale[j][1])*.5f-.015f)
				{ printf("PACK OVERLAP L%d R%d %s/%s\n",level,def->parts[i].runner,def->parts[i].name,def->parts[j].name); ok=false; }
		}
	}
	return ok;
}

bool meshValidateKits(void)
{
	bool ok=true;
	for (int level=1; level<=20; level++) {
		const kitDef* d = level <= 8 ? &kitDefs[level-1] : (level <= 14 ? &kitDefsMid[level-9] : &kitDefsLate[level-15]);
		ok &= validateKitDef(d, level);
	}
	printf(ok ? "KIT VALIDATION: 20 DEFINITIONS OK\n" : "KIT VALIDATION: ERROR\n");
	return ok;
}

bool meshAuditAllKits(void)
{
	bool ok=true; int passed=0;
	for (int level=1; level<=20; level++) {
		const kitDef* def = level <= 8 ? &kitDefs[level-1] : (level <= 14 ? &kitDefsMid[level-9] : &kitDefsLate[level-15]);
		meshBuildKit(level);
		bool row = validateKitDef(def,level) && partCount == PART_SPEC_COUNT && meshCount > 0 &&
			meshCount <= (int)(sizeof(meshData)/sizeof(meshData[0])) && runnerBaseCount > 0 && activePackedValid;
		for (int i=0;i<partCount;i++) {
			if (parts[i].firstVertex < 0 || parts[i].vertexCount <= 0 || parts[i].firstVertex+parts[i].vertexCount > meshCount ||
				stubCount[i] <= 0 || stubFirst[i]+stubCount[i] > meshCount) row=false;
		}
		if(!row) printf("GEOMETRY AUDIT L%02d FAIL parts=%d runners=%d verts=%d\n",level,partCount,activeRunners,meshCount);
		if(row) passed++; else ok=false;
	}
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

bool meshCollisionAuditAllKits(void)
{
	bool all=true;
	for (int level=1; level<=20; level++) {
		meshBuildKit(level);
		int looseHits=0, assembledHits=0;
		// This mirrors the normal deterministic runner-order cut flow.  The
		// live cut path uses these same 4x3 slots and the same full-size bounds.
		for (int i=0;i<partCount;i++) {
			float amin[3],amax[3];
			int slot=i%12, col=slot%4, row=slot/4;
			float cx=meshLooseX()+(-.72f+.48f*col);
			float cz=meshLooseZ()+(-.42f+.50f*row);
			float off[3]={cx-(parts[i].min[0]+parts[i].max[0])*.5f, MAT_TOP-parts[i].min[1], cz-(parts[i].min[2]+parts[i].max[2])*.5f};
			for(int a=0;a<3;a++){amin[a]=parts[i].min[a]+off[a];amax[a]=parts[i].max[a]+off[a];}
			for(int j=0;j<i;j++) {
				float bmin[3],bmax[3]; int bs=j%12, bc=bs%4, br=bs/4;
				float bx=meshLooseX()+(-.72f+.48f*bc), bz=meshLooseZ()+(-.42f+.50f*br);
				float boff[3]={bx-(parts[j].min[0]+parts[j].max[0])*.5f, MAT_TOP-parts[j].min[1], bz-(parts[j].min[2]+parts[j].max[2])*.5f};
				for(int a=0;a<3;a++){bmin[a]=parts[j].min[a]+boff[a];bmax[a]=parts[j].max[a]+boff[a];}
				if(strictAabbOverlap(amin,amax,bmin,bmax)) looseHits++;
			}
		}
		for(int i=0;i<socketCount;i++) for(int j=0;j<i;j++)
			if(strictAabbOverlap(sockets[i].min,sockets[i].max,sockets[j].min,sockets[j].max)) assembledHits++;
		bool pass=looseHits==0 && assembledHits==0;
		// Kept deliberately short: the 3DS console is 50 columns wide and this
		// audit must leave all twenty cut/assembly counts readable on screen.
		printf("L%02d %-18.18s CUT=%02d ASM=%02d %s\n",level,activeKitName,looseHits,assembledHits,pass?"OK":"FAIL");
		if(!pass) all=false;
	}
	printf("COLLISION AUDIT %s\n",all?"20/20 OK":"FAIL");
	meshBuildKit(1);
	return all;
}

void meshBuildKit(int level)
{
	if (level < 1) level = 1;
	if (level > 20) level = 20;
	activeLevel = level;
	const kitDef* def = level <= 8 ? &kitDefs[level - 1] : (level <= 14 ? &kitDefsMid[level - 9] : &kitDefsLate[level - 15]);
	activeKitName = def->name;
	activeRunners = def->runners;
	for (int i = 0; i < PART_SPEC_COUNT; i++)
	{
		// Repack each authored body into a deliberate 5x2 runner-cell layout.
		// Legacy positions were for robot boxes and made modern recipe shapes merge.
		activeSpecs[i] = partSpecs[i];
		activeSpecs[i].name = def->parts[i].name;
		// Runner display bodies are deliberately compact; full-scale silhouette is
		// restored at the assembly sockets, but no two sprue cells may overlap.
		float fullW=def->parts[i].size[0], fullH=def->parts[i].size[1], fullD=def->parts[i].size[2];
		float w=fminf(fullW,.58f), h=fminf(fullH,.55f), d=fminf(fullD,.22f);
		int ordinal=0; for (int j=0;j<i;j++) if (def->parts[j].runner==def->parts[i].runner) ordinal++;
		int col=ordinal%5, row=ordinal/5;
		float x=-1.36f + .68f*col;
		// Upper row hangs from the top rail; lower row rises from the bottom rail.
		float y = row==0 ? .74f-h*.5f : -.74f+h*.5f;
		activeSpecs[i].body.cx=x; activeSpecs[i].body.cy=y; activeSpecs[i].body.cz=0.0f;
		// Preserve the authored body for loose/assembled geometry.  w/h/d are
		// only runner-display extents, applied at draw time about bodyCentre.
		activeSpecs[i].body.sx=fullW; activeSpecs[i].body.sy=fullH; activeSpecs[i].body.sz=fullD;
		runnerDisplayScale[i][0]=w/fullW; runnerDisplayScale[i][1]=h/fullH; runnerDisplayScale[i][2]=d/fullD;
		activeSpecs[i].stub.cx=x; activeSpecs[i].nub.cx=x;
		activeSpecs[i].stub.cz=activeSpecs[i].nub.cz=0.0f;
		if (row==0) {
			activeSpecs[i].stub.cy=.875f; activeSpecs[i].nub.cy=.785f;
		} else {
			activeSpecs[i].stub.cy=-.875f; activeSpecs[i].nub.cy=-.785f;
		}
		activeTarget[i][0] = STAND_X + def->parts[i].target[0]; activeTarget[i][1] = def->parts[i].target[1]; activeTarget[i][2] = STAND_Z + ASSEMBLY_FRONT_Z + def->parts[i].target[2]; activeParent[i]=def->parts[i].parent;
	}
	activePackedValid = validatePackedRunner(def, level);
	if (!activePackedValid) printf("PACK VALIDATION ERROR L%d\n",level);
	resolveAssemblyTargets(def);
	meshCount = 0;
	partCount = 0;

	// Bedroom shell: exact room bounds and six non-overlapping back-wall pieces.
	roomFloorFirst=meshCount; addBox(0,-4.12f,0,15,.12f,13); roomFloorCount=meshCount-roomFloorFirst;
	roomFirst=meshCount;
	// Door [-5.60,-3.60] x [-4.06,.80], window [.60,3.90] x [-1.80,1.20].
	addBox(-6.55f,-.87f,-6.42f,1.90f,6.38f,.16f); // full height left of door
	addBox(-1.50f,-.87f,-6.42f,4.20f,6.38f,.16f); // between door/window: [-3.60,.60]
	addBox( 5.70f,-.87f,-6.42f,3.60f,6.38f,.16f); // right of window: [3.90,7.50]
	addBox(-4.60f,1.56f,-6.42f,2.00f,1.52f,.16f); // header above door
	addBox( 2.25f,-2.93f,-6.42f,3.30f,2.26f,.16f); // wall below window
	addBox( 2.25f,1.76f,-6.42f,3.30f,1.12f,.16f); // wall above window
	addBox(-7.42f,-.87f,0,.16f,6.38f,13); addBox(7.42f,-.87f,0,.16f,6.38f,13);
	// The front wall has no opening: a single joined panel closes the former
	// dark seam between the bed and desk.  Door/window remain on the rear wall.
	addBox(0,-.87f,6.42f,15.0f,6.38f,.16f);
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

	// The bench: a wooden desk with a cutting mat laid on it. Everything is a
	// plain slab - at 320x240 the silhouette and the colour do all the work, and
	// wood grain would not survive the resolution.
	//
	// Top, apron and legs share one range because they share one material, so
	// the whole desk still costs a single draw.
	deskFirst = meshCount;
	addBox(BENCH_X, DESK_Y, BENCH_Z, 7.40f, DESK_T, 4.80f);

	addBox(BENCH_X, APRON_Y, BENCH_Z+2.24f, 7.00f, APRON_T, 0.14f);
	addBox(BENCH_X, APRON_Y, BENCH_Z-2.24f, 7.00f, APRON_T, 0.14f);
	addBox(BENCH_X+3.46f, APRON_Y, BENCH_Z, 0.14f, APRON_T, 4.48f);
	addBox(BENCH_X-3.46f, APRON_Y, BENCH_Z, 0.14f, APRON_T, 4.48f);

	addBox(BENCH_X-3.46f, LEG_Y, BENCH_Z-2.24f, LEG_W, LEG_H, LEG_W);
	addBox(BENCH_X+3.46f, LEG_Y, BENCH_Z-2.24f, LEG_W, LEG_H, LEG_W);
	addBox(BENCH_X-3.46f, LEG_Y, BENCH_Z+2.24f, LEG_W, LEG_H, LEG_W);
	addBox(BENCH_X+3.46f, LEG_Y, BENCH_Z+2.24f, LEG_W, LEG_H, LEG_W);
	deskCount = meshCount - deskFirst;

	matFirst = meshCount;
	addBox(3.12f, MAT_Y, 3.69f, 4.55f, MAT_T, 3.00f);
	matCount = meshCount - matFirst;

	// The mat's printed grid, deliberately coarse. A fine grid turns to mush on
	// a 320-pixel screen; squares this size still read as squares.
	gridFirst = meshCount;
	{
		static const float lineX[] = { -1.6f, -0.8f, 0.0f, 0.8f, 1.6f };
		static const float lineZ[] = { -0.70f, 0.10f, 0.90f };

		for (int i = 0; i < (int)(sizeof(lineX)/sizeof(lineX[0])); i++)
		addBox(3.12f+lineX[i], GRID_Y, 3.69f, GRID_W, GRID_T, 2.96f);

		for (int i = 0; i < (int)(sizeof(lineZ)/sizeof(lineZ[0])); i++)
		addBox(3.12f, GRID_Y, 3.69f+lineZ[i]-0.10f, 4.51f, GRID_T, GRID_W);
	}
	gridCount = meshCount - gridFirst;

	// The stand the kit is built on. Two boxes: a plinth resting on the mat and
	// a post standing in it, which the finished parts hang off.
	standFirst = meshCount;
	addBox(STAND_X, MAT_TOP + PLINTH_T*0.5f,          STAND_Z, PLINTH_W, PLINTH_T, PLINTH_D);
	addBox(STAND_X, MAT_TOP + PLINTH_T + POST_H*0.5f, STAND_Z, POST_W,   POST_H,   POST_D);
	standCount = meshCount - standFirst;

	// Flat shoebox: shallow base, side walls and a separate lid resting on top.
	float boxW, boxD, boxH;
	kitBoxOuterSize(&boxW, &boxD, &boxH);
	kitTrayFirst = meshCount;
	addBox(KIT_BOX_X, DESK_Y + DESK_T + KIT_BOX_T*0.5f, KIT_BOX_Z, boxW, KIT_BOX_T, boxD); // base
	addBox(KIT_BOX_X - boxW*0.5f + KIT_BOX_T*0.5f, KIT_BOX_Y, KIT_BOX_Z, KIT_BOX_T, boxH, boxD); // left
	addBox(KIT_BOX_X + boxW*0.5f - KIT_BOX_T*0.5f, KIT_BOX_Y, KIT_BOX_Z, KIT_BOX_T, boxH, boxD); // right
	addBox(KIT_BOX_X, KIT_BOX_Y, KIT_BOX_Z - boxD*0.5f + KIT_BOX_T*0.5f, boxW, boxH, KIT_BOX_T); // back
	addBox(KIT_BOX_X, KIT_BOX_Y, KIT_BOX_Z + boxD*0.5f - KIT_BOX_T*0.5f, boxW, boxH, KIT_BOX_T); // front
	kitTrayCount = meshCount - kitTrayFirst;

	kitLidFirst = meshCount;
	addBox(KIT_BOX_X, KIT_BOX_Y + boxH*0.5f + KIT_BOX_T*0.5f, KIT_BOX_Z, boxW + KIT_BOX_T, KIT_BOX_T, boxD + KIT_BOX_T); // lid
	kitLidCount = meshCount - kitLidFirst;
	// A raised, centred print panel on the lid: simple artwork that remains crisp
	// at 3DS resolution, rather than a texture the game cannot afford.
	kitArtFirst = meshCount;
	addBox(KIT_BOX_X, KIT_BOX_Y + KIT_BOX_H + KIT_BOX_T + 0.012f, KIT_BOX_Z - 0.10f, 1.65f, 0.025f, 0.82f);
	kitArtCount = meshCount - kitArtFirst;
	kitSpareFirst = meshCount;
	// A fourth runner starts a separate closed side box. Main boxes never exceed
	// three runners; the spare sits on the widened left desk wing, not the mat.
	if (activeRunners > 3)
	{
		addBox(SPARE_BOX_X, DESK_Y + DESK_T + 0.20f, SPARE_BOX_Z, SPARE_BOX_W - 0.13f, 0.40f, SPARE_BOX_D - 0.12f);
		addBox(SPARE_BOX_X, DESK_Y + DESK_T + 0.43f, SPARE_BOX_Z, SPARE_BOX_W, 0.10f, SPARE_BOX_D);
	}
	kitSpareCount = meshCount - kitSpareFirst;

	// The runner: frame first, then the runner-side half of every gate. All of
	// it is drawn as one range and none of it ever moves.
	//
	// A real sprue is not a hollow rectangle with the parts floating loose in
	// the middle of it - that is the giveaway on a counterfeit one. It is a thin
	// outer frame braced by inner bars, and the parts hang off whichever bar is
	// nearest. So: four rails, one cross-bar splitting the frame into an upper
	// and a lower lane, and one short upright bracing the lower lane where the
	// span is otherwise widest. The cross-bar is thinner than the outer frame,
	// the way the moulded ones are.
	//
	// Bar faces line up with the gate stubs exactly - every stub is 0.09 tall
	// and reaches 0.045 from its own centre to the face it grew out of:
	//   top rail    y  0.92..1.04   underside  0.92  <- upper lane hangs down
	//   cross-bar   y -0.15..-0.05  both faces       <- upper stands up on it,
	//                                                   lower hangs down off it
	//   bottom rail y -1.04..-0.92  top face  -0.92  <- lower lane stands up
	// Outer extent is 3.60 by 2.08. The side rails run the full height so the
	// corners are closed, and the inner bars run 0.06 into them at each end.
	runnerFirst = meshCount;
	addBox( 0.00f,  0.98f, 0.0f, 3.60f, 0.12f, 0.12f); // top rail
	addBox( 0.00f, -0.98f, 0.0f, 3.60f, 0.12f, 0.12f); // bottom rail
	addBox(-1.74f,  0.00f, 0.0f, 0.12f, 2.08f, 0.12f); // left rail
	addBox( 1.74f,  0.00f, 0.0f, 0.12f, 2.08f, 0.12f); // right rail
	addBox( 0.00f, -0.10f, 0.0f, 3.48f, 0.10f, 0.12f); // cross-bar
	addBox( 0.72f, -0.54f, 0.0f, 0.10f, 0.88f, 0.12f); // lower-lane brace

	runnerBaseCount = meshCount - runnerFirst;
	for (int i = 0; i < PART_SPEC_COUNT; i++) {
		stubFirst[i] = meshCount;
		addBoxSpec(&activeSpecs[i].stub);
		stubCount[i] = meshCount - stubFirst[i];
	}

	runnerCount = meshCount - runnerFirst;

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
		else if (shape == SH_STAR) addStar5(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize);
		else if (shape == SH_GRILL) addFanGrill(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize);
		else if (shape == SH_FAN_BLADE) addFanBlade(spec->body.cx,spec->body.cy,spec->body.cz,shapeSize,def->parts[i].spin);
		else addBoxSpec(&spec->body);
		p->vertexCount = meshCount - p->firstVertex;

		p->bodyCentre[0] = spec->body.cx;
		p->bodyCentre[1] = spec->body.cy;
		p->bodyCentre[2] = spec->body.cz;
		p->runnerScale[0]=runnerDisplayScale[i][0];
		p->runnerScale[1]=runnerDisplayScale[i][1];
		p->runnerScale[2]=runnerDisplayScale[i][2];
		p->nubFullOffset[0]=p->nubFullOffset[2]=0.0f;
		p->nubFullOffset[1]=(spec->nub.cy>spec->body.cy ? 1.0f : -1.0f) * (spec->body.sy*(1.0f-p->runnerScale[1])*.5f);

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
		setBounds(p->min, p->max, &spec->body);
		p->min[0]=fminf(p->min[0],spec->nub.cx-spec->nub.sx*.5f); p->max[0]=fmaxf(p->max[0],spec->nub.cx+spec->nub.sx*.5f);
		p->min[1]=fminf(p->min[1],spec->nub.cy+p->nubFullOffset[1]-spec->nub.sy*.5f); p->max[1]=fmaxf(p->max[1],spec->nub.cy+p->nubFullOffset[1]+spec->nub.sy*.5f);

		// While still on the runner the body is drawn scaled about bodyCentre and
		// the nub is drawn where it was authored, with no nubFullOffset - see the
		// body/nub transforms in sceneRender.  Mirroring that here keeps the tap
		// box the size of the thing on screen; a full-size box on a part squeezed
		// into a 0.68 runner cell reaches into its neighbours' cells.
		const float bodySize[3] = { spec->body.sx, spec->body.sy, spec->body.sz };
		for (int a = 0; a < 3; a++)
		{
			float half = bodySize[a] * p->runnerScale[a] * .5f;
			p->runnerMin[a] = p->bodyCentre[a] - half;
			p->runnerMax[a] = p->bodyCentre[a] + half;
		}
		p->runnerMin[0]=fminf(p->runnerMin[0],spec->nub.cx-spec->nub.sx*.5f); p->runnerMax[0]=fmaxf(p->runnerMax[0],spec->nub.cx+spec->nub.sx*.5f);
		p->runnerMin[1]=fminf(p->runnerMin[1],spec->nub.cy-spec->nub.sy*.5f); p->runnerMax[1]=fmaxf(p->runnerMax[1],spec->nub.cy+spec->nub.sy*.5f);
		p->runnerMin[2]=fminf(p->runnerMin[2],spec->nub.cz-spec->nub.sz*.5f); p->runnerMax[2]=fmaxf(p->runnerMax[2],spec->nub.cz+spec->nub.sz*.5f);

		// Snipping tests the runner-side stub only, so a tap on the gate cuts
		// rather than selects.
		setBounds(p->gateMin, p->gateMax, &spec->stub);
	}

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

const vertex* meshVertices(void) { return meshData; }
int meshVertexCount(void)        { return meshCount; }
const meshPart* meshParts(void)  { return parts; }
int meshPartCount(void)          { return partCount; }

// Reported against meshBoxCapacity, so both have to be in the same unit: whole
// boxes worth of the vertex buffer. Round up, because the round shapes cost a
// fraction of a box each (a cylinder is 96 vertices, a star 120) and flooring
// would report headroom that is not there on the run that overflows.
int meshBoxCount(void)    { return (meshCount + VERTS_PER_BOX - 1) / VERTS_PER_BOX; }
int meshBoxCapacity(void) { return MAX_BOXES; }

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
int meshKitSpareFirstVertex(void) { return kitSpareFirst; }
int meshKitSpareVertexCount(void) { return kitSpareCount; }

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

void meshKitBoxBounds(float min[3], float max[3])
{
	// Same widening the box is built with, or the extra 0.21 a side on a
	// multi-runner kit would be drawn but not tappable.
	float boxW, boxD, boxH;
	kitBoxOuterSize(&boxW, &boxD, &boxH);
	min[0] = KIT_BOX_X - (boxW + KIT_BOX_T)*0.5f;
	min[1] = DESK_Y + DESK_T;
	min[2] = KIT_BOX_Z - (boxD + KIT_BOX_T)*0.5f;
	max[0] = KIT_BOX_X + (boxW + KIT_BOX_T)*0.5f;
	max[1] = KIT_BOX_Y + boxH*0.5f + KIT_BOX_T;
	max[2] = KIT_BOX_Z + (boxD + KIT_BOX_T)*0.5f;
}

void meshKitBoxedOffset(float out[3])
{
	out[0] = BENCH_X;
	// Final flat-runner landing: the same rear-desk position the shoebox used,
	// resting just above the desk surface rather than over the cutting mat.
	out[1] = -1.45f;
	out[2] = KIT_BOX_Z;
}
