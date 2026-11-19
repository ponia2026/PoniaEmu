// SPDX-License-Identifier: GPL-2.0-or-later
// Self-test Libretro core: moving color bars (XRGB8888 256x224) + 440 Hz tone. Pressing any button inverts bars.
#include "libretro.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

static retro_environment_t env; static retro_video_refresh_t vid; static retro_audio_sample_batch_t aud;
static retro_input_poll_t poll_; static retro_input_state_t inp;
static uint32_t fb[256 * 224]; static unsigned t; static double ph;

void retro_set_environment(retro_environment_t e) { env = e; }
void retro_set_video_refresh(retro_video_refresh_t v) { vid = v; }
void retro_set_audio_sample(retro_audio_sample_t a) { (void)a; }
void retro_set_audio_sample_batch(retro_audio_sample_batch_t a) { aud = a; }
void retro_set_input_poll(retro_input_poll_t p) { poll_ = p; }
void retro_set_input_state(retro_input_state_t i) { inp = i; }
void retro_init(void) {}
void retro_deinit(void) {}
unsigned retro_api_version(void) { return RETRO_API_VERSION; }
void retro_get_system_info(struct retro_system_info* i) {
    memset(i, 0, sizeof *i); i->library_name = "testcore"; i->library_version = "1"; i->valid_extensions = "bin"; i->need_fullpath = false; }
void retro_get_system_av_info(struct retro_system_av_info* i) {
    i->geometry.base_width = 256; i->geometry.base_height = 224; i->geometry.max_width = 256; i->geometry.max_height = 224;
    i->geometry.aspect_ratio = 4.0f / 3.0f; i->timing.fps = 60.0; i->timing.sample_rate = 48000.0; }
void retro_set_controller_port_device(unsigned p, unsigned d) { (void)p; (void)d; }
void retro_reset(void) { t = 0; }
size_t retro_serialize_size(void) { return sizeof t; }
bool retro_serialize(void* d, size_t n) { if (n < sizeof t) return false; memcpy(d, &t, sizeof t); return true; }
bool retro_unserialize(const void* d, size_t n) { if (n < sizeof t) return false; memcpy(&t, d, sizeof t); return true; }
bool retro_load_game(const struct retro_game_info* g) {
    (void)g; enum retro_pixel_format f = RETRO_PIXEL_FORMAT_XRGB8888; return env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &f); }
void retro_unload_game(void) {}
void* retro_get_memory_data(unsigned id) { (void)id; return 0; }
size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
void retro_run(void) {
    poll_();
    int any = 0; for (unsigned i = 0; i < 16; ++i) any |= inp(0, RETRO_DEVICE_JOYPAD, 0, i);
    static const uint32_t bars[8] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};
    for (int y = 0; y < 224; ++y) for (int x = 0; x < 256; ++x) {
        uint32_t c = bars[((x + t) / 32) & 7]; fb[y * 256 + x] = any ? ~c & 0xFFFFFF : c; }
    vid(fb, 256, 224, 256 * 4);
    int16_t s[800 * 2];
    for (int i = 0; i < 800; ++i) { int16_t v = (int16_t)(sin(ph) * 6000); ph += 2 * M_PI * 440 / 48000; s[2 * i] = s[2 * i + 1] = v; }
    aud(s, 800);
    ++t;
}
