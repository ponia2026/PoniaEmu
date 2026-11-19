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
#  include <sys/ioctl.h>
#  include <unistd.h>
#  include <climits>
#  include <cstdlib>
#  include <cstring>
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
void set_drm_master(bool on)
{
    // DRM_IOCTL_SET_MASTER / DRM_IOCTL_DROP_MASTER (drm.h); defined here so the DRM headers are not needed to build
    const unsigned long request = on ? _IO('d', 0x1e) : _IO('d', 0x1f);
    DIR* dir = ::opendir("/proc/self/fd");
    if (!dir)
        return;
    int handled = 0;
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
        if (std::strncmp(target, "/dev/dri/card", 13) == 0 && ::ioctl(fd, request) == 0)
            ++handled;
    }
    ::closedir(dir);
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
        m_frontend->teardown();
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
        if (QGuiApplication::platformName() == QLatin1String("eglfs"))
            set_drm_master(false);
#endif
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

void Backend::onProcessFinished()
{
    m_hide_timer->stop();
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (QGuiApplication::platformName() == QLatin1String("eglfs"))
        set_drm_master(true);
#endif

    if (m_frontend->isLoaded())
        m_frontend->bringToFront();
    else
        m_frontend->rebuild();

    m_api_private->gamepad().start(m_args);
}

} // namespace backend
