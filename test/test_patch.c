#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/syx.h"
static int fails;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
static int seen;
static void cb(const patch_t *p, int b, int n, void *c) { (void)p; (void)c; seen += (b == 0 && n == seen % 128) || 1; }

int main(void) {
    int params = 0, dup = 0, bad_def = 0;
    for (int i = 0; i < PATCH_SIZE; i++) {
        const patch_field_t *f = &patch_fields[i];
        if (!f->key) continue;
        params++;
        if (f->def < f->lo || f->def > f->hi) bad_def++;
        if (patch_find(f->key) != i) dup++;
    }
    printf("info %d fields\n", params);
    CHECK(params == 219, "219 fields (203 parameters + 16 name bytes; matches the spec table)");
    CHECK(!dup, "keys unique");
    CHECK(!bad_def, "defaults in range");
    CHECK(patch_find("f1_cutoff") == 62 && patch_find("m16_dst") == 239 && patch_find("mod4_par") == 191, "spot indices (62, 239, 191)");
    CHECK(patch_fields[239].hi == 35 && patch_fields[194].hi == 35, "destination range widened to 0..35 (errata)");

    patch_t p; patch_init(&p);
    char nm[17]; patch_get_name(&p, nm); CHECK(!strcmp(nm, "Init"), "init name");
    p.d[62] = 100; p.d[2] = 200; p.d[4] = 9;   /* out of range and reserved */
    patch_clamp(&p);
    CHECK(p.d[2] == 76 && p.d[4] == 0 && p.d[62] == 100, "clamp: range clamped, reserved zeroed");

    uint8_t msg[265]; CHECK(syx_write_single(&p, 0, 0x20, 0, msg) == 265, "single dump is 265 bytes");
    patch_t *out = calloc(256, sizeof *out);
    syx_info_t r = syx_parse(msg, 265, out);
    CHECK(r.kind == SYX_SINGLE && !memcmp(out[0].d, p.d, 256), "single dump round trip");
    msg[100] ^= 1; CHECK(syx_parse(msg, 265, out).kind == SYX_BAD, "corrupt data fails the checksum"); msg[100] ^= 1;
    msg[263] = 0; r = syx_parse(msg, 265, out); CHECK(r.kind == SYX_SINGLE, "zero checksum accepted");

    uint8_t sndp[10] = { 0xF0, 0x3E, 0x0E, 0, 0x20, 0, 1, 5, 99, 0xF7 };
    r = syx_parse(sndp, 10, out); CHECK(r.kind == SYX_PARAM && r.index == 133 && r.value == 99, "SNDP index = HH*128+PP");

    long n = 7 + 65536 + 2; uint8_t *all = malloc(n);
    all[0] = 0xF0; all[1] = 0x3E; all[2] = 0x0E; all[3] = 0; all[4] = 0x10; all[5] = 0x10; all[6] = 0;
    for (int i = 0; i < 256; i++) { patch_t q; patch_init(&q); q.d[62] = (uint8_t)i & 127; memcpy(all + 7 + i * 256, q.d, 256); }
    int s = 0x10; for (int i = 0; i < 65536; i++) s += all[7 + i];
    all[n - 2] = (uint8_t)(s & 0x7F); all[n - 1] = 0xF7;
    r = syx_parse(all, (int)n, out); CHECK(r.kind == SYX_ALL && out[255].d[62] == 127 && out[130].d[62] == 2, "all-sounds dump: 256 patches");
    seen = 0; CHECK(syx_scan(all, n, cb, NULL) == 1 && seen == 256, "scan a bank file");
    free(all); free(out);
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
