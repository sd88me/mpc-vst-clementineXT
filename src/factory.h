/* The factory sounds, read from the user's own ROM image (never shipped). The 256 factory sounds sit in the image as bit-packed
 * records; this decodes them into SDATA blocks. Format notes: docs/DESIGN.md section 10. */
#pragma once
#include "patch.h"
#define FACTORY_IMAGE_SIZE 262144
/* img: the 256 KB image (chip A on even bytes, chip B on odd bytes). Fills out[256] (bank A 0-127, bank B 128-255);
 * returns 1 if the image holds a factory sound set (every sound name is printable), else 0 and out is untouched. */
int factory_decode(const uint8_t *img, patch_t out[256]);
