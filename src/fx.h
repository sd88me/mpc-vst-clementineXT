/* The XT's per-sound effect and chorus (manual "Effects"; SDATA 76, 81, 83, 86 and 82). Stereo in place at 40 kHz.
 * Type order (0-9) follows the manual's list; the firmware's real index order and types 10..35 are not known yet, so the
 * engine maps any unknown type to "off". All rates, depths and delay times below are placeholders until calibrated. */
#pragma once

#define FX_MAX_DELAY 65536   /* samples per channel: 1.6 s at 40 kHz */

typedef struct {
    float dl[2][FX_MAX_DELAY];   /* delay lines (chorus/flanger/delay share them) */
    int wr;
    float lfo;                   /* LFO phase 0..1 */
    float lfo_chorus;            /* the always-available chorus has its own ~0.5 Hz LFO */
    float fb[2];                 /* feedback / filter state */
    float env[2];                /* envelope follower (auto-wah) */
    float svf_ic1[2], svf_ic2[2];
    float od_lp[2], od_hp[2];    /* overdrive speaker filter state */
    float cdl[2][2048];          /* the chorus' own short delay lines */
    int cwr;
} fx_t;

enum { FX_CHORUS, FX_FLANGER1, FX_FLANGER2, FX_WAH_LP, FX_WAH_BP, FX_OVERDRIVE, FX_AMPMOD, FX_DELAY, FX_PANDELAY, FX_MODDELAY, FX_TYPES };

/* One stereo sample through the effect selected by `type` with its three parameters (0..127 each); `tempo_bpm` is used by the delays. */
void fx_run(fx_t *f, int type, int p1, int p2, int p3, float tempo_bpm, float *l, float *r);

/* The chorus switch on the Amplifier page: two short delays modulated by a sine of about 0.5 Hz, mixed with the dry signal. */
void chorus_run(fx_t *f, int mode, float *l, float *r);
