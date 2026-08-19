#pragma once

#include "debug.h"

// Copies everything an audit prints to a file on the SD card, so the result can
// be read by a machine instead of by a person squinting at a screenshot.
//
// The audits already say exactly what they mean - "PAINT AUDIT PASS",
// "COLLISION AUDIT 20/20 OK". The problem was never the wording, it was that the
// words only ever existed in the top screen's framebuffer: to find out whether a
// check passed, somebody had to hand-build a binary, run it, screenshot it and
// read it. That is why twenty of these went unrun for weeks at a time.
//
// Teeing rather than redirecting: the console output still appears on screen
// exactly as before, so every existing way of looking at an audit still works.
// This only adds a second copy.
//
// Compiled to nothing unless a TEST_*_AUDIT flag is on - see TEST_ANY_AUDIT in
// debug.h. The shipped game never opens this file and never carries the code.

// Which flag this build was made for, written into the log's first line. The
// harness knows what it asked for, so this is here for the human who finds a log
// on a card weeks later with no idea what produced it.
#if TEST_AUDIT_ALL_KITS
#define AUDIT_LOG_NAME "TEST_AUDIT_ALL_KITS"
#elif TEST_CAMERA_IDLE_AUDIT
#define AUDIT_LOG_NAME "TEST_CAMERA_IDLE_AUDIT"
#elif TEST_LEVEL1_WORKSPACE_AUDIT
#define AUDIT_LOG_NAME "TEST_LEVEL1_WORKSPACE_AUDIT"
#elif TEST_CAMERA_PAN_AUDIT
#define AUDIT_LOG_NAME "TEST_CAMERA_PAN_AUDIT"
#elif TEST_CEILING_AUDIT
#define AUDIT_LOG_NAME "TEST_CEILING_AUDIT"
#elif TEST_COLLISION_AUDIT
#define AUDIT_LOG_NAME "TEST_COLLISION_AUDIT"
#elif TEST_HINT_AUDIT
#define AUDIT_LOG_NAME "TEST_HINT_AUDIT"
#elif TEST_PAINT_AUDIT
#define AUDIT_LOG_NAME "TEST_PAINT_AUDIT"
#elif TEST_SAVELOAD_AUDIT
#define AUDIT_LOG_NAME "TEST_SAVELOAD_AUDIT"
#elif TEST_UNDO_AUDIT
#define AUDIT_LOG_NAME "TEST_UNDO_AUDIT"
#elif TEST_TOPSCREEN_AUDIT
#define AUDIT_LOG_NAME "TEST_TOPSCREEN_AUDIT"
#elif TEST_WORKZOOM_AUDIT
#define AUDIT_LOG_NAME "TEST_WORKZOOM_AUDIT"
#elif TEST_PICKPAD_AUDIT
#define AUDIT_LOG_NAME "TEST_PICKPAD_AUDIT"
#else
#define AUDIT_LOG_NAME "none"
#endif

#if TEST_ANY_AUDIT

// Opens sdmc:/3ds/modelkit/audit.log and starts copying stdout into it.
// name is written into the header line so a log can be identified out of
// context. Safe to call twice; the second call does nothing.
void auditLogBegin(const char* name);

// Writes the end marker and closes. The harness waits for that marker rather
// than for a timeout, so a run that died half way through is a failure it can
// see rather than an empty log it has to guess about.
void auditLogEnd(void);

#else
#define auditLogBegin(name) ((void)0)
#define auditLogEnd()       ((void)0)
#endif
