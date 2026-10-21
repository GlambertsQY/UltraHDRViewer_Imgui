# Native entry points are resolved by name from Java.
-keepclasseswithmembernames class * {
    native <methods>;
}
-keep class com.uhdrviewer.MainActivity { *; }
