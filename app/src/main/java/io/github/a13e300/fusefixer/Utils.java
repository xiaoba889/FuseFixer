package io.github.a13e300.fusefixer;

import android.annotation.SuppressLint;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.IntentFilter;
import android.os.Build;

public class Utils {
    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    public static void registerExportedReceiver(Context context, BroadcastReceiver receiver, IntentFilter intentFilter) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(receiver, intentFilter, Context.RECEIVER_EXPORTED);
        } else {
            context.registerReceiver(receiver, intentFilter);
        }
    }
}
