/* 40 kHz core -> 44.1 kHz output. Ratio is 400:441, so the polyphase bank has 441 exact phases. */
#pragma once
#define RS_TAPS 96
#define RS_PHASES 441
#define RS_STEP 400
typedef enum { RS_CLEAN = 0, RS_VINTAGE = 1 } rs_mode_t;
typedef void (*rs_gen_t)(void *ctx, float *lr);   /* produce one 40 kHz stereo frame */
typedef struct {
    float bank[RS_PHASES][RS_TAPS];
    float hist[RS_TAPS * 2][2];   /* doubled ring so the taps are contiguous */
    int wr;
    int phase;                    /* n*400 mod 441 for the next output frame */
    int need;                     /* input frames still to pull before the next output */
} rs_t;
void rs_init(rs_t *r, rs_mode_t mode);
void rs_render(rs_t *r, rs_gen_t gen, void *ctx, float *out_lr, int frames);
