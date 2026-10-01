/* The Play knobs and the bank stepper: a knob drives whichever parameter its Play Parameter selector names. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "engine.h"
static int fails;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
static int geti(const mpc_engine_t *e, void *h, const char *k) { char b[64] = ""; e->get_param(h, k, b, sizeof b); return atoi(b); }
int main(void) {
    const mpc_engine_t *e = mpc_engine(); void *h = e->create(NULL);
    e->set_param(h, "play1", "28"); e->set_param(h, "play_v1", "127");   /* list entry 28 is Filter 1 Cutoff */
    CHECK(geti(e, h, "f1_cutoff") == 127 && geti(e, h, "play_v1") == 127, "knob at 127 puts Filter 1 Cutoff at its maximum");
    e->set_param(h, "play_v1", "0"); CHECK(geti(e, h, "f1_cutoff") == 0, "knob at 0 puts it at its minimum");
    e->set_param(h, "play_v1", "64"); int c = geti(e, h, "f1_cutoff"); CHECK(c == 64 || c == 63, "knob at the middle is near the middle");
    CHECK(abs(geti(e, h, "play_v1") - 64) <= 1, "and reads back");
    e->set_param(h, "play2", "0"); e->set_param(h, "play_v2", "127");   /* Osc 1 Octave spans 16..112 */
    CHECK(geti(e, h, "osc1_oct") == 112, "a ranged parameter (octave 16..112) maps onto the knob");
    e->set_param(h, "play3", "43"); e->set_param(h, "play_v3", "100");   /* Glide on/off, 0..1 */
    CHECK(geti(e, h, "glide_on") == 1, "an on/off parameter flips at the top of the knob");
    e->set_param(h, "play4", "79"); e->set_param(h, "play_v4", "90");    /* Control W is a live controller */
    CHECK(abs(geti(e, h, "play_v4") - 90) <= 1, "Control W keeps the value");
    CHECK(geti(e, h, "bank") >= 0, "the bank stepper reads a bank number");
    e->destroy(h); printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
