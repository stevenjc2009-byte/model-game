// The button list, in one place.
//
// Two screens print it: the top-screen console on the workbench, and the touch
// screen's Controls page inside Options. Held as data rather than as two blocks
// of printf, so the two can never drift apart.

#pragma once

typedef struct
{
	const char* key;     ///< What you press. Kept short enough for the touch screen's left column.
	const char* action;  ///< What it does.
} controlLine;

const controlLine* controlList(void);

// Eight, and the console counts on it: the static block it sits in fills rows 1
// to 13 and the live readout is parked at row 14, so a ninth line would print
// over the readout.
int controlCount(void);
