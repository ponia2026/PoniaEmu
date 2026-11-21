// Pegasus Frontend
// Copyright (C) 2017-2019  Mátyás Mustoha
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


#pragma once

#include <QObject>
#include <QProcess>

#include <functional>

namespace model { class GameFile; }


namespace helpers {
QString abs_launchcmd(const QString& cmd, const QString& base_dir);
QString abs_workdir(const QString& workdir, const QString& base_dir, const QString& fallback_workdir);
} // namespace helpers


/// Launches and manages external processes
///
/// Launches external processes and detects their success or failure.
class ProcessLauncher : public QObject {
    Q_OBJECT

public:
    explicit ProcessLauncher(QObject* parent = nullptr);

    // Set by the Backend. It is called right before the program is started and returns true when it needs the frontend to give up
    // the screen first: the program is then held back until the Backend calls startPending() (see Backend::beforeProcessStart).
    void setBeforeStart(std::function<bool()> callback) { m_before_start = std::move(callback); }
    void startPending();

signals:
    void processLaunchOk();
    void processLaunchError(QString);
    void processRuntimeError(QString);
    void processFinished();

public slots:
    void onLaunchRequested(const model::GameFile*);
    void onTeardownComplete();
    // Android: the launched app is a separate activity, so nobody reports its end; the Backend calls this
    // when PoniaEmu comes back to the foreground.
    void onAndroidReturned() { emit processFinished(); }

private slots:
    void onProcessStarted();
    void onProcessError(QProcess::ProcessError);
    void onProcessFinished(int, QProcess::ExitStatus);

private:
    QProcess* m_process;
    bool m_waiting_after_teardown;

    std::function<bool()> m_before_start;
    bool m_has_pending = false;
    QString m_pending_command;
    QStringList m_pending_args;
    QString m_pending_workdir;

    void runProcess(const QString&, const QStringList&, const QString&);
    void startProcessNow(const QString&, const QStringList&, const QString&);

    void beforeRun(const QString&);
    void afterRun();
};
