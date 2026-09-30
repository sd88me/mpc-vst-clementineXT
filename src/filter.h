/* Filter 1 and Filter 2. State-variable filters (TPT form, which is the bilinear transform prewarped at the pole frequency);
 * the cutoff and resonance laws are tables fitted to the firmware's measured magnitude responses (docs/CALIBRATION.md).
 *   12 dB LP  = one 2nd-order section, Q from resonance
 *   24 dB LP  = a critically damped section (Q 0.5) followed by the resonant section
 * The other Filter 1 types are approximations built from the same sections (see filter.c for what is and isn't calibrated). */
#pragma once

#define F1_TYPES 13

typedef struct { float ic1, ic2; } svf_t;

typedef struct {
    svf_t a, b, c;            /* up to three sections in series/parallel, depending on the type */
    float shold;              /* sample-and-hold value and phase (type 9) */
    float sphase;
    float f2;                 /* filter 2 one-pole state */
} filt_t;

/* Pole frequency table lookups: `cutoff` is the 0..127 cutoff value plus modulation in the same units (may be fractional). */
float filt_pole_g(float cutoff);          /* TPT coefficient g = tan(pi*fp/fs) at 40 kHz */
float filt_damping(float reso);           /* k = 1/Q for the resonance value 0..127 (fractional allowed) */

/* One Filter 1 sample. type 0..12, cutoff/reso as above, special = the extra parameter (0..127). */
float filter1_run(filt_t *f, int type, float x, float cutoff, float reso, int special);

/* One Filter 2 sample: 6 dB low-pass (hp = 0) or high-pass (hp = 1). */
float filter2_run(filt_t *f, int hp, float x, float cutoff);
