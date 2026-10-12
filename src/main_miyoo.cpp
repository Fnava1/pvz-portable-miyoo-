#include "main_miyoo.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <atomic>

#ifndef _WIN32
#include <sys/ioctl.h>
#include <linux/fb.h>
#endif

#if defined(__has_include)
  #if __has_include(<SDL/SDL.h>)
    #include <SDL/SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #elif __has_include(<SDL.h>)
    #include <SDL.h>
  #endif
#else
  #include <SDL.h>
#endif

#include "graphics/SoftwareRenderer.hpp"
#include "audio/OpenMptMusicInterface.hpp"
#include "input/HandheldInput.hpp"

// Global shutdown request indicator (set by signals or quit events)
static volatile sig_atomic_t gShutdownRequested = 0;

void ResetFramebuffer() {
#ifndef _WIN32
    int fb = open("/dev/fb0", O_RDWR);
    if (fb >= 0) {
        struct fb_var_screeninfo vinfo;
        if (ioctl(fb, FBIOGET_VSCREENINFO, &vinfo) == 0) {
            if (vinfo.yoffset != 0 || vinfo.xoffset != 0) {
                vinfo.yoffset = 0;
                vinfo.xoffset = 0;
                ioctl(fb, FBIOPAN_DISPLAY, &vinfo);
            }
        }
        close(fb);
    }
#endif
}

static void CrashSignalHandler(int sig, siginfo_t* info, void* context) {
    (void)context;
    // Always restore framebuffer panning first to eliminate black screen
    ResetFramebuffer();

    const char msg[] = "\n========================================\nFATAL CRASH DETECTED BY SIGNAL HANDLER\n";
    (void)write(STDERR_FILENO, msg, sizeof(msg) - 1);

    char buf[128];
    int len = snprintf(buf, sizeof(buf), "Signal %d received at fault address %p\n", sig, info ? info->si_addr : nullptr);
    if (len > 0) {
        (void)write(STDERR_FILENO, buf, static_cast<size_t>(len));
    }

    const char endmsg[] = "========================================\n";
    (void)write(STDERR_FILENO, endmsg, sizeof(endmsg) - 1);

    _exit(1);
}

static void TermSignalHandler(int sig) {
    (void)sig;
    // Request clean event loop unwinding; no exit(0)
    gShutdownRequested = 1;
}

static void InstallSignalHandlers() {
#ifndef _WIN32
    struct sigaction sa_crash;
    std::memset(&sa_crash, 0, sizeof(sa_crash));
    sa_crash.sa_sigaction = CrashSignalHandler;
    sa_crash.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa_crash, nullptr);
    sigaction(SIGBUS, &sa_crash, nullptr);
    sigaction(SIGILL, &sa_crash, nullptr);
    sigaction(SIGFPE, &sa_crash, nullptr);
    sigaction(SIGABRT, &sa_crash, nullptr);

    struct sigaction sa_term;
    std::memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_handler = TermSignalHandler;
    sigaction(SIGTERM, &sa_term, nullptr);
    sigaction(SIGINT, &sa_term, nullptr);
    sigaction(SIGHUP, &sa_term, nullptr);
#endif
}

static void SetupWorkingDirectory() {
#ifndef _WIN32
    char exePath[1024] = {0};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        char* slash = strrchr(exePath, '/');
        if (slash) {
            *slash = '\0';
            chdir(exePath);
        }
    }
#endif
}

#ifndef PVZ_UNIT_TEST
int main(int argc, char* argv[]) {
    bool dryRun = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0 || std::strcmp(argv[i], "-v") == 0) {
            std::printf("Plants vs. Zombies (Miyoo Mini Plus Edition) v1.0.0\n");
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("Usage: PvZ [options]\n");
            std::printf("  --dry-run, -d    Initialize subsystems and exit cleanly\n");
            std::printf("  --version, -v    Display version information\n");
            std::printf("  --help, -h       Display this help message\n");
            return 0;
        }
        if (std::strcmp(argv[i], "--dry-run") == 0 || std::strcmp(argv[i], "-d") == 0 ||
            std::strcmp(argv[i], "--test") == 0) {
            dryRun = true;
        }
    }

    SetupWorkingDirectory();
    InstallSignalHandlers();

    // Initialize handheld subsystems
    PvZ::Input::HandheldInput inputHandler;
    PvZ::Graphics::SoftwareRenderer renderer;

    // Connect menu button / escape to orderly shutdown
    inputHandler.SetOnMenuEscapeCallback([&]() {
        gShutdownRequested = 1;
    });

    int sdlInitResult = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

#if defined(SDL_MAJOR_VERSION) && (SDL_MAJOR_VERSION >= 2)
    SDL_Window* window = nullptr;
    SDL_Renderer* sdlRenderer = nullptr;
    if (sdlInitResult == 0) {
        window = SDL_CreateWindow(
            "Plants vs. Zombies",
            SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,
            640, 480,
            SDL_WINDOW_SHOWN
        );
        if (window) {
            sdlRenderer = SDL_CreateRenderer(
                window,
                -1,
                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
            );
            if (!sdlRenderer) {
                sdlRenderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            }
        }
    } else {
        std::fprintf(stderr, "Warning: SDL_Init failed (%s), running in headless fallback mode\n", SDL_GetError());
    }
#else
    SDL_Surface* screen = nullptr;
    if (sdlInitResult == 0) {
        screen = SDL_SetVideoMode(640, 480, 32, SDL_HWSURFACE | SDL_DOUBLEBUF);
        if (!screen) {
            screen = SDL_SetVideoMode(640, 480, 32, SDL_SWSURFACE);
        }
        renderer.Initialize(640, 480, screen);
    } else {
        std::fprintf(stderr, "Warning: SDL_Init failed (%s), running in headless fallback mode\n", SDL_GetError());
    }
#endif

    // Main game loop - unwinds cleanly on quit (no exit(0))
    bool running = !dryRun;
    while (running && !gShutdownRequested) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (event.type == SDL_KEYDOWN) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                    break;
                }
            }
            inputHandler.HandleSDLEvent(event);
        }

        if (!running || gShutdownRequested) {
            break;
        }

#if defined(SDL_MAJOR_VERSION) && (SDL_MAJOR_VERSION >= 2)
        if (sdlRenderer) {
            SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
            SDL_RenderClear(sdlRenderer);
            SDL_RenderPresent(sdlRenderer);
        }
#else
        if (screen) {
            SDL_FillRect(screen, nullptr, SDL_MapRGB(screen->format, 0, 0, 0));
            SDL_Flip(screen);
        }
#endif

        SDL_Delay(16);
    }

    // Clean teardown of window/renderer
#if defined(SDL_MAJOR_VERSION) && (SDL_MAJOR_VERSION >= 2)
    if (sdlRenderer) {
        SDL_DestroyRenderer(sdlRenderer);
        sdlRenderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
#endif

    if (sdlInitResult == 0) {
        SDL_Quit();
    }

    // Invokes ResetFramebuffer() using ioctl(fb, FBIOPAN_DISPLAY, &vinfo)
    // after SDL_Quit() to reset /dev/fb0 pan offsets to (0,0), eliminating
    // post-game black screen on OnionOS
    ResetFramebuffer();

    return 0;
}
#endif // PVZ_UNIT_TEST
