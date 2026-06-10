# UltraHDR Code Refactor - Decisions

- Use LOG_ERROR/LOG_INFO macros from src/log.h instead of introducing a logging dependency.
- Preserve printf in main.cpp for CLI help text, per task scope.
- Keep tone-mapping math unchanged; only move mode dispatch outside the inner pixel loop.
2026-06-10: Kept tone-mapping coefficients untouched per scope; only extracted UI/window constants and added GPU texture-size guards + OpenGL minimized-window guard.
2026-06-10: Removed Application::renderControlPanel/renderInfoPanel/renderAboutDialog from app.cpp/app.h because UIPanels::render* functions are the active UI path.
