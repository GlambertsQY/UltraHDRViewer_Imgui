#include "app.h"
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Enable DPI awareness for crisp rendering on HiDPI displays
    SetProcessDPIAware();
#endif

    AppConfig config;
    config.backend = "opengl";
    config.windowWidth = 1440;
    config.windowHeight = 900;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--vulkan") == 0 || strcmp(argv[i], "-v") == 0) {
            config.backend = "vulkan";
        } else if (strcmp(argv[i], "--opengl") == 0 || strcmp(argv[i], "-g") == 0) {
            config.backend = "opengl";
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            config.windowWidth = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            config.windowHeight = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Ultra HDR Viewer\n");
            printf("Usage: UltraHDRViewer [options] [image_file]\n\n");
            printf("Options:\n");
            printf("  --opengl, -g    Use OpenGL backend (default)\n");
            printf("  --vulkan, -v    Use Vulkan backend\n");
            printf("  --width  N      Window width\n");
            printf("  --height N      Window height\n");
            printf("  --help,   -h    Show this help\n");
            return 0;
        }
    }

    Application app;
    if (!app.init(config)) {
        fprintf(stderr, "Failed to initialize application\n");
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
