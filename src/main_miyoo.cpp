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
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
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

#ifndef _WIN32
static std::atomic<bool> sWatchdogRunning{false};
static pthread_t sWatchdogThread;

static void* HardwareWatchdogThreadFunc(void* arg) {
    (void)arg;
    int fds[4];
    int numFds = 0;
    for (int i = 0; i < 4; ++i) {
        char devPath[32];
        snprintf(devPath, sizeof(devPath), "/dev/input/event%d", i);
        int fd = open(devPath, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            fds[numFds++] = fd;
        }
    }

    if (numFds == 0) {
        return nullptr;
    }

    bool selectHeld = false;
    bool startHeld = false;
    bool menuHeld = false;
    uint32_t menuPressTime = 0;
    bool emergencyTriggered = false;

    struct pollfd pfd[4];
    for (int i = 0; i < numFds; ++i) {
        pfd[i].fd = fds[i];
        pfd[i].events = POLLIN;
    }

    while (sWatchdogRunning.load(std::memory_order_relaxed)) {
        int ret = poll(pfd, numFds, 50);
        if (ret > 0) {
            for (int i = 0; i < numFds; ++i) {
                if (pfd[i].revents & POLLIN) {
                    struct input_event ev;
                    while (read(fds[i], &ev, sizeof(ev)) == sizeof(ev)) {
                        if (ev.type == EV_KEY) {
                            bool isDown = (ev.value == 1 || ev.value == 2);
                            bool isUp = (ev.value == 0);

                            // Select key: KEY_ESC (1) or BTN_SELECT (0x13a)
                            if (ev.code == KEY_ESC || ev.code == 0x13a) {
                                selectHeld = isDown;
                            }
                            // Start key: KEY_ENTER (28) or BTN_START (0x13b)
                            else if (ev.code == KEY_ENTER || ev.code == 0x13b) {
                                startHeld = isDown;
                            }
                            // Menu key: KEY_HOME (102), KEY_RIGHTALT (100), or KEY_MENU (139)
                            else if (ev.code == 102 || ev.code == 100 || ev.code == 139) {
                                if (isDown && !menuHeld) {
                                    menuHeld = true;
                                    menuPressTime = SDL_GetTicks();
                                } else if (isUp) {
                                    menuHeld = false;
                                }
                            }
                            // Power key: KEY_POWER (116)
                            else if (ev.code == 116) {
                                if (isDown) {
                                    emergencyTriggered = true;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (selectHeld && startHeld) {
            emergencyTriggered = true;
        }
        if (menuHeld && (SDL_GetTicks() - menuPressTime >= 1000)) {
            emergencyTriggered = true;
        }

        if (emergencyTriggered) {
            gShutdownRequested = 1;
#ifndef PVZ_UNIT_TEST
            if (gLawnApp) {
                gLawnApp->CloseRequestAsync();
            }
#endif
            // Allow up to 1000ms for main loop to exit gracefully
            for (int w = 0; w < 20 && sWatchdogRunning.load(std::memory_order_relaxed); ++w) {
                usleep(50000);
            }

            // If main loop is still frozen/hung, force safe immediate exit to OnionOS
            if (sWatchdogRunning.load(std::memory_order_relaxed)) {
                sync();
                ResetFramebuffer();
                _exit(0);
            }
            break;
        }
    }

    for (int i = 0; i < numFds; ++i) {
        close(fds[i]);
    }
    return nullptr;
}

static void StartHardwareWatchdog() {
    sWatchdogRunning.store(true);
    pthread_create(&sWatchdogThread, nullptr, HardwareWatchdogThreadFunc, nullptr);
}

static void StopHardwareWatchdog() {
    if (sWatchdogRunning.load()) {
        sWatchdogRunning.store(false);
        pthread_join(sWatchdogThread, nullptr);
    }
}
#else
static void StartHardwareWatchdog() {}
static void StopHardwareWatchdog() {}
#endif

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
    StartHardwareWatchdog();
    gLawnApp->Start();
    StopHardwareWatchdog();
    gLawnApp->Shutdown();
    delete gLawnApp;
    gLawnApp = nullptr;

    // Reset framebuffer panning offsets to eliminate black screen on OnionOS
    ResetFramebuffer();

    return 0;
}
#endif // PVZ_UNIT_TEST
