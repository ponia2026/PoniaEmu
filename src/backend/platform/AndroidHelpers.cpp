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


#include "AndroidHelpers.h"

#include "Log.h"
#include "ponia_version.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>  // Required for PermissionResultMap
#include <QStandardPaths>
#include <QCoreApplication>
#include <QtCore/private/qandroidextras_p.h>
#include <QJniEnvironment>
#include <QJniObject>
#include <QUrl>


namespace {
QStringList query_string_array(const char* const method)
{
    static constexpr auto JNI_SIGNATURE = "()[Ljava/lang/String;";

    QJniEnvironment jni_env;
    const auto jni_path_arr_raw = QJniObject::callStaticObjectMethod(android::jni_classname(), method, JNI_SIGNATURE);
    const auto jni_path_arr = jni_path_arr_raw.object<jobjectArray>();
    const jsize path_count = jni_env->GetArrayLength(jni_path_arr);

    QStringList out;
    out.reserve(path_count);

    for (jsize i = 0; i < path_count; i++) {
        const auto jni_path_raw = QJniObject(jni_env->GetObjectArrayElement(jni_path_arr, i));
        out.append(jni_path_raw.toString());
    }

    return out;
}
} // namespace


namespace android {

const char* jni_classname() {
    return "org/ponia/PoniaEmu/MainActivity";
}

QString primary_storage_path()
{
    static constexpr auto JNI_METHOD = "primaryStoragePath";
    static constexpr auto JNI_SIGNATURE = "()Ljava/lang/String;";

    const auto result_obj = QJniObject::callStaticObjectMethod(jni_classname(), JNI_METHOD, JNI_SIGNATURE);
    return result_obj.toString();
}

QStringList storage_paths()
{
    QStringList paths = query_string_array("sdcardPaths");
    if (paths.empty())
        paths.append(primary_storage_path());  // TODO: Qt 6 emplace_back
    return paths;
}

QStringList granted_paths()
{
    return query_string_array("grantedPaths");
}

void request_saf_permission(const std::function<void()>& cb_success)
{
    constexpr int REQ_OPEN_DOCUMENT_TREE = 0x1;

    const auto activity_cb = [cb_success](int requestCode, int resultCode, const QJniObject& data) {
        const jint RESULT_OK = QJniObject::getStaticField<jint>("android/app/Activity", "RESULT_OK");
        if (requestCode != REQ_OPEN_DOCUMENT_TREE || resultCode != RESULT_OK || !data.isValid())
            return;

        const QJniObject uri = data.callObjectMethod("getData", "()Landroid/net/Uri;");
        if (!uri.isValid())
            return;

        static constexpr auto REMEMBER_FN = "rememberGrantedPath";
        static constexpr auto REMEMBER_SIGN = "(Landroid/net/Uri;)V";
        QJniObject::callStaticMethod<void>(jni_classname(), REMEMBER_FN, REMEMBER_SIGN, uri.object());

        cb_success();
    };

    const QJniObject action = QJniObject::fromString(QStringLiteral("android.intent.action.OPEN_DOCUMENT_TREE"));
    const QJniObject intent("android/content/Intent", "(Ljava/lang/String;)V", action.object<jstring>());
    QtAndroidPrivate::startActivity(intent, REQ_OPEN_DOCUMENT_TREE, activity_cb);
}

bool has_external_storage_access()
{
    // Android 11
    if (QNativeInterface::QAndroidApplication::sdkVersion() >= 30) {
        static constexpr auto JNI_METHOD = "getAllStorageAccess";
        const auto has_permission = QJniObject::callStaticMethod<jboolean>(jni_classname(), JNI_METHOD);
        if (!has_permission)
            return false;
    }


    const QStringList required_permissions {
        QStringLiteral("android.permission.WRITE_EXTERNAL_STORAGE"),
    };

    const bool has_all_permissions = std::all_of(
        required_permissions.cbegin(),
        required_permissions.cend(),
        [](const QString& p){ return QtAndroidPrivate::checkPermission(p).result() == QtAndroidPrivate::Authorized; });
    if (has_all_permissions)
        return true;

    for (const QString& p : required_permissions) {
        if (QtAndroidPrivate::requestPermission(p).result() != QtAndroidPrivate::Authorized)
            return false;
    }

    return true;
}

void install_bundled_themes(const QString& themes_dir)
{
    // Qt 的 assets:/ 目录枚举在这里不可靠，交给 Java 的 AssetManager 解包
    static constexpr auto JNI_METHOD = "installBundledThemes";
    static constexpr auto JNI_SIGNATURE = "(Ljava/lang/String;Ljava/lang/String;)V";

    const QJniObject jni_dir = QJniObject::fromString(themes_dir);
    const QJniObject jni_ver = QJniObject::fromString(QStringLiteral(PONIA_ASSET_STAMP));
    QJniObject::callStaticMethod<void>(jni_classname(), JNI_METHOD, JNI_SIGNATURE,
                                       jni_dir.object<jstring>(), jni_ver.object<jstring>());
}

void restart_app()
{
    QJniObject::callStaticMethod<void>(jni_classname(), "restartApp", "()V");
}

QString run_am_call(const QStringList& args)
{
    QJniEnvironment jni_env;

    std::vector<QJniObject> str_objs;
    str_objs.reserve(args.length());
    for (const QString& arg : args)
        str_objs.emplace_back(QJniObject::fromString(arg));

    jobjectArray jni_arr = jni_env->NewObjectArray(args.size(), jni_env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < str_objs.size(); i++)
        jni_env->SetObjectArrayElement(jni_arr, i, str_objs.at(i).object<jstring>());

    static constexpr auto JNI_METHOD = "launchAmCommand";
    static constexpr auto JNI_SIGNATURE = "([Ljava/lang/String;)Ljava/lang/String;";
    const auto result_obj = QJniObject::callStaticObjectMethod(jni_classname(), JNI_METHOD, JNI_SIGNATURE, jni_arr);
    return result_obj.toString();
}

QString launch_player(const QStringList& args)
{
    QJniEnvironment jni_env;

    std::vector<QJniObject> str_objs;
    str_objs.reserve(args.length());
    for (const QString& arg : args)
        str_objs.emplace_back(QJniObject::fromString(arg));

    jobjectArray jni_arr = jni_env->NewObjectArray(args.size(), jni_env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < str_objs.size(); i++)
        jni_env->SetObjectArrayElement(jni_arr, i, str_objs.at(i).object<jstring>());

    static constexpr auto JNI_METHOD = "launchPlayer";
    static constexpr auto JNI_SIGNATURE = "([Ljava/lang/String;)Ljava/lang/String;";
    const auto result_obj = QJniObject::callStaticObjectMethod(jni_classname(), JNI_METHOD, JNI_SIGNATURE, jni_arr);
    return result_obj.toString();
}

void start_download(const QString& url, const QString& dest_path)
{
    static constexpr auto JNI_METHOD = "startDownload";
    static constexpr auto JNI_SIGNATURE = "(Ljava/lang/String;Ljava/lang/String;)V";
    const QJniObject jni_url = QJniObject::fromString(url);
    const QJniObject jni_dest = QJniObject::fromString(dest_path);
    QJniObject::callStaticMethod<void>(jni_classname(), JNI_METHOD, JNI_SIGNATURE,
                                       jni_url.object<jstring>(), jni_dest.object<jstring>());
}

QString poll_download()
{
    static constexpr auto JNI_METHOD = "pollDownload";
    static constexpr auto JNI_SIGNATURE = "()Ljava/lang/String;";
    return QJniObject::callStaticObjectMethod(jni_classname(), JNI_METHOD, JNI_SIGNATURE).toString();
}

QString to_content_uri(const QString& path)
{
    static constexpr auto JNI_METHOD = "toContentUri";
    static constexpr auto JNI_SIGNATURE = "(Ljava/lang/String;)Ljava/lang/String;";

    QJniObject jni_path_str = QJniObject::fromString(path);

    QJniEnvironment jni_env;
    const auto result_obj = QJniObject::callStaticObjectMethod(
        jni_classname(),
        JNI_METHOD,
        JNI_SIGNATURE,
        jni_path_str.object<jobject>());
    return result_obj.toString();
}

QString to_document_uri(const QString& path)
{
    const QFileInfo finfo(path);
    const QString abs_path = finfo.absoluteFilePath();
    const QString abs_dir = finfo.absolutePath();

    const QString most_specific_root = [&abs_path](){
        QString result = QChar('/');
        const QStringList all_roots = storage_paths();
        for (const QString& root : all_roots) {
            if (abs_path.startsWith(root) && result.length() < root.length())
                result = root;
        }
        return result;
    }();
    const QDir storage_root(most_specific_root);

    const QString prefix = [&most_specific_root](){
        if (most_specific_root == primary_storage_path())
            return QStringLiteral("primary:");

        QList<QStringView> parts = QStringView(most_specific_root).split(QChar('/'));
        return parts.last().toString() + QChar(':');
    }();
    const QString rel_dir = prefix + storage_root.relativeFilePath(abs_dir);
    const QString rel_path = prefix + storage_root.relativeFilePath(abs_path);

    const auto uri_encode = [](const QString& path) {
        return QUrl::toPercentEncoding(path, "_-!.~'()*");
    };
    const QString uri_str = QStringLiteral("content://com.android.externalstorage.documents/tree/%1/document/%2")
        .arg(uri_encode(rel_dir), uri_encode(rel_path));

    return uri_str;
}

} // namespace android
