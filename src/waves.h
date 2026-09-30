/* XT wave layout: a wave is 128 signed 8-bit samples stored as 64 (w[64+n] = -w[63-n]); a table is 64 slots naming
 * waves; slots 61-63 are always triangle, square, saw. Mips: 128+64+32+...+1 = 255 (256 with padding) per wave. */
#pragma once
#include <stdint.h>
#define WAVE_HALF 64
#define WAVE_LEN 128
#define WAVE_MIPS 256
#define TABLE_SLOTS 64
#define TABLE_EMPTY (-1)

typedef struct { int8_t half[WAVE_HALF]; } wave_t;
typedef struct { int16_t slot[TABLE_SLOTS]; } table_ctl_t;          /* wave number per slot or TABLE_EMPTY */
typedef struct { int8_t mip[TABLE_SLOTS][WAVE_MIPS]; } table_t;     /* built table: 64 x 256 int8 */

void wave_expand(const wave_t *w, int8_t out[WAVE_LEN]);
void wave_pack(const int8_t in[WAVE_LEN], wave_t *w);              /* keeps the first half */
void wave_mips(const int8_t full[WAVE_LEN], int8_t mip[WAVE_MIPS]);
/* Fill empty slots by interpolating between the nearest filled neighbours. Placeholder (time-domain crossfade) until the
 * oracle shows what the firmware does; tables whose slots are all filled are unaffected. */
void table_build(const table_ctl_t *ctl, const wave_t *waves, int nwaves, table_t *out);

/* Open set: original tables, ours. Returns 0 on success. */
#define OPEN_TABLES 4
int open_table(int n, wave_t *waves /* [TABLE_SLOTS] */, table_ctl_t *ctl, const char **name);
