#include "controls.h"

static const controlLine lines[] =
{
	{ "Stylus tap",       "snip / pick up / fit a part"  },
	{ "Stylus rub",       "file the selected part's nub" },
	{ "Stylus drag / Pad", "turn the bench"              },
	{ "D-Pad Left/Right", "page the manual"              },
	{ "L / R",            "zoom out / in"                },
	{ "A",                "reset the view"               },
	{ "SELECT",           "pause menu"                   },
	{ "START",            "close the game"               },
};

const controlLine* controlList(void)
{
	return lines;
}

int controlCount(void)
{
	return (int)(sizeof(lines) / sizeof(lines[0]));
}
