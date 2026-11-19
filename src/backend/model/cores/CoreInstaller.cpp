// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works

#include "CoreInstaller.h"

#include "Log.h"
#include "model/gaming/Collection.h"
#include "ponia_cores.h"
#include "ponia_systems.h"
#include "GameSystem.h"
#ifdef Q_OS_ANDROID
#include "platform/AndroidHelpers.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

#include <algorithm>
#include <zlib.h>


namespace {
QString cores_dir()
{
#ifdef Q_OS_ANDROID
    // = getFilesDir()/cores, which PlayerActivity passes to PoniaPlay as a cores directory
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/cores");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/poniaemu/cores");
#endif
}

QStringList search_dirs()
{
    QStringList dirs { cores_dir() };
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + QStringLiteral("/cores") << app;   // on Android `app` is the APK native library directory
    const QByteArray env = qgetenv("PONIA_CORES_DIR");
    if (!env.isEmpty())
        dirs << QString::fromLocal8Bit(env);
#ifndef Q_OS_ANDROID
    dirs << QStringLiteral("/usr/lib/poniaemu/cores") << QStringLiteral("/usr/lib/libretro");
#endif
    return dirs;
}

bool core_installed(const std::string& id)
{
    const QString file = QString::fromStdString(ponia::core_file(id));
    for (const QString& d : search_dirs()) {
        if (QFileInfo::exists(d + QLatin1Char('/') + file) || QFileInfo::exists(d + QStringLiteral("/lib") + file))
            return true;
    }
    return false;
}

QString download_url(const ponia::CoreInfo& ci)
{
    const QString name = QString::fromLatin1(ci.dl_id ? ci.dl_id : ci.id);
    const QString arch = QSysInfo::buildCpuArchitecture();
    const QString base = QStringLiteral("https://buildbot.libretro.com/nightly/");
#ifdef Q_OS_ANDROID
    const QString abi = arch == QLatin1String("arm64") ? QStringLiteral("arm64-v8a")
                      : arch == QLatin1String("arm") ? QStringLiteral("armeabi-v7a")
                      : arch == QLatin1String("x86_64") ? QStringLiteral("x86_64") : QStringLiteral("x86");
    return base + QStringLiteral("android/latest/") + abi + QLatin1Char('/') + name + QStringLiteral("_libretro_android.so.zip");
#else
    const QString dir = arch == QLatin1String("arm64") ? QStringLiteral("aarch64")
                      : arch == QLatin1String("arm") ? QStringLiteral("armv7-neon-hf")
                      : arch == QLatin1String("x86_64") ? QStringLiteral("x86_64") : QStringLiteral("x86");
    return base + QStringLiteral("linux/") + dir + QStringLiteral("/latest/") + name + QStringLiteral("_libretro.so.zip");
#endif
}

// First entry of a (non-zip64) zip archive -> bytes. Buildbot packages contain exactly one .so.
bool unzip_first(const QByteArray& zip, QByteArray& out)
{
    auto u16 = [&](int o) { return quint32(uchar(zip[o])) | (quint32(uchar(zip[o + 1])) << 8); };
    auto u32 = [&](int o) { return u16(o) | (u16(o + 2) << 16); };
    const int n = zip.size();
    int eocd = -1;
    for (int i = n - 22; i >= 0 && i >= n - 22 - 65535; --i) {
        if (u32(i) == 0x06054b50u) { eocd = i; break; }
    }
    if (eocd < 0) return false;
    const int cd = int(u32(eocd + 16));
    if (cd < 0 || cd + 46 > n || u32(cd) != 0x02014b50u) return false;
    const quint32 method = u16(cd + 10);
    const quint32 csize = u32(cd + 20), usize = u32(cd + 24);
    const int lho = int(u32(cd + 42));
    if (lho < 0 || lho + 30 > n || u32(lho) != 0x04034b50u) return false;
    const int data = lho + 30 + int(u16(lho + 26)) + int(u16(lho + 28));
    if (data < 0 || qint64(data) + csize > n) return false;
    if (method == 0) { out = zip.mid(data, int(csize)); return out.size() == int(usize); }
    if (method != 8) return false;
    out.resize(int(usize));
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;
    s.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(zip.constData() + data));
    s.avail_in = csize;
    s.next_out = reinterpret_cast<Bytef*>(out.data());
    s.avail_out = usize;
    const int r = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    return r == Z_STREAM_END;
}
} // namespace


namespace model {
CoreInstaller::CoreInstaller(QObject* parent)
    : QObject(parent)
{}

QString CoreInstaller::signature() const
{
    QStringList ids;
    for (const QString& id : m_missing) ids << id;
    ids.sort();
    return ids.join(QLatin1Char(','));
}

bool CoreInstaller::needed() const
{
    return m_state == QLatin1String("idle") && !m_missing.empty() && !m_declined;
}

int CoreInstaller::totalKb() const
{
    int kb = 0;
    for (const QString& id : m_missing) {
        if (const ponia::CoreInfo* ci = ponia::core_info(id.toStdString())) kb += int(ci->approx_kb);
    }
    return kb;
}

QStringList CoreInstaller::missingNames() const
{
    QStringList l;
    for (const QString& id : m_missing) l << id;
    return l;
}

void CoreInstaller::scan(const std::vector<model::Collection*>& collections)
{
    QSet<QString> wanted;
    QSet<QString> seen_systems;
    const auto want_system = [&](const std::string& sys) {
        if (sys.empty()) return;
        const QString key = QString::fromStdString(sys);
        if (seen_systems.contains(key)) return;
        seen_systems.insert(key);
        const std::string cid = ponia::core_id_for_system(sys);
        if (!cid.empty() && ponia::core_info(cid) && !core_installed(cid))
            wanted.insert(QString::fromStdString(cid));
    };
    for (const model::Collection* col : collections) {
        const auto& games = col->gameList()->entries();
        if (games.empty()) {   // empty collection: fall back to the collection short name/name
            want_system(ponia::resolve_system(col->shortName().toStdString(), col->name().toStdString()));
            continue;
        }
        for (const model::Game* game : games)   // merged collections (such as ATARI) are judged game by game, which is the only way to tell 2600/5200/7800 apart
            want_system(ponia::system_key_for_game(*game));
    }
    m_missing.assign(wanted.cbegin(), wanted.cend());
    std::sort(m_missing.begin(), m_missing.end());

    QFile marker(cores_dir() + QStringLiteral("/.declined"));
    m_declined = marker.open(QIODevice::ReadOnly) && QString::fromUtf8(marker.readAll()).trimmed() == signature();
    Log::info(LOGMSG("Libretro cores missing: %1%2").arg(signature(), m_declined ? QStringLiteral(" (declined earlier)") : QString()));
    emit changed();
}

void CoreInstaller::dismiss()
{
    QDir().mkpath(cores_dir());
    QFile marker(cores_dir() + QStringLiteral("/.declined"));
    if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
        marker.write(signature().toUtf8());
    m_declined = true;
    emit changed();
}

void CoreInstaller::start()
{
    if (m_state == QLatin1String("downloading") || m_missing.empty())
        return;
    if (!QDir().mkpath(cores_dir())) {
        finish(QStringLiteral("error"), tr("Cannot create the cores folder."));
        return;
    }
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    m_queue = m_missing;
    m_index = 0;
    m_progress = 0;
    m_error.clear();
    m_state = QStringLiteral("downloading");
    emit changed();
    downloadNext();
}

void CoreInstaller::downloadNext()
{
    if (m_index >= int(m_queue.size())) {
        finish(QStringLiteral("done"));
        return;
    }
    const QString id = m_queue[m_index];
    const ponia::CoreInfo* ci = ponia::core_info(id.toStdString());
    if (!ci) { ++m_index; downloadNext(); return; }

    m_current = id;
    emit progressChanged();

    const QUrl url(download_url(*ci));
    Log::info(LOGMSG("Downloading core %1 from %2").arg(id, url.toString()));

#ifdef Q_OS_ANDROID
    // Qt 6.4 on Android cannot do HTTPS without bundled OpenSSL: let Java download it, then poll.
    m_zip_path = cores_dir() + QLatin1Char('/') + id + QStringLiteral(".zip.part");
    android::start_download(url.toString(), m_zip_path);
    if (!m_poll) {
        m_poll = new QTimer(this);
        m_poll->setInterval(250);
        connect(m_poll, &QTimer::timeout, this, &CoreInstaller::pollAndroidDownload);
    }
    m_poll->start();
    return;
#endif

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        const qreal frac = total > 0 ? qreal(got) / qreal(total) : 0;
        m_progress = (m_index + frac) / qreal(m_queue.size());
        emit progressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, &CoreInstaller::onReplyFinished);
}

void CoreInstaller::onReplyFinished()
{
    QNetworkReply* reply = m_reply;
    m_reply = nullptr;
    const QString id = m_queue[m_index];
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        finish(QStringLiteral("error"), tr("Download failed (%1): %2").arg(id, reply->errorString()));
        return;
    }
    installZip(reply->readAll());
}

void CoreInstaller::pollAndroidDownload()
{
#ifdef Q_OS_ANDROID
    const QStringList p = android::poll_download().split(QLatin1Char('|'));
    if (p.size() < 4)
        return;
    const QString st = p[0];
    const qint64 done = p[1].toLongLong(), total = p[2].toLongLong();
    if (st == QLatin1String("running")) {
        const qreal frac = total > 0 ? qreal(done) / qreal(total) : 0;
        m_progress = (m_index + frac) / qreal(m_queue.size());
        emit progressChanged();
        return;
    }
    m_poll->stop();
    const QString id = m_queue[m_index];
    if (st == QLatin1String("error")) {
        QFile::remove(m_zip_path);
        finish(QStringLiteral("error"), tr("Download failed (%1): %2").arg(id, p.mid(3).join(QLatin1Char('|'))));
        return;
    }
    QFile zf(m_zip_path);
    QByteArray zip;
    if (zf.open(QIODevice::ReadOnly))
        zip = zf.readAll();
    zf.close();
    QFile::remove(m_zip_path);
    installZip(zip);
#endif
}

void CoreInstaller::installZip(const QByteArray& zip)
{
    const QString id = m_queue[m_index];
    QByteArray so;
    if (!unzip_first(zip, so) || so.size() < 1024) {
        finish(QStringLiteral("error"), tr("Unpacking failed (%1).").arg(id));
        return;
    }
    const QString path = cores_dir() + QLatin1Char('/') + QString::fromStdString(ponia::core_file(id.toStdString()));
    QFile f(path + QStringLiteral(".part"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(so) != so.size()) {
        finish(QStringLiteral("error"), tr("Cannot write %1.").arg(path));
        return;
    }
    f.close();
    QFile::remove(path);
    f.rename(path);
    QFile::setPermissions(path, QFile::permissions(path) | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    Log::info(LOGMSG("Installed core %1 (%2 bytes)").arg(path).arg(so.size()));

    ++m_index;
    m_progress = qreal(m_index) / qreal(m_queue.size());
    emit progressChanged();
    downloadNext();
}

void CoreInstaller::finish(const QString& state, const QString& error)
{
    m_state = state;
    m_error = error;
    if (state == QLatin1String("done")) {
        m_progress = 1;
        m_missing.clear();
        QFile::remove(cores_dir() + QStringLiteral("/.declined"));
    }
    else if (!error.isEmpty()) {
        Log::warning(error);
    }
    emit progressChanged();
    emit changed();
}
} // namespace model
