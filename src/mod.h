/* LFOs and the modulation-amount law of the XT (docs/CALIBRATION.md).
 * Measured: LFO rate in Hz = 0.02608 * 2^(rate/12) (0.026 Hz at 0, 1.05 Hz at 64, 40 Hz at 127); a matrix amount a (0..127 stored,
 * -64..+63 shown) scales its destination by 2 * sign * 2^((|a|-32)/4) native units for a full-scale source (pitch: semitones,
 * volume/mix/cutoff: 0..127 units); keytrack and keyfollow sources are (note - 64) / 128. */
#pragma once
#include <stdint.h>

typedef struct {
    float phase;         /* 0..1 */
    float held;          /* value of S&H / random segment start */
    float target;        /* random: value at the end of the segment */
    uint32_t rng;
    float delay_left;    /* seconds until the LFO starts after a retrigger */
    float rate_c, hz_c;  /* cached rate value and its frequency (exp2f is slow on the ARM devices) */
} lfo_t;

enum { LFO_SIN, LFO_TRI, LFO_SQR, LFO_SAW, LFO_RND, LFO_SH };

float lfo_rate_hz(float rate);                 /* rate value 0..127 (may be fractional) */
void lfo_reset(lfo_t *l, uint32_t seed, int free_phase, float delay_s);
/* Advance by one 40 kHz sample and return -1..+1. symmetry 0..127 (64 = centre), humanize 0..127. */
float lfo_tick(lfo_t *l, int shape, float rate, int symmetry, int humanize);
/* The same, advancing n samples at once (control-rate use). */
float lfo_tick_n(lfo_t *l, int shape, float rate, int symmetry, int humanize, int n);
float lfo_eval(const lfo_t *l, int shape, int symmetry);   /* the value at the current phase, without advancing */

/* Amount law: 0..127 stored -> multiplier (signed) of the destination's native units for a full-scale source. */
float mod_amount_gain(int amount);
