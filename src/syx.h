/* Microwave 2/XT SysEx: F0 3E 0E dev cmd ... F7. Single dump 10h, SNDP 20h. */
#pragma once
#include "patch.h"
#define SYX_SNDD 0x10
#define SYX_SNDP 0x20
typedef enum { SYX_BAD = 0, SYX_SINGLE, SYX_ALL, SYX_PARAM } syx_kind_t;
typedef struct { syx_kind_t kind; int bank, num; int index, value; } syx_info_t;   /* index/value for SYX_PARAM */
/* Parse one message. SYX_SINGLE fills patches[0]; SYX_ALL fills patches[0..255]. `patches` must hold 256. */
syx_info_t syx_parse(const uint8_t *m, int len, patch_t *patches);
/* Write a single dump (edit buffer 20 00 unless loc given); returns bytes written (265). */
int syx_write_single(const patch_t *p, int dev, int bb, int nn, uint8_t out[265]);
/* Scan a file's bytes for messages; calls cb per parsed sound (a bank file yields many). Returns messages accepted. */
int syx_scan(const uint8_t *data, long len, void (*cb)(const patch_t *, int bank, int num, void *), void *ctx);
