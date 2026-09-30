/* The engine's state is the XT's 256-byte SDATA block (SysEx spec 3.1). Index = byte offset. */
#pragma once
#include <stdint.h>
#define PATCH_SIZE 256
#define PATCH_NAME_AT 240
#define PATCH_NAME_LEN 16
typedef struct { const char *key, *name; int16_t lo, hi, def; } patch_field_t;   /* key == NULL: reserved */
typedef struct { uint8_t d[PATCH_SIZE]; } patch_t;
extern const patch_field_t patch_fields[PATCH_SIZE];
extern const uint8_t patch_init_reserved[][2];   /* {index, value}, terminated by {0, 0} */
typedef struct { uint8_t cc, index, kind; } patch_cc_t;   /* kind: 0 direct, 1 octave, 2 semitone, 3 arp range, 4 osc keytrack */
extern const patch_cc_t patch_cc_map[];                    /* terminated by cc == 0 */
int patch_apply_cc(patch_t *p, int cc, int value);        /* the SDATA index changed, or -1 if cc is not a sound parameter */
void patch_init(patch_t *p);                 /* an init sound */
void patch_clamp(patch_t *p);                /* range-clamp every named field; reserved bytes and the version byte are kept */
int patch_find(const char *key);             /* field index or -1 */
void patch_get_name(const patch_t *p, char out[PATCH_NAME_LEN + 1]);
void patch_set_name(patch_t *p, const char *s);
