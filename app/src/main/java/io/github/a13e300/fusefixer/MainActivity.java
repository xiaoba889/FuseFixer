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
import android.os.Bundle;
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

    private TextView injectStatusTextView;

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this);
        var scrollRoot = new ScrollView(this);
        var rootView = new LinearLayout(this);
        scrollRoot.addView(rootView);
        rootView.setOrientation(LinearLayout.VERTICAL);
        setContentView(scrollRoot);
        ViewCompat.setOnApplyWindowInsetsListener(rootView, new OnApplyWindowInsetsListener() {
            @Override
            public @NonNull WindowInsetsCompat onApplyWindowInsets(@NonNull View v, @NonNull WindowInsetsCompat insets) {
                var systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
                v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
                return insets;
            }
        });

        injectStatusTextView = new TextView(this);
        rootView.addView(injectStatusTextView);
        updateStatus();

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
                        updateStatus();
                    }
                } catch (Throwable e) {
                    Log.e(TAG, "send: ", e);
                }
            }
        };

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
        new Thread(() -> {
            try {
                Log.d(TAG, "polling ref ...");
                var r = refq.remove();
                Log.d(TAG, "polled = " + r);
                mWeakRef = null;
            } catch (InterruptedException e) {
                throw new RuntimeException(e);
            }
        }).start();
        /*
        new Thread(() -> {
            while (true) {
                var r = mWeakRef;
                if (r == null) {
                    Log.d(TAG, "mWeakRef is null");
                    break;
                }
                if (r.get() == null) {
                    mWeakRef = null;
                    Log.d(TAG, "mWeakRef.get() is null");
                    break;
                } else {
                    Log.d(TAG, "still alive");
                }
                try {
                    Thread.sleep(1000);
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            }
        }).start();*/

        Utils.registerExportedReceiver(this, mReceiver, new IntentFilter(ACTION_SET_STATUS));
    }


    @SuppressLint("SetTextI18n")
    private void updateStatus() {
        if (mInjectedPkg == null) {
            injectStatusTextView.setText("inject status: querying ...");
        } else {
            injectStatusTextView.setText("inject status: injected " + mInjectedPkg + " pid=" + mInjectedPid);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unregisterReceiver(mReceiver);
    }
}
