#include <stdlib.h>
#include <string.h>
#include "syx.h"

static int xsum(int bb, int nn, const uint8_t *d, int n) {
    int s = bb + nn;
    for (int i = 0; i < n; i++) s += d[i];
    return s & 0x7F;
}

syx_info_t syx_parse(const uint8_t *m, int len, patch_t *patches) {
    syx_info_t r = { SYX_BAD, 0, 0, 0, 0 };
    if (len < 6 || m[0] != 0xF0 || m[1] != 0x3E || m[2] != 0x0E || m[len - 1] != 0xF7) return r;
    if (m[4] == SYX_SNDP && len == 10) { r.kind = SYX_PARAM; r.index = m[6] * 128 + m[7]; r.value = m[8]; return r; }
    if (m[4] != SYX_SNDD || len < 9) return r;
    int bb = m[5], nn = m[6], n = (bb == 0x10) ? 256 : 1, body = n * PATCH_SIZE;
    if (len != 7 + body + 2) return r;
    /* Some senders write a zero checksum; accept a wrong one only when it is exactly the omitted value. */
    int sum = xsum(bb, nn, m + 7, body);
    if (m[7 + body] != sum && m[7 + body] != 0) return r;
    for (int i = 0; i < n; i++) { memcpy(patches[i].d, m + 7 + i * PATCH_SIZE, PATCH_SIZE); patch_clamp(&patches[i]); }
    r.bank = bb; r.num = nn; r.kind = n == 256 ? SYX_ALL : SYX_SINGLE;
    return r;
}

int syx_write_single(const patch_t *p, int dev, int bb, int nn, uint8_t out[265]) {
    out[0] = 0xF0; out[1] = 0x3E; out[2] = 0x0E; out[3] = (uint8_t)dev; out[4] = SYX_SNDD; out[5] = (uint8_t)bb; out[6] = (uint8_t)nn;
    for (int i = 0; i < PATCH_SIZE; i++) out[7 + i] = p->d[i] & 0x7F;
    out[263] = (uint8_t)xsum(bb, nn, out + 7, PATCH_SIZE);
    out[264] = 0xF7;
    return 265;
}

int syx_scan(const uint8_t *data, long len, void (*cb)(const patch_t *, int, int, void *), void *ctx) {
    patch_t *tmp = malloc(256 * sizeof *tmp);
    int ok = 0;
    for (long i = 0; i < len; i++) {
        if (data[i] != 0xF0) continue;
        long j = i + 1;
        while (j < len && data[j] != 0xF7) j++;
        if (j >= len) break;
        syx_info_t r = syx_parse(data + i, (int)(j - i + 1), tmp);
        if (r.kind == SYX_SINGLE) { cb(&tmp[0], r.bank, r.num, ctx); ok++; }
        else if (r.kind == SYX_ALL) { for (int k = 0; k < 256; k++) cb(&tmp[k], k >> 7, k & 127, ctx); ok++; }
        i = j;
    }
    free(tmp);
    return ok;
}
