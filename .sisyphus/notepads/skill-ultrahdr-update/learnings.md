# Learnings

- The skill file now matches the refactored split: config.h, log.h, tonemap.cpp/h, and ui_panels.cpp/h are documented as first-class modules.
- The GLFW hint order note now reflects the fixed app.cpp flow, with the old bug language removed.
- Desktop and Android ImageData differences are called out clearly, which helps avoid cross-platform confusion during future updates.
- Test coverage is documented around Google Test, ctest, and the baseline pixel diff file at `tests/baseline/ref_pixels.raw`.
- README.md still lags the updated skill on src structure; treat README-only mismatches as known inconsistencies, not regressions.
## 2026-06-11
- Verified tone mapping baselines exist and are 64 bytes each.
- First pixels: reinhard `51,85,109,255`, aces `95,157,188,255`, uncharted2 `39,69,93,255`.
