package io.github.a13e300.fusefixer;

import android.annotation.SuppressLint;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.IntentFilter;
import android.os.Build;
import android.system.Os;
import android.system.OsConstants;
import android.system.StructStat;

public class Utils {
    @SuppressLint("UnspecifiedRegisterReceiverFlag")
    public static void registerExportedReceiver(Context context, BroadcastReceiver receiver, IntentFilter intentFilter) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            context.registerReceiver(receiver, intentFilter, Context.RECEIVER_EXPORTED);
        } else {
            context.registerReceiver(receiver, intentFilter);
        }
    }

    // from sys/sysmacros.h

    public static long major(long dev) {
        return (((dev) >>> 32) & 0xfffff000L) | ((dev >> 8) & 0xfff);
    }

    public static long minor(long dev) {
        return ((((dev) >>> 12) & 0xffffff00L) | ((dev) & 0xff));
    }

    private static final int S_IFMT = 00170000;
    private static final int S_IFSOCK = 0140000;
    private static final int S_IFLNK = 0120000;
    private static final int S_IFREG = 0100000;
    private static final int S_IFBLK = 0060000;
    private static final int S_IFDIR = 0040000;
    private static final int S_IFCHR = 0020000;
    private static final int S_IFIFO = 0010000;
    private static final int S_ISUID = 0004000;
    private static final int S_ISGID = 0002000;
    private static final int S_ISVTX = 0001000;
    private static final int S_IRWXU = 00700;
    private static final int S_IRUSR = 00400;
    private static final int S_IWUSR = 00200;
    private static final int S_IXUSR = 00100;
    private static final int S_IRWXG = 00070;
    private static final int S_IRGRP = 00040;
    private static final int S_IWGRP = 00020;
    private static final int S_IXGRP = 00010;
    private static final int S_IRWXO = 00007;
    private static final int S_IROTH = 00004;
    private static final int S_IWOTH = 00002;
    private static final int S_IXOTH = 00001;

    @SuppressLint("DefaultLocale")
    public static String statToString(StructStat st) {
        var sb = new StringBuilder();
        switch (st.st_mode & S_IFMT) {
            case S_IFREG -> sb.append("-");
            case S_IFDIR -> sb.append("d");
            case S_IFBLK -> sb.append("b");
            case S_IFCHR -> sb.append("c");
            case S_IFIFO -> sb.append("p");
            case S_IFLNK -> sb.append("l");
            case S_IFSOCK -> sb.append("s");
            default -> sb.append("?");
        }
        sb.append((st.st_mode & S_IRUSR) != 0 ? 'r' : '-');
        sb.append((st.st_mode & S_IWUSR) != 0 ? 'w' : '-');
        if ((st.st_mode & S_ISUID) == 0) {
            sb.append((st.st_mode & S_IXUSR) != 0 ? 'x' : '-');
        } else {
            sb.append((st.st_mode & S_IXUSR) != 0 ? 's' : 'S');
        }
        sb.append((st.st_mode & S_IRGRP) != 0 ? 'r' : '-');
        sb.append((st.st_mode & S_IWGRP) != 0 ? 'w' : '-');
        if ((st.st_mode & S_ISGID) == 0) {
            sb.append((st.st_mode & S_IXGRP) != 0 ? 'x' : '-');
        } else {
            sb.append((st.st_mode & S_IXGRP) != 0 ? 't' : 'T');
        }
        sb.append((st.st_mode & S_IROTH) != 0 ? 'r' : '-');
        sb.append((st.st_mode & S_IWOTH) != 0 ? 'w' : '-');
        if ((st.st_mode & S_ISVTX) == 0) {
            sb.append((st.st_mode & S_IXOTH) != 0 ? 'x' : '-');
        } else {
            sb.append((st.st_mode & S_IXOTH) != 0 ? 't' : 'T');
        }

        sb.append("\nInode: ").append(st.st_ino);
        sb.append("\nDevice: ").append(major(st.st_dev)).append(',').append(minor(st.st_dev));
        sb.append("\nUid: ").append(st.st_uid).append(" Gid: ").append(st.st_gid);

        return sb.toString();
    }

    public static native int rmdir(String path);

    public static native int unlink(String path);

    static {
        System.loadLibrary("fusefixer");
    }
}
