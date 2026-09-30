#include <string.h>
#include "patch_tab.h"

void patch_init(patch_t *p) {
    memset(p, 0, sizeof *p);
    p->d[0] = 1;
    for (int i = 1; i < PATCH_SIZE; i++) if (patch_fields[i].key) p->d[i] = (uint8_t)patch_fields[i].def;
    for (int i = 0; patch_init_reserved[i][0]; i++) p->d[patch_init_reserved[i][0]] = patch_init_reserved[i][1];
    patch_set_name(p, "Init");
}

/* Reserved bytes are left alone: the firmware writes non-zero values there (9, 22, 33, 44, 69, 78 in factory sounds), and a
 * loaded sound must save back byte for byte. patch_init leaves them 0. */
void patch_clamp(patch_t *p) {
    for (int i = 1; i < PATCH_SIZE; i++) {
        const patch_field_t *f = &patch_fields[i];
        if (!f->key) continue;
        if (p->d[i] < f->lo) p->d[i] = (uint8_t)f->lo;
        else if (p->d[i] > f->hi) p->d[i] = (uint8_t)f->hi;
    }
}

int patch_find(const char *key) {
    for (int i = 1; i < PATCH_SIZE; i++) if (patch_fields[i].key && !strcmp(patch_fields[i].key, key)) return i;
    return -1;
}

void patch_get_name(const patch_t *p, char out[PATCH_NAME_LEN + 1]) {
    memcpy(out, p->d + PATCH_NAME_AT, PATCH_NAME_LEN);
    out[PATCH_NAME_LEN] = 0;
    for (int n = PATCH_NAME_LEN - 1; n >= 0 && out[n] == ' '; n--) out[n] = 0;
}

void patch_set_name(patch_t *p, const char *s) {
    size_t n = strlen(s);
    for (int i = 0; i < PATCH_NAME_LEN; i++) p->d[PATCH_NAME_AT + i] = (uint8_t)(i < (int)n && s[i] >= 32 ? s[i] : 32);
}

int patch_apply_cc(patch_t *p, int cc, int value) {
    for (const patch_cc_t *m = patch_cc_map; m->cc; m++) {
        if (m->cc != cc) continue;
        int v = value & 127;
        switch (m->kind) {
        case 1: v = 16 + 12 * (v > 8 ? 8 : v); break;
        case 2: v = 52 + (v > 24 ? 24 : v); break;
        case 3: v += 1; break;
        case 4: v = (v * 72 + 63) / 127; break;   /* -100%..+200% is 0..72; 48 is +100% (measured) */
        }
        const patch_field_t *f = &patch_fields[m->index];
        p->d[m->index] = (uint8_t)(v < f->lo ? f->lo : v > f->hi ? f->hi : v);
        return m->index;
    }
    return -1;
}
