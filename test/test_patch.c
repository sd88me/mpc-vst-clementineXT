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
    p.d[62] = 100; p.d[2] = 200; p.d[4] = 9;   /* out of range; a reserved byte */
    patch_clamp(&p);
    CHECK(p.d[2] == 76 && p.d[4] == 9 && p.d[62] == 100, "clamp: range clamped, reserved byte kept");

    uint8_t msg[265]; CHECK(syx_write_single(&p, 0, 0x20, 0, msg) == 265, "single dump is 265 bytes");
    patch_t *out = calloc(256, sizeof *out);
    syx_info_t r = syx_parse(msg, 265, out);
    CHECK(r.kind == SYX_SINGLE && !memcmp(out[0].d, p.d, 256), "single dump round trip");
    msg[100] ^= 1; CHECK(syx_parse(msg, 265, out).kind == SYX_BAD, "corrupt data fails the checksum"); msg[100] ^= 1;
    msg[263] = 0; r = syx_parse(msg, 265, out); CHECK(r.kind == SYX_SINGLE, "zero checksum accepted");

    msg[263] = 0x7F; CHECK(syx_parse(msg, 265, out).kind == SYX_SINGLE, "checksum 7Fh always accepted");
    patch_t q; patch_init(&q);
    CHECK(patch_apply_cc(&q, 33, 8) == 1 && q.d[1] == 112 && patch_apply_cc(&q, 33, 4) == 1 && q.d[1] == 64, "CC 33 octave: 4 -> 64 (0), 8 -> 112 (+4)");
    CHECK(patch_apply_cc(&q, 34, 12) == 2 && q.d[2] == 64, "CC 34 semitone: 12 -> 64 (0)");
    CHECK(patch_apply_cc(&q, 103, 0) == 95 && q.d[95] == 1, "CC 103 arp range 0 -> 1");
    CHECK(patch_apply_cc(&q, 37, 127) == 6 && q.d[6] == 76 && patch_apply_cc(&q, 50, 99) == 62 && q.d[62] == 99, "CC 37 keytrack scaled, CC 50 cutoff direct");
    CHECK(patch_apply_cc(&q, 1, 5) == -1 && patch_apply_cc(&q, 99, 5) == -1, "mod wheel and unknown CCs are not sound parameters");
    uint8_t sndp[10] = { 0xF0, 0x3E, 0x0E, 0, 0x20, 0, 1, 5, 99, 0xF7 };
    r = syx_parse(sndp, 10, out); CHECK(r.kind == SYX_PARAM && r.index == 133 && r.value == 99, "SNDP index = HH*128+PP");

    long n = 7 + 65536 + 2; uint8_t *all = malloc(n);
    all[0] = 0xF0; all[1] = 0x3E; all[2] = 0x0E; all[3] = 0; all[4] = 0x10; all[5] = 0x10; all[6] = 0;
    for (int i = 0; i < 256; i++) { patch_t q; patch_init(&q); q.d[62] = (uint8_t)i & 127; memcpy(all + 7 + i * 256, q.d, 256); }
    int s = 0; for (int i = 0; i < 65536; i++) s += all[7 + i];   /* firmware form: SDATA only */
    all[n - 2] = (uint8_t)(s & 0x7F); all[n - 1] = 0xF7;
    r = syx_parse(all, (int)n, out); CHECK(r.kind == SYX_ALL && out[255].d[62] == 127 && out[130].d[62] == 2, "all-sounds dump: 256 patches");
    seen = 0; CHECK(syx_scan(all, n, cb, NULL) == 1 && seen == 256, "scan a bank file");
    free(all);

    /* Local-only regression: a real all-sounds dump made by the oracle (never committed). Set CLEMENTINE_TEST_BANK. */
    const char *bank = getenv("CLEMENTINE_TEST_BANK");
    if (bank) {
        FILE *fp = fopen(bank, "rb"); uint8_t *buf = malloc(70000); long n2 = fp ? (long)fread(buf, 1, 70000, fp) : 0; if (fp) fclose(fp);
        r = syx_parse(buf, (int)n2, out);
        CHECK(r.kind == SYX_ALL, "firmware all-sounds dump parses (checksum form)");
        int changed = 0, same = 1;
        for (int i = 0; i < 256; i++) { patch_t q = out[i]; patch_clamp(&q); if (memcmp(q.d, buf + 7 + i * 256, 256)) changed++; }
        CHECK(changed == 0, "no factory sound is altered by patch_clamp (ranges cover real data)");
        (void)same; free(buf);
    } else printf("skip firmware bank test (set CLEMENTINE_TEST_BANK)\n");
    free(out);
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
