/* Loads a real bank when CLEMENTINE_TEST_BANK_DIR names a folder holding a .syx (local only; never committed). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "engine.h"
int main(void) {
    const char *dir = getenv("CLEMENTINE_TEST_BANK_DIR");
    if (!dir) { printf("skip (set CLEMENTINE_TEST_BANK_DIR)\n"); return 0; }
    const mpc_engine_t *e = mpc_engine(); void *h = e->create(dir); char b[600]; int fails = 0;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
    e->get_param(h, "patch_name", b, sizeof b); CHECK(b[0] && strcmp(b, "Init"), "bank loaded: program 0 has its own name, not the built-in Init");
    e->set_param(h, "program", "183"); e->get_param(h, "patch_name", b, sizeof b); CHECK(!strncmp(b, "Init Sound V1.1", 15), "program 183 = init sound");
    e->get_param(h, "patch_text", b, sizeof b); CHECK(!strncmp(b, "183  Init Sound V1.1", 20), "patch_text = program number, two spaces, name");
    e->get_param(h, "bank_text", b, sizeof b); CHECK(b[0] >= '0' && b[0] <= '9' && b[1] == ' ' && b[2] == ' ', "bank_text = bank number, two spaces, name");
    e->get_param(h, "lfo1_rate", b, sizeof b); CHECK(atoi(b) == 100, "init sound lfo1_rate 100");
    char st[600]; e->get_param(h, "state", st, sizeof st);
    void *h2 = e->create(NULL); e->set_param(h2, "state", st); e->get_param(h2, "patch_name", b, sizeof b);
    CHECK(!strncmp(b, "Init Sound V1.1", 15), "state round trip into a bank-less instance");
    uint8_t on[3] = {0x90, 60, 100}; int16_t out[256]; e->midi(h, on, 3); double r = 0;
    for (int k = 0; k < 20; k++) { e->render(h, out, 128); for (int i = 0; i < 256; i++) r += (double)out[i] * out[i]; }
    CHECK(r > 0, "audio renders");
    e->destroy(h); e->destroy(h2); printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
