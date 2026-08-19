// Camera positioning: where the bench camera sits, how it eases between the
// game's framings (workbench, photo mode, runner, assembly), and the two
// transforms - buildModelView and buildRunnerView - everything downstream is
// built from, rendering and picking alike.
//
// The orbit/zoom/pan state below is exposed as plain extern globals rather
// than behind accessor functions: main.c's frame loop reads and writes
// angleX/angleY/camDist/camPanX/camPanZ directly, several times a frame, as
// part of stylus and Circle Pad handling. Wrapping every one of those in a
// setter would just move the same read-modify-write out to a function call
// with no change in behaviour, so this item (43 - main.c split) left them as
// they were and only moved the functions.

#pragma once

#include <3ds.h>
#include <citro3d.h>
#include <stdbool.h>

// Player zoom/orbit limits. CAM_NEAR_LIMIT and CAM_FAR_LIMIT bound the
// shoulder-button zoom in main.c's input handling; PITCH_LIMIT and PITCH_FLOOR
// bound the Circle Pad tilt there too, through clampCameraPitch below.
// CAM_NEAR_FOCUS is how close the near limit is allowed to come in once a part
// has been locked onto. CAM_START is the very first camDist, before
// frameWorkbenchCamera has ever run. CAM_BLOCK_SKIN only matters inside this
// file's own furniture-blocking search.
#define CAM_NEAR_LIMIT 1.8f
#define CAM_FAR_LIMIT  6.5f
#define CAM_START      4.25f
#define PITCH_LIMIT    1.45f
#define PITCH_FLOOR    0.0f
#define CAM_BLOCK_SKIN 0.05f
#define CAM_NEAR_FOCUS 1.0f

// How many framings photo mode cycles through - main.c wraps photoShot
// against this on every X press.
#define PHOTO_SHOT_COUNT 4

// Orbit pitch/yaw, distance, and what the camera is orbiting. See camera.c
// for what each field means in detail; the comments are unchanged from when
// this state lived in main.c.
extern float angleX, angleY;
extern float camDist;
// An automatic lean-in on top of camDist, in world units, while a small part
// is being filed. Written by updateFocus in main.c, spent by this file. Kept
// apart from camDist so that letting go of the work restores the player's own
// distance exactly rather than approximately. Zero at every framing.
extern float camWorkZoom;
extern float focus[3];
extern float focusAmt;
extern float camPanX, camPanZ;
extern bool  photoMode;
extern int   photoShot;

void frameWorkbenchCamera(void);
void framePhotoCamera(void);
void frameRunnerCamera(void);
void frameAssemblyCamera(void);

// Hold the orbit pitch between PITCH_FLOOR and PITCH_LIMIT. Lives here rather
// than inline in main.c's input handling so the pan audit can drive the same
// two lines the Circle Pad drives, instead of a second copy free to disagree
// with them.
void clampCameraPitch(void);

// The pivot and blend amount frameWorkbenchCamera opens on, and updateFocus
// (in main.c) eases back to whenever nothing more specific has claimed the
// camera. Exposed because both call it.
void benchPivot(float out[3], float* amt);

// Re-reads the furniture the camera is not allowed to pass through. Called
// whenever the blocker list can have changed shape - a new kit's box stack -
// never during play.
void refreshBlockers(void);

// How far back the camera is actually standing right now - camDist less any
// automatic lean-in, floored the same way the view matrix floors it. The
// number to check when the question is where the camera ended up rather than
// where the player asked it to be.
float cameraStandDistance(void);

void buildModelView(C3D_Mtx* out);
void buildRunnerView(C3D_Mtx* out, const C3D_Mtx* modelView);
void cameraWorldPosition(const C3D_Mtx* modelView, float out[3]);
