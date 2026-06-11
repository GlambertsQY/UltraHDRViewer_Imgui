# Learnings - Remove Inline Tone Mapping from native.cpp

## What was done
- Deleted 3 inline tone curve functions (ringbuf, acesFilm, unch2) from native.cpp (lines 56-67)
- Deleted `applyToneMap(AndroidApp*)` wrapper function (lines 69-97)
- Replaced 4 call sites to delegate to `::applyToneMap(const HDRData&, ImageData&, float, float, int)` from image_loader_android.cpp

## Key observations
- The old `applyToneMap(AndroidApp*)` had 4 call sites: loadImage (1) + renderUI (3: combo, exposure slider, gamma slider)
- All call sites now explicitly set `app->textureDirty = true` (was previously hidden inside the deleted function)
- The tonemap logic now lives exclusively in `image_loader_android.cpp` via `toneMapImage()` + `applyToneMap()`
- Desktop LSP shows pre-existing errors for this file (SDL.h, ANativeWindow, etc.) - Android NDK not in include path

## Verified
- Zero new LSP errors introduced
- All 4 call sites updated with correct 5-arg delegation pattern
