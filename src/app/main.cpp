// Pegasus Frontend
// Copyright (C) 2017-2020  Mátyás Mustoha
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


#include "backend/Backend.h"
#include "ponia_version.h"
#include "backend/Paths.h"
#include "backend/platform/TerminalKbd.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QStandardPaths>
#include <QSettings>
#include <QtPlugin>

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QVector>
#include <memory>
#endif

#ifdef Q_OS_ANDROID
#include "backend/platform/AndroidHelpers.h"
#endif


#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
namespace {

struct DrmConnector {
    QString type;       // kernel connector type, for example "HDMI-A"
    QString qt_name;    // name that Qt's eglfs KMS code gives the output, for example "HDMI1"
    bool connected = false;
    QStringList modes;  // "1920x1080", ... in the order the kernel lists them
};

// Qt names an eglfs output after the connector type and its id, and shortens some of the types.
QString qt_output_prefix(const QString& kernel_type)
{
    static const QHash<QString, QString> names = {
        { QStringLiteral("HDMI-A"), QStringLiteral("HDMI") },
        { QStringLiteral("HDMI-B"), QStringLiteral("HDMI") },
        { QStringLiteral("DP"), QStringLiteral("DP") },
        { QStringLiteral("eDP"), QStringLiteral("eDP") },
        { QStringLiteral("DVI-I"), QStringLiteral("DVI") },
        { QStringLiteral("DVI-D"), QStringLiteral("DVI") },
        { QStringLiteral("DVI-A"), QStringLiteral("DVI") },
        { QStringLiteral("VGA"), QStringLiteral("VGA") },
        { QStringLiteral("LVDS"), QStringLiteral("LVDS") },
        { QStringLiteral("DSI"), QStringLiteral("DSI") },
        { QStringLiteral("Composite"), QStringLiteral("Composite") },
        { QStringLiteral("TV"), QStringLiteral("TV") },
        { QStringLiteral("S-Video"), QStringLiteral("TV") },
    };
    return names.value(kernel_type);
}

QString read_first_line(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll()).trimmed();
}

// The connectors of the first graphics card that has any (eglfs uses that card too).
QVector<DrmConnector> list_drm_connectors()
{
    QVector<DrmConnector> result;
    const QDir drm(QStringLiteral("/sys/class/drm"));
    const QStringList entries = drm.entryList({ QStringLiteral("card*-*") }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    QString card;
    for (const QString& entry : entries) {
        const int dash = entry.indexOf(QLatin1Char('-'));
        const QString this_card = entry.left(dash);
        if (card.isEmpty())
            card = this_card;
        if (this_card != card)
            continue;

        const QString rest = entry.mid(dash + 1);                // for example "HDMI-A-1"
        const int last_dash = rest.lastIndexOf(QLatin1Char('-'));
        if (last_dash < 0)
            continue;

        DrmConnector connector;
        connector.type = rest.left(last_dash);
        const QString prefix = qt_output_prefix(connector.type);
        if (prefix.isEmpty())
            continue;
        connector.qt_name = prefix + rest.mid(last_dash + 1);
        connector.connected = read_first_line(drm.filePath(entry + QStringLiteral("/status"))) == QLatin1String("connected");

        // sysfs files report a bogus size, so atEnd() never becomes true: read everything at once.
        QFile modes(drm.filePath(entry + QStringLiteral("/modes")));
        if (modes.open(QIODevice::ReadOnly | QIODevice::Text))
            connector.modes = QString::fromUtf8(modes.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        result.append(connector);
    }
    return result;
}

// Some boards expose an unused analog output (composite/TV) next to HDMI. Qt's eglfs backend hands the only
// display controller to the first output it finds, which leaves HDMI without one: the program runs, but the
// screen stays black. Write a KMS configuration that drives the connected digital output and switches the others off.
// A user supplied QT_QPA_EGLFS_KMS_CONFIG or ~/.config/poniaemu/kms.json takes precedence.
std::unique_ptr<QTemporaryFile> prepare_eglfs_output()
{
    if (!qEnvironmentVariableIsEmpty("QT_QPA_EGLFS_KMS_CONFIG"))
        return nullptr;

    const QString user_config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                              + QStringLiteral("/poniaemu/kms.json");
    if (QFileInfo::exists(user_config)) {
        qputenv("QT_QPA_EGLFS_KMS_CONFIG", user_config.toUtf8());
        return nullptr;
    }

    const QVector<DrmConnector> connectors = list_drm_connectors();
    static const QStringList priority = {
        QStringLiteral("HDMI-A"), QStringLiteral("HDMI-B"), QStringLiteral("DP"), QStringLiteral("DVI-I"),
        QStringLiteral("DVI-D"), QStringLiteral("DVI-A"), QStringLiteral("VGA"), QStringLiteral("eDP"),
        QStringLiteral("DSI"), QStringLiteral("LVDS"),
    };
    const DrmConnector* chosen = nullptr;
    for (const QString& type : priority) {
        for (const DrmConnector& connector : connectors) {
            if (connector.connected && connector.type == type) {
                chosen = &connector;
                break;
            }
        }
        if (chosen)
            break;
    }
    if (!chosen)
        return nullptr;

    // Low-end GPUs cannot drive a 4K desktop-sized scene smoothly, so use 1080p when the display offers it.
    // PONIAEMU_KMS_MODE overrides it, for example "3840x2160" or "preferred".
    QString mode = qEnvironmentVariable("PONIAEMU_KMS_MODE");
    if (mode.isEmpty())
        mode = chosen->modes.contains(QLatin1String("1920x1080")) ? QStringLiteral("1920x1080") : QStringLiteral("preferred");

    QJsonArray outputs;
    for (const DrmConnector& connector : connectors) {
        QJsonObject output;
        output[QStringLiteral("name")] = connector.qt_name;
        output[QStringLiteral("mode")] = (&connector == chosen) ? mode : QStringLiteral("off");
        outputs.append(output);
    }
    QJsonObject root;
    root[QStringLiteral("hwcursor")] = false;
    root[QStringLiteral("outputs")] = outputs;

    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/poniaemu-kms-XXXXXX.json"));
    if (!file->open())
        return nullptr;
    file->write(QJsonDocument(root).toJson());
    file->flush();
    qputenv("QT_QPA_EGLFS_KMS_CONFIG", file->fileName().toUtf8());
    return file;
}

// Is a mouse (or anything else that moves a pointer) connected? The kernel lists the handlers of every input device.
bool has_mouse()
{
    QFile devices(QStringLiteral("/proc/bus/input/devices"));
    if (!devices.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;   // cannot tell: keep Qt's default
    while (!devices.atEnd()) {
        const QByteArray line = devices.readLine();
        if (line.startsWith("H: Handlers=") && line.contains("mouse"))
            return true;
    }
    return false;
}

// Without a desktop session Qt would default to the xcb plugin and fail ("could not connect to display").
// Use the framebuffer/KMS output then, so that starting the program from a console works on a TV box.
std::unique_ptr<QTemporaryFile> prepare_linux_display()
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")
        && qEnvironmentVariableIsEmpty("DISPLAY")
        && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "eglfs");
    }

    if (qgetenv("QT_QPA_PLATFORM").startsWith("eglfs")) {
        // The frontend is driven by a gamepad, so without a mouse there is nothing to show a pointer for. On KMS, Qt draws the
        // pointer with its own software cursor, and on some boards that code crashes right after a game has ended and the
        // frontend takes the screen back. QT_QPA_EGLFS_HIDECURSOR=0 (or any value set by the user) keeps the pointer.
        if (!qEnvironmentVariableIsSet("QT_QPA_EGLFS_HIDECURSOR") && !has_mouse())
            qputenv("QT_QPA_EGLFS_HIDECURSOR", "1");
        return prepare_eglfs_output();
    }
    return nullptr;
}

} // namespace
#endif

#ifdef WITH_APNG_SUPPORT
Q_IMPORT_PLUGIN(ApngImagePlugin)
#endif


backend::CliArgs handle_cli_args(QGuiApplication&);
bool request_runtime_permissions();
bool portable_txt_present();

int main(int argc, char *argv[])
{
    Q_INIT_RESOURCE(frontend);
    Q_INIT_RESOURCE(themes);
    Q_INIT_RESOURCE(qmlutils);
#ifdef PEGASUS_USING_CMAKE  // TODO: Unify the build system
    Q_INIT_RESOURCE(assets);
    Q_INIT_RESOURCE(locales);
#endif
#ifndef Q_OS_ANDROID
    Q_INIT_RESOURCE(mm_shaders);   // the "_a" video shaders of Qt Multimedia (src/app/mm_shaders)
#endif

    // Let themes read small local files (e.g. the optional overrides.json of the bundled theme).
    // Qt disables XMLHttpRequest GET on local files unless this is set.
    if (qEnvironmentVariableIsEmpty("QML_XHR_ALLOW_FILE_READ"))
        qputenv("QML_XHR_ALLOW_FILE_READ", "1");

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Must run before the QGuiApplication is created; the file has to stay around until the program exits.
    const auto linux_display_config = prepare_linux_display();
    Q_UNUSED(linux_display_config)
#endif

    TerminalKbd::on_startup();

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // On a console without a desktop (KMS/DRM, eglfs) the Qt Quick context can come up without one-channel (R8) textures. Qt
    // Multimedia then asks for "..._a" video shaders (alpha-texture variants) that the distribution's Qt does not ship, and the
    // videos stay blank ("Failed to find shader ...yuv_triplanar_a.frag.qsb"). An OpenGL ES 3.0 context always has R8.
    // PONIA_GL_FORMAT=es3|core|compat|default overrides the choice (default: es3 when no desktop session is running).
    {
        QString choice = qEnvironmentVariable("PONIA_GL_FORMAT").trimmed().toLower();
        if (choice.isEmpty()) {
            const bool desktop = !qEnvironmentVariableIsEmpty("DISPLAY") || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
            const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
            choice = (!desktop || platform.startsWith("eglfs")) ? QStringLiteral("es3") : QStringLiteral("default");
        }
        QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
        bool set = true;
        if (choice == QLatin1String("es3")) {
            fmt.setRenderableType(QSurfaceFormat::OpenGLES);
            fmt.setVersion(3, 0);
        } else if (choice == QLatin1String("core")) {
            fmt.setRenderableType(QSurfaceFormat::OpenGL);
            fmt.setVersion(3, 3);
            fmt.setProfile(QSurfaceFormat::CoreProfile);
        } else if (choice == QLatin1String("compat")) {
            fmt.setRenderableType(QSurfaceFormat::OpenGL);
            fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
        } else {
            set = false;
        }
        if (set)
            QSurfaceFormat::setDefaultFormat(fmt);
    }
#endif

    QCoreApplication::addLibraryPath(QStringLiteral("lib/plugins"));
    QCoreApplication::addLibraryPath(QStringLiteral("lib"));
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("poniaemu"));
    app.setApplicationVersion(QStringLiteral(PONIA_VERSION));
    app.setOrganizationName(QStringLiteral("poniaemu"));
    app.setOrganizationDomain(QStringLiteral("ponia-frontend.org"));
    app.setWindowIcon(QIcon(QStringLiteral(":/icon.png")));

#ifndef Q_OS_ANDROID
    // The menus use system fonts, and a minimal system may have no CJK font at all (language names showed as boxes).
    // Register the fonts that ship with the bundled theme; Qt then also uses them as fallback for missing glyphs.
    const QStringList font_dirs = QStandardPaths::locateAll(QStandardPaths::AppDataLocation,
        QStringLiteral("themes/macaron/assets/fonts"), QStandardPaths::LocateDirectory);
    for (const QString& font_dir : font_dirs) {
        const QFileInfoList fonts = QDir(font_dir).entryInfoList({ QStringLiteral("*.ttf"), QStringLiteral("*.otf") }, QDir::Files);
        for (const QFileInfo& font : fonts)
            QFontDatabase::addApplicationFont(font.absoluteFilePath());
    }
#endif

    if (!request_runtime_permissions())
        return 1;

    backend::CliArgs cli_args = handle_cli_args(app);
    cli_args.portable |= portable_txt_present();

    backend::Backend backend(cli_args);
    backend.start();

    return app.exec();
}

bool request_runtime_permissions()
{
#ifdef Q_OS_ANDROID
    return android::has_external_storage_access();
#endif

    return true;
}


bool portable_txt_present()
{
#ifdef Q_OS_ANDROID
    // NOTE: On Android, the executable location is not generally accessible
    return false;
#else
    const QString path = paths::app_dir_path() + QStringLiteral("/portable.txt");
    return QFileInfo::exists(path);
#endif
}


QCommandLineOption add_cli_option(QCommandLineParser& parser, const QString& name, const QString& desc)
{
    QCommandLineOption arg(name, desc);
    parser.addOption(arg);
    return arg;
}

backend::CliArgs handle_cli_args(QGuiApplication& app)
{
#define CMDMSG QStringLiteral

    QCommandLineParser argparser;
    argparser.setApplicationDescription(CMDMSG(
        "\nPegasus is a graphical frontend for browsing your game library (especially\n"
        "retro games) and launching them from one place. It's focusing on customization,\n"
        "cross platform support (including embedded devices) and high performance."));

    const QCommandLineOption arg_portable = add_cli_option(argparser,
        QStringLiteral("portable"),
        CMDMSG("Do not read or write config files outside the program's directory"));

    const QCommandLineOption arg_silent = add_cli_option(argparser,
        QStringLiteral("silent"),
        CMDMSG("Do not print log messages to the terminal"));

    const QCommandLineOption arg_menu_reboot = add_cli_option(argparser,
        QStringLiteral("disable-menu-reboot"),
        CMDMSG("Hides the system reboot entry in the main menu"));

    const QCommandLineOption arg_menu_shutdown = add_cli_option(argparser,
        QStringLiteral("disable-menu-shutdown"),
        CMDMSG("Hides the system shutdown entry in the main menu"));

    const QCommandLineOption arg_menu_suspend = add_cli_option(argparser,
        QStringLiteral("disable-menu-suspend"),
        CMDMSG("Hides the system suspend entry in the main menu"));

    const QCommandLineOption arg_menu_appclose = add_cli_option(argparser,
        QStringLiteral("disable-menu-appclose"),
        CMDMSG("Hides the closing Pegasus entry in the main menu"));

    const QCommandLineOption arg_menu_settings = add_cli_option(argparser,
        QStringLiteral("disable-menu-settings"),
        CMDMSG("Hides the settings menu entry in the main menu"));

    const QCommandLineOption arg_menu_kiosk = add_cli_option(argparser,
        QStringLiteral("kiosk"),
        CMDMSG("Alias for:\n"
               "--disable-menu-reboot\n"
               "--disable-menu-shutdown\n"
               "--disable-menu-appclose\n"
               "--disable-menu-settings"));

    const QCommandLineOption arg_gamepad_autoconfig = add_cli_option(argparser,
        QStringLiteral("disable-gamepad-autoconfig"),
        CMDMSG("Disables the automatic layout detection for connected gamepads.\n"
               "When you connect a gamepad, Pegasus tries to guess its button and axis layout "
               "automatically based on a list of known devices. Unfortunately this doesn't seem "
               "to work perfectly with some platforms and devices (eg. arcades), in which case "
               "you can disable this feature here."));

    argparser.addHelpOption();
    argparser.addVersionOption();
    argparser.process(app); // may quit!

    backend::CliArgs args;
    args.portable = argparser.isSet(arg_portable);
    args.silent = argparser.isSet(arg_silent);
    args.enable_menu_appclose = !(argparser.isSet(arg_menu_kiosk) || argparser.isSet(arg_menu_appclose));
    args.enable_menu_settings = !(argparser.isSet(arg_menu_kiosk) || argparser.isSet(arg_menu_settings));
    args.enable_gamepad_autoconfig = !argparser.isSet(arg_gamepad_autoconfig);
#ifdef Q_OS_ANDROID
    args.enable_menu_shutdown = false;
    args.enable_menu_reboot = false;
#else
    args.enable_menu_shutdown = !(argparser.isSet(arg_menu_kiosk) || argparser.isSet(arg_menu_shutdown));
    args.enable_menu_reboot = !(argparser.isSet(arg_menu_kiosk) || argparser.isSet(arg_menu_reboot));
#endif
    // No "Suspend" entry on any platform: the boards PoniaEmu runs on (TV boxes, small ARM boards) mostly cannot wake up again.
    // The --disable-menu-suspend option is still accepted, it just changes nothing.
    (void)arg_menu_suspend;
    args.enable_menu_suspend = false;
    return args;

#undef CMDMSG
}
