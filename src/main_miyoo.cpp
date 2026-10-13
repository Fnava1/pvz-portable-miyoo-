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

#include <SDL.h>

#ifndef PVZ_UNIT_TEST
#include "LawnApp.h"
#include "Resources.h"
#include "PvzpLib/PvzpStringFile.h"

using namespace Sexy;
#endif

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

#ifndef _WIN32
    sync();
#endif
    _exit(1);
}

static void TermSignalHandler(int sig) {
    (void)sig;
    gShutdownRequested = 1;
#ifndef PVZ_UNIT_TEST
    if (gLawnApp) {
        gLawnApp->CloseRequestAsync();
    }
#endif
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
        if (std::strcmp(argv[i], "--dry-run") == 0 || std::strcmp(argv[i], "-d") == 0) {
            SetupWorkingDirectory();
            InstallSignalHandlers();
            ResetFramebuffer();
            return 0;
        }
    }

    SetupWorkingDirectory();
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    InstallSignalHandlers();

    PvzpStringListSetColors(gLawnStringFormats, gLawnStringFormatCount);
    gExtractResourcesByName = Sexy::ExtractResourcesByName;

    gLawnApp = new LawnApp();
    gLawnApp->SetArgs(argc, argv);
    gLawnApp->Init();
    InstallSignalHandlers();
    gLawnApp->Start();
    gLawnApp->Shutdown();
    delete gLawnApp;
    gLawnApp = nullptr;

    // Reset framebuffer panning offsets to eliminate black screen on OnionOS
    ResetFramebuffer();

    return 0;
}
#endif // PVZ_UNIT_TEST
