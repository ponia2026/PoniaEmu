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

// 把 APK 里 assets/themes/ 下的附带主题解包到 themes_dir（版本变了才重新解包）
void install_bundled_themes(const QString& themes_dir);

// 结束本进程并在片刻后重新启动 App（用于切换主题，避免销毁 QMediaPlayer 卡死）
void restart_app();

QString run_am_call(const QStringList&);
// 内置 Libretro 播放器：开 PlayerActivity（独立进程）。回传空字串＝成功，否则为错误信息。
QString launch_player(const QStringList&);
// 用系统 TLS（Java HttpURLConnection）下载档案：Qt 6.4 安卓版没有可用的 TLS 后端。start 后用 poll 取进度。
void start_download(const QString& url, const QString& dest_path);
QString poll_download();   // "state|done|total|error"
QString to_content_uri(const QString&);
QString to_document_uri(const QString&);

} // namespace android
