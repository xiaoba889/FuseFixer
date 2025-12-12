package io.github.a13e300.fusefixer;

import de.robv.android.xposed.IXposedHookLoadPackage;
import de.robv.android.xposed.callbacks.XC_LoadPackage;

public class Entry implements IXposedHookLoadPackage {
    @Override
    public void handleLoadPackage(XC_LoadPackage.LoadPackageParam lpparam) throws Throwable {
        if ("com.android.providers.media.module".equals(lpparam.packageName)
         || "com.google.android.providers.media.module".equals(lpparam.packageName)) {
            System.loadLibrary("fusefixer");
        }
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
