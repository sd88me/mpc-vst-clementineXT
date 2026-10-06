#include "factory.h"
#include <stdlib.h>
#include <string.h>

/* Sound n is a 188-byte record at FACTORY_AT + 188 n. Each byte is stored inverted. The record is a bit stream, MSB first: the
 * SDATA bytes in index order, each in the number of bits listed below. A width of 0 means the byte is not stored and is 0. */
#define FACTORY_AT 0x28000
#define FACTORY_REC 188
static const uint8_t widths[PATCH_SIZE] = {
    7, 7, 7, 7, 3, 7, 7, 7, 6, 7, 7, 0, 7, 7, 7, 3, 1, 7, 7, 1, 7, 6, 7, 7, 0, 7, 6, 7, 7, 7, 7, 1,
    6, 7, 7, 0, 6, 7, 7, 7, 7, 1, 1, 6, 7, 7, 0, 7, 7, 7, 7, 7, 7, 3, 3, 1, 7, 3, 7, 7, 7, 7, 7, 7,
    5, 7, 7, 7, 6, 7, 7, 7, 7, 7, 2, 7, 7, 7, 7, 7, 7, 7, 4, 7, 7, 7, 7, 1, 3, 3, 7, 7, 2, 7, 5, 4,
    5, 2, 2, 1, 1, 4, 4, 4, 4, 4, 7, 4, 1, 2, 7, 2, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7, 7, 3, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 1, 3, 3, 1, 3, 3, 7, 7, 7, 7, 7, 7, 7, 7, 7, 3, 7, 7,
    4, 7, 2, 7, 7, 7, 7, 4, 7, 2, 7, 7, 7, 7, 6, 7, 6, 6, 6, 7, 6, 6, 6, 7, 6, 6, 6, 7, 6, 6, 6, 7,
    6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7,
    6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 6, 7, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7
};

static void decode_one(const uint8_t *rec, patch_t *p) {
    unsigned pos = 0;
    for (int i = 0; i < PATCH_SIZE; i++) {
        unsigned v = 0;
        for (int b = 0; b < widths[i]; b++, pos++) v = v << 1 | ((~rec[pos >> 3] >> (7 - (pos & 7))) & 1u);
        p->d[i] = (uint8_t)v;
    }
}

int factory_decode(const uint8_t *img, patch_t out[256]) {
    patch_t *tmp = malloc(256 * sizeof *tmp);   /* decoded into a scratch first so a failed check leaves out alone */
    if (!tmp) return 0;
    for (int n = 0; n < 256; n++) {
        decode_one(img + FACTORY_AT + FACTORY_REC * n, &tmp[n]);
        for (int i = PATCH_NAME_AT; i < PATCH_NAME_AT + PATCH_NAME_LEN; i++)
            if (tmp[n].d[i] < 32 || tmp[n].d[i] > 126) { free(tmp); return 0; }
    }
    memcpy(out, tmp, 256 * sizeof *tmp);
    free(tmp);
    return 1;
}
