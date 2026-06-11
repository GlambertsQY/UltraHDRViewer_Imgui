# Learnings: Refactor native.cpp loadImage to delegate

## What was done
- Replaced inline UHDR decode + stb_image fallback in `native.cpp::loadImage()` with delegation to `loadImageFile()` then `loadRegularImage()` (both in `image_loader_android.cpp`)
- Removed dead includes: `ultrahdr_api.h`, `stb_image.h`, `<fstream>`
- OpenGL texture creation, tone mapping application, and all JNI interfaces preserved unchanged

## Key observations
- `loadImageFile()` internally calls `parseExif()` and handles file I/O — native.cpp no longer needs to read the file
- `loadImageFile()` returns false for non-Ultra-HDR images (uhdr_dec_set_image fails), enabling clean fallback to `loadRegularImage()`
- `loadRegularImage()` uses `stbi_load_from_memory` instead of `stbi_load` (reads file internally), also calls `parseExif()`
- The old code called `is_uhdr_image()` for pre-detection — this function is from the removed `ultrahdr_api.h` and is not needed since `loadImageFile()` handles detection via decode failure
- `applyToneMap()` and `toneMapImage()` are both declared in `hdr_types.h` and implemented in `image_loader_android.cpp`

## Verification
- LSP diagnostics show only pre-existing NDK header resolution errors (SDL.h, android/log.h, GL_TEXTURE_2D) — not related to this change
- No stale references to removed symbols in native.cpp
- JNI nativeOnLoadImage interface unchanged
- Line count: 474 → 404 (70 lines removed)
- Android Gradle build could not be verified (gradle-wrapper.jar is corrupt at 130 bytes — pre-existing repo issue)

## Pattern
- Android `image_loader_android.cpp` is the canonical place for image decode logic
- `native.cpp` should be a thin UI orchestrator (SDL + ImGui + GL), not contain decode logic
