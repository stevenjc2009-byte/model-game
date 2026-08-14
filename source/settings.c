#include "settings.h"

static int masterVolume = 100;

int settingsVolume(void)
{
	return masterVolume;
}

void settingsVolumeStep(int delta)
{
	masterVolume += delta;
	if (masterVolume < 0)   masterVolume = 0;
	if (masterVolume > 100) masterVolume = 100;
}
