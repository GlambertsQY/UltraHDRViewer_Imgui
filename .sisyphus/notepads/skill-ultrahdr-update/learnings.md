# Learnings

- The skill file now matches the refactored split: config.h, log.h, tonemap.cpp/h, and ui_panels.cpp/h are documented as first-class modules.
- The GLFW hint order note now reflects the fixed app.cpp flow, with the old bug language removed.
- Desktop and Android ImageData differences are called out clearly, which helps avoid cross-platform confusion during future updates.
- Test coverage is documented around Google Test, ctest, and the baseline pixel diff file at `tests/baseline/ref_pixels.raw`.
