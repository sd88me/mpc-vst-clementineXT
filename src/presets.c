#include <string.h>
#include "presets.h"

typedef struct { const char *key; int v; } kv_t;
typedef struct { const char *name; const kv_t *kv; } preset_t;

/* Wave tables 0..3 are the open set (additive sweep, pulse width, sync saw, two-tone). Envelope times: 0 fast, 127 slow. */
static const kv_t p_sweep[] = { {"wavetable", 0}, {"w1_start", 0}, {"wenv_l1", 0}, {"fenv_a", 0}, {"f1_cutoff", 70}, {"f1_reso", 20}, {"f1_env", 90}, {"fenv_d", 60}, {"fenv_s", 30}, {"aenv_r", 50}, {"aenv_a", 4}, {"mix_w2", 0}, {0} };
static const kv_t p_pwm[] = { {"wavetable", 1}, {"w1_start", 10}, {"lfo1_rate", 40}, {"lfo1_shape", 1}, {"m1_src", 1}, {"m1_amt", 88}, {"m1_dst", 3}, {"f1_cutoff", 90}, {"mix_w2", 0}, {"osc2_detune", 0}, {"aenv_r", 40}, {0} };
static const kv_t p_sync[] = { {"wavetable", 2}, {"w1_start", 30}, {"w1_env", 70}, {"wenv_t1", 50}, {"f1_cutoff", 100}, {"f1_reso", 40}, {"aenv_d", 70}, {"aenv_s", 90}, {"aenv_r", 30}, {"mix_w2", 0}, {0} };
static const kv_t p_duo[] = { {"wavetable", 3}, {"w1_start", 25}, {"w2_start", 45}, {"mix_w1", 100}, {"mix_w2", 100}, {"osc2_detune", 70}, {"f1_cutoff", 85}, {"aenv_a", 20}, {"aenv_r", 70}, {"chorus", 1}, {0} };
static const kv_t p_bass[] = { {"wavetable", 2}, {"w1_start", 8}, {"osc1_oct", 2}, {"mix_w2", 0}, {"f1_type", 0}, {"f1_cutoff", 35}, {"f1_reso", 55}, {"f1_env", 100}, {"fenv_d", 40}, {"fenv_s", 0}, {"aenv_s", 100}, {"aenv_r", 15}, {"alloc", 1}, {0} };
static const kv_t p_pad[] = { {"wavetable", 0}, {"w1_start", 18}, {"w2_start", 34}, {"mix_w1", 100}, {"mix_w2", 100}, {"osc2_detune", 66}, {"aenv_a", 70}, {"aenv_r", 90}, {"f1_cutoff", 60}, {"f1_type", 1}, {"fx_type", 32}, {"fx_p1", 80}, {"fx_p2", 50}, {"fx_p3", 50}, {"assign", 1}, {"detune", 30}, {0} };
static const kv_t p_pluck[] = { {"wavetable", 0}, {"w1_start", 40}, {"w1_env", 60}, {"wenv_t1", 30}, {"f1_cutoff", 55}, {"f1_env", 80}, {"fenv_d", 35}, {"fenv_s", 0}, {"aenv_d", 45}, {"aenv_s", 0}, {"aenv_r", 35}, {"mix_w2", 0}, {0} };
static const kv_t p_lead[] = { {"wavetable", 2}, {"w1_start", 20}, {"mix_w2", 0}, {"alloc", 1}, {"glide_on", 1}, {"glide_time", 40}, {"f1_cutoff", 95}, {"f1_reso", 30}, {"lfo1_rate", 70}, {"lfo1_shape", 0}, {"m1_src", 2}, {"m1_amt", 74}, {"m1_dst", 0}, {"aenv_r", 30}, {0} };
static const kv_t p_uni[] = { {"wavetable", 2}, {"w1_start", 35}, {"mix_w2", 0}, {"assign", 2}, {"detune", 50}, {"depan", 90}, {"f1_cutoff", 110}, {"aenv_r", 50}, {"fx_type", 1}, {"fx_p1", 30}, {"fx_p2", 60}, {"fx_p3", 50}, {0} };
static const kv_t p_ring[] = { {"wavetable", 3}, {"w1_start", 10}, {"w2_start", 50}, {"osc2_semi", 7}, {"mix_w1", 60}, {"mix_w2", 60}, {"mix_ring", 90}, {"f1_cutoff", 100}, {"aenv_d", 60}, {"aenv_s", 60}, {"aenv_r", 40}, {0} };
static const kv_t p_noise[] = { {"wavetable", 0}, {"w1_start", 5}, {"mix_w1", 40}, {"mix_w2", 0}, {"mix_noise", 70}, {"f1_type", 3}, {"f1_cutoff", 60}, {"f1_reso", 60}, {"f1_env", 90}, {"fenv_d", 45}, {"fenv_s", 0}, {"aenv_d", 50}, {"aenv_s", 0}, {"aenv_r", 40}, {0} };
static const kv_t p_wobble[] = { {"wavetable", 1}, {"w1_start", 25}, {"mix_w2", 0}, {"osc1_oct", 2}, {"f1_cutoff", 50}, {"f1_reso", 70}, {"lfo1_rate", 62}, {"lfo1_shape", 1}, {"m1_src", 1}, {"m1_amt", 100}, {"m1_dst", 9}, {"alloc", 1}, {"aenv_s", 110}, {0} };

static const preset_t presets[PRESET_COUNT] = {
    { "Sweeper", p_sweep }, { "Pulse Motion", p_pwm }, { "Sync Stab", p_sync }, { "Twin Glass", p_duo }, { "Low Saw", p_bass }, { "Slow Air Pad", p_pad },
    { "Soft Pluck", p_pluck }, { "Glide Lead", p_lead }, { "Unison Wall", p_uni }, { "Ring Bell", p_ring }, { "Noise Hit", p_noise }, { "Wobble", p_wobble },
};

void presets_fill(patch_t *bank) {
    for (int i = 0; i < 256; i++) patch_init(&bank[i]);
    for (int i = 0; i < PRESET_COUNT; i++) {
        patch_t *p = &bank[i];
        for (const kv_t *e = presets[i].kv; e->key; e++) { int ix = patch_find(e->key); if (ix >= 0) p->d[ix] = (uint8_t)e->v; }
        patch_clamp(p);
        patch_set_name(p, presets[i].name);
    }
}
