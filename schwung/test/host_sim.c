/* Host simulation for the Schwung module: drives plugin_api_v2 the way the chain host does (x86, no Move needed). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "plugin_api_v1.h"
extern plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);
static int fails;
#define CHECK(c, m) do { int ok_ = (c); printf("%s %s\n", ok_ ? "ok  " : "FAIL", m); fails += !ok_; } while (0)
static void sleep_ms(int ms) { struct timespec ts = { 0, ms * 1000000L }; nanosleep(&ts, NULL); }
static double block_rms(plugin_api_v2_t *a, void *h, int blocks) {
    int16_t out[256]; double e = 0;
    for (int b = 0; b < blocks; b++) { a->render_block(h, out, 128); for (int i = 0; i < 256; i++) e += (double)out[i] * out[i]; }
    return sqrt(e / (blocks * 256.0));
}
int main(void) {
    setenv("CLEMENTINE_XT_DATA", "/tmp/clxt_sim_data", 1);
    plugin_api_v2_t *a = move_plugin_init_v2(NULL);
    CHECK(a && a->api_version == MOVE_PLUGIN_API_VERSION_2, "init returns the v2 table");
    void *h = a->create_instance("/nonexistent", NULL);
    CHECK(h != NULL, "create_instance returns at once");
    char buf[70000]; int n;
    CHECK(a->get_param(h, "chain_params", buf, sizeof buf) > 1000 && buf[0] == '[', "chain_params is served before the engine is ready");
    CHECK(a->get_param(h, "ui_hierarchy", buf, sizeof buf) > 1000 && buf[0] == '{', "ui_hierarchy is served before the engine is ready");
    int16_t out[256]; memset(out, 7, sizeof out);
    a->render_block(h, out, 128);
    int sil = 1; for (int i = 0; i < 256; i++) if (out[i]) sil = 0;
    int was_ready = 0; if (a->get_param(h, "ready", buf, sizeof buf) > 0) was_ready = atoi(buf);
    if (!was_ready) CHECK(sil, "silence while loading");
    for (int t = 0; t < 600 && !(a->get_param(h, "ready", buf, sizeof buf) > 0 && atoi(buf) == 1); t++) sleep_ms(10);
    CHECK(a->get_param(h, "ready", buf, sizeof buf) > 0 && atoi(buf) == 1, "the worker publishes the engine");
    uint8_t on[3] = { 0x90, 60, 100 };
    a->on_midi(h, on, 3, MOVE_MIDI_SOURCE_INTERNAL);
    CHECK(block_rms(a, h, 80) > 50.0, "a note makes sound");
    CHECK(a->get_param(h, "preset_count", buf, sizeof buf) > 0 && atoi(buf) == 256, "preset_count is 256");
    n = a->get_param(h, "preset_name", buf, sizeof buf); CHECK(n > 0, "preset_name reads");
    char first[64]; snprintf(first, sizeof first, "%s", buf);
    a->set_param(h, "preset", "3");
    CHECK(a->get_param(h, "preset", buf, sizeof buf) > 0 && atoi(buf) == 3, "preset selects a sound");
    a->set_param(h, "f1_type", "12dB LP");
    CHECK(a->get_param(h, "f1_type", buf, sizeof buf) > 0 && atoi(buf) == 1, "an enum accepts its label");
    a->set_param(h, "f1_type", "4");
    CHECK(a->get_param(h, "f1_type", buf, sizeof buf) > 0 && atoi(buf) == 4, "and its index");
    a->set_param(h, "f1_cutoff", "100.0");
    CHECK(a->get_param(h, "f1_cutoff", buf, sizeof buf) > 0 && atoi(buf) == 100, "a number with a decimal point is read");
    a->set_param(h, "play1", "28"); a->set_param(h, "play_v1", "127");
    CHECK(a->get_param(h, "f1_cutoff", buf, sizeof buf) > 0 && atoi(buf) == 127, "a Play knob drives its parameter");
    CHECK(a->get_param(h, "bank_list", buf, sizeof buf) > 2 && buf[0] == '[' && strstr(buf, "\"index\":0"), "bank_list is a JSON list");
    CHECK(a->get_param(h, "no_such_key", buf, sizeof buf) < 0, "an unknown key reads as missing");
    uint8_t off[3] = { 0x80, 60, 0 };
    a->on_midi(h, off, 3, MOVE_MIDI_SOURCE_INTERNAL);
    for (int i = 0; i < 100; i++) block_rms(a, h, 10);
    a->destroy_instance(h);
    sleep_ms(100);
    printf(fails ? "FAILED\n" : "PASSED\n");
    return fails;
}
