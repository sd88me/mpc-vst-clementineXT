/* Arpeggiator smoke test: held keys are stepped through, audio stops after release. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "engine.h"
int main(void) {
    const mpc_engine_t *e = mpc_engine(); void *h = e->create(NULL); int fails = 0;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
    e->set_param(h, "arp_on", "1"); e->set_param(h, "arp_tempo", "127"); e->set_param(h, "arp_clock", "5"); e->set_param(h, "arp_pattern", "0"); e->set_param(h, "aenv_r", "0");
    uint8_t k[3][3] = {{0x90, 60, 100}, {0x90, 64, 100}, {0x90, 67, 100}}; for (int i = 0; i < 3; i++) e->midi(h, k[i], 3);
    int16_t out[256]; double e0 = 0, gaps = 0; int blocks = 0, quiet = 0; double rb[400];
    for (int b = 0; b < 400; b++) { e->render(h, out, 128); double r = 0; for (int i = 0; i < 256; i++) r += (double)out[i] * out[i]; e0 += r; rb[blocks++] = r; }
    if (getenv("ARPDBG")) for (int b = 0; b < 60; b++) printf("%g\n", rb[b]);
    for (int b = 0; b < blocks; b++) if (rb[b] < 0.6 * e0 / blocks) quiet++;
    CHECK(e0 > 0, "arp produces audio while keys are held");
    (void)gaps; (void)blocks; CHECK(quiet > 0, "gate closes between steps");
    uint8_t off[3] = {0x80, 60, 0}; for (int i = 0; i < 3; i++) { off[1] = k[i][1]; e->midi(h, off, 3); }
    for (int b = 0; b < 400; b++) e->render(h, out, 128);
    double r = 0; for (int b = 0; b < 20; b++) { e->render(h, out, 128); for (int i = 0; i < 256; i++) r += (double)out[i] * out[i]; }
    CHECK(r < 1, "silent after release");
    e->destroy(h); printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
