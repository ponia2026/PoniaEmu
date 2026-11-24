// Pegasus Frontend
// Copyright (C) 2018  Mátyás Mustoha
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.


#include "Backend.h"

#include "AppSettings.h"
#include "ponia_version.h"
#include "Log.h"
#include "FrontendLayer.h"
#include "ProcessLauncher.h"
#include "ScriptRunner.h"
#include "Paths.h"
#include "platform/PowerCommands.h"
#ifdef Q_OS_ANDROID
#  include "platform/AndroidHelpers.h"
#endif
#include "types/AppCloseType.h"

// For type registration
#include "model/Api.h"
#include "model/keys/Key.h"
#include "model/gaming/Assets.h"
#include "model/gaming/GameFile.h"
#include "model/internal/Internal.h"
#include "utils/FolderListModel.h"
#include "SortFilterProxyModel/qqmlsortfilterproxymodel.h"
#include "SortFilterProxyModel/filters/filtersqmltypes.h"
#include "SortFilterProxyModel/proxyroles/proxyrolesqmltypes.h"
#include "SortFilterProxyModel/sorters/sortersqmltypes.h"

#include <QGuiApplication>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif
#include <QQmlEngine>
#include <QTimer>

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#  include <dirent.h>
#  include <dlfcn.h>
#  include <sys/ioctl.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <climits>
#  include <cstdint>
#  include <cstdlib>
#  include <cstring>
#  include <map>
#  include <tuple>
#  include <vector>
#endif

#if defined(WITH_SDL_GAMEPAD) || defined(WITH_SDL_POWER)
#include <SDL.h>
#endif

namespace model { class Key; }
namespace model { class Keys; }
class FolderListModel;


namespace {
void print_metainfo()
{
    Log::info(LOGMSG(PONIA_PRODUCT_NAME " v" PONIA_VERSION " " PONIA_BUILD_DATE " " PONIA_GIT_SHORT));
    Log::info(LOGMSG("Running on %1 (%2, %3)").arg(
        QSysInfo::prettyProductName(),
        QSysInfo::currentCpuArchitecture(),
        QGuiApplication::platformName()));
    Log::info(LOGMSG("Qt version %1").arg(qVersion()));
}

void create_config_dirs()
{
    QDir configDir(paths::writableConfigDir());
    if (!configDir.mkpath(QStringLiteral("themes/")))
        Log::warning(LOGMSG("Failed to automatically create the `themes` directory"));
#ifdef Q_OS_ANDROID
    android::install_bundled_themes(configDir.absoluteFilePath(QStringLiteral("themes")));
#endif
}

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
// On KMS/DRM (eglfs) the process that owns the screen is the DRM master, and tearing down the UI does not give that up.
// A game started afterwards (the built-in player, an emulator, a port) then cannot take the screen and shows nothing.
// So the master role is handed over explicitly: dropped before the game starts, taken back when it has ended.
//
// Some display drivers (seen on a Rockchip RK3288) come back with the main plane detached after a game that drew through its own
// KMS session: the player cannot restore the screen, the kernel then answers every page flip of the frontend with "resource busy"
// and the picture stays frozen. So the screen mode is remembered before the master role is dropped, and set again if the main
// plane has lost its framebuffer when the role is taken back. The DRM functions are looked up at run time (libdrm is there
// whenever Qt runs on KMS), so nothing is needed to build.

// The leading part of libdrm's public structures (xf86drmMode.h); only what is read or passed on is declared.
struct DrmModeInfo {
    uint32_t clock;
    uint16_t hdisplay, hsync_start, hsync_end, htotal, hskew;
    uint16_t vdisplay, vsync_start, vsync_end, vtotal, vscan;
    uint32_t vrefresh, flags, type;
    char name[32];
};
struct DrmRes {
    int count_fbs; uint32_t* fbs;
    int count_crtcs; uint32_t* crtcs;
    int count_connectors; uint32_t* connectors;
    int count_encoders; uint32_t* encoders;
    uint32_t min_width, max_width, min_height, max_height;
};
struct DrmCrtc {
    uint32_t crtc_id, buffer_id, x, y, width, height;
    int mode_valid;
    DrmModeInfo mode;
    int gamma_size;
};
struct DrmEncoder { uint32_t encoder_id, encoder_type, crtc_id, possible_crtcs, possible_clones; };
struct DrmConnector { uint32_t connector_id, encoder_id, connector_type, connector_type_id; };   // the rest is not read
struct DrmFb { uint32_t fb_id, width, height, pitch, bpp, depth, handle; };

struct DrmApi {
    DrmRes* (*get_resources)(int) = nullptr;
    void (*free_resources)(DrmRes*) = nullptr;
    DrmCrtc* (*get_crtc)(int, uint32_t) = nullptr;
    void (*free_crtc)(DrmCrtc*) = nullptr;
    DrmConnector* (*get_connector)(int, uint32_t) = nullptr;
    void (*free_connector)(DrmConnector*) = nullptr;
    DrmEncoder* (*get_encoder)(int, uint32_t) = nullptr;
    void (*free_encoder)(DrmEncoder*) = nullptr;
    int (*set_crtc)(int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, int, DrmModeInfo*) = nullptr;
    DrmFb* (*get_fb)(int, uint32_t) = nullptr;
    void (*free_fb)(DrmFb*) = nullptr;
    bool ok = false;
};

const DrmApi& drm_api()
{
    static const DrmApi api = [] {
        DrmApi a;
        void* lib = ::dlopen("libdrm.so.2", RTLD_LAZY);
        if (!lib)
            return a;
        auto sym = [lib](const char* name) { return ::dlsym(lib, name); };
        a.get_resources = reinterpret_cast<decltype(a.get_resources)>(sym("drmModeGetResources"));
        a.free_resources = reinterpret_cast<decltype(a.free_resources)>(sym("drmModeFreeResources"));
        a.get_crtc = reinterpret_cast<decltype(a.get_crtc)>(sym("drmModeGetCrtc"));
        a.free_crtc = reinterpret_cast<decltype(a.free_crtc)>(sym("drmModeFreeCrtc"));
        // the "current" variant does not probe the connector again
        a.get_connector = reinterpret_cast<decltype(a.get_connector)>(sym("drmModeGetConnectorCurrent"));
        if (!a.get_connector)
            a.get_connector = reinterpret_cast<decltype(a.get_connector)>(sym("drmModeGetConnector"));
        a.free_connector = reinterpret_cast<decltype(a.free_connector)>(sym("drmModeFreeConnector"));
        a.get_encoder = reinterpret_cast<decltype(a.get_encoder)>(sym("drmModeGetEncoder"));
        a.free_encoder = reinterpret_cast<decltype(a.free_encoder)>(sym("drmModeFreeEncoder"));
        a.set_crtc = reinterpret_cast<decltype(a.set_crtc)>(sym("drmModeSetCrtc"));
        a.get_fb = reinterpret_cast<decltype(a.get_fb)>(sym("drmModeGetFB"));
        a.free_fb = reinterpret_cast<decltype(a.free_fb)>(sym("drmModeFreeFB"));
        a.ok = a.get_resources && a.free_resources && a.get_crtc && a.free_crtc && a.get_connector && a.free_connector
            && a.get_encoder && a.free_encoder && a.set_crtc && a.get_fb && a.free_fb;
        return a;
    }();
    return api;
}

struct SavedCrtc {
    dev_t device;
    uint32_t crtc_id, buffer_id, x, y;
    DrmModeInfo mode;
    std::vector<uint32_t> connectors;
};
std::vector<SavedCrtc> g_saved_crtcs;

void save_crtcs(int fd, dev_t device)
{
    const DrmApi& api = drm_api();
    if (!api.ok)
        return;
    DrmRes* const res = api.get_resources(fd);
    if (!res)
        return;
    for (int i = 0; i < res->count_crtcs; ++i) {
        DrmCrtc* const crtc = api.get_crtc(fd, res->crtcs[i]);
        if (!crtc)
            continue;
        if (crtc->buffer_id != 0 && crtc->mode_valid) {
            SavedCrtc s;
            s.device = device;
            s.crtc_id = crtc->crtc_id;
            s.buffer_id = crtc->buffer_id;
            s.x = crtc->x;
            s.y = crtc->y;
            s.mode = crtc->mode;
            for (int c = 0; c < res->count_connectors; ++c) {
                DrmConnector* const conn = api.get_connector(fd, res->connectors[c]);
                if (!conn)
                    continue;
                if (conn->encoder_id) {
                    if (DrmEncoder* const enc = api.get_encoder(fd, conn->encoder_id)) {
                        if (enc->crtc_id == crtc->crtc_id)
                            s.connectors.push_back(conn->connector_id);
                        api.free_encoder(enc);
                    }
                }
                api.free_connector(conn);
            }
            if (!s.connectors.empty())
                g_saved_crtcs.push_back(std::move(s));
        }
        api.free_crtc(crtc);
    }
    api.free_resources(res);
}

// A framebuffer filled with black (a "dumb" buffer, created once per device and size and reused). Returns 0 when it cannot be made.
uint32_t black_framebuffer(int fd, dev_t device, uint32_t width, uint32_t height)
{
    struct CreateDumb { uint32_t height, width, bpp, flags, handle, pitch; uint64_t size; };
    struct MapDumb { uint32_t handle, pad; uint64_t offset; };
    struct FbCmd { uint32_t fb_id, width, height, pitch, bpp, depth, handle; };
    static std::map<std::tuple<dev_t, uint32_t, uint32_t>, uint32_t> cache;
    const auto key = std::make_tuple(device, width, height);
    const auto it = cache.find(key);
    if (it != cache.end())
        return it->second;

    CreateDumb create = {height, width, 32, 0, 0, 0, 0};
    if (ioctl(fd, _IOWR('d', 0xB2, CreateDumb), &create) != 0)
        return 0;
    MapDumb map = {create.handle, 0, 0};
    if (ioctl(fd, _IOWR('d', 0xB3, MapDumb), &map) == 0) {
        void* const mem = mmap(nullptr, size_t(create.size), PROT_READ | PROT_WRITE, MAP_SHARED, fd, off_t(map.offset));
        if (mem != MAP_FAILED) {
            memset(mem, 0, size_t(create.size));
            munmap(mem, size_t(create.size));
        }
    }
    FbCmd fb = {0, width, height, create.pitch, 32, 24, create.handle};
    if (ioctl(fd, _IOWR('d', 0xAE, FbCmd), &fb) != 0)
        return 0;
    cache[key] = fb.fb_id;
    return fb.fb_id;
}

// The main plane can already be detached when the frontend hides its window, before the master role is dropped, so the
// framebuffer to show again is looked up now: one of the frontend's own framebuffers (they are listed per open file, so every
// open file of the device is asked) that has the size of the mode. The mode and outputs come from the display controller if it
// is still enabled, otherwise from what was remembered before the game.
void restore_crtcs(int master_fd, dev_t device, const std::vector<int>& fds)
{
    const DrmApi& api = drm_api();
    if (!api.ok) {
        Log::info(LOGMSG("Screen check after the game skipped: libdrm not found"));
        return;
    }
    DrmRes* const res = api.get_resources(master_fd);
    if (!res)
        return;
    for (int i = 0; i < res->count_crtcs; ++i) {
        DrmCrtc* const crtc = api.get_crtc(master_fd, res->crtcs[i]);
        if (!crtc)
            continue;
        const uint32_t crtc_id = crtc->crtc_id;
        const bool lost = crtc->buffer_id == 0;
        DrmModeInfo mode = crtc->mode;
        bool have_mode = crtc->mode_valid != 0;
        uint32_t x = crtc->x, y = crtc->y;
        api.free_crtc(crtc);
        if (!lost)
            continue;

        std::vector<uint32_t> connectors;
        for (int c = 0; c < res->count_connectors; ++c) {
            DrmConnector* const conn = api.get_connector(master_fd, res->connectors[c]);
            if (!conn)
                continue;
            if (conn->encoder_id) {
                if (DrmEncoder* const enc = api.get_encoder(master_fd, conn->encoder_id)) {
                    if (enc->crtc_id == crtc_id)
                        connectors.push_back(conn->connector_id);
                    api.free_encoder(enc);
                }
            }
            api.free_connector(conn);
        }
        uint32_t remembered_fb = 0;
        for (const SavedCrtc& s : g_saved_crtcs) {
            if (s.device != device || s.crtc_id != crtc_id)
                continue;
            if (!have_mode) { mode = s.mode; x = s.x; y = s.y; have_mode = true; }
            if (connectors.empty()) connectors = s.connectors;
            remembered_fb = s.buffer_id;
        }
        if (!have_mode || connectors.empty())
            continue;   // this controller was not showing anything before the game either

        // Show a black picture first: the buffers still around at this moment may be the text console's, and showing that for a
        // second or two until the frontend draws again looks like a flash of a command line.
        uint32_t fb_id = black_framebuffer(master_fd, device, mode.hdisplay, mode.vdisplay);
        if (!fb_id && remembered_fb) {
            if (DrmFb* const fb = api.get_fb(master_fd, remembered_fb)) {
                fb_id = remembered_fb;
                api.free_fb(fb);
            }
        }
        for (const int fd : fds) {
            if (fb_id)
                break;
            DrmRes* const own = api.get_resources(fd);
            if (!own)
                continue;
            for (int f = own->count_fbs - 1; f >= 0 && !fb_id; --f) {
                if (DrmFb* const fb = api.get_fb(fd, own->fbs[f])) {
                    if (fb->width == mode.hdisplay && fb->height == mode.vdisplay)
                        fb_id = fb->fb_id;
                    api.free_fb(fb);
                }
            }
            api.free_resources(own);
        }
        // Some drivers do not list the framebuffers per file. Framebuffer ids are global, so ask for them by number: the newest one
        // with the size of the mode is the frontend's (the console's own buffer is created at boot and has a lower number).
        for (uint32_t id = 4096; id > 0 && !fb_id; --id) {
            if (DrmFb* const fb = api.get_fb(master_fd, id)) {
                if (fb->width == mode.hdisplay && fb->height == mode.vdisplay)
                    fb_id = fb->fb_id;
                api.free_fb(fb);
            }
        }
        if (!fb_id) {
            Log::info(LOGMSG("The screen lost its picture during the game and no framebuffer of the frontend was found to show again"));
            continue;
        }
        const int rc = api.set_crtc(master_fd, crtc_id, fb_id, x, y, connectors.data(), int(connectors.size()), &mode);
        Log::info(LOGMSG("The screen lost its picture during the game; set the display mode again (framebuffer %1, %2x%3): %4")
            .arg(QString::number(fb_id), QString::number(mode.hdisplay), QString::number(mode.vdisplay),
                 rc == 0 ? QStringLiteral("ok") : QStringLiteral("failed (%1)").arg(rc)));
    }
    api.free_resources(res);
}

void set_drm_master(bool on)
{
    // DRM_IOCTL_SET_MASTER / DRM_IOCTL_DROP_MASTER (drm.h); defined here so the DRM headers are not needed to build
    const unsigned long request = on ? _IO('d', 0x1e) : _IO('d', 0x1f);
    DIR* dir = ::opendir("/proc/self/fd");
    if (!dir)
        return;
    int handled = 0;
    std::map<dev_t, int> masters;   // one open file per device that is (or is about to be) the master
    std::map<dev_t, std::vector<int>> all_fds;   // every open file of each device
    if (!on)
        g_saved_crtcs.clear();
    while (const dirent* entry = ::readdir(dir)) {
        const int fd = std::atoi(entry->d_name);
        if (fd <= 2)
            continue;
        char target[PATH_MAX];
        const QByteArray link = (QStringLiteral("/proc/self/fd/") + QString::fromLatin1(entry->d_name)).toLocal8Bit();
        const ssize_t len = ::readlink(link.constData(), target, sizeof(target) - 1);
        if (len <= 0)
            continue;
        target[len] = 0;
        if (std::strncmp(target, "/dev/dri/card", 13) != 0)
            continue;
        struct stat st;
        const bool known = ::fstat(fd, &st) == 0;
        if (known)
            all_fds[st.st_rdev].push_back(fd);
        if (!on && known && !masters.count(st.st_rdev)) {
            masters[st.st_rdev] = fd;
            save_crtcs(fd, st.st_rdev);   // before the role is dropped
        }
        if (::ioctl(fd, request) == 0) {
            ++handled;
            if (on && known && !masters.count(st.st_rdev))
                masters[st.st_rdev] = fd;
        }
    }
    ::closedir(dir);
    if (on) {
        for (const auto& m : masters)
            restore_crtcs(m.second, m.first, all_fds[m.first]);
    }
    Log::info(LOGMSG("DRM master %1 on %2 device(s)").arg(on ? QStringLiteral("taken") : QStringLiteral("released")).arg(handled));
}
#endif

// On some platforms only one program can draw to the screen at a time,
// so Pegasus must hide its frontend as soon as the game has started.
bool platform_requires_hiding()
{
#ifdef Q_OS_ANDROID
    // Games are separate apps on top of the activity stack; the frontend can stay alive in the background.
    return false;
#else
    const QString platform = QGuiApplication::platformName();
    return platform == QLatin1String("eglfs")
        || platform == QLatin1String("linuxfb")
        || platform == QLatin1String("vkkhrdisplay")
        || platform.startsWith(QLatin1String("minimal"));
#endif
}

void register_api_classes()
{
    // register API classes:
    //   this should come before the ApiObject constructor,
    //   as that may produce language change signals

    // PoniaEmu: the same types are available as both `Pegasus.*` (for existing
    // Pegasus themes) and `Ponia.*` (for new themes)
    constexpr const char* API_URIS[] = { "Pegasus.Model", "Ponia.Model" };
    const QString error_msg = LOGMSG("Sorry, you cannot create this type in QML.");

    for (const char* const API_URI : API_URIS) {
        qmlRegisterUncreatableType<model::Collection>(API_URI, 0, 7, "Collection", error_msg);
        qmlRegisterUncreatableType<model::Game>(API_URI, 0, 2, "Game", error_msg);
        qmlRegisterUncreatableType<model::Assets>(API_URI, 0, 2, "GameAssets", error_msg);
        qmlRegisterUncreatableType<model::Locales>(API_URI, 0, 11, "Locales", error_msg);
        qmlRegisterUncreatableType<model::Themes>(API_URI, 0, 11, "Themes", error_msg);
        qmlRegisterUncreatableType<model::Providers>(API_URI, 0, 11, "Providers", error_msg);
        qmlRegisterUncreatableType<model::Key>(API_URI, 0, 10, "Key", error_msg);
        qmlRegisterUncreatableType<model::Keys>(API_URI, 0, 10, "Keys", error_msg);
        qmlRegisterUncreatableType<model::GamepadManager>(API_URI, 0, 12, "GamepadManager", error_msg);
        qmlRegisterUncreatableType<model::DeviceInfo>(API_URI, 0, 13, "Device", error_msg);
        qmlRegisterUncreatableType<model::CoreInstaller>(API_URI, 0, 14, "CoreInstaller", error_msg);
    }

    // QML utilities
    qmlRegisterType<FolderListModel>("Pegasus.FolderListModel", 1, 0, "FolderListModel");
    qmlRegisterType<FolderListModel>("Ponia.FolderListModel", 1, 0, "FolderListModel");

    // third-party
    qqsfpm::registerSorterTypes();
    qqsfpm::registerFiltersTypes();
    qqsfpm::registerProxyRoleTypes();
    qqsfpm::registerQQmlSortFilterProxyModelTypes();
}

void on_app_close(AppCloseType type)
{
    if (type == AppCloseType::SUSPEND) {
        return platform::power::suspend();
    }
    ScriptRunner::run(ScriptEvent::QUIT);
    switch (type) {
        case AppCloseType::REBOOT:
            ScriptRunner::run(ScriptEvent::REBOOT);
            break;
        case AppCloseType::SHUTDOWN:
            ScriptRunner::run(ScriptEvent::SHUTDOWN);
            break;
        default: break;
    }

    Log::info(LOGMSG("Closing PoniaEmu, goodbye!"));
    Log::close();

    QCoreApplication::quit();
#ifdef Q_OS_ANDROID
    // quit() leaves the activity on screen; close it so Android returns to the home screen
    if (type == AppCloseType::QUIT)
        QJniObject::callStaticMethod<void>(android::jni_classname(), "quitApp", "()V");
#endif
    switch (type) {
        case AppCloseType::REBOOT:
            platform::power::reboot();
            break;
        case AppCloseType::SHUTDOWN:
            platform::power::shutdown();
            break;
        default: break;
    }
}
} // namespace


namespace backend {

Backend::Backend()
    : Backend(CliArgs {})
{}

Backend::~Backend()
{
    delete m_hide_timer;
    delete m_launcher;
    delete m_frontend;
    delete m_providerman;
    delete m_api_private;
    delete m_api_public;

#if defined(WITH_SDL_GAMEPAD) || defined(WITH_SDL_POWER)
    SDL_Quit();
#endif
}

Backend::Backend(const CliArgs& args)
    : m_args(args)
{
    // Make sure this comes before any file related operations
    AppSettings::general.portable = args.portable;

    Log::init(args.silent);
    print_metainfo();
    create_config_dirs();
    register_api_classes();

    AppSettings::load_providers();
    AppSettings::load_config();

    m_api_public = new model::ApiObject(args);
    m_api_private = new model::Internal(args);
    m_frontend = new FrontendLayer(m_api_public, m_api_private);
    m_launcher = new ProcessLauncher();
    m_providerman = new ProviderManager();

    m_hide_timer = new QTimer();
    m_hide_timer->setSingleShot(true);
    QObject::connect(m_hide_timer, &QTimer::timeout, [this](){
        Log::info(LOGMSG("Hiding the frontend after the configured delay"));
        m_frontend->teardown();
    });

#ifdef Q_OS_ANDROID
    // The frontend is kept alive while a game (another app) runs. Nothing reports when that app ends, so
    // watch for the frontend returning to the foreground; without it the gamepad stays stopped.
    QObject::connect(qGuiApp, &QGuiApplication::applicationStateChanged, [this](Qt::ApplicationState state){
        if (!m_android_game_running)
            return;
        if (state != Qt::ApplicationActive) {
            m_android_left_foreground = true;
        }
        else if (m_android_left_foreground) {
            m_android_game_running = false;
            m_android_left_foreground = false;
            Log::info(LOGMSG("Back from the game"));
            m_launcher->onAndroidReturned();
        }
    });
#endif

    // the following communication is required because process handling
    // and destroying/rebuilding the frontend stack are asynchronous tasks;
    // see the relevant classes. Depending on the settings and the platform,
    // the frontend may also be kept alive while the game runs.

    // the Api asks the Launcher to start the game
    QObject::connect(m_api_public, &model::ApiObject::launchGameFile,
                     m_launcher, &ProcessLauncher::onLaunchRequested);

    // the Launcher tries to start the game, ask the Frontend
    // to tear down the UI, then report back to the Api
    QObject::connect(m_launcher, &ProcessLauncher::processLaunchOk,
                     m_api_public, &model::ApiObject::onGameLaunchOk);

    QObject::connect(m_api_public, &model::ApiObject::gameFileLaunched,
                     m_providerman, &ProviderManager::onGameLaunched);

    QObject::connect(m_launcher, &ProcessLauncher::processLaunchError,
                     m_api_public, &model::ApiObject::onGameLaunchError);

    QObject::connect(m_launcher, &ProcessLauncher::processLaunchOk,
                     [this](){ onProcessLaunched(); });

    QObject::connect(m_frontend, &FrontendLayer::teardownComplete,
                     m_launcher, &ProcessLauncher::onTeardownComplete);
    QObject::connect(m_frontend, &FrontendLayer::teardownComplete,
                     [this](){ onTeardownDone(); });
    QObject::connect(m_launcher, &ProcessLauncher::processLaunchError,
                     [this](){ onProcessLaunchFailed(); });
    m_launcher->setBeforeStart([this](){ return beforeProcessStart(); });

    // when the game ends, the Launcher wakes up the Api and the Frontend
    QObject::connect(m_launcher, &ProcessLauncher::processFinished,
                     m_api_public, &model::ApiObject::onGameProcessFinished);
    QObject::connect(m_api_public, &model::ApiObject::gameFileFinished,
                     m_providerman, &ProviderManager::onGameFinished);

    QObject::connect(m_launcher, &ProcessLauncher::processFinished,
                     [this](){ onProcessFinished(); });

    // Setting changes
    QObject::connect(m_api_private->settings().localesPtr(), &model::Locales::localeChanged,
                     m_api_public, &model::ApiObject::onLocaleChanged);
    QObject::connect(m_api_private->settings().themesPtr(), &model::Themes::themeChanged,
                     m_api_public, &model::ApiObject::onThemeChanged);
    QObject::connect(m_api_private->settings().keyEditorPtr(), &model::KeyEditor::keysChanged,
                     m_api_public->keysPtr(), &model::Keys::refresh_keys);
    QObject::connect(m_api_private->settingsPtr(), &model::Settings::providerReloadingRequested,
                     [this](){ onScanRequested(true); });

    QObject::connect(m_api_public, &model::ApiObject::favoritesChanged,
                     [this](){ onFavoritesChanged(); });

    // Loading progress
    QObject::connect(m_providerman, &ProviderManager::scanStarted,
                     m_api_private->scannerPtr(), &model::ScannerState::onScanStarted);
    QObject::connect(m_providerman, &ProviderManager::scanFinished,
                     m_api_private->scannerPtr(), &model::ScannerState::onScanFinished);
    QObject::connect(m_providerman, &ProviderManager::scanProgressChanged,
                     m_api_private->scannerPtr(), &model::ScannerState::onScanProgressChanged);
    QObject::connect(m_providerman, &ProviderManager::scanFinished,
                     [this](){ onScanFinished(); });
    QObject::connect(m_api_public, &model::ApiObject::gamedataReady,
                     m_api_private->scannerPtr(), &model::ScannerState::onUiReady);

    // partial QML reload
    QObject::connect(&m_api_private->meta(), &model::Meta::qmlClearCacheRequested,
                     m_frontend, &FrontendLayer::clearCache);

    // quit/reboot/shutdown request
    QObject::connect(&m_api_private->system(), &model::System::appCloseRequested, on_app_close);
}

void Backend::start()
{
    m_api_private->settings().postInit();
    onProcessFinished();
    onScanRequested(AppSettings::general.scan_on_launch);
}

void Backend::onScanRequested(const bool force_refresh)
{
    m_api_public->clearGameData();
    m_providerman->run(force_refresh);
}

void Backend::onScanFinished()
{
    std::vector<model::Collection*> colls;
    std::swap(m_providerman->foundCollections(), colls);

    std::vector<model::Game*> games;
    std::swap(m_providerman->foundGames(), games);

    m_api_public->setGameData(std::move(colls), std::move(games));
}

void Backend::onFavoritesChanged()
{
    m_providerman->onFavoritesChanged(m_api_public->allGames()->entries());
}

void Backend::onProcessLaunched()
{
    int delay_sec = AppSettings::general.hide_delay_after_launch;
    if (delay_sec != 0 && platform_requires_hiding()) {
        Log::info(LOGMSG("This platform allows only one program on the screen, hiding PoniaEmu immediately"));
        delay_sec = 0;
    }

    if (delay_sec == 0) {
        // normally the frontend is already gone: it was torn down before the program started (beforeProcessStart)
        if (!m_screen_released && m_frontend->isLoaded()) {
            m_frontend->teardown();
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
            if (QGuiApplication::platformName() == QLatin1String("eglfs"))
                set_drm_master(false);
#endif
        }
    }
    else if (delay_sec > 0) {
        Log::info(LOGMSG("Keeping the frontend visible for %1 seconds").arg(delay_sec));
        m_hide_timer->start(delay_sec * 1000);
    }
    else {
        Log::info(LOGMSG("Keeping the frontend visible while the program runs"));
    }
#ifdef Q_OS_ANDROID
    if (delay_sec < 0) {
        m_android_game_running = true;
        m_android_left_foreground = false;
    }
#endif

    m_api_private->gamepad().stop();
}

// Called right before a program is started. When the frontend is going to hide (the default delay of 0, or a platform that allows
// only one program on the screen), tear it down first and start the program only when that is done and the screen has been given up.
bool Backend::beforeProcessStart()
{
#ifdef Q_OS_ANDROID
    return false;
#else
    int delay_sec = AppSettings::general.hide_delay_after_launch;
    if (delay_sec != 0 && platform_requires_hiding())
        delay_sec = 0;
    if (delay_sec != 0 || !m_frontend->isLoaded())
        return false;

    m_start_after_teardown = true;
    m_frontend->teardown();   // asynchronous: teardownComplete follows, and onTeardownDone() starts the program
    return true;
#endif
}

void Backend::onTeardownDone()
{
    if (!m_start_after_teardown)
        return;
    m_start_after_teardown = false;
    m_screen_released = true;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (QGuiApplication::platformName() == QLatin1String("eglfs"))
        set_drm_master(false);
#endif
    m_launcher->startPending();
}

// The program could not be started although the frontend has already been torn down: bring the frontend back.
void Backend::onProcessLaunchFailed()
{
    if (!m_screen_released)
        return;
    m_screen_released = false;
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (QGuiApplication::platformName() == QLatin1String("eglfs"))
        set_drm_master(true);
#endif
    if (!m_frontend->isLoaded())
        m_frontend->rebuild();
    m_api_private->gamepad().start(m_args);
}

void Backend::onProcessFinished()
{
    m_screen_released = false;
    m_hide_timer->stop();
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (QGuiApplication::platformName() == QLatin1String("eglfs"))
        set_drm_master(true);
#endif

    if (m_frontend->isLoaded())
        m_frontend->bringToFront();
    else
        m_frontend->rebuild(true);

    m_api_private->gamepad().start(m_args);
}

} // namespace backend
