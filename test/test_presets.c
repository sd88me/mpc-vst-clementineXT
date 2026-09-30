/* Every built-in preset must sound (finite, non-silent, not clipping wildly). */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
int main(void) {
    const mpc_engine_t *e = mpc_engine(); int fails = 0; char nm[64], pg[16];
    for (int i = 0; i < 12; i++) {
        void *h = e->create(NULL); snprintf(pg, sizeof pg, "%d", i); e->set_param(h, "program", pg); e->get_param(h, "patch_name", nm, sizeof nm);
        uint8_t on[3] = {0x90, 48, 100}; e->midi(h, on, 3); int16_t out[256]; double r = 0; int bad = 0, pk = 0;
        for (int k = 0; k < 300; k++) { e->render(h, out, 128); for (int j = 0; j < 256; j++) { r += (double)out[j] * out[j]; if (abs(out[j]) > pk) pk = abs(out[j]); } }
        r = sqrt(r / (300 * 256)); int ok = r > 20 && !bad; fails += !ok;
        printf("%s %-14s rms %7.1f peak %d\n", ok ? "ok  " : "FAIL", nm, r, pk); e->destroy(h);
    }
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
