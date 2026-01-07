-keep class io.github.a13e300.fusefixer.Entry {
    *;
}

-repackageclasses "fusefixer"

-assumenosideeffects class kotlin.jvm.internal.Intrinsics {
    public static void check*(...);
    public static void throw*(...);
}

-assumenosideeffects class java.util.Objects {
    public static ** requireNonNull(...);
}

-allowaccessmodification
-overloadaggressively
