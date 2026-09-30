#include <string.h>
#include "patch_tab.h"

void patch_init(patch_t *p) {
    memset(p, 0, sizeof *p);
    p->d[0] = 1;
    for (int i = 1; i < PATCH_SIZE; i++) if (patch_fields[i].key) p->d[i] = (uint8_t)patch_fields[i].def;
    patch_set_name(p, "Init");
}

void patch_clamp(patch_t *p) {
    p->d[0] = 1;   /* format 0 is unpublished; we only read format 1 layout */
    for (int i = 1; i < PATCH_SIZE; i++) {
        const patch_field_t *f = &patch_fields[i];
        if (!f->key) { p->d[i] = 0; continue; }
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
