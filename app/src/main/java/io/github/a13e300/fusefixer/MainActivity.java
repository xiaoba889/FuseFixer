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
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Binder;
import android.os.Build;
import android.os.Bundle;
import android.system.Os;
import android.util.Log;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import androidx.activity.EdgeToEdge;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.view.OnApplyWindowInsetsListener;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import org.jspecify.annotations.NonNull;

import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;

public class MainActivity extends AppCompatActivity {

    BroadcastReceiver mReceiver;

    String mInjectedPkg;
    int mInjectedPid = -1;
    private WeakReference<Binder> mWeakRef;

    private TextView mInjectStatusTextView;
    private Thread mPollingThread;
    private boolean mPollStopped = false;
    private boolean mChecking;
    private TextView mInfoTextView;
    private LinearLayout mRootView;

    @SuppressLint("PrivateApi")
    private static String getProp(String k) {
        try {
            var pc = Class.forName("android.os.SystemProperties");
            var m = pc.getDeclaredMethod("get", String.class);
            return (String) m.invoke(null, k);
        } catch (Throwable t) {
            Log.e(TAG, "getProp", t);
        }
        return null;
    }

    @SuppressLint("PrivateApi")
    private static boolean getBoolProp(String k) {
        try {
            var pc = Class.forName("android.os.SystemProperties");
            var m = pc.getDeclaredMethod("getBoolean", String.class, boolean.class);
            return (boolean) m.invoke(null, k, false);
        } catch (Throwable t) {
            Log.e(TAG, "getProp", t);
        }
        return false;
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        var scrollRoot = new ScrollView(this);
        mRootView = new LinearLayout(this);
        scrollRoot.addView(mRootView);
        mRootView.setOrientation(LinearLayout.VERTICAL);
        setContentView(scrollRoot);
        ViewCompat.setOnApplyWindowInsetsListener(mRootView, new OnApplyWindowInsetsListener() {
            @Override
            public @NonNull WindowInsetsCompat onApplyWindowInsets(@NonNull View v, @NonNull WindowInsetsCompat insets) {
                var systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
                v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
                return insets;
            }
        });

        setupInfo();
        setupStatus();
    }

    private void setupInfo() {
        mInfoTextView = new TextView(this);
        mRootView.addView(mInfoTextView);
        mInfoTextView.setTextIsSelectable(true);
        mInfoTextView.append("FuseFixer ver " + BuildConfig.VERSION_CODE + " (" + BuildConfig.VERSION_NAME + ")\n");

        var uname = Os.uname();
        mInfoTextView.append("Kernel: " + uname.release + "\n");
        mInfoTextView.append("Android: " + Build.VERSION.RELEASE + "\n");
        mInfoTextView.append("SDK: " + Build.VERSION.SDK_INT_FULL + "\n");

        var fuseBpf = getBoolProp("ro.fuse.bpf.is_running");
        mInfoTextView.append("fuse bpf: " + (fuseBpf ? "supported" : "unsupported") + "\n");
        var appDataIsolation = getBoolProp("persist.sys.vold_app_data_isolation_enabled");
        mInfoTextView.append("AppDataIsolation: " + (appDataIsolation ? "enabled" : "disabled") + "\n");
        if (!fuseBpf && !appDataIsolation) {
            mInfoTextView.append("App data isolation is required to fix Android/data access.\n");
            mInfoTextView.append("Use `setprop persist.sys.vold_app_data_isolation_enabled 1` to enable it.\n");
        }
    }

    private void setupStatus() {
        mInjectStatusTextView = new TextView(this);
        mRootView.addView(mInjectStatusTextView);
        updateStatus();

        mInjectStatusTextView.setOnClickListener(v -> {
            checkModule();
        });

        mReceiver = new BroadcastReceiver() {
            @Override
            public void onReceive(Context context, Intent intent) {
                try {
                    var p = (PendingIntent) intent.getParcelableExtra(EXTRA_PENDING_INTENT);
                    if (p == null) {
                        return;
                    }
                    var pkg = p.getCreatorPackage();
                    if (PKG_MEDIA_PROVIDERS.equals(pkg) || PKG_MEDIA_PROVIDERS_GOOGLE.equals(pkg)) {
                        mInjectedPkg = pkg;
                        mInjectedPid = intent.getIntExtra(EXTRA_PID, -1);
                        if (mPollingThread != null) {
                            mPollingThread.interrupt();
                            mPollStopped = true;
                        }
                        updateStatus();
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "send: ", e);
                }
            }
        };
        Utils.registerExportedReceiver(this, mReceiver, new IntentFilter(ACTION_SET_STATUS));

        checkModule();
    }

    private void checkModule() {
        if (mChecking) return;
        mInjectedPkg = null;
        mInjectedPid = -1;
        mPollStopped = false;
        updateStatus();
        mChecking = true;

        var binder = new Binder();
        var refq = new ReferenceQueue<Binder>();
        mWeakRef = new WeakReference<>(binder, refq);
        var myIntent = new Intent(ACTION_GET_STATUS).setPackage(BuildConfig.APPLICATION_ID);
        var myPendingIntent = PendingIntent.getBroadcast(this, 1, myIntent, PendingIntent.FLAG_IMMUTABLE);
        myIntent.putExtra(EXTRA_PENDING_INTENT, myPendingIntent);
        myIntent.getExtras().putBinder(EXTRA_BINDER, binder);
        myIntent.setPackage(PKG_MEDIA_PROVIDERS_GOOGLE);
        sendBroadcast(myIntent);
        myIntent.setPackage(PKG_MEDIA_PROVIDERS);
        sendBroadcast(myIntent);
        // TODO: hook JavaBinder's destructor
        mPollingThread = new Thread(() -> {
            try {
                Thread.sleep(2000);
                Runtime.getRuntime().gc();
                Log.d(TAG, "polling ref ...");
                var r = refq.remove();
                Log.d(TAG, "polled = " + r);
                mWeakRef = null;
                runOnUiThread(() -> {
                    mPollStopped = true;
                    updateStatus();
                });
            } catch (InterruptedException e) {
                Log.d(TAG, "return");
            }
        });
        mPollingThread.start();
    }


    @SuppressLint("SetTextI18n")
    private void updateStatus() {
        mChecking = false;
        mPollingThread = null;
        if (mInjectedPkg == null) {
            if (mPollStopped) {
                mInjectStatusTextView.setText("Module status: not hooked (touch to recheck)\n");
            } else {
                mInjectStatusTextView.setText("Module status: checking ...\n");
            }
        } else {
            mInjectStatusTextView.setText("Module status: hooked " + mInjectedPkg + " pid=" + mInjectedPid + "\n");
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unregisterReceiver(mReceiver);
        if (mPollingThread != null) {
            mPollingThread.interrupt();
        }
    }
}
