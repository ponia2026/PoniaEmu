// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// ponia_play: minimal Libretro host (replaces RetroArch as the launcher of cores). Linux: thin exe (main.cpp); Android: called from PlayerActivity via JNI/SDL_main.
// Software-rendered cores only for now (hardware GL cores: TODO). SDL2 for video/audio/input.
//
// Usage: poniaplay --core X_libretro.so --content ROM [--system-dir D] [--save-dir D]
//                  [--option key=value]... [--frames N] [--shot out.ppm]

#include <deque>
#include "libretro.h"
#include "ponia_systems.h"
#include "ponia_cores.h"
#include "ponia_play.h"
#include "archive.h"
#include "bios_sync.h"

#include <SDL.h>
#include <dlfcn.h>
#ifdef __ANDROID__
#include <link.h>
#include <sys/mman.h>
#endif

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/stat.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

// stderr is invisible on Android -> logcat (tag "poniaplay")
static void plog(const char* fmt, ...) {
    va_list a; va_start(a, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_INFO, "poniaplay", fmt, a);
#else
    std::vfprintf(stderr, fmt, a);
#endif
    va_end(a);
}

namespace {

// ---------- core function table ----------
struct Core {
    void* h = nullptr;
    void (*set_environment)(retro_environment_t) = nullptr;
    void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*set_input_poll)(retro_input_poll_t) = nullptr;
    void (*set_input_state)(retro_input_state_t) = nullptr;
    void (*init)() = nullptr;
    void (*deinit)() = nullptr;
    unsigned (*api_version)() = nullptr;
    void (*get_system_info)(retro_system_info*) = nullptr;
    void (*get_system_av_info)(retro_system_av_info*) = nullptr;
    void (*set_controller_port_device)(unsigned, unsigned) = nullptr;
    void (*reset)() = nullptr;
    void (*run)() = nullptr;
    size_t (*serialize_size)() = nullptr;
    bool (*serialize)(void*, size_t) = nullptr;
    bool (*unserialize)(const void*, size_t) = nullptr;
    bool (*load_game)(const retro_game_info*) = nullptr;
    void (*unload_game)() = nullptr;
    void* (*get_memory_data)(unsigned) = nullptr;
    size_t (*get_memory_size)(unsigned) = nullptr;
} core;

template<typename T> bool sym(T& fn, const char* name) {
    fn = reinterpret_cast<T>(dlsym(core.h, name));
    if (!fn) plog("poniaplay: missing symbol %s\n", name);
    return fn != nullptr;
}

#ifdef __ANDROID__
// On Android 9, bionic's tmpfile() ignores TMPDIR and always writes to /data/local/tmp (the app has no permission) -> returns NULL, and a core that then fwrite(NULL)s aborts
// (retro_serialize_size of yabasanshiro and AppleWin both hit this). After the core is loaded, its tmpfile entry in the GOT is replaced with our version:
// create a temporary file in the save directory and unlink it at once (it disappears when closed, same semantics as tmpfile).
std::string g_tmp_dir = ".";
#endif
bool g_tmp_patched = false;
#ifdef __ANDROID__
FILE* ponia_tmpfile() {
    std::string p = g_tmp_dir + "/.ponia-tmp-XXXXXX";
    std::vector<char> b(p.begin(), p.end()); b.push_back(0);
    int fd = mkstemp(b.data());
    if (fd < 0) return nullptr;
    unlink(b.data());
    FILE* f = fdopen(fd, "w+b");
    if (!f) close(fd);
    return f;
}
struct TmpPatchCtx { const char* name; int patched; };
int tmp_patch_cb(struct dl_phdr_info* info, size_t, void* data) {
    auto* c = static_cast<TmpPatchCtx*>(data);
    if (!info->dlpi_name || !std::strstr(info->dlpi_name, c->name)) return 0;
    const ElfW(Dyn)* dyn = nullptr;
    for (int i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) dyn = (const ElfW(Dyn)*)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if (!dyn) return 1;
    auto fix = [&](uintptr_t v) { return v >= info->dlpi_addr ? v : v + info->dlpi_addr; };   // some linkers have already relocated, some have not
    const ElfW(Sym)* symtab = nullptr; const char* strtab = nullptr;
    const ElfW(Rela)* jmprel = nullptr; size_t jmpsz = 0; const ElfW(Rela)* rela = nullptr; size_t relasz = 0;
    for (; dyn->d_tag != DT_NULL; ++dyn) switch (dyn->d_tag) {
        case DT_SYMTAB: symtab = (const ElfW(Sym)*)fix(dyn->d_un.d_ptr); break;
        case DT_STRTAB: strtab = (const char*)fix(dyn->d_un.d_ptr); break;
        case DT_JMPREL: jmprel = (const ElfW(Rela)*)fix(dyn->d_un.d_ptr); break;
        case DT_PLTRELSZ: jmpsz = dyn->d_un.d_val; break;
        case DT_RELA: rela = (const ElfW(Rela)*)fix(dyn->d_un.d_ptr); break;
        case DT_RELASZ: relasz = dyn->d_un.d_val; break;
        default: break;
    }
    if (!symtab || !strtab) return 1;
    auto scan = [&](const ElfW(Rela)* r, size_t sz) {
        for (size_t i = 0; r && i < sz / sizeof(ElfW(Rela)); ++i) {
            const unsigned type = ELF64_R_TYPE(r[i].r_info);
            if (type != 1025 /*GLOB_DAT*/ && type != 1026 /*JUMP_SLOT*/) continue;
            const char* nm = strtab + symtab[ELF64_R_SYM(r[i].r_info)].st_name;
            if (std::strcmp(nm, "tmpfile") != 0) continue;
            void** slot = (void**)(info->dlpi_addr + r[i].r_offset);
            const long pg = sysconf(_SC_PAGESIZE);
            void* page = (void*)((uintptr_t)slot & ~(uintptr_t)(pg - 1));
            if (mprotect(page, pg, PROT_READ | PROT_WRITE) == 0) { *slot = (void*)&ponia_tmpfile; ++c->patched; }
        }
    };
    scan(jmprel, jmpsz); scan(rela, relasz);
    return 1;
}
void patch_core_tmpfile(const char* path) {
    const char* base = std::strrchr(path, '/'); base = base ? base + 1 : path;
    TmpPatchCtx c{base, 0};
    dl_iterate_phdr(tmp_patch_cb, &c);
    g_tmp_patched = c.patched > 0;
    if (c.patched) plog("poniaplay: tmpfile() of %s redirected to %s (%d slot)\n", base, g_tmp_dir.c_str(), c.patched);
}
#endif

bool load_core(const char* path) {
    core.h = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
    if (!core.h) { plog("poniaplay: dlopen: %s\n", dlerror()); return false; }
#ifdef __ANDROID__
    patch_core_tmpfile(path);
#endif
    bool ok = true;
    ok &= sym(core.set_environment, "retro_set_environment");
    ok &= sym(core.set_video_refresh, "retro_set_video_refresh");
    ok &= sym(core.set_audio_sample, "retro_set_audio_sample");
    ok &= sym(core.set_audio_sample_batch, "retro_set_audio_sample_batch");
    ok &= sym(core.set_input_poll, "retro_set_input_poll");
    ok &= sym(core.set_input_state, "retro_set_input_state");
    ok &= sym(core.init, "retro_init");
    ok &= sym(core.deinit, "retro_deinit");
    ok &= sym(core.api_version, "retro_api_version");
    ok &= sym(core.get_system_info, "retro_get_system_info");
    ok &= sym(core.get_system_av_info, "retro_get_system_av_info");
    ok &= sym(core.set_controller_port_device, "retro_set_controller_port_device");
    ok &= sym(core.reset, "retro_reset");
    ok &= sym(core.run, "retro_run");
    ok &= sym(core.serialize_size, "retro_serialize_size");
    ok &= sym(core.serialize, "retro_serialize");
    ok &= sym(core.unserialize, "retro_unserialize");
    ok &= sym(core.load_game, "retro_load_game");
    ok &= sym(core.unload_game, "retro_unload_game");
    ok &= sym(core.get_memory_data, "retro_get_memory_data");
    ok &= sym(core.get_memory_size, "retro_get_memory_size");
    return ok;
}

// ---------- global state ----------
struct Frontend {
    std::string system_dir = ".", save_dir = ".", content_path, base_name, shot_path;
    std::map<std::string, std::string> opt_default, opt_user;
    retro_pixel_format fmt = RETRO_PIXEL_FORMAT_0RGB1555;
    unsigned width = 0, height = 0;
    float aspect = 0;
    double fps = 60, sample_rate = 44100;
    unsigned rotation = 0;

    SDL_Window* win = nullptr;
    SDL_Renderer* ren = nullptr;
    SDL_Texture* tex = nullptr;
    unsigned tex_w = 0, tex_h = 0;
    SDL_AudioDeviceID audio = 0;
    SDL_GameController* pad = nullptr;
    std::vector<uint8_t> last_frame; unsigned last_pitch = 0, last_w = 0, last_h = 0;

    bool quit = false, show_fps = false;
    bool menu = false; int menu_sel = 0;   // in-game menu (hotkey + X)
    uint16_t pad_mask = 0;      // current RetroPad bitmask (as sent to the core)
    bool hotkey_held = false, hotkey_used = false;
    int res_level = 1;          // in-game internal resolution 1X..4X (hotkey + X: 1X -> 2X -> 3X -> 4X -> 1X)
    std::string core_name;      // core file base name, used to pick the resolution option
    bool var_update = false;    // tell the core that an option changed
    bool ff = false, rew = false;   // hotkey + R2 fast-forward, hotkey + L2 rewind (both held)
    bool draw_frame = true;     // false while fast-forwarding: skip presenting in-between frames
    std::deque<std::vector<uint8_t>> rbuf; size_t rbytes = 0; unsigned rcount = 0;   // rewind ring of core snapshots
    std::string osd; Uint32 osd_until = 0;   // short on-screen message
    int select_pulse = 0;       // frames left for a SELECT tap
    unsigned frames_done = 0, max_frames = 0;
    double fps_now = 0; Uint32 fps_t0 = 0; unsigned fps_n = 0;
    // ---- hardware (OpenGL / GLES) rendering cores: they draw into our offscreen FBO, we read it back every frame ----
    bool hw_req = false;                 // the core asked for SET_HW_RENDER
    retro_hw_render_callback hw{};       // our copy (callbacks filled in)
    SDL_GLContext hw_ctx = nullptr;      // the core own GL context (SDL renderer keeps a separate one)
    unsigned hw_fbo = 0, hw_tex = 0, hw_rb = 0, hw_w = 0, hw_h = 0;
    std::vector<uint8_t> hw_pix, hw_flip;
    // ---- zero-copy present (hardware cores): the core's FBO texture is drawn straight to the window by a shader quad ----
    bool zc = false, zc_off = false;     // zc = active; zc_off = --no-zerocopy (force the old read-back path for A/B tests)
    unsigned zc_prog = 0, zc_vbo = 0, zc_vao = 0, zc_w = 0, zc_h = 0;   // zc_w/h = size of the last frame inside hw_tex
    int zc_loc_tex = -1, zc_loc_sharp = -1, zc_loc_texel = -1, zc_loc_win = -1;
    float sharpen = 0.f;   // --sharpen: 0 = off; ~0.3..0.8 = edge sharpening applied by the present shader (zero-copy path only)
    unsigned hud_tex = 0, menu_tex = 0; int hud_tw = 0, hud_th = 0, menu_tw = 0, menu_th = 0;   // overlay textures (size as allocated on the GPU)
    SDL_Surface* hud_surf = nullptr; SDL_Renderer* hud_ren = nullptr;      // overlays are painted on the CPU with SDL's software renderer
    SDL_Surface* menu_surf = nullptr; SDL_Renderer* menu_ren = nullptr;    // and uploaded only when their content changes
    std::string hud_key; int hud_x = 0, hud_wpx = 0, hud_hpx = 0; long long menu_key = -1;
    Uint32 tex_pf = 0;                   // pixel format of F.tex
    bool last_hw = false;
    int16_t axis[4] = {0, 0, 0, 0};   // left X/Y, right X/Y (libretro analog, -32768..32767)
    bool analog_used = false;          // the core reads RETRO_DEVICE_ANALOG: then the left stick is not also a D-pad
    unsigned latch = 0;          // RetroPad bits pressed since the last poll (a tap shorter than one poll interval must still reach the core)
    bool debug_input = false;    // --debug-input: log what SDL delivers and what the core asks for
    bool no_ffrew = false;       // Dolphin: fast-forward / rewind disabled (freezes on some devices)
    bool no_serialize = false;   // core crashes in retro_serialize (tmpfile): no rewind / save states
} F;

void log_cb(enum retro_log_level lvl, const char* fmt, ...) {
    static const char* n[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    char msg[1024];
    va_list a; va_start(a, fmt); std::vsnprintf(msg, sizeof msg, fmt, a); va_end(a);
    plog("[core %s] %s", n[lvl & 3], msg);   // go through plog: only then does it reach logcat on Android
}

std::string state_path() { return F.save_dir + "/" + F.base_name + ".state"; }
std::string sram_path() { return F.save_dir + "/" + F.base_name + ".srm"; }

bool read_file(const std::string& p, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(p.c_str(), "rb"); if (!f) return false;
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    size_t r = n > 0 ? std::fread(out.data(), 1, n, f) : 0; std::fclose(f);
    return r == out.size();
}
bool write_file(const std::string& p, const void* d, size_t n) {
    FILE* f = std::fopen(p.c_str(), "wb"); if (!f) return false;
    size_t w = std::fwrite(d, 1, n, f); std::fclose(f); return w == n;
}

void osd_show(const char* t);   // defined with the HUD below
void save_state() {
    if (F.no_serialize) { osd_show("NO SAVE"); return; }
    size_t n = core.serialize_size();
    if (!n) { osd_show("SAVE NOT SUPPORTED"); return; }
    std::vector<uint8_t> b(n);
    if (core.serialize(b.data(), n) && write_file(state_path(), b.data(), n)) {
        plog("poniaplay: state saved\n");
        osd_show("STATE SAVED");
    } else osd_show("SAVE FAILED");
}
void load_state() {
    if (F.no_serialize) { osd_show("NO SAVE"); return; }
    std::vector<uint8_t> b;
    if (!read_file(state_path(), b)) { osd_show("NO SAVED STATE"); return; }
    if (core.unserialize(b.data(), b.size())) {
        plog("poniaplay: state loaded\n");
        osd_show("STATE LOADED");
    } else osd_show("LOAD FAILED");
}
// ---- rewind ring: a snapshot every 3 frames, <= 64 MB / 600 entries; cores with big states (>4 MB) get no rewind ----
void rewind_capture() {
    if (F.no_serialize || F.no_ffrew) return;   // no_ffrew: do not even ask the core for its state size
    static size_t n = (size_t)-1;
    if (n == (size_t)-1) { n = core.serialize_size(); if (n > (4u << 20)) n = 0; }
    if (!n || (++F.rcount % 3)) return;
    std::vector<uint8_t> b(n);
    if (!core.serialize(b.data(), n)) return;
    F.rbytes += b.size(); F.rbuf.emplace_back(std::move(b));
    while (F.rbuf.size() > 600 || F.rbytes > (64u << 20)) { F.rbytes -= F.rbuf.front().size(); F.rbuf.pop_front(); }
}
bool rewind_step() {
    if (F.rbuf.empty()) return false;
    std::vector<uint8_t> b = std::move(F.rbuf.back()); F.rbuf.pop_back(); F.rbytes -= b.size();
    return core.unserialize(b.data(), b.size());
}

void load_sram() {
    void* m = core.get_memory_data(RETRO_MEMORY_SAVE_RAM); size_t n = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    std::vector<uint8_t> b;
    if (m && n && read_file(sram_path(), b)) std::memcpy(m, b.data(), b.size() < n ? b.size() : n);
}
void save_sram() {
    void* m = core.get_memory_data(RETRO_MEMORY_SAVE_RAM); size_t n = core.get_memory_size(RETRO_MEMORY_SAVE_RAM);
    if (m && n) write_file(sram_path(), m, n);
}

// ---------- hardware rendering: minimal GL function table (loaded with SDL_GL_GetProcAddress, no GL headers needed) ----------
namespace gl {
typedef unsigned GLenum_t; typedef unsigned GLuint_t; typedef int GLint_t; typedef int GLsizei_t;
const GLenum_t FRAMEBUFFER = 0x8D40, RENDERBUFFER = 0x8D41, COLOR_ATTACHMENT0 = 0x8CE0, DEPTH_ATTACHMENT = 0x8D00,
               DEPTH_STENCIL_ATTACHMENT = 0x821A, DEPTH24_STENCIL8 = 0x88F0, DEPTH_COMPONENT16 = 0x81A5,
               TEXTURE_2D = 0x0DE1, RGBA = 0x1908, UNSIGNED_BYTE = 0x1401, TEXTURE_MIN_FILTER = 0x2801,
               TEXTURE_MAG_FILTER = 0x2800, NEAREST = 0x2600, PACK_ALIGNMENT = 0x0D05, FRAMEBUFFER_COMPLETE = 0x8CD5;
struct Fns {
    void (*GenFramebuffers)(GLsizei_t, GLuint_t*) = nullptr;
    void (*BindFramebuffer)(GLenum_t, GLuint_t) = nullptr;
    void (*GenTextures)(GLsizei_t, GLuint_t*) = nullptr;
    void (*BindTexture)(GLenum_t, GLuint_t) = nullptr;
    void (*TexImage2D)(GLenum_t, GLint_t, GLint_t, GLsizei_t, GLsizei_t, GLint_t, GLenum_t, GLenum_t, const void*) = nullptr;
    void (*TexParameteri)(GLenum_t, GLenum_t, GLint_t) = nullptr;
    void (*FramebufferTexture2D)(GLenum_t, GLenum_t, GLenum_t, GLuint_t, GLint_t) = nullptr;
    void (*GenRenderbuffers)(GLsizei_t, GLuint_t*) = nullptr;
    void (*BindRenderbuffer)(GLenum_t, GLuint_t) = nullptr;
    void (*RenderbufferStorage)(GLenum_t, GLenum_t, GLsizei_t, GLsizei_t) = nullptr;
    void (*FramebufferRenderbuffer)(GLenum_t, GLenum_t, GLenum_t, GLuint_t) = nullptr;
    GLenum_t (*CheckFramebufferStatus)(GLenum_t) = nullptr;
    void (*ReadPixels)(GLint_t, GLint_t, GLsizei_t, GLsizei_t, GLenum_t, GLenum_t, void*) = nullptr;
    void (*PixelStorei)(GLenum_t, GLint_t) = nullptr;
    void (*Viewport)(GLint_t, GLint_t, GLsizei_t, GLsizei_t) = nullptr;
    void (*DeleteFramebuffers)(GLsizei_t, const GLuint_t*) = nullptr;
    void (*DeleteTextures)(GLsizei_t, const GLuint_t*) = nullptr;
    void (*DeleteRenderbuffers)(GLsizei_t, const GLuint_t*) = nullptr;
} fn;

// Extra entry points for the zero-copy present path (shader quad drawn straight from the core's FBO texture).
// Loaded separately: if any is missing we just keep the old read-back path.
typedef char GLchar_t; typedef float GLfloat_t; typedef ptrdiff_t GLsizeiptr_t; typedef unsigned char GLboolean_t;
const GLenum_t VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30, COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82,
               ARRAY_BUFFER = 0x8892, STREAM_DRAW = 0x88E0, FLOAT = 0x1406, TRIANGLE_STRIP = 5, COLOR_BUFFER_BIT = 0x4000,
               BLEND = 0x0BE2, DEPTH_TEST = 0x0B71, SCISSOR_TEST = 0x0C11, CULL_FACE = 0x0B44, STENCIL_TEST = 0x0B90,
               ONE = 1, ONE_MINUS_SRC_ALPHA = 0x0303, TEXTURE0 = 0x84C0, LINEAR = 0x2601, TEXTURE_WRAP_S = 0x2802,
               TEXTURE_WRAP_T = 0x2803, CLAMP_TO_EDGE = 0x812F;
struct Zc {
    GLuint_t (*CreateShader)(GLenum_t) = nullptr;
    void (*ShaderSource)(GLuint_t, GLsizei_t, const GLchar_t* const*, const GLint_t*) = nullptr;
    void (*CompileShader)(GLuint_t) = nullptr;
    void (*GetShaderiv)(GLuint_t, GLenum_t, GLint_t*) = nullptr;
    void (*GetShaderInfoLog)(GLuint_t, GLsizei_t, GLsizei_t*, GLchar_t*) = nullptr;
    void (*DeleteShader)(GLuint_t) = nullptr;
    GLuint_t (*CreateProgram)() = nullptr;
    void (*AttachShader)(GLuint_t, GLuint_t) = nullptr;
    void (*BindAttribLocation)(GLuint_t, GLuint_t, const GLchar_t*) = nullptr;
    void (*LinkProgram)(GLuint_t) = nullptr;
    void (*GetProgramiv)(GLuint_t, GLenum_t, GLint_t*) = nullptr;
    void (*UseProgram)(GLuint_t) = nullptr;
    GLint_t (*GetUniformLocation)(GLuint_t, const GLchar_t*) = nullptr;
    void (*Uniform1i)(GLint_t, GLint_t) = nullptr;
    void (*Uniform1f)(GLint_t, float) = nullptr;
    void (*Uniform2f)(GLint_t, float, float) = nullptr;
    void (*Uniform4f)(GLint_t, float, float, float, float) = nullptr;
    void (*GenBuffers)(GLsizei_t, GLuint_t*) = nullptr;
    void (*BindBuffer)(GLenum_t, GLuint_t) = nullptr;
    void (*BufferData)(GLenum_t, GLsizeiptr_t, const void*, GLenum_t) = nullptr;
    void (*BufferSubData)(GLenum_t, GLsizeiptr_t, GLsizeiptr_t, const void*) = nullptr;
    void (*EnableVertexAttribArray)(GLuint_t) = nullptr;
    void (*DisableVertexAttribArray)(GLuint_t) = nullptr;
    void (*VertexAttribPointer)(GLuint_t, GLint_t, GLenum_t, GLboolean_t, GLsizei_t, const void*) = nullptr;
    void (*DrawArrays)(GLenum_t, GLint_t, GLsizei_t) = nullptr;
    void (*ClearColor)(GLfloat_t, GLfloat_t, GLfloat_t, GLfloat_t) = nullptr;
    void (*Clear)(unsigned) = nullptr;
    void (*Enable)(GLenum_t) = nullptr;
    void (*Disable)(GLenum_t) = nullptr;
    void (*BlendFunc)(GLenum_t, GLenum_t) = nullptr;
    void (*ActiveTexture)(GLenum_t) = nullptr;
    void (*TexSubImage2D)(GLenum_t, GLint_t, GLint_t, GLint_t, GLsizei_t, GLsizei_t, GLenum_t, GLenum_t, const void*) = nullptr;
    // VAO is optional (absent on plain GLES2 / old GL): then we just rebind the buffer every frame
    void (*GenVertexArrays)(GLsizei_t, GLuint_t*) = nullptr;
    void (*BindSampler)(GLuint_t, GLuint_t) = nullptr;   // GLES3 only; optional
    void (*BindVertexArray)(GLuint_t) = nullptr;
    void (*ColorMask)(GLboolean_t, GLboolean_t, GLboolean_t, GLboolean_t) = nullptr;
} zc;
template <class T> inline bool load(T& f, const char* name) { f = (T)SDL_GL_GetProcAddress(name); return f != nullptr; }
inline bool load_zc() {
    bool ok = true;
    ok &= load(zc.CreateShader, "glCreateShader"); ok &= load(zc.ShaderSource, "glShaderSource"); ok &= load(zc.CompileShader, "glCompileShader");
    ok &= load(zc.GetShaderiv, "glGetShaderiv"); ok &= load(zc.GetShaderInfoLog, "glGetShaderInfoLog"); ok &= load(zc.DeleteShader, "glDeleteShader");
    ok &= load(zc.CreateProgram, "glCreateProgram"); ok &= load(zc.AttachShader, "glAttachShader"); ok &= load(zc.BindAttribLocation, "glBindAttribLocation");
    ok &= load(zc.LinkProgram, "glLinkProgram"); ok &= load(zc.GetProgramiv, "glGetProgramiv"); ok &= load(zc.UseProgram, "glUseProgram");
    ok &= load(zc.GetUniformLocation, "glGetUniformLocation"); ok &= load(zc.Uniform1i, "glUniform1i");
    ok &= load(zc.Uniform1f, "glUniform1f"); ok &= load(zc.Uniform2f, "glUniform2f"); ok &= load(zc.Uniform4f, "glUniform4f");
    ok &= load(zc.GenBuffers, "glGenBuffers"); ok &= load(zc.BindBuffer, "glBindBuffer"); ok &= load(zc.BufferData, "glBufferData");
    ok &= load(zc.BufferSubData, "glBufferSubData"); ok &= load(zc.EnableVertexAttribArray, "glEnableVertexAttribArray");
    ok &= load(zc.DisableVertexAttribArray, "glDisableVertexAttribArray"); ok &= load(zc.VertexAttribPointer, "glVertexAttribPointer");
    ok &= load(zc.DrawArrays, "glDrawArrays"); ok &= load(zc.ClearColor, "glClearColor"); ok &= load(zc.Clear, "glClear");
    ok &= load(zc.Enable, "glEnable"); ok &= load(zc.Disable, "glDisable"); ok &= load(zc.BlendFunc, "glBlendFunc");
    ok &= load(zc.ActiveTexture, "glActiveTexture"); ok &= load(zc.TexSubImage2D, "glTexSubImage2D"); ok &= load(zc.ColorMask, "glColorMask");
    load(zc.GenVertexArrays, "glGenVertexArrays"); load(zc.BindVertexArray, "glBindVertexArray");
    load(zc.BindSampler, "glBindSampler");
    if (!zc.GenVertexArrays || !zc.BindVertexArray) zc.GenVertexArrays = nullptr, zc.BindVertexArray = nullptr;
    return ok;
}
inline bool load_all() {
    bool ok = true;
    ok &= load(fn.GenFramebuffers, "glGenFramebuffers"); ok &= load(fn.BindFramebuffer, "glBindFramebuffer");
    ok &= load(fn.GenTextures, "glGenTextures"); ok &= load(fn.BindTexture, "glBindTexture");
    ok &= load(fn.TexImage2D, "glTexImage2D"); ok &= load(fn.TexParameteri, "glTexParameteri");
    ok &= load(fn.FramebufferTexture2D, "glFramebufferTexture2D"); ok &= load(fn.GenRenderbuffers, "glGenRenderbuffers");
    ok &= load(fn.BindRenderbuffer, "glBindRenderbuffer"); ok &= load(fn.RenderbufferStorage, "glRenderbufferStorage");
    ok &= load(fn.FramebufferRenderbuffer, "glFramebufferRenderbuffer"); ok &= load(fn.CheckFramebufferStatus, "glCheckFramebufferStatus");
    ok &= load(fn.ReadPixels, "glReadPixels"); ok &= load(fn.PixelStorei, "glPixelStorei"); ok &= load(fn.Viewport, "glViewport");
    ok &= load(fn.DeleteFramebuffers, "glDeleteFramebuffers"); ok &= load(fn.DeleteTextures, "glDeleteTextures");
    ok &= load(fn.DeleteRenderbuffers, "glDeleteRenderbuffers");
    return ok;
}
} // namespace gl

// ---------- per-second timing (printed with --debug-input): where does a frame's time go? ----------
struct Perf {
    double run = 0, vcb = 0, read = 0, upload = 0, draw = 0, swap = 0;   // accumulated milliseconds
    unsigned n = 0;
} P;
inline double now_ms() { return (double)SDL_GetPerformanceCounter() * 1000.0 / (double)SDL_GetPerformanceFrequency(); }

uintptr_t hw_get_fbo() { return (uintptr_t)F.hw_fbo; }
retro_proc_address_t hw_get_proc(const char* sym) { return (retro_proc_address_t)SDL_GL_GetProcAddress(sym); }

// The core's GL context must be current whenever we call into the core (SDL's renderer switches to its own one while presenting).
void hw_enter() {
    if (!F.hw_ctx) return;
    SDL_GL_MakeCurrent(F.win, F.hw_ctx);
    if (F.hw_fbo) { gl::fn.BindFramebuffer(gl::FRAMEBUFFER, F.hw_fbo); gl::fn.Viewport(0, 0, (int)F.hw_w, (int)F.hw_h); }
}

bool hw_alloc_fbo(unsigned w, unsigned h) {
    if (F.hw_fbo) { gl::fn.DeleteFramebuffers(1, &F.hw_fbo); gl::fn.DeleteTextures(1, &F.hw_tex); if (F.hw_rb) gl::fn.DeleteRenderbuffers(1, &F.hw_rb); F.hw_fbo = F.hw_tex = F.hw_rb = 0; }
    gl::fn.GenFramebuffers(1, &F.hw_fbo); gl::fn.BindFramebuffer(gl::FRAMEBUFFER, F.hw_fbo);
    gl::fn.GenTextures(1, &F.hw_tex); gl::fn.BindTexture(gl::TEXTURE_2D, F.hw_tex);
    gl::fn.TexImage2D(gl::TEXTURE_2D, 0, (int)gl::RGBA, (int)w, (int)h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    const int filt = F.zc ? (int)gl::LINEAR : (int)gl::NEAREST;   // zero-copy: the quad shader samples this texture, bilinear looks right when scaled to the screen
    gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filt); gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filt);
    if (F.zc) { gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, (int)gl::CLAMP_TO_EDGE); gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, (int)gl::CLAMP_TO_EDGE); }
    gl::fn.FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, F.hw_tex, 0);
    if (F.hw.depth) {
        gl::fn.GenRenderbuffers(1, &F.hw_rb); gl::fn.BindRenderbuffer(gl::RENDERBUFFER, F.hw_rb);
        gl::fn.RenderbufferStorage(gl::RENDERBUFFER, F.hw.stencil ? gl::DEPTH24_STENCIL8 : gl::DEPTH_COMPONENT16, (int)w, (int)h);
        gl::fn.FramebufferRenderbuffer(gl::FRAMEBUFFER, F.hw.stencil ? gl::DEPTH_STENCIL_ATTACHMENT : gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, F.hw_rb);
    }
    F.hw_w = w; F.hw_h = h;
    const bool ok = gl::fn.CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    if (!ok) plog("poniaplay: HW framebuffer %ux%u incomplete\n", w, h);
    return ok;
}

// Build the shader program + quad buffer for the zero-copy present. Returns false (and logs why) if anything is unavailable;
// the caller then keeps the old read-back path. Needs the core's GL context current.
bool zc_compile(unsigned type, const char* src, unsigned& out) {
    out = gl::zc.CreateShader(type);
    gl::zc.ShaderSource(out, 1, &src, nullptr);
    gl::zc.CompileShader(out);
    int ok = 0; gl::zc.GetShaderiv(out, gl::COMPILE_STATUS, &ok);
    if (!ok) { char log[512] = {0}; gl::zc.GetShaderInfoLog(out, sizeof log - 1, nullptr, log); plog("poniaplay: zero-copy shader error: %s\n", log); }
    return ok != 0;
}
bool zc_init() {
    if (F.zc_off) { plog("poniaplay: zero-copy present disabled (--no-zerocopy)\n"); return false; }
    if (!gl::load_zc()) { plog("poniaplay: zero-copy present: GL entry points missing, using read-back\n"); return false; }
    // GLSL that is valid on both GLES2/3 and desktop compatibility profiles (no #version line)
    static const char* vs =
        "attribute vec2 aPos; attribute vec2 aUV; varying vec2 vUV; varying vec4 vNS; varying vec4 vEW;\n"
        "uniform vec2 uTexel;\n"
        // neighbour coordinates come from the vertex stage (not computed per pixel): Mali can then prefetch them (no dependent texture reads)
        "void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUV = aUV;\n"
        "  vNS = vec4(aUV.x, aUV.y - uTexel.y, aUV.x, aUV.y + uTexel.y); vEW = vec4(aUV.x + uTexel.x, aUV.y, aUV.x - uTexel.x, aUV.y); }\n";
    static const char* fs =
        "#ifdef GL_ES\nprecision mediump float;\n#endif\n"
        "varying vec2 vUV; varying vec4 vNS; varying vec4 vEW; uniform sampler2D uTex;\n"
        "uniform float uSharp;\n"   // uTexel lives in the vertex stage only (a uniform shared by both stages must have equal precision). Edge pixels may read 1 texel outside the picture; the min/max clamp below limits the damage
        "void main() {\n"
        "  vec4 c = texture2D(uTex, vUV);\n"
        "  if (uSharp > 0.0) {\n"
        "    vec3 n = texture2D(uTex, vNS.xy).rgb;\n"
        "    vec3 s = texture2D(uTex, vNS.zw).rgb;\n"
        "    vec3 e = texture2D(uTex, vEW.xy).rgb;\n"
        "    vec3 w = texture2D(uTex, vEW.zw).rgb;\n"
        "    vec3 mn = min(min(min(n, s), min(e, w)), c.rgb), mx = max(max(max(n, s), max(e, w)), c.rgb);\n"
        "    vec3 r = c.rgb + (c.rgb - (n + s + e + w) * 0.25) * (uSharp * 2.0);\n"
        "    c.rgb = clamp(r, mn, mx);\n"   // clamped to the local min/max like CAS: sharpens edges without halos/ringing
        "  }\n"
        "  gl_FragColor = c;\n"
        "}\n";
    unsigned v = 0, f = 0;
    if (!zc_compile(gl::VERTEX_SHADER, vs, v) || !zc_compile(gl::FRAGMENT_SHADER, fs, f)) return false;
    F.zc_prog = gl::zc.CreateProgram();
    gl::zc.AttachShader(F.zc_prog, v); gl::zc.AttachShader(F.zc_prog, f);
    gl::zc.BindAttribLocation(F.zc_prog, 0, "aPos"); gl::zc.BindAttribLocation(F.zc_prog, 1, "aUV");
    gl::zc.LinkProgram(F.zc_prog);
    int ok = 0; gl::zc.GetProgramiv(F.zc_prog, gl::LINK_STATUS, &ok);
    gl::zc.DeleteShader(v); gl::zc.DeleteShader(f);
    if (!ok) { plog("poniaplay: zero-copy shader link failed, using read-back\n"); return false; }
    F.zc_loc_tex = gl::zc.GetUniformLocation(F.zc_prog, "uTex");
    F.zc_loc_sharp = gl::zc.GetUniformLocation(F.zc_prog, "uSharp");
    F.zc_loc_texel = gl::zc.GetUniformLocation(F.zc_prog, "uTexel");
    F.zc_loc_win = gl::zc.GetUniformLocation(F.zc_prog, "uWin");
    if (gl::zc.GenVertexArrays) { gl::zc.GenVertexArrays(1, &F.zc_vao); gl::zc.BindVertexArray(F.zc_vao); }
    gl::zc.GenBuffers(1, &F.zc_vbo);
    gl::zc.BindBuffer(gl::ARRAY_BUFFER, F.zc_vbo);
    gl::zc.BufferData(gl::ARRAY_BUFFER, 16 * sizeof(float), nullptr, gl::STREAM_DRAW);   // 4 vertices x (x, y, u, v)
    if (gl::zc.BindVertexArray) gl::zc.BindVertexArray(0);
    gl::zc.BindBuffer(gl::ARRAY_BUFFER, 0);
    return true;
}

// After the SDL window/renderer exist: create the core's GL context + offscreen FBO, then tell the core (context_reset).
bool hw_setup(const retro_system_av_info& av) {
    const auto t = F.hw.context_type;
    const bool es = (t == RETRO_HW_CONTEXT_OPENGLES2 || t == RETRO_HW_CONTEXT_OPENGLES3 || t == RETRO_HW_CONTEXT_OPENGLES_VERSION);
    int maj = (int)F.hw.version_major, min = (int)F.hw.version_minor;
    if (t == RETRO_HW_CONTEXT_OPENGLES2) { maj = 2; min = 0; }
    else if (t == RETRO_HW_CONTEXT_OPENGLES3) { maj = 3; min = 0; }
    else if (t == RETRO_HW_CONTEXT_OPENGL) { maj = 2; min = 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, es ? SDL_GL_CONTEXT_PROFILE_ES : (t == RETRO_HW_CONTEXT_OPENGL_CORE ? SDL_GL_CONTEXT_PROFILE_CORE : SDL_GL_CONTEXT_PROFILE_COMPATIBILITY));
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, maj); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, min);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    F.hw_ctx = SDL_GL_CreateContext(F.win);
    if (!F.hw_ctx) { plog("poniaplay: cannot create GL context (%d.%d %s): %s\n", maj, min, es ? "ES" : "GL", SDL_GetError()); return false; }
    SDL_GL_MakeCurrent(F.win, F.hw_ctx);
    if (!gl::load_all()) { plog("poniaplay: GL functions missing\n"); return false; }
    F.zc = zc_init();
    if (F.zc) { SDL_GL_SetSwapInterval(1); plog("poniaplay: zero-copy present ON (core texture drawn straight to the window)\n"); }
    unsigned w =av.geometry.max_width ? av.geometry.max_width : av.geometry.base_width;
    unsigned h = av.geometry.max_height ? av.geometry.max_height : av.geometry.base_height;
    if (!hw_alloc_fbo(w, h)) return false;
    plog("poniaplay: HW render ready (%s %d.%d, FBO %ux%u)\n", es ? "GLES" : "GL", maj, min, w, h);
    if (F.hw.context_reset) F.hw.context_reset();
    hw_enter();
    return true;
}

void hw_teardown() {
    if (!F.hw_ctx) return;
    hw_enter();
    if (F.hw.context_destroy) F.hw.context_destroy();
    if (F.hud_ren) { SDL_DestroyRenderer(F.hud_ren); F.hud_ren = nullptr; }
    if (F.hud_surf) { SDL_FreeSurface(F.hud_surf); F.hud_surf = nullptr; }
    if (F.menu_ren) { SDL_DestroyRenderer(F.menu_ren); F.menu_ren = nullptr; }
    if (F.menu_surf) { SDL_FreeSurface(F.menu_surf); F.menu_surf = nullptr; }
    SDL_GL_MakeCurrent(F.win, nullptr);
    SDL_GL_DeleteContext(F.hw_ctx); F.hw_ctx = nullptr;
}

// ---------- environment ----------
bool env_cb(unsigned cmd, void* data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool*)data = true; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        auto f = *(const retro_pixel_format*)data;
        if (f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565) return false;
        F.fmt = f; return true; }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: *(const char**)data = F.system_dir.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char**)data = F.save_dir.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((retro_log_callback*)data)->log = log_cb; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned*)data = 0; return true;   // ask for legacy SET_VARIABLES
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        for (auto* v = (const retro_variable*)data; v && v->key; ++v) {
            std::string val = v->value ? v->value : "";   // "Description; first|second|..."
            if (F.debug_input) plog("opt: %s = %s\n", v->key, val.c_str());   // --debug-input: list every core option with its values (first = default)
            size_t p = val.find("; ");
            if (p != std::string::npos) { val = val.substr(p + 2); size_t q = val.find('|'); if (q != std::string::npos) val = val.substr(0, q); }
            F.opt_default[v->key] = val;
        }
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* v = (retro_variable*)data;
        auto u = F.opt_user.find(v->key);
        if (u != F.opt_user.end()) { v->value = u->second.c_str(); return true; }
        auto d = F.opt_default.find(v->key);
        if (d != F.opt_default.end()) { v->value = d->second.c_str(); return true; }
        return false; }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool*)data = F.var_update; F.var_update = false; return true;
    case RETRO_ENVIRONMENT_SET_ROTATION: F.rotation = *(const unsigned*)data; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY: {
        auto* g = (const retro_game_geometry*)data;
        F.width = g->base_width; F.height = g->base_height; F.aspect = g->aspect_ratio; return true; }
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        auto* a = (const retro_system_av_info*)data;
        F.width = a->geometry.base_width; F.height = a->geometry.base_height; F.aspect = a->geometry.aspect_ratio;
        F.fps = a->timing.fps; return true; }
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
#ifdef __ANDROID__
        *(unsigned*)data = RETRO_HW_CONTEXT_OPENGLES3;
#else
        *(unsigned*)data = RETRO_HW_CONTEXT_OPENGL_CORE;
#endif
        return true;
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
        auto* cb = (retro_hw_render_callback*)data;
        bool ok = (cb->context_type == RETRO_HW_CONTEXT_OPENGLES2 || cb->context_type == RETRO_HW_CONTEXT_OPENGLES3 || cb->context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION);
#ifndef __ANDROID__
        ok = ok || cb->context_type == RETRO_HW_CONTEXT_OPENGL || cb->context_type == RETRO_HW_CONTEXT_OPENGL_CORE;
#endif
        if (!ok) { plog("poniaplay: unsupported HW context type %d\n", (int)cb->context_type); return false; }
        cb->get_current_framebuffer = hw_get_fbo;
        cb->get_proc_address = hw_get_proc;
        F.hw = *cb; F.hw_req = true;
        return true; }
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *(unsigned*)data = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY: return true;
    default: return false;   // incl. SET_HW_RENDER (hardware cores unsupported for now)
    }
}

// ---------- video ----------

// 5x7 bitmap font (uppercase letters used by the menu, digits, ':' and space). Each glyph = 7 rows of 5 bits (MSB = left).
const uint8_t* glyph(char c) {
    static const uint8_t SP[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t COLON[7] = {0, 4, 0, 0, 4, 0, 0};
    static const uint8_t L[26][7] = {
        {14, 17, 17, 31, 17, 17, 17},   // A
        {30, 17, 17, 30, 17, 17, 30},   // B
        {14, 17, 16, 16, 16, 17, 14},   // C
        {30, 17, 17, 17, 17, 17, 30},   // D
        {31, 16, 16, 30, 16, 16, 31},   // E
        {31, 16, 16, 30, 16, 16, 16},   // F
        {14, 17, 16, 23, 17, 17, 15},   // G
        {17, 17, 17, 31, 17, 17, 17},   // H
        {14, 4, 4, 4, 4, 4, 14},        // I
        {7, 2, 2, 2, 2, 18, 12},        // J
        {17, 18, 20, 24, 20, 18, 17},   // K
        {16, 16, 16, 16, 16, 16, 31},   // L
        {17, 27, 21, 21, 17, 17, 17},   // M
        {17, 25, 21, 19, 17, 17, 17},   // N
        {14, 17, 17, 17, 17, 17, 14},   // O
        {30, 17, 17, 30, 16, 16, 16},   // P
        {14, 17, 17, 17, 21, 18, 13},   // Q
        {30, 17, 17, 30, 20, 18, 17},   // R
        {15, 16, 16, 14, 1, 1, 30},     // S
        {31, 4, 4, 4, 4, 4, 4},         // T
        {17, 17, 17, 17, 17, 17, 14},   // U
        {17, 17, 17, 17, 17, 10, 4},    // V
        {17, 17, 17, 21, 21, 27, 17},   // W
        {17, 17, 10, 4, 10, 17, 17},    // X
        {17, 17, 10, 4, 4, 4, 4},       // Y
        {31, 1, 2, 4, 8, 16, 31},       // Z
    };
    static const uint8_t D[10][7] = {
        {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14}, {14, 17, 1, 2, 4, 8, 31}, {31, 2, 4, 2, 1, 17, 14}, {2, 6, 10, 18, 31, 2, 2},
        {31, 16, 30, 1, 1, 17, 14}, {6, 8, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8}, {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}};
    if (c >= '0' && c <= '9') return D[c - '0'];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z') return L[c - 'A'];
    if (c == ':') return COLON;
    return SP;
}

void draw_text(int x, int y, int s, const char* t) {
    for (; *t; ++t, x += 6 * s) {
        const uint8_t* g = glyph(*t);
        for (int r = 0; r < 7; ++r) for (int c = 0; c < 5; ++c)
            if (g[r] & (1 << (4 - c))) { SDL_Rect q{x + c * s, y + r * s, s, s}; SDL_RenderFillRect(F.ren, &q); }
    }
}

const int MENU_ITEMS = 6;
const char* menu_label(int i, char* buf, size_t n) {
    switch (i) {
    case 0: return "RESUME";
    case 1: return "SAVE STATE";
    case 2: return "LOAD STATE";
    case 3: return "RESET";
    case 4: std::snprintf(buf, n, "SHOW FPS: %s", F.show_fps ? "ON" : "OFF"); return buf;
    default: return "QUIT";
    }
}

void osd_show(const char* t) { F.osd = t; F.osd_until = SDL_GetTicks() + 1500; }

// HUD: every on-screen hint (FPS, resolution, save/load, fast-forward, rewind) uses ONE style - white 5x7 text on a
// translucent plate, right-aligned in the top-right corner, one line per message, always "NAME VALUE" in capitals.
void hud_collect(std::vector<std::string>& lines) {
    if (F.show_fps) { char b[24]; std::snprintf(b, sizeof b, "FPS %d", (int)(F.fps_now + 0.5)); lines.push_back(b); }
    if (F.ff) lines.push_back("@FF");        // two right triangles
    else if (F.rew) lines.push_back("@REW");  // two left triangles
    if (F.osd_until && SDL_GetTicks() < F.osd_until && !F.osd.empty()) lines.push_back(F.osd);
}
inline int hud_scale(int oh) { return oh / 240 < 2 ? 2 : oh / 240; }
// pixel size of the HUD block for these lines (matches the layout in hud_paint)
void hud_extent(const std::vector<std::string>& lines, int s, int& w, int& h) {
    const int pad = 2 * s, margin = 3 * s;
    int maxw = 0;
    for (const std::string& t : lines) {
        const bool tri = (t == "@FF" || t == "@REW");
        const int tw = tri ? 9 * s : (int)t.size() * 6 * s - s;
        if (tw > maxw) maxw = tw;
    }
    w = maxw + 2 * pad + margin + s;
    h = margin + (int)lines.size() * (7 * s + 2 * pad + s) + s;
}
// Paint the HUD lines right-aligned into a target `ow` pixels wide (the whole window, or just the HUD strip).
void hud_paint(const std::vector<std::string>& lines, int ow, int s) {
    const int pad = 2 * s, margin = 3 * s;
    SDL_SetRenderDrawBlendMode(F.ren, SDL_BLENDMODE_BLEND);
    int y = margin;
    for (const std::string& t : lines) {
        const bool tri = (t == "@FF" || t == "@REW");
        const int w = tri ? 9 * s : (int)t.size() * 6 * s - s;     // two triangles = 4 + 1 gap + 4 cells
        SDL_Rect plate{ow - margin - w - 2 * pad, y, w + 2 * pad, 7 * s + 2 * pad};
        SDL_SetRenderDrawColor(F.ren, 0, 0, 0, 150);
        SDL_RenderFillRect(F.ren, &plate);
        SDL_SetRenderDrawColor(F.ren, 255, 255, 255, 255);
        if (tri) {
            const bool right = (t == "@FF");
            for (int k = 0; k < 2; ++k) {
                const int x0 = plate.x + pad + k * 5 * s;
                for (int r = 0; r < 7; ++r) {
                    const int len = 4 - (r > 3 ? r - 3 : 3 - r);                 // 1..4 cells, widest at the middle row
                    SDL_Rect q{right ? x0 : x0 + (4 - len) * s, y + pad + r * s, len * s, s};
                    SDL_RenderFillRect(F.ren, &q);
                }
            }
        } else draw_text(plate.x + pad, y + pad, s, t.c_str());
        y += plate.h + s;
    }
    SDL_SetRenderDrawBlendMode(F.ren, SDL_BLENDMODE_NONE);
}
void draw_hud(int ow, int oh) {
    std::vector<std::string> lines;
    hud_collect(lines);
    if (lines.empty()) return;
    hud_paint(lines, ow, hud_scale(oh));
}

void draw_menu(int ow, int oh) {
    SDL_SetRenderDrawBlendMode(F.ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(F.ren, 0, 0, 0, 150);
    SDL_Rect all{0, 0, ow, oh}; SDL_RenderFillRect(F.ren, &all);

    const int s = oh / 150 < 2 ? 2 : oh / 150;       // pixel scale
    const int rowh = 11 * s, w = 20 * 6 * s, h = (MENU_ITEMS + 2) * rowh;
    SDL_Rect panel{(ow - w) / 2, (oh - h) / 2, w, h};
    SDL_SetRenderDrawColor(F.ren, 28, 30, 38, 235); SDL_RenderFillRect(F.ren, &panel);
    SDL_SetRenderDrawColor(F.ren, 255, 255, 255, 60); SDL_RenderDrawRect(F.ren, &panel);

    SDL_SetRenderDrawColor(F.ren, 255, 255, 255, 255);
    draw_text(panel.x + 3 * s, panel.y + rowh / 2, s, "MENU");
    for (int i = 0; i < MENU_ITEMS; ++i) {
        char buf[32]; const char* lab = menu_label(i, buf, sizeof buf);
        SDL_Rect row{panel.x + 2 * s, panel.y + (i + 2) * rowh - s, w - 4 * s, rowh - s};
        const bool sel = (i == F.menu_sel);
        if (sel) { SDL_SetRenderDrawColor(F.ren, 255, 200, 80, 255); SDL_RenderFillRect(F.ren, &row); }
        SDL_SetRenderDrawColor(F.ren, sel ? 30 : 235, sel ? 30 : 235, sel ? 36 : 240, 255);
        draw_text(row.x + 2 * s, row.y + (row.h - 7 * s) / 2, s, lab);
    }
    SDL_SetRenderDrawBlendMode(F.ren, SDL_BLENDMODE_NONE);
}

// ---------- zero-copy present: draw the core's FBO texture straight to the window ----------
void zc_ndc(float px, float py, int ow, int oh, float* out) { out[0] = 2.f * px / (float)ow - 1.f; out[1] = 1.f - 2.f * py / (float)oh; }

// one textured quad (TRIANGLE_STRIP order TL, TR, BL, BR); v = 4 x (x, y, u, v)
void zc_draw(const float* v) {
    gl::zc.BindBuffer(gl::ARRAY_BUFFER, F.zc_vbo);
    gl::zc.BufferSubData(gl::ARRAY_BUFFER, 0, 16 * sizeof(float), v);
    gl::zc.VertexAttribPointer(0, 2, gl::FLOAT, 0, 4 * sizeof(float), (const void*)0);
    gl::zc.VertexAttribPointer(1, 2, gl::FLOAT, 0, 4 * sizeof(float), (const void*)(2 * sizeof(float)));
    gl::zc.EnableVertexAttribArray(0); gl::zc.EnableVertexAttribArray(1);
    gl::zc.DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
}

// axis-aligned pixel rectangle covering the texture 0..1 (top row of the image = first uploaded row)
void zc_draw_rect(int ow, int oh, int x, int y, int w, int h) {
    float v[16];
    const float px[4] = {(float)x, (float)(x + w), (float)x, (float)(x + w)}, py[4] = {(float)y, (float)y, (float)(y + h), (float)(y + h)};
    const float tu[4] = {0.f, 1.f, 0.f, 1.f}, tv[4] = {0.f, 0.f, 1.f, 1.f};
    for (int i = 0; i < 4; ++i) { zc_ndc(px[i], py[i], ow, oh, &v[i * 4]); v[i * 4 + 2] = tu[i]; v[i * 4 + 3] = tv[i]; }
    zc_draw(v);
}

// (re)create a CPU overlay surface + software renderer of the given size
void zc_overlay_surface(SDL_Surface*& surf, SDL_Renderer*& ren, int w, int h) {
    if (surf && surf->w == w && surf->h == h) return;
    if (ren) SDL_DestroyRenderer(ren);
    if (surf) SDL_FreeSurface(surf);
    surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);   // bytes R,G,B,A = GL_RGBA / GL_UNSIGNED_BYTE
    ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
}
void zc_overlay_clear(SDL_Renderer* ren) {
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);
}
// upload the surface into its texture (allocate on first use / size change, otherwise update in place)
void zc_overlay_upload(unsigned& tex, int& tw, int& th, SDL_Surface* s) {
    if (!tex) {
        gl::fn.GenTextures(1, &tex); gl::fn.BindTexture(gl::TEXTURE_2D, tex);
        gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, (int)gl::NEAREST); gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, (int)gl::NEAREST);
        gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, (int)gl::CLAMP_TO_EDGE); gl::fn.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, (int)gl::CLAMP_TO_EDGE);
        tw = th = 0;
    }
    gl::fn.BindTexture(gl::TEXTURE_2D, tex);
    gl::fn.PixelStorei(0x0CF5 /*UNPACK_ALIGNMENT*/, 4);
    if (tw != s->w || th != s->h) { gl::fn.TexImage2D(gl::TEXTURE_2D, 0, (int)gl::RGBA, s->w, s->h, 0, gl::RGBA, gl::UNSIGNED_BYTE, s->pixels); tw = s->w; th = s->h; }
    else gl::zc.TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, s->w, s->h, gl::RGBA, gl::UNSIGNED_BYTE, s->pixels);
}

void present_zc() {
    int ow = 0, oh = 0; SDL_GL_GetDrawableSize(F.win, &ow, &oh);
    if (ow <= 0 || oh <= 0 || !F.zc_w || !F.zc_h || !F.hw_tex) return;
    const double t0 = now_ms();
    auto& g = gl::fn; auto& z = gl::zc;

    // ---- where the picture goes: same letterbox + rotation rules as the SDL path ----
    float asp = F.aspect > 0 ? F.aspect : (float)F.zc_w / (float)F.zc_h;
    const unsigned rot = F.rotation & 3;   // libretro: each unit = 90 degrees counter-clockwise
    if ((rot & 1) && asp > 1.f) asp = 1.f / asp;
    int dh = oh, dw = (int)(oh * asp);
    if (dw > ow) { dw = ow; dh = (int)(ow / asp); }
    const int rw = (rot & 1) ? dh : dw, rh = (rot & 1) ? dw : dh;     // the rectangle before rotating
    static const int COS[4] = {1, 0, -1, 0}, SIN[4] = {0, 1, 0, -1};
    const float c = (float)COS[rot], sn = (float)SIN[rot];
    const float hx = rw * 0.5f, hy = rh * 0.5f, cx = ow * 0.5f, cy = oh * 0.5f;
    const float kx[4] = {-hx, hx, -hx, hx}, ky[4] = {-hy, -hy, hy, hy};   // TL TR BL BR, y pointing down
    // texture window: the frame sits in the lower-left w x h corner of the (larger) FBO texture; half-texel inset keeps bilinear from bleeding
    const float u0 = 0.5f / (float)F.hw_w, u1 = ((float)F.zc_w - 0.5f) / (float)F.hw_w;
    const float vlo = 0.5f / (float)F.hw_h, vhi = ((float)F.zc_h - 0.5f) / (float)F.hw_h;
    const float vt = F.hw.bottom_left_origin ? vhi : vlo, vb = F.hw.bottom_left_origin ? vlo : vhi;   // v of the image's top / bottom row
    const float tu[4] = {u0, u1, u0, u1}, tv[4] = {vt, vt, vb, vb};
    float quad[16];
    for (int i = 0; i < 4; ++i) {
        const float px = cx + kx[i] * c + ky[i] * sn, py = cy - kx[i] * sn + ky[i] * c;   // rotate counter-clockwise on screen
        zc_ndc(px, py, ow, oh, &quad[i * 4]); quad[i * 4 + 2] = tu[i]; quad[i * 4 + 3] = tv[i];
    }

    // ---- GL state: the core may have left anything enabled; set exactly what the quad needs ----
    g.BindFramebuffer(gl::FRAMEBUFFER, 0);
    g.Viewport(0, 0, ow, oh);
    z.Disable(gl::DEPTH_TEST); z.Disable(gl::SCISSOR_TEST); z.Disable(gl::CULL_FACE); z.Disable(gl::STENCIL_TEST); z.Disable(gl::BLEND);
    z.ColorMask(1, 1, 1, 1);
    z.ClearColor(0.f, 0.f, 0.f, 1.f); z.Clear(gl::COLOR_BUFFER_BIT);
    z.UseProgram(F.zc_prog); z.Uniform1i(F.zc_loc_tex, 0);
    z.Uniform1f(F.zc_loc_sharp, F.sharpen);
    z.Uniform2f(F.zc_loc_texel, 1.f / (float)F.hw_w, 1.f / (float)F.hw_h);
    z.Uniform4f(F.zc_loc_win, std::min(u0, u1), std::min(vlo, vhi), std::max(u0, u1), std::max(vlo, vhi));
    z.ActiveTexture(gl::TEXTURE0);
    if (z.BindSampler) z.BindSampler(0, 0);   // Dolphin leaves a sampler object (mipmap filter) bound on unit 0 → our non-mipmapped texture becomes "incomplete" = black
    if (z.BindVertexArray) z.BindVertexArray(F.zc_vao);
    g.BindTexture(gl::TEXTURE_2D, F.hw_tex);
    g.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, (int)gl::LINEAR); g.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, (int)gl::LINEAR);   // a core may have changed them
    zc_draw(quad);
    z.Uniform1f(F.zc_loc_sharp, 0.f);   // HUD / menu overlays go through the same program: never sharpen them

    // ---- overlays (HUD, menu): painted on the CPU only when their content changes ----
    z.Enable(gl::BLEND); z.BlendFunc(gl::ONE, gl::ONE_MINUS_SRC_ALPHA);   // the software renderer leaves premultiplied colours
    std::vector<std::string> lines; hud_collect(lines);
    if (!lines.empty()) {
        const int s = hud_scale(oh);
        int hw_ = 0, hh_ = 0; hud_extent(lines, s, hw_, hh_);
        if (hw_ > ow) hw_ = ow;
        std::string key = std::to_string(ow) + "x" + std::to_string(oh) + "\n";
        for (const std::string& t : lines) { key += t; key += '\n'; }
        if (key != F.hud_key) {
            zc_overlay_surface(F.hud_surf, F.hud_ren, hw_, hh_);
            if (F.hud_surf && F.hud_ren) {
                SDL_Renderer* saved = F.ren; F.ren = F.hud_ren;
                zc_overlay_clear(F.ren);
                hud_paint(lines, hw_, s);
                F.ren = saved;
                zc_overlay_upload(F.hud_tex, F.hud_tw, F.hud_th, F.hud_surf);
                F.hud_key = key; F.hud_wpx = hw_; F.hud_hpx = hh_;
            }
        }
        if (F.hud_tex && F.hud_key == key) { g.BindTexture(gl::TEXTURE_2D, F.hud_tex); zc_draw_rect(ow, oh, ow - F.hud_wpx, 0, F.hud_wpx, F.hud_hpx); }
    } else F.hud_key.clear();
    if (F.menu) {
        const long long key = (long long)F.menu_sel | (F.show_fps ? 0x10LL : 0LL) | ((long long)ow << 8) | ((long long)oh << 28);
        if (key != F.menu_key) {
            zc_overlay_surface(F.menu_surf, F.menu_ren, ow, oh);
            if (F.menu_surf && F.menu_ren) {
                SDL_Renderer* saved = F.ren; F.ren = F.menu_ren;
                zc_overlay_clear(F.ren);
                draw_menu(ow, oh);
                F.ren = saved;
                zc_overlay_upload(F.menu_tex, F.menu_tw, F.menu_th, F.menu_surf);
                F.menu_key = key;
            }
        }
        if (F.menu_tex && F.menu_key == key) { g.BindTexture(gl::TEXTURE_2D, F.menu_tex); zc_draw_rect(ow, oh, 0, 0, ow, oh); }
    } else F.menu_key = -1;
    z.Disable(gl::BLEND);

    // leave things tidy for the core (it sets what it needs; RetroArch does the same)
    z.DisableVertexAttribArray(0); z.DisableVertexAttribArray(1);
    if (z.BindVertexArray) z.BindVertexArray(0);
    z.BindBuffer(gl::ARRAY_BUFFER, 0);
    z.UseProgram(0);
    g.BindTexture(gl::TEXTURE_2D, 0);

    const double t1 = now_ms(); P.draw += t1 - t0;
    SDL_GL_SwapWindow(F.win);   // vsync: this is where waiting for the display shows up
    P.swap += now_ms() - t1;
}

void present() {
    if (F.zc) { present_zc(); return; }
    if (!F.tex) return;
    int ow, oh; SDL_GetRendererOutputSize(F.ren, &ow, &oh);
    float asp = F.aspect > 0 ? F.aspect : (float)F.tex_w / (float)F.tex_h;
    const unsigned rot = F.rotation & 3;   // libretro: each unit = 90 degrees counter-clockwise
    if ((rot & 1) && asp > 1.f) asp = 1.f / asp;   // sideways content: the displayed picture is portrait
    int dh = oh, dw = (int)(oh * asp);
    if (dw > ow) { dw = ow; dh = (int)(ow / asp); }
    SDL_SetRenderDrawColor(F.ren, 0, 0, 0, 255);
    SDL_RenderClear(F.ren);
    if (rot == 0) {
        SDL_Rect dst{(ow - dw) / 2, (oh - dh) / 2, dw, dh};
        SDL_RenderCopy(F.ren, F.tex, nullptr, &dst);
    } else {
        // destination rect is the unrotated one (w/h swapped for 90/270); SDL rotates it about its centre, clockwise degrees
        const int rw = (rot & 1) ? dh : dw, rh = (rot & 1) ? dw : dh;
        SDL_Rect dst{(ow - rw) / 2, (oh - rh) / 2, rw, rh};
        SDL_RenderCopyEx(F.ren, F.tex, nullptr, &dst, -90.0 * rot, nullptr, SDL_FLIP_NONE);
    }
    draw_hud(ow, oh);
    if (F.menu) draw_menu(ow, oh);
    SDL_RenderPresent(F.ren);
}

// Read the core's FBO back into CPU memory as top-down RGBA rows. Only the old (non zero-copy) path and --shot use this.
const void* hw_read_frame(unsigned w, unsigned h, size_t& pitch) {
    const size_t row = (size_t)w * 4;
    F.hw_pix.resize(row * h);
    gl::fn.BindFramebuffer(gl::FRAMEBUFFER, F.hw_fbo);
    gl::fn.PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::fn.ReadPixels(0, 0, (int)w, (int)h, gl::RGBA, gl::UNSIGNED_BYTE, F.hw_pix.data());
    pitch = row;
    if (!F.hw.bottom_left_origin) return F.hw_pix.data();
    F.hw_flip.resize(row * h);   // GL rows run bottom-up: flip
    for (unsigned y = 0; y < h; ++y) std::memcpy(&F.hw_flip[(size_t)y * row], &F.hw_pix[(size_t)(h - 1 - y) * row], row);
    return F.hw_flip.data();
}

void video_cb(const void* data, unsigned w, unsigned h, size_t pitch) {
    struct Timed { double t0 = now_ms(); ~Timed() { P.vcb += now_ms() - t0; } } timed;   // whole callback (includes the vsync wait inside the swap)
    if (!data) { if (F.draw_frame) { present(); hw_enter(); } return; }   // duplicated frame: re-present previous
    if (!F.draw_frame) return;                                         // fast-forward: skip in-between frames
    const bool hwf = (data == RETRO_HW_FRAME_BUFFER_VALID);             // hardware core: the frame is in our FBO
    if (hwf && F.zc) {   // zero-copy: the frame never leaves the GPU
        if (w > F.hw_w || h > F.hw_h) hw_alloc_fbo(w > F.hw_w ? w : F.hw_w, h > F.hw_h ? h : F.hw_h);
        F.zc_w = w; F.zc_h = h;
        if (!F.shot_path.empty()) {   // --shot wants the pixels: read them back (only then)
            size_t sp = 0; const void* s = hw_read_frame(w, h, sp);
            F.last_w = w; F.last_h = h; F.last_pitch = (unsigned)sp; F.last_hw = true;
            F.last_frame.assign((const uint8_t*)s, (const uint8_t*)s + sp * h);
        }
        present();
        hw_enter();   // the core gets its FBO / viewport back
        return;
    }
    const Uint32 pf = hwf ? SDL_PIXELFORMAT_ABGR8888                    // RGBA bytes as read back from GL
                    : F.fmt == RETRO_PIXEL_FORMAT_XRGB8888 ? SDL_PIXELFORMAT_ARGB8888
                    : F.fmt == RETRO_PIXEL_FORMAT_RGB565 ? SDL_PIXELFORMAT_RGB565 : SDL_PIXELFORMAT_ARGB1555;
    const void* src = data; size_t spitch = pitch;
    if (hwf) {
        if (w > F.hw_w || h > F.hw_h) hw_alloc_fbo(w > F.hw_w ? w : F.hw_w, h > F.hw_h ? h : F.hw_h);
        const double tr = now_ms();
        src = hw_read_frame(w, h, spitch);
        P.read += now_ms() - tr;
    }
    if (!F.tex || F.tex_w != w || F.tex_h != h || F.tex_pf != pf) {
        if (F.tex) SDL_DestroyTexture(F.tex);
        F.tex = SDL_CreateTexture(F.ren, pf, SDL_TEXTUREACCESS_STREAMING, (int)w, (int)h);
        F.tex_w = w; F.tex_h = h; F.tex_pf = pf;
    }
    const double tu = now_ms();
    SDL_UpdateTexture(F.tex, nullptr, src, (int)spitch);
    P.upload += now_ms() - tu;
    if (!F.shot_path.empty()) {
        F.last_w = w; F.last_h = h; F.last_pitch = (unsigned)spitch; F.last_hw = hwf;
        F.last_frame.assign((const uint8_t*)src, (const uint8_t*)src + spitch * h);
    }
    const double td = now_ms();
    present();
    P.draw += now_ms() - td;
    if (hwf) hw_enter();   // SDL's renderer used its own context while presenting: give the core its context back
}

// run one core frame with the core's GL context current (timed)
void run_core() {
    const double t0 = now_ms();
    hw_enter();
    core.run();
    P.run += now_ms() - t0; ++P.n;
}

// ---------- audio ----------
void audio_batch_cb_impl(const int16_t* d, size_t frames) {
    if (F.audio && !F.ff && !F.rew) SDL_QueueAudio(F.audio, d, (Uint32)(frames * 4));
}
size_t audio_batch_cb(const int16_t* d, size_t frames) { audio_batch_cb_impl(d, frames); return frames; }
void audio_cb(int16_t l, int16_t r) { int16_t s[2] = {l, r}; audio_batch_cb_impl(s, 1); }

// ---------- input ----------
// Fixed Xbox-style positional layout: bottom/right/left/top = SDL A/B/X/Y.
// RetroPad B/A/Y/X are positioned bottom/right/left/top, so this is a straight positional pass-through.
const struct { SDL_GameControllerButton sdl; unsigned rb; } MAP[] = {
    {SDL_CONTROLLER_BUTTON_A, RETRO_DEVICE_ID_JOYPAD_B}, {SDL_CONTROLLER_BUTTON_B, RETRO_DEVICE_ID_JOYPAD_A},
    {SDL_CONTROLLER_BUTTON_X, RETRO_DEVICE_ID_JOYPAD_Y}, {SDL_CONTROLLER_BUTTON_Y, RETRO_DEVICE_ID_JOYPAD_X},
    {SDL_CONTROLLER_BUTTON_START, RETRO_DEVICE_ID_JOYPAD_START},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, RETRO_DEVICE_ID_JOYPAD_L}, {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, RETRO_DEVICE_ID_JOYPAD_R},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, RETRO_DEVICE_ID_JOYPAD_L3}, {SDL_CONTROLLER_BUTTON_RIGHTSTICK, RETRO_DEVICE_ID_JOYPAD_R3},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
};

// Android lists the TV remote (aml_keypad, cec_input ...) as a "game controller" too, and it comes first. Opening
// just the first one meant a real gamepad was never opened, so none of its buttons (hotkeys, D-pad) ever arrived.
static bool pad_is_remote(const char* name) {
    std::string n = name ? name : "";
    for (auto& c : n) c = (char)std::tolower((unsigned char)c);
    for (const char* w : { "keypad", "keyboard", "remote", "cec_input", "accelerometer" })
        if (n.find(w) != std::string::npos) return true;
    return false;
}

void open_pad() {
    int best = -1, fallback = -1;   // best = a real gamepad, fallback = a remote (used only when nothing better exists)
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        if (pad_is_remote(SDL_GameControllerNameForIndex(i))) { if (fallback < 0) fallback = i; }
        else { best = i; break; }
    }
    if (F.pad) {   // already open: only swap a remote for a real gamepad that showed up later
        if (best < 0 || !pad_is_remote(SDL_GameControllerName(F.pad))) return;
        SDL_GameControllerClose(F.pad); F.pad = nullptr;
    }
    const int pick = best >= 0 ? best : fallback;
    if (pick < 0) return;
    F.pad = SDL_GameControllerOpen(pick);
    if (F.pad) plog("[pad] using '%s'\n", SDL_GameControllerName(F.pad));
}

void open_menu() {
    F.menu = true; F.menu_sel = 0;
    if (F.audio) { SDL_ClearQueuedAudio(F.audio); SDL_PauseAudioDevice(F.audio, 1); }
}
void close_menu() {
    F.menu = false;
    if (F.audio) SDL_PauseAudioDevice(F.audio, 0);
}
void menu_activate() {
    switch (F.menu_sel) {
    case 0: close_menu(); break;
    case 1: save_state(); close_menu(); break;
    case 2: load_state(); close_menu(); break;
    case 3: core.reset(); close_menu(); break;
    case 4: F.show_fps = !F.show_fps; break;
    default: F.quit = true; break;
    }
}
void menu_move(int d) { F.menu_sel = (F.menu_sel + d + MENU_ITEMS) % MENU_ITEMS; }

// Internal-resolution option per core (value for 1X..4X). Only cores listed here (hardware-rendered ones, once the
// player supports them) can change resolution; software cores have a fixed output and just report "1X ONLY".
struct ResOpt { const char* core; const char* key; const char* v[4]; };
const ResOpt RES_OPTS[] = {
    {"ppsspp",            "ppsspp_internal_resolution",           {"480x272", "960x544", "1440x816", "1920x1088"}},
    {"flycast",           "flycast_internal_resolution",          {"640x480", "1280x960", "1920x1440", "2560x1920"}},
    {"mupen64plus_next",  "mupen64plus-43screensize",             {"320x240", "640x480", "960x720", "1280x960"}},
    {"parallel_n64",      "parallel-n64-screensize",              {"320x240", "640x480", "960x720", "1280x960"}},
};
void cycle_resolution() {
    const ResOpt* ro = nullptr;
    for (auto& r : RES_OPTS) if (F.core_name.find(r.core) != std::string::npos) { ro = &r; break; }
    char b[24];
    if (!ro) { std::snprintf(b, sizeof b, "RES 1X ONLY"); osd_show(b); return; }
    int next = F.res_level % 4 + 1;
    if (!ro->v[next - 1]) next = 1;   // this core has fewer steps: wrap to 1X
    F.res_level = next;
    F.opt_user[ro->key] = ro->v[next - 1];
    F.var_update = true;
    std::snprintf(b, sizeof b, "RES %dX", next); osd_show(b);
}

void hotkey_combo(SDL_GameControllerButton b) {
    F.hotkey_used = true;
    switch (b) {
    case SDL_CONTROLLER_BUTTON_START: F.quit = true; break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: save_state(); break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: load_state(); break;
    case SDL_CONTROLLER_BUTTON_Y: F.show_fps = !F.show_fps; break;   // the FPS number itself is the indicator, no text message
    case SDL_CONTROLLER_BUTTON_X: cycle_resolution(); break;
    default: F.hotkey_used = false; break;
    }
}

void poll_input() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (F.debug_input) {
            switch (e.type) {
            case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP:
                plog("input: controller button %d %s\n", (int)e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up"); break;
            case SDL_JOYBUTTONDOWN: case SDL_JOYBUTTONUP:
                plog("input: joystick button %d %s\n", (int)e.jbutton.button, e.type == SDL_JOYBUTTONDOWN ? "down" : "up"); break;
            case SDL_KEYDOWN: plog("input: key %d\n", (int)e.key.keysym.sym); break;
            default: break;
            }
        }
        switch (e.type) {
        case SDL_QUIT: F.quit = true; break;
        case SDL_CONTROLLERDEVICEADDED: open_pad(); break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (F.pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(F.pad))) { SDL_GameControllerClose(F.pad); F.pad = nullptr; open_pad(); }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            for (auto& b : MAP) if (b.sdl == e.cbutton.button) F.latch |= 1u << b.rb;
            if (F.menu) {   // menu navigation: D-pad = move, bottom button (A) = confirm, right button (B) / Start = close
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: menu_move(-1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: menu_move(1); break;
                case SDL_CONTROLLER_BUTTON_A: menu_activate(); break;
                case SDL_CONTROLLER_BUTTON_B:
                case SDL_CONTROLLER_BUTTON_START: close_menu(); break;
                default: break;
                }
                break;
            }
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { F.hotkey_held = true; F.hotkey_used = false; }
            else if (F.hotkey_held) hotkey_combo((SDL_GameControllerButton)e.cbutton.button);
            break;
        case SDL_CONTROLLERBUTTONUP:
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                if (F.hotkey_held && !F.hotkey_used) F.select_pulse = 6;   // plain tap = SELECT
                F.hotkey_held = false;
            }
            break;
        case SDL_KEYDOWN:   // keyboard fallback: Esc quit (closes the menu first), F1/M menu, F5 save, F7 load, F3 FPS
            if (F.menu) {
                switch (e.key.keysym.sym) {
                case SDLK_UP: menu_move(-1); break;
                case SDLK_DOWN: menu_move(1); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: menu_activate(); break;
                case SDLK_ESCAPE: case SDLK_AC_BACK: case SDLK_F1: case SDLK_m: close_menu(); break;
                default: break;
                }
                break;
            }
            if (e.key.keysym.sym == SDLK_F1 || e.key.keysym.sym == SDLK_m) open_menu();
            else if (e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_AC_BACK) F.quit = true;
            else if (e.key.keysym.sym == SDLK_F5) save_state();
            else if (e.key.keysym.sym == SDLK_F7) load_state();
            else if (e.key.keysym.sym == SDLK_F3) F.show_fps = !F.show_fps;
            break;
        }
    }
    F.axis[0] = F.axis[1] = F.axis[2] = F.axis[3] = 0;
    F.ff = F.rew = false;
    if (F.pad && F.hotkey_held && !F.menu && !F.no_ffrew) {   // hotkey + R2 = fast-forward, hotkey + L2 = rewind
        const int TT = 12000;
        if (SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > TT) { F.ff = true; F.hotkey_used = true; }
        else if (SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > TT) { F.rew = true; F.hotkey_used = true; }
    }
    uint16_t m = 0;
    if (F.pad && !F.hotkey_held) {
        for (auto& b : MAP) if (SDL_GameControllerGetButton(F.pad, b.sdl)) m |= 1u << b.rb;
        const int T = 16000;   // left stick as D-pad (only for cores that do not read the analog stick themselves)
        int ax = SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (!F.analog_used) {
            if (ax < -T) m |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT; if (ax > T) m |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
            if (ay < -T) m |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;   if (ay > T) m |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
        }
        // triggers are axes in SDL: L2 / R2 (Dreamcast triggers, PS1 L2/R2 ...)
        if (SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 12000) m |= 1u << RETRO_DEVICE_ID_JOYPAD_L2;
        if (SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 12000) m |= 1u << RETRO_DEVICE_ID_JOYPAD_R2;
        F.axis[0] = (int16_t)ax; F.axis[1] = (int16_t)ay;
        F.axis[2] = (int16_t)SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_RIGHTX);
        F.axis[3] = (int16_t)SDL_GameControllerGetAxis(F.pad, SDL_CONTROLLER_AXIS_RIGHTY);
    }
    if (!F.hotkey_held) {
        const Uint8* ks = SDL_GetKeyboardState(nullptr);
        static const struct { SDL_Scancode sc; unsigned rb; } KM[] = {
            {SDL_SCANCODE_UP, RETRO_DEVICE_ID_JOYPAD_UP}, {SDL_SCANCODE_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
            {SDL_SCANCODE_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT}, {SDL_SCANCODE_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
            {SDL_SCANCODE_Z, RETRO_DEVICE_ID_JOYPAD_B}, {SDL_SCANCODE_X, RETRO_DEVICE_ID_JOYPAD_A},
            {SDL_SCANCODE_A, RETRO_DEVICE_ID_JOYPAD_Y}, {SDL_SCANCODE_S, RETRO_DEVICE_ID_JOYPAD_X},
            {SDL_SCANCODE_RETURN, RETRO_DEVICE_ID_JOYPAD_START}, {SDL_SCANCODE_RSHIFT, RETRO_DEVICE_ID_JOYPAD_SELECT},
            {SDL_SCANCODE_BACKSPACE, RETRO_DEVICE_ID_JOYPAD_SELECT},
            {SDL_SCANCODE_Q, RETRO_DEVICE_ID_JOYPAD_L}, {SDL_SCANCODE_W, RETRO_DEVICE_ID_JOYPAD_R},
        };
        if (ks) for (auto& k : KM) if (ks[k.sc]) m |= 1u << k.rb;
    }
    if (F.select_pulse > 0) { m |= 1u << RETRO_DEVICE_ID_JOYPAD_SELECT; }
    if (F.menu) m = 0;
    if (!F.hotkey_held && !F.menu) m |= (uint16_t)F.latch;
    F.latch = 0;
    if (F.debug_input) { static uint16_t last = 0; if (m != last) { plog("input: pad mask %04x\n", (unsigned)m); last = m; } }
    F.pad_mask = m;
}

// libretro input_poll callback: intentionally empty. Cores such as mupen64plus (libco coroutines) call it while running on their own
// stack; SDL's joystick update calls into Java (JNI) there, and ART then aborts with a bogus StackOverflowError.
// We poll SDL once per frame in the main loop (poll_input) before entering the core instead.
void input_poll_cb() {}

int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (F.debug_input) {   // each distinct (port, device, index, id) the core asks for, once
        static std::vector<unsigned> seen;
        const unsigned key = (port << 24) ^ (device << 16) ^ (index << 8) ^ id;
        bool known = false;
        for (unsigned k : seen) if (k == key) { known = true; break; }
        if (!known && seen.size() < 64) { seen.push_back(key); plog("input: core asks port=%u device=%u index=%u id=%u\n", port, device, index, id); }
    }
    if (port == 0 && (device & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG) {
        F.analog_used = true;
        if (index > RETRO_DEVICE_INDEX_ANALOG_RIGHT || id > RETRO_DEVICE_ID_ANALOG_Y) return 0;
        return F.axis[index * 2 + id];
    }
    if (port != 0 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)F.pad_mask;
    return id < 16 ? (F.pad_mask >> id) & 1 : 0;
}

void write_shot() {
    if (F.shot_path.empty() || F.last_frame.empty()) return;
    FILE* f = std::fopen(F.shot_path.c_str(), "wb"); if (!f) return;
    std::fprintf(f, "P6\n%u %u\n255\n", F.last_w, F.last_h);
    for (unsigned y = 0; y < F.last_h; ++y) for (unsigned x = 0; x < F.last_w; ++x) {
        uint8_t r, g, b; const uint8_t* p = &F.last_frame[y * F.last_pitch];
        if (F.last_hw) { r = p[x * 4]; g = p[x * 4 + 1]; b = p[x * 4 + 2]; }
        else if (F.fmt == RETRO_PIXEL_FORMAT_XRGB8888) { uint32_t v = ((const uint32_t*)p)[x]; r = v >> 16; g = v >> 8; b = v; }
        else if (F.fmt == RETRO_PIXEL_FORMAT_RGB565) { uint16_t v = ((const uint16_t*)p)[x]; r = ((v >> 11) & 31) << 3; g = ((v >> 5) & 63) << 2; b = (v & 31) << 3; }
        else { uint16_t v = ((const uint16_t*)p)[x]; r = ((v >> 10) & 31) << 3; g = ((v >> 5) & 31) << 3; b = (v & 31) << 3; }
        std::fputc(r, f); std::fputc(g, f); std::fputc(b, f);
    }
    std::fclose(f);
}


// ---------- system -> core table ----------
// System short name (matches the icon key of systems.js in the theme) -> core file name. Can be overridden by ~/.config/poniaemu/cores.ini (one "system=core.so" per line).
// Ports are handled separately.

bool file_exists(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

std::string exe_dir() {
    char b[4096]; ssize_t n = ::readlink("/proc/self/exe", b, sizeof b - 1);
    if (n <= 0) return ".";
    b[n] = 0; std::string s = b; size_t k = s.find_last_of('/');
    return k == std::string::npos ? "." : s.substr(0, k);
}

std::string find_core_file(const std::string& file, const std::vector<std::string>& cores_dirs, const std::string& system = "");

std::string resolve_core(const std::string& system, const std::vector<std::string>& cores_dirs) {
    const std::string sys_key = ponia::resolve_system(system);   // alias (collection short name) -> system key; an unknown one is used as the key as it is
    const std::string key = sys_key.empty() ? system : sys_key;
    std::string file;
    const std::string cid = ponia::core_id_for_system(key);
    if (!cid.empty()) file = ponia::core_file(cid);
    const char* home = std::getenv("HOME");
    if (home) {   // user override table
        FILE* f = std::fopen((std::string(home) + "/.config/poniaemu/cores.ini").c_str(), "r");
        if (f) {
            char line[512];
            while (std::fgets(line, sizeof line, f)) {
                std::string l = line; size_t e = l.find('=');
                if (e == std::string::npos) continue;
                auto trim = [](std::string x) { size_t a = x.find_first_not_of(" \t\r\n"), b = x.find_last_not_of(" \t\r\n"); return a == std::string::npos ? std::string() : x.substr(a, b - a + 1); };
                if (trim(l.substr(0, e)) == key || trim(l.substr(0, e)) == system) file = trim(l.substr(e + 1));
            }
            std::fclose(f);
        }
    }
    if (file.empty()) return "";
    return find_core_file(file, cores_dirs, system);
}

std::string find_core_file(const std::string& file, const std::vector<std::string>& cores_dirs, const std::string& system) {
    const char* home = std::getenv("HOME");
    if (file.find('/') != std::string::npos) return file;
    std::vector<std::string> dirs;
    for (auto& cd : cores_dirs) dirs.push_back(cd);
    if (const char* e = std::getenv("PONIA_CORES_DIR")) dirs.push_back(e);
    dirs.push_back(exe_dir() + "/cores");
    dirs.push_back(exe_dir() + "/../lib/poniaemu/cores");
    if (home) dirs.push_back(std::string(home) + "/.config/poniaemu/cores");
    dirs.push_back("/usr/lib/poniaemu/cores");
    dirs.push_back("/usr/lib/libretro");
    for (auto& d : dirs) {
        if (file_exists(d + "/" + file)) return d + "/" + file;
        if (file_exists(d + "/lib" + file)) return d + "/lib" + file;   // Android: a .so inside the APK must be named lib*.so
    }
    plog("poniaplay: core %s for system '%s' not found (searched %zu dirs)\n", file.c_str(), system.c_str(), dirs.size());
    return "";
}

} // namespace

int ponia_play(int argc, char** argv) {
    std::string core_path, system_name;
    std::vector<std::string> cores_dirs;
    std::string bios_arg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--core") core_path = next();
        else if (a == "--system") system_name = next();
        else if (a == "--cores-dir") cores_dirs.push_back(next());
        else if (a == "--content") F.content_path = next();
        else if (a == "--system-dir") F.system_dir = next();
        else if (a == "--save-dir") F.save_dir = next();
        else if (a == "--bios-dir") bios_arg = next();
        else if (a == "--debug-input") F.debug_input = true;
        else if (a == "--no-zerocopy") F.zc_off = true;   // A/B test: force the old read-back present path
        else if (a == "--sharpen") F.sharpen = (float)std::atof(next().c_str());   // 0=off, try 0.3~0.8
        else if (a == "--frames") F.max_frames = (unsigned)std::atoi(next().c_str());
        else if (a == "--shot") F.shot_path = next();
        else if (a == "--option") { std::string kv = next(); size_t p = kv.find('='); if (p != std::string::npos) F.opt_user[kv.substr(0, p)] = kv.substr(p + 1); }
    }
    ::setenv("TMPDIR", F.save_dir.c_str(), 1);   // Android has no /tmp; some cores (AppleWin) use tmpfile()
#ifdef __ANDROID__
    g_tmp_dir = F.save_dir;
#endif
    if (core_path.empty() && !system_name.empty()) core_path = resolve_core(system_name, cores_dirs);
    if (core_path.empty() || F.content_path.empty()) {
        plog("usage: poniaplay (--core X_libretro.so | --system fc) --content ROM [--cores-dir D] [--system-dir D] [--save-dir D] [--option k=v]\n");
        return 2;
    }
    size_t sl = F.content_path.find_last_of("/\\");
    F.base_name = F.content_path.substr(sl == std::string::npos ? 0 : sl + 1);
    size_t dot = F.base_name.find_last_of('.'); if (dot != std::string::npos) F.base_name.resize(dot);

    // Ponia.Roms/BIOS -> the system directory of the core; mame2003_plus also needs the BIOS next to the game, which is solved with links in a staging directory
    {
        const std::string bios = ponia::find_bios_dir(F.content_path, bios_arg);
        if (!bios.empty()) {
            const int n = ponia::sync_bios(bios, F.system_dir);
            plog("poniaplay: BIOS folder %s -> %s (%d files)\n", bios.c_str(), F.system_dir.c_str(), n);
            if (core_path.find("mame") != std::string::npos) {   // mame2003_plus / mamearcade: the BIOS must be in the same directory as the game
                const std::string staged = ponia::stage_mame_content(F.content_path, bios, F.save_dir + "/.biosroms");
                if (!staged.empty()) { plog("poniaplay: MAME content staged at %s\n", staged.c_str()); F.content_path = staged; }
            }
        }
    }
    // A few cores register "old-style options" whose first value is not the real default (by the old convention the first one = the default); fix it here by core name; --option has priority
    {
        static const struct { const char* core; const char* key; const char* val; } CORE_DEFAULTS[] = {
            {"applewin", "applewin_machine", "Apple //e (enhanced)"},   // the first value "Apple II (original)" does not boot by itself and stops at the ??@@ screen
            // PSP: 3-frame buffering (same as the standalone version). With the 2 frames that the core defaults to, a Mali-G31 class GPU only reaches 41-55 fps in 2X matches, while 3 frames hold about 60; the price is slightly more input latency
            {"ppsspp", "ppsspp_inflight_frames", "Up to 2"},
        };
        size_t q0 = core_path.find_last_of("/\\");
        const std::string cn = core_path.substr(q0 == std::string::npos ? 0 : q0 + 1);
        for (const auto& d : CORE_DEFAULTS)
            if (cn.find(d.core) != std::string::npos && !F.opt_user.count(d.key)) F.opt_user[d.key] = d.val;
    }
    // The Dolphin core needs dolphin-emu/Sys (GPL data files): when the BIOS folder does not provide it, unpack packs/Dolphin.zip from the APK into the system directory
    if (core_path.find("dolphin") != std::string::npos && !file_exists(F.system_dir + "/dolphin-emu/Sys/codehandler.bin")) {
#ifdef __ANDROID__
        if (SDL_RWops* rw = SDL_RWFromFile("packs/Dolphin.zip", "rb")) {
            std::vector<uint8_t> z; uint8_t buf[1 << 15]; size_t n;
            while ((n = SDL_RWread(rw, buf, 1, sizeof buf)) > 0) z.insert(z.end(), buf, buf + n);
            SDL_RWclose(rw);
            const std::string tmp = F.save_dir + "/.dolphin-sys.zip";
            std::string err;
            if (write_file(tmp, z.data(), z.size()) && ponia::archive_extract_all(tmp, F.system_dir, err, 256ull << 20))
                plog("poniaplay: Dolphin Sys unpacked into %s\n", F.system_dir.c_str());
            else plog("poniaplay: Dolphin Sys unpack failed: %s\n", err.c_str());
            ::unlink(tmp.c_str());
        } else plog("poniaplay: no Dolphin Sys data (BIOS folder / APK pack missing)\n");
#else
        plog("poniaplay: Dolphin needs dolphin-emu/Sys in the BIOS folder\n");
#endif
    }
    { size_t q = core_path.find_last_of("/\\"); F.core_name = core_path.substr(q == std::string::npos ? 0 : q + 1); F.no_serialize = F.core_name.find("yabasanshiro") != std::string::npos && !g_tmp_patched;   // when the tmpfile patch succeeded, saving does not have to be paused
      // GameCube (Dolphin): fast-forward/rewind are always off (it freezes on some devices, and the state is large, so rewinding makes no sense anyway). Saving/loading is not affected
      F.no_ffrew = F.core_name.find("dolphin") != std::string::npos; }
    if (!load_core(core_path.c_str())) return 3;
    if (core.api_version() != RETRO_API_VERSION) { plog("poniaplay: API version mismatch\n"); return 3; }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { plog("SDL: %s\n", SDL_GetError()); return 4; }
    core.set_environment(env_cb);
    core.init();
    core.set_video_refresh(video_cb);
    core.set_audio_sample(audio_cb);
    core.set_audio_sample_batch(audio_batch_cb);
    core.set_input_poll(input_poll_cb);
    core.set_input_state(input_state_cb);

    retro_system_info si{}; core.get_system_info(&si);
    retro_game_info gi{}; std::vector<uint8_t> rom;

    // Archives (.zip / .7z): like RetroArch, unpack here unless the core says it reads archives itself (block_extract,
    // e.g. fbneo / mame). Cores that take the file in memory get just the one matching entry; cores that need a real
    // path (e.g. cue/bin discs) get the whole archive unpacked into a scratch directory that is removed on exit.
    const uint64_t kMaxRomBytes = 256ull << 20, kMaxExtractBytes = 4ull << 30;
    std::string load_path = F.content_path;
    struct ExtractGuard { std::string dir; ~ExtractGuard() { if (!dir.empty()) ponia::remove_tree(dir); } } extract_guard;
    bool from_archive = false;
    if (!si.block_extract && ponia::archive_kind(F.content_path) != ponia::ArchiveKind::None) {
        std::vector<ponia::ArchiveEntry> ents; std::string err;
        if (!ponia::archive_list(F.content_path, ents, err)) { plog("poniaplay: cannot open archive: %s\n", err.c_str()); return 5; }
        const int pick = ponia::archive_pick(ents, si.valid_extensions);
        if (pick < 0) { plog("poniaplay: no file in the archive matches the core's extensions (%s)\n", si.valid_extensions ? si.valid_extensions : "-"); return 5; }
        std::string inner = ents[(size_t)pick].name;
        plog("poniaplay: archive entry '%s' (%llu bytes)\n", inner.c_str(), (unsigned long long)ents[(size_t)pick].size);
        from_archive = true;
        if (si.need_fullpath) {
            extract_guard.dir = F.save_dir + "/.extract";
            ponia::remove_tree(extract_guard.dir);   // leftovers from a crashed run
            std::string rel = inner;
            if (!ponia::archive_safe_path(rel)) { plog("poniaplay: unsafe path in archive\n"); return 5; }
            if (!ponia::archive_extract_all(F.content_path, extract_guard.dir, err, kMaxExtractBytes)) { plog("poniaplay: cannot unpack archive: %s\n", err.c_str()); return 5; }
            load_path = extract_guard.dir + "/" + rel;
        } else {
            if (!ponia::archive_read(F.content_path, inner, rom, err, kMaxRomBytes)) { plog("poniaplay: cannot unpack '%s': %s\n", inner.c_str(), err.c_str()); return 5; }
            load_path = inner;   // cores look at the extension of gi.path to tell the format
        }
    }
    gi.path = load_path.c_str();
    if (!si.need_fullpath) {
        if (!from_archive && !read_file(F.content_path, rom)) { plog("poniaplay: cannot read content\n"); return 5; }
        gi.data = rom.data(); gi.size = rom.size();
    }
    // Fallback: when a core refuses the content (most often because the arcade romset version does not match), try the next core of the same kind in turn (it must be installed and must read the content the same way)
    {
        std::vector<std::string> alt;
        const std::string sk0 = ponia::resolve_system(system_name);
        const std::string sk = sk0.empty() ? system_name : sk0;
        if (sk == "mame") alt = {"mame2003_plus", "fbneo"};
        else if (sk == "pico8") alt = {"retro8"};
        else if (sk == "fbneo" || sk == "neogeo" || sk == "cps1" || sk == "cps2") alt = {"mamearcade", "mame2003_plus"};
        size_t ai = 0;
        while (!core.load_game(&gi)) {
            plog("poniaplay: core %s rejected content\n", F.core_name.c_str());
            std::string np;
            while (ai < alt.size() && np.empty()) {
                const std::string file = ponia::core_file(alt[ai++]);
                np = find_core_file(file, cores_dirs);
                if (np.find(F.core_name) != std::string::npos) np.clear();
            }
            if (np.empty()) return 5;
            plog("poniaplay: fallback to %s\n", np.c_str());
            core.deinit();                      // leave the old library mapped (some cores crash on dlclose)
            if (!load_core(np.c_str()) || core.api_version() != RETRO_API_VERSION) return 3;
            { size_t q = np.find_last_of("/\\"); F.core_name = np.substr(q == std::string::npos ? 0 : q + 1); }
            core.set_environment(env_cb); core.init();
            core.set_video_refresh(video_cb); core.set_audio_sample(audio_cb); core.set_audio_sample_batch(audio_batch_cb);
            core.set_input_poll(input_poll_cb); core.set_input_state(input_state_cb);
            retro_system_info si2{}; core.get_system_info(&si2);
            if (si2.block_extract != si.block_extract || si2.need_fullpath != si.need_fullpath) { plog("poniaplay: fallback core reads content differently, giving up\n"); return 5; }
        }
    }

    if (core.set_controller_port_device) core.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    retro_system_av_info av{}; core.get_system_av_info(&av);
    F.width = av.geometry.base_width; F.height = av.geometry.base_height; F.aspect = av.geometry.aspect_ratio;
    F.fps = av.timing.fps > 1 ? av.timing.fps : 60; F.sample_rate = av.timing.sample_rate;

    F.win = SDL_CreateWindow("Ponia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, F.width * 3, F.height * 3,
                             SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI | (F.hw_req ? SDL_WINDOW_OPENGL : 0));
    if (!F.win) { plog("SDL window: %s\n", SDL_GetError()); return 4; }
    SDL_ShowCursor(SDL_DISABLE);
    F.ren = SDL_CreateRenderer(F.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!F.ren) F.ren = SDL_CreateRenderer(F.win, -1, SDL_RENDERER_SOFTWARE);
    if (!F.ren) { plog("SDL renderer: %s\n", SDL_GetError()); return 4; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (F.hw_req && !hw_setup(av)) { plog("poniaplay: hardware render setup failed\n"); return 4; }

    SDL_AudioSpec want{}, have{};
    want.freq = (int)(F.sample_rate + 0.5); want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
    F.audio = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (F.audio) SDL_PauseAudioDevice(F.audio, 0);
    else plog("poniaplay: no audio (%s); using timer pacing\n", SDL_GetError());

    open_pad();
    load_sram();
    F.fps_t0 = SDL_GetTicks();

    const Uint32 target_bytes = F.audio ? (Uint32)(have.freq * 4 * 0.06) : 0;   // keep ~60 ms queued
    Uint32 next_tick = SDL_GetTicks();
    while (!F.quit) {
        if (F.menu) { poll_input(); present(); next_tick = SDL_GetTicks(); SDL_Delay(16); continue; }
        if (F.ff) {   // fast-forward: 4 frames per loop, only the last one is drawn, no audio, no pacing
            for (int i = 0; i < 4 && !F.quit; ++i) { F.draw_frame = (i == 3); poll_input(); run_core(); ++F.frames_done; ++F.fps_n; }
            F.draw_frame = true; next_tick = SDL_GetTicks();
            if (F.audio) SDL_ClearQueuedAudio(F.audio);
            continue;
        }
        if (F.rew) {   // rewind: step back one snapshot (~3 frames) per loop at normal pace, no audio
            if (F.audio) SDL_ClearQueuedAudio(F.audio);
            Uint32 now = SDL_GetTicks(); if (now < next_tick) SDL_Delay(next_tick - now); next_tick = SDL_GetTicks() + (Uint32)(1000.0 / F.fps);
            if (rewind_step()) { poll_input(); run_core(); } else osd_show("REWIND END");
            continue;
        }
        if (F.audio) { while (SDL_GetQueuedAudioSize(F.audio) > target_bytes && !F.quit) SDL_Delay(1); }
        else { Uint32 now = SDL_GetTicks(); if (now < next_tick) SDL_Delay(next_tick - now); next_tick += (Uint32)(1000.0 / F.fps); }
        poll_input(); run_core();
        rewind_capture();
        if (F.select_pulse > 0) --F.select_pulse;
        ++F.frames_done; ++F.fps_n;
        Uint32 t = SDL_GetTicks();
        if (t - F.fps_t0 >= 1000) { F.fps_now = F.fps_n * 1000.0 / (t - F.fps_t0); if (F.debug_input) {
                plog("fps: %.1f\n", F.fps_now);
                if (P.n) {   // average milliseconds per frame this second; core = run - vcb (vcb = our video callback, which contains read/upload/draw/swap)
                    const double k = 1.0 / P.n;
                    plog("perf: run %.1f ms = core %.1f + video_cb %.1f [read %.1f, upload %.1f, draw %.1f, swap/vsync %.1f] (%s)\n",
                         P.run * k, (P.run - P.vcb) * k, P.vcb * k, P.read * k, P.upload * k, P.draw * k, P.swap * k, F.zc ? "zero-copy" : "old path");
                }
            }
            P = Perf(); F.fps_n = 0; F.fps_t0 = t; }
        if (F.max_frames && F.frames_done >= F.max_frames) break;
    }
    write_shot();
    save_sram();
    core.unload_game();
    hw_teardown();
    core.deinit();
    if (F.audio) SDL_CloseAudioDevice(F.audio);
    SDL_Quit();
    plog("poniaplay: exit after %u frames\n", F.frames_done);
    return 0;
}
