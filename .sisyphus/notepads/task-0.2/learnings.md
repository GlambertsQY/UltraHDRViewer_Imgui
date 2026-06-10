# Task 0.2 Learnings

## ExifParser Characterization

### Test Image: MVIMG_20251123_200209.jpg
- `valid == true` — parser successfully finds EXIF data
- `make` and `model` populated (non-empty)
- `orientation` parsed as 0 — EXIF tag may be absent or stored in sub-IFD not traversed by current parser; struct default (1) overridden by parseIFD setting it to 0 from a missing/unparseable tag
- `pixelX` and `pixelY` > 0 — dimensions extracted correctly
- GPS data: `hasGPS` may be false depending on image; conditional assertions used

### CMakeLists.txt Pattern
- `file(GLOB TEST_SOURCES *.cpp)` to auto-discover test files
- Pre-existing test files (image_load_test.cpp) may have unresolvable dependencies; use `list(FILTER ... EXCLUDE REGEX)` to skip them
- Source files under test need `list(APPEND TEST_SOURCES ../src/exif_parser.cpp)` for linker resolution
- Include dirs unchanged from original: `${CMAKE_SOURCE_DIR}/src`, `${UHDR_DIR}`, `${DEPS_DIR}`

## Task A.4 - File Dialog Deduplication
- Removed duplicate openFileDialog() from image_loader.cpp (Win32 headers + function)
- Removed declaration from image_loader.h
- Updated app.cpp openImageFile() to use openImageDialog(m_window) from file_dialog.h
- Simplified from 11-line #ifdef block to 3-line call
- app.cpp already included file_dialog.h so no new #include needed
- Build: 0 errors, all 9 tests pass
