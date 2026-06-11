2026-06-10: `./gradlew -q help` could not run because the Gradle wrapper main class was missing/unresolvable in this environment, so build verification was blocked.
2026-06-11: LSP diagnostics for `android/app/src/main/cpp/native.cpp` still report missing Android/SDL/OpenGL includes in this workspace, so verification is blocked by environment configuration rather than the config-constant changes.
2026-06-11: `./gradlew assembleDebug` also failed here with `org.gradle.wrapper.GradleWrapperMain` missing, so Android build verification is unavailable in this environment.
