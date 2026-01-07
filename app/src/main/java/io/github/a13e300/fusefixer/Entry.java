package io.github.a13e300.fusefixer;

import static io.github.a13e300.fusefixer.Constants.ACTION_GET_STATUS;
import static io.github.a13e300.fusefixer.Constants.ACTION_SET_STATUS;
import static io.github.a13e300.fusefixer.Constants.EXTRA_BINDER;
import static io.github.a13e300.fusefixer.Constants.EXTRA_PENDING_INTENT;
import static io.github.a13e300.fusefixer.Constants.EXTRA_PID;
import static io.github.a13e300.fusefixer.Constants.PKG_MEDIA_PROVIDERS;
import static io.github.a13e300.fusefixer.Constants.PKG_MEDIA_PROVIDERS_GOOGLE;
import static io.github.a13e300.fusefixer.Constants.TAG;

import android.annotation.SuppressLint;
import android.app.AndroidAppHelper;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.Process;
import android.util.Log;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class Entry implements IXposedHookLoadPackage {
    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) throws Throwable {
        if (PKG_MEDIA_PROVIDERS.equals(lpparam.packageName) || PKG_MEDIA_PROVIDERS_GOOGLE.equals(lpparam.packageName)) {
            System.loadLibrary("fusefixer");

            Log.d(TAG, "injected");

            var handler = new Handler(Looper.getMainLooper());
            handler.post(() -> {
                try {
                    var app = AndroidAppHelper.currentApplication();
                    if (app == null) {
                        Log.e(TAG, "app is null??");
                    } else {
                        var receiver = new BroadcastReceiver() {
                            @Override
                            public void onReceive(Context context, Intent intent) {
                                try {
                                    Log.d(TAG, "recv " + intent);
                                    var p = (PendingIntent) intent.getParcelableExtra(EXTRA_PENDING_INTENT);
                                    if (p == null) {
                                        Log.e(TAG, "no pendingintent?");
                                        return;
                                    }
                                    if (BuildConfig.APPLICATION_ID.equals(p.getCreatorPackage())) {
                                        var myIntent = new Intent(ACTION_SET_STATUS).setPackage(BuildConfig.APPLICATION_ID);
                                        var myPendingIntent = PendingIntent.getBroadcast(app, 1, myIntent, PendingIntent.FLAG_IMMUTABLE);
                                        myIntent.putExtra(EXTRA_PENDING_INTENT, myPendingIntent);
                                        myIntent.putExtra(EXTRA_PID, Process.myPid());
                                        myIntent.getExtras().putBinder(EXTRA_BINDER, myIntent.getExtras().getBinder(EXTRA_BINDER));
                                        app.sendBroadcast(myIntent);
                                    } else {
                                        Log.e(TAG, "invalid pkg " + p.getCreatorPackage());
                                    }
                                } catch (Throwable e) {
                                    Log.e("FuseFixer", "send: ", e);
                                }
                            }
                        };

                        Utils.registerExportedReceiver(app, receiver, new IntentFilter(ACTION_GET_STATUS));
                        Log.d(TAG, "registered");
                    }
                } catch (Throwable t) {
                    Log.e(TAG, "register", t);
                }
            });

        /*
        var clz = XposedHelpers.findClass("com.android.providers.media.fuse.FuseDaemon", lpparam.classLoader);
        XposedBridge.hookAllConstructors(clz, new XC_MethodHook() {
            @Override
            protected void beforeHookedMethod(MethodHookParam param) throws Throwable {
                Log.d("FuseFixer", "force enable uncached mode");
                param.args[5] = true;
            }
        });*/
        }
    }
}
