// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// First-run Libretro core downloader: finds the cores the collections need but the device lacks, asks the theme to
// show a consent dialog (api.cores.needed), downloads them from the Libretro buildbot and unpacks them into the
// cores directory that PoniaPlay searches.
#pragma once

#include <QObject>
#include <QStringList>
#include <QString>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace model {
class Collection;

class CoreInstaller : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool needed READ needed NOTIFY changed)               // show the consent dialog
    Q_PROPERTY(int missingCount READ missingCount NOTIFY changed)
    Q_PROPERTY(int totalKb READ totalKb NOTIFY changed)              // approximate download size
    Q_PROPERTY(QStringList missingNames READ missingNames NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)              // idle | downloading | done | error
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)  // 0..1 over all cores
    Q_PROPERTY(QString current READ current NOTIFY progressChanged)  // core being downloaded
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)

public:
    explicit CoreInstaller(QObject* parent = nullptr);

    bool needed() const;
    int missingCount() const { return int(m_missing.size()); }
    int totalKb() const;
    QStringList missingNames() const;
    QString state() const { return m_state; }
    qreal progress() const { return m_progress; }
    QString current() const { return m_current; }
    QString errorText() const { return m_error; }

    // Called when the game data is ready.
    void scan(const std::vector<model::Collection*>& collections);

    Q_INVOKABLE void start();     // user agreed: download everything that is missing
    Q_INVOKABLE void dismiss();   // user declined: do not ask again until the set of missing cores changes
    Q_INVOKABLE void acknowledge();   // user confirmed the "done" or "error" message: back to idle (the frontend is rebuilt after every game, so this must not live in the theme)

signals:
    void changed();
    void progressChanged();

private:
    void downloadNext();
    void onReplyFinished();
    void installZip(const QByteArray& zip);   // unpack + write the .so, then continue with the next core
    void pollAndroidDownload();
    void finish(const QString& state, const QString& error = QString());
    QString signature() const;

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    std::vector<QString> m_missing;     // core ids
    QString m_state = QStringLiteral("idle");
    QString m_error;
    QString m_current;
    qreal m_progress = 0;
    int m_index = 0;
    bool m_declined = false;
    std::vector<QString> m_queue;
    QTimer* m_poll = nullptr;          // Android: polls the Java downloader
    QString m_zip_path;
};
} // namespace model
