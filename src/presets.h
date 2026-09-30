/* Built-in sounds (original, made for the open wave set) used when no .syx bank is found next to the plugin. */
#pragma once
#include "patch.h"
#define PRESET_COUNT 12
void presets_fill(patch_t *bank /* [256] */);   /* slots 0..PRESET_COUNT-1 get the sounds, the rest an init sound */
