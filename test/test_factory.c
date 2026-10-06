/* Local-only: the factory-sound decoder against the firmware's own all-sounds dump (tools/oracle `dumpall`).
 *   CLEMENTINE_ROM_DIR    folder with the user's ROM (two 128 KB halves or one 256 KB image)
 *   CLEMENTINE_ORACLE_DIR folder with all.syx
 * The dump is the firmware's view of a unit that also holds stored edits: 12 slots differ from the factory records (4, 93, 98, 102,
 * 103, 104, 182, 187, 230, 234, 254, 255), the other 244 must be byte-identical. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "factory.h"
#include "syx.h"
#include "wavedata.h"
int main(void) {
    const char *rom = getenv("CLEMENTINE_ROM_DIR"), *orc = getenv("CLEMENTINE_ORACLE_DIR");
    if (!rom || !orc) { printf("skip (set CLEMENTINE_ROM_DIR and CLEMENTINE_ORACLE_DIR)\n"); return 0; }
    int fails = 0;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
    uint8_t *img = wavedata_rom_image(rom);
    CHECK(img != NULL, "the ROM image loads");
    if (!img) return 1;
    static patch_t f[256], d[256];
    CHECK(factory_decode(img, f), "the factory sounds decode");
    char path[2048]; snprintf(path, sizeof path, "%s/all.syx", orc);
    FILE *fp = fopen(path, "rb"); static uint8_t buf[70000]; size_t n = fp ? fread(buf, 1, sizeof buf, fp) : 0; if (fp) fclose(fp);
    syx_info_t in = syx_parse(buf, (int)n, d);
    CHECK(in.kind == SYX_ALL, "the firmware dump parses");
    static const int edited[] = { 4, 93, 98, 102, 103, 104, 182, 187, 230, 234, 254, 255 };
    int same = 0, bad = 0;
    for (int i = 0; i < 256; i++) {
        int skip = 0; for (unsigned k = 0; k < sizeof edited / sizeof *edited; k++) skip |= edited[k] == i;
        if (skip) continue;
        if (memcmp(f[i].d, d[i].d, PATCH_SIZE)) { bad++; printf("info sound %d differs\n", i); } else same++;
    }
    CHECK(same == 244 && !bad, "244 sounds are byte-identical to the firmware's");
    uint8_t *junk = calloc(1, FACTORY_IMAGE_SIZE);
    patch_t keep = f[0]; static patch_t o[256]; o[0] = keep;
    CHECK(!factory_decode(junk, o) && !memcmp(&o[0], &keep, sizeof keep), "an image without sounds is refused and leaves the output alone");
    free(junk); free(img);
    printf(fails ? "FAILED\n" : "PASSED\n"); return fails;
}
