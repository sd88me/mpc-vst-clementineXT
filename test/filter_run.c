/* Local-only: run Filter 1 over a float32 file: filter_run <type> <cutoff> <reso> <special> <in.f32> <out.f32> */
#include <stdio.h>
#include <stdlib.h>
#include "filter.h"
int main(int argc, char **argv) {
    if (argc < 7) return 2;
    int type = atoi(argv[1]), sp = atoi(argv[4]); float cut = (float)atof(argv[2]), reso = (float)atof(argv[3]);
    FILE *f = fopen(argv[5], "rb"); if (!f) return 3;
    fseek(f, 0, SEEK_END); long n = ftell(f) / 4; fseek(f, 0, SEEK_SET);
    float *x = malloc(4 * n); if (fread(x, 4, n, f) != (size_t)n) return 4; fclose(f);
    filt_t st = {0};
    for (long i = 0; i < n; i++) x[i] = filter1_run(&st, type, x[i], cut, reso, sp);
    f = fopen(argv[6], "wb"); fwrite(x, 4, n, f); fclose(f); free(x); return 0;
}
