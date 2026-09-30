/* Local-only: render the init sound through our 40 kHz core so it can be compared with oracle renders (tools/oracle render).
 *   render_cmp <bank_dir_with_wave_data> <init.bin> <note> <vel> <samples> <out.f32> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
void clementine_render40k(void *inst, float *out, int n);
int main(int argc, char **argv) {
    if (argc < 7) return 2;
    const mpc_engine_t *e = mpc_engine(); void *h = e->create(argv[1]);
    FILE *f = fopen(argv[2], "rb"); uint8_t d[256]; if (!f || fread(d, 1, 256, f) != 256) return 3; fclose(f);
    static char st[600]; int o = sprintf(st, "P0 "); for (int i = 0; i < 256; i++) o += sprintf(st + o, "%02x", d[i]);
    e->set_param(h, "state", st);
    int n = atoi(argv[5]); float *buf = malloc(sizeof(float) * n);
    uint8_t on[3] = {0x90, (uint8_t)atoi(argv[3]), (uint8_t)atoi(argv[4])}; e->midi(h, on, 3);
    clementine_render40k(h, buf, n);
    f = fopen(argv[6], "wb"); fwrite(buf, 4, n, f); fclose(f);
    e->destroy(h); free(buf); return 0;
}
