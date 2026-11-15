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
#include "SexyAppFramework/Common.h"

#ifndef PVZ_UNIT_TEST
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/ZenGarden.h"
#include "Lawn/Challenge.h"
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
            vinfo.yoffset = 0;
            vinfo.xoffset = 0;
            ioctl(fb, FBIOPAN_DISPLAY, &vinfo);
        }
        close(fb);
    }
#endif
}

int DetectTargetCpuMhz() {
    // 1. Check TARGET_CPU_MHZ environment variable (set by launch.sh)
    const char* envMhz = std::getenv("TARGET_CPU_MHZ");
    if (envMhz && *envMhz) {
        int mhz = std::atoi(envMhz);
        if (mhz >= 1000 && mhz <= 2000) {
            return mhz;
        }
    }

#ifndef _WIN32
    // 2. Multi-tier hardware detection fallback
    bool isPlus = false;

    // Check OnionOS deviceModel (354 = Miyoo Mini Plus, 283 = Miyoo Mini)
    int fdModel = open("/tmp/deviceModel", O_RDONLY);
    if (fdModel >= 0) {
        char modelBuf[16] = {0};
        ssize_t n = read(fdModel, modelBuf, sizeof(modelBuf) - 1);
        close(fdModel);
        if (n > 0 && std::strncmp(modelBuf, "354", 3) == 0) {
            isPlus = true;
        }
    }

    // Check factory AXP test utility (exists only in Plus internal NAND)
    if (!isPlus && access("/customer/app/axp_test", F_OK) == 0) {
        isPlus = true;
    }

    // Check wlan0 network interface sysfs directory
    if (!isPlus && access("/sys/class/net/wlan0", F_OK) == 0) {
        isPlus = true;
    }

    // Return safe model-specific clock: 1800 MHz for Plus, 1400 MHz for non-Plus
    return isPlus ? 1800 : 1400;
#else
    return 1400;
#endif
}

void RestoreCpuClockSafe(int targetCpuMhz) {
    if (targetCpuMhz <= 0) return;

#ifndef _WIN32
    // 1. Ensure CPU1 is online in case OnionOS put it offline during sleep
    int fdCore1 = open("/sys/devices/system/cpu/cpu1/online", O_WRONLY);
    if (fdCore1 >= 0) {
        (void)write(fdCore1, "1\n", 2);
        close(fdCore1);
    }

    // 2. Set governor to userspace
    int fdGov = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", O_WRONLY);
    if (fdGov >= 0) {
        (void)write(fdGov, "userspace\n", 10);
        close(fdGov);
    }

    // 3. Write target frequency in kHz (e.g. 1800000 for MMP, 1400000 for MM)
    char freqBuf[32];
    int len = snprintf(freqBuf, sizeof(freqBuf), "%d000\n", targetCpuMhz);
    if (len > 0) {
        int fdSpeed = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed", O_WRONLY);
        if (fdSpeed >= 0) {
            (void)write(fdSpeed, freqBuf, static_cast<size_t>(len));
            close(fdSpeed);
        }
    }
#endif
}

static void CrashSignalHandler(int sig, siginfo_t* info, void* context) {
    (void)context;
    // Always restore framebuffer panning first to eliminate black screen
    ResetFramebuffer();

    Sexy::DumpLogRingBufferToCrashFile("/tmp/pvz_crash.log");

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
    int fd = open("crash.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        (void)write(fd, msg, sizeof(msg) - 1);
        if (len > 0) {
            (void)write(fd, buf, static_cast<size_t>(len));
        }
        (void)write(fd, endmsg, sizeof(endmsg) - 1);
        close(fd);
    }
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
#ifndef _WIN32
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(1, &cpuset); // Pin watchdog to Core 1
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
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

    int targetCpuMhz = DetectTargetCpuMhz();
    uint64_t lastTickMs = 0;
#ifndef _WIN32
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        lastTickMs = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
    }
#endif

    while (sWatchdogRunning.load(std::memory_order_relaxed)) {
        int ret = poll(pfd, numFds, 50);
#ifndef _WIN32
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            uint64_t nowMs = static_cast<uint64_t>(ts.tv_sec) * 1000ULL + static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
            if (lastTickMs != 0 && nowMs > lastTickMs) {
                uint64_t diffMs = nowMs - lastTickMs;
                // poll timeout is 50ms; if diffMs > 1200ms, the system was suspended (OnionOS keymon sleep)
                if (diffMs > 1200ULL) {
                    RestoreCpuClockSafe(targetCpuMhz);
                }
            }
            lastTickMs = nowMs;
        }
#endif
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
#ifndef EV_SW
#define EV_SW 0x05
#endif
#ifndef SW_LID
#define SW_LID 0x00
#endif
                        else if (ev.type == EV_SW && ev.code == SW_LID) {
                            if (ev.value != 0) {
                                // Miyoo Flip clamshell lid closed: pause gameplay and mute audio
#ifndef PVZ_UNIT_TEST
                                if (gLawnApp) {
                                    if (gLawnApp->mBoard) {
                                        gLawnApp->mBoard->Pause(true);
                                    }
                                    gLawnApp->Mute(true);
                                }
#endif
                            } else {
                                // Miyoo Flip clamshell lid opened: unmute audio, keep gameplay paused for safety
#ifndef PVZ_UNIT_TEST
                                if (gLawnApp) {
                                    gLawnApp->Unmute(true);
                                }
#endif
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
            std::printf("Plants vs. Zombies (Miyoo Mini Plus Edition) v1.1.0\n");
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
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#ifndef _WIN32
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // Pin main game & software rasterizer thread to Core 0
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    InstallSignalHandlers();
    Sexy::InitLogRingBuffer();

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
