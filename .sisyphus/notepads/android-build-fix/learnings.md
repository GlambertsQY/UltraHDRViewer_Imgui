2026-06-10: Android NDK should be set with `android.ndkVersion` in `android/app/build.gradle`; remove any `defaultConfig.ndk` block to avoid stale version settings.
2026-06-10: `android/app/src/main/cpp/image_data.h` and `android/app/src/main/cpp/exif_data.h` were dormant duplicates of `hdr_types.h` and can be deleted safely when no sources include them.
