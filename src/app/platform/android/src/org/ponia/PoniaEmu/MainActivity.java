// Pegasus Frontend
// Copyright (C) 2017-2021  Mátyás Mustoha
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


package org.ponia.PoniaEmu;

import android.app.ActivityManager;
import android.content.Context;
import android.app.Activity;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.UriPermission;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.BatteryManager;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.Settings;
import androidx.core.content.FileProvider;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedList;
import java.util.List;


public class MainActivity extends org.qtproject.qt.android.bindings.QtActivity {
    private static Activity m_self;
    private static PackageManager m_pm;
    private static int m_icon_density;


    private static boolean m_sdl_ready = false;
    private static Context m_app_context;
    private org.libsdl.app.HIDDeviceManager m_hid_manager;

    // C++ 的「退出」：QCoreApplication::quit() 只会结束 Qt 事件循环，画面还留在前景。
    // 这里把 Activity 关掉（并从最近任务移除），安卓就会回到桌面；Activity 销毁时一并结束进程。
    public static void quitApp() {
        final Activity a = m_self;
        if (a == null)
            return;
        a.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                a.finishAndRemoveTask();
                // Qt does not end the process by itself after quit(); a stale process would show an empty
                // window the next time the app starts, so end it shortly after the activity is gone.
                new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        android.os.Process.killProcess(android.os.Process.myPid());
                    }
                }, 400);
            }
        });
    }

    @Override
    public void onCreate(android.os.Bundle savedInstanceState) {
        m_app_context = getApplicationContext();
        // 必须在 Qt 的原生线程启动之前，让 SDL 的 Java 端就绪（手柄靠 SDL）
        try {
            System.loadLibrary("SDL2");
            org.libsdl.app.SDL.setupJNI();
            org.libsdl.app.SDL.initialize();
            // SDL 的 HIDAPI 层需要它先注册回调，否则 SDL_hid_init 会空指针崩溃
            m_hid_manager = org.libsdl.app.HIDDeviceManager.acquire(this);
            m_sdl_ready = true;
        } catch (Throwable t) {
            android.util.Log.w("PoniaFE", "SDL init failed: " + t);
        }
        super.onCreate(savedInstanceState);
        if (m_sdl_ready)
            org.libsdl.app.SDL.setContext(this);
    }

    @Override
    protected void onDestroy() {
        final boolean closing = isFinishing();
        if (m_hid_manager != null) {
            org.libsdl.app.HIDDeviceManager.release(m_hid_manager);
            m_hid_manager = null;
        }
        super.onDestroy();
        // 使用者选了「退出」：连进程一起结束，下次启动才是干净的
        if (closing)
            android.os.Process.killProcess(android.os.Process.myPid());
    }

    // 只有真正的手柄/摇杆才交给 SDL；遥控器（只有方向键源）SDL 也会当成摇杆，
    // 若被 SDL 吃掉，Qt 就收不到按键，界面无法操作
    private static boolean isRealGamepad(int deviceId) {
        android.view.InputDevice dev = android.view.InputDevice.getDevice(deviceId);
        if (dev == null)
            return false;
        final int src = dev.getSources();
        return (src & android.view.InputDevice.SOURCE_GAMEPAD) == android.view.InputDevice.SOURCE_GAMEPAD
            || (src & android.view.InputDevice.SOURCE_JOYSTICK) == android.view.InputDevice.SOURCE_JOYSTICK;
    }

    @Override
    public boolean dispatchKeyEvent(android.view.KeyEvent event) {
        if (m_sdl_ready) {
            final int id = event.getDeviceId();
            final boolean is_joy = isRealGamepad(id) && org.libsdl.app.SDLControllerManager.isDeviceSDLJoystick(id);
            if (is_joy) {
                final int code = event.getKeyCode();
                int r = -99;
                if (event.getAction() == android.view.KeyEvent.ACTION_DOWN)
                    r = org.libsdl.app.SDLControllerManager.onNativePadDown(id, code);
                else if (event.getAction() == android.view.KeyEvent.ACTION_UP)
                    r = org.libsdl.app.SDLControllerManager.onNativePadUp(id, code);
                if (r == 0)
                    return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    @Override
    public boolean dispatchGenericMotionEvent(android.view.MotionEvent event) {
        if (m_sdl_ready
            && isRealGamepad(event.getDeviceId())
            && (event.getSource() & android.view.InputDevice.SOURCE_JOYSTICK) != 0
            && event.getAction() == android.view.MotionEvent.ACTION_MOVE
            && org.libsdl.app.SDLControllerManager.handleJoystickMotionEvent(event))
            return true;
        return super.dispatchGenericMotionEvent(event);
    }

    @Override
    protected void onStart() {
        super.onStart();
        m_self = this;
        m_pm = getPackageManager();

        ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        m_icon_density = am.getLauncherLargeIconDensity();
    }


    // Qt 6.4 安卓版：销毁带 QMediaPlayer 的 QML 项（如 Grid 主题的视频/音乐）会在 ~QAndroidMediaPlayer 里永久卡死，
    // 所以切换主题时不销毁界面，直接重启整个 App（0.3 秒后由闹钟拉起，并立刻结束本进程）。
    public static void restartApp() {
        try {
            Context c = m_app_context;
            Intent i = c.getPackageManager().getLaunchIntentForPackage(c.getPackageName());
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            android.app.PendingIntent pi = android.app.PendingIntent.getActivity(
                c, 0, i, android.app.PendingIntent.FLAG_CANCEL_CURRENT);
            android.app.AlarmManager am = (android.app.AlarmManager) c.getSystemService(Context.ALARM_SERVICE);
            am.set(android.app.AlarmManager.RTC, System.currentTimeMillis() + 300, pi);
        } catch (Exception e) {
            android.util.Log.w("PoniaFE", "restartApp failed: " + e);
        }
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    // 把 APK 里 assets/themes/<名字>/ 解包到 destThemesDir/<名字>/（marker 内容等于 version 就跳过）
    public static void installBundledThemes(String destThemesDir, String versionIn) {
        String version = versionIn;
        try {
            android.content.res.AssetManager am = m_app_context.getAssets();
            String[] names = am.list("themes");
            if (names == null)
                return;
            // 标记里带上 APK 的更新时间：同一个 git 版本重新安装（主题内容变了）也会重新解包
            try {
                version = version + "-" + m_app_context.getPackageManager()
                    .getPackageInfo(m_app_context.getPackageName(), 0).lastUpdateTime;
            } catch (Exception ignored) {
            }
            for (String name : names) {
                File dest = new File(destThemesDir, name);
                File marker = new File(dest, ".ponia_bundled");
                if (marker.exists() && version.equals(new String(java.nio.file.Files.readAllBytes(marker.toPath()), "UTF-8").trim()))
                    continue;
                deleteRecursive(dest);
                copyAssetDir(am, "themes/" + name, dest);
                java.io.FileOutputStream out = new java.io.FileOutputStream(marker);
                out.write(version.getBytes("UTF-8"));
                out.close();
            }
        } catch (Exception e) {
            android.util.Log.w("PoniaFE", "installBundledThemes failed: " + e);
        }
    }

    private static void deleteRecursive(File f) {
        File[] children = f.listFiles();
        if (children != null)
            for (File c : children)
                deleteRecursive(c);
        f.delete();
    }

    private static void copyAssetDir(android.content.res.AssetManager am, String assetPath, File dest) throws java.io.IOException {
        String[] children = am.list(assetPath);
        if (children != null && children.length > 0) {
            dest.mkdirs();
            for (String c : children)
                copyAssetDir(am, assetPath + "/" + c, new File(dest, c));
            return;
        }
        dest.getParentFile().mkdirs();
        java.io.InputStream in = am.open(assetPath);
        java.io.FileOutputStream out = new java.io.FileOutputStream(dest);
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0)
            out.write(buf, 0, n);
        out.close();
        in.close();
    }

    public static App[] appList() {
        Intent intent = new Intent(Intent.ACTION_MAIN, null);
        intent.addCategory(Intent.CATEGORY_LAUNCHER);
        List<ResolveInfo> activities = m_pm.queryIntentActivities(intent, 0);

        App[] entries = new App[activities.size()];
        for (int i = 0; i < activities.size(); i++)
            entries[i] = new App(m_pm, activities.get(i));

        return entries;
    }


    public static byte[] appIcon(String packageName) {
        Drawable drawable = null;
        try {
            // NOTE: while there is m_pm.getApplicationInfo(), unfortunately
            //       that returns low density images for most apps
            ApplicationInfo appinfo = m_pm.getApplicationInfo(packageName, 0);
            Resources resources = m_pm.getResourcesForApplication(appinfo);
            Intent launch_intent = m_pm.getLaunchIntentForPackage(packageName);
            ResolveInfo resolveinfo = m_pm.resolveActivity(launch_intent, 0);
            // NOTE: getDrawableForDensity() has changed in API 21-22
            drawable = resources.getDrawableForDensity(resolveinfo.activityInfo.getIconResource(), m_icon_density);
        }
        catch (Exception ex) { }
        if (drawable == null)
            drawable = m_pm.getDefaultActivityIcon();

        Bitmap bitmap = drawableToBitmap(drawable);
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream);
        return stream.toByteArray();
    }


    private static Bitmap drawableToBitmap(Drawable drawable) {
        if (drawable instanceof BitmapDrawable) {
            // TODO: handle null
            return ((BitmapDrawable) drawable).getBitmap();
        }

        int w = Math.max(1, drawable.getIntrinsicWidth());
        int h = Math.max(1, drawable.getIntrinsicHeight());
        Bitmap bitmap = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);

        Canvas canvas = new Canvas(bitmap);
        drawable.setBounds(0, 0, canvas.getWidth(), canvas.getHeight());
        drawable.draw(canvas);
        return bitmap;
    }


    public static String primaryStoragePath() {
        final File storage = Environment.getExternalStorageDirectory();
        return storage.getAbsolutePath();
    }


    public static String[] sdcardPaths() {
        // Functions with high API level dependencies:
        // - https://developer.android.com/reference/android/os/storage/StorageManager#getStorageVolumes()
        // - https://developer.android.com/reference/android/os/storage/StorageVolume#getDirectory()

        final StorageManager storage_man = (StorageManager) m_self.getSystemService(Context.STORAGE_SERVICE);

        List<StorageVolume> storage_vols = null;
        if (Build.VERSION.SDK_INT >= 24) {
            storage_vols = storage_man.getStorageVolumes();
        }
        else {
            try {
                final Method volumelist_getter = StorageManager.class.getMethod("getVolumeList");
                final StorageVolume[] storage_vols_arr = (StorageVolume[]) volumelist_getter.invoke(storage_man);
                storage_vols = Arrays.asList(storage_vols_arr);
            } catch (IllegalAccessException e) {
                e.printStackTrace();
            } catch (InvocationTargetException e) {
                e.printStackTrace();
            } catch (NoSuchMethodException e) {
                e.printStackTrace();
            }
        }

        List<File> mount_points = new ArrayList<File>();
        if (Build.VERSION.SDK_INT >= 30) {
            for (StorageVolume sv : storage_vols)
                mount_points.add(sv.getDirectory());
        }
        else {
            try {
                final Method dir_getter = StorageVolume.class.getMethod("getPathFile");
                for (StorageVolume sv : storage_vols) {
                    final File mount_point = (File) dir_getter.invoke(sv);
                    mount_points.add(mount_point);
                }
            } catch (IllegalAccessException e) {
                e.printStackTrace();
            } catch (InvocationTargetException e) {
                e.printStackTrace();
            } catch (NoSuchMethodException e) {
                e.printStackTrace();
            }
        }

        List<String> paths = new ArrayList<String>();
        for (File mp : mount_points) {
            if (mp != null)
                paths.add(mp.getAbsolutePath());
        }
        paths.add("/"); // Always add the root
        return paths.toArray(new String[paths.size()]);
    }


    public static String[] grantedPaths() {
        List<String> paths = new ArrayList();
        for (UriPermission uriperm : m_self.getContentResolver().getPersistedUriPermissions()) {
            final Uri uri = uriperm.getUri();
            paths.add(uri.getPath());
        }
        return paths.toArray(new String[paths.size()]);
    }


    public static void rememberGrantedPath(Uri uri) {
        m_self
            .getContentResolver()
            .takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
    }


    public static boolean getAllStorageAccess() {
        if (Build.VERSION.SDK_INT < 30)
            return true;

        if (Environment.isExternalStorageManager())
            return true;

        Intent intent = new Intent();
        intent.setAction(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
        intent.setData(Uri.parse("package:" + m_self.getPackageName()));
        m_self.startActivity(intent);
        return false;
    }

    public static BatteryInfo queryBattery() {
        final IntentFilter ifilter = new IntentFilter(Intent.ACTION_BATTERY_CHANGED);
        final Intent batIntent = m_self.registerReceiver(null, ifilter);

        final int batStatus = batIntent.getIntExtra(BatteryManager.EXTRA_STATUS, -1);
        if (batStatus == BatteryManager.BATTERY_STATUS_UNKNOWN)
            return null;

        final boolean hasBattery = batIntent.getBooleanExtra(BatteryManager.EXTRA_PRESENT, true);
        final boolean batPlugged = batIntent.getIntExtra(BatteryManager.EXTRA_PLUGGED, -1) > 0;
        final boolean batCharged = batStatus == BatteryManager.BATTERY_STATUS_FULL;

        final int batLevel = batIntent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1);
        final int batScale = batIntent.getIntExtra(BatteryManager.EXTRA_SCALE, -1);
        final float batPercent = (batLevel >= 0 && batScale > 0)
            ? batLevel / (float) batScale
            : Float.NaN;

        return new BatteryInfo(hasBattery, batPlugged, batCharged, batPercent);
    }

    public static String launchAmCommand(String[] args_arr) {
        final LinkedList<String> args = new LinkedList(Arrays.asList(args_arr));
        if (args.isEmpty())
            return "No arguments provided to 'am'";

        final String am_command = args.pop().toLowerCase();
        if (!am_command.equals("start"))
            return "For 'am', only the 'start' command is supported at the moment, '" + am_command + "' is not";

        try {
            Intent intent = IntentHelper.parseIntentCommand(args);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            m_self.startActivity(intent);
        }
        catch (Exception e) {
            return e.toString() + ": " + e.getMessage();
        }

        return null;
    }

    // Built-in Libretro player: open PlayerActivity (separate process) with poniaplay-style arguments.
    public static String launchPlayer(String[] args) {
        try {
            Intent intent = new Intent(m_self, PlayerActivity.class);
            intent.putExtra("args", args);
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            m_self.startActivity(intent);
        }
        catch (Exception e) {
            return e.toString() + ": " + e.getMessage();
        }
        return null;
    }

    // ---- File download over the system TLS stack (Qt 6.4 on Android has no usable TLS backend without bundled OpenSSL) ----
    private static volatile String m_dl_state = "idle";   // idle | running | done | error
    private static volatile long m_dl_done = 0, m_dl_total = 0;
    private static volatile String m_dl_error = "";

    public static void startDownload(final String url_str, final String dest_path) {
        m_dl_state = "running"; m_dl_done = 0; m_dl_total = 0; m_dl_error = "";
        new Thread(new Runnable() {
            @Override
            public void run() {
                java.net.HttpURLConnection conn = null;
                try {
                    conn = (java.net.HttpURLConnection) new java.net.URL(url_str).openConnection();
                    conn.setConnectTimeout(20000);
                    conn.setReadTimeout(30000);
                    conn.setInstanceFollowRedirects(true);
                    final int code = conn.getResponseCode();
                    if (code != 200)
                        throw new java.io.IOException("HTTP " + code);
                    m_dl_total = conn.getContentLengthLong();
                    try (java.io.InputStream in = conn.getInputStream();
                         java.io.FileOutputStream out = new java.io.FileOutputStream(dest_path)) {
                        byte[] buf = new byte[65536];
                        int n;
                        while ((n = in.read(buf)) > 0) {
                            out.write(buf, 0, n);
                            m_dl_done += n;
                        }
                    }
                    m_dl_state = "done";
                }
                catch (Exception e) {
                    m_dl_error = e.toString();
                    m_dl_state = "error";
                }
                finally {
                    if (conn != null) conn.disconnect();
                }
            }
        }).start();
    }

    // "state|done|total|error"
    public static String pollDownload() {
        return m_dl_state + "|" + m_dl_done + "|" + m_dl_total + "|" + m_dl_error;
    }

    public static String toContentUri(String path) {
        final Uri uri = FileProvider.getUriForFile(
            m_self,
            "org.ponia.PoniaEmu.files",
            new File(path));
        return uri.toString();
    }
}
