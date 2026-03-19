# Add project specific ProGuard rules here.

# libultrahdr
-keep class com.google.media.codecs.ultrahdr.** { *; }

# Keep native methods
-keepclasseswithmembernames class * {
    native <methods>;
}
