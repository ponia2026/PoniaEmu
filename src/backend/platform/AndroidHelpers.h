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

#include <QString>


namespace android {

const char* jni_classname();

QString primary_storage_path();
QStringList storage_paths();
bool has_external_storage_access();

QStringList granted_paths();
void request_saf_permission(const std::function<void()>&);

// Unpack the bundled themes under assets/themes/ of the APK into themes_dir (only unpacked again when the version changed)
void install_bundled_themes(const QString& themes_dir);

// End this process and start the app again a moment later (used when switching themes, to avoid the QMediaPlayer destruction hang)
void restart_app();

QString run_am_call(const QStringList&);
// Built-in Libretro player: opens PlayerActivity (separate process). Returns an empty string on success, otherwise an error message.
QString launch_player(const QStringList&);
// Download a file through the system TLS (Java HttpURLConnection): the Qt 6.4 Android build has no usable TLS backend. After start, poll for the progress.
void start_download(const QString& url, const QString& dest_path);
QString poll_download();   // "state|done|total|error"
QString to_content_uri(const QString&);
QString to_document_uri(const QString&);

} // namespace android
