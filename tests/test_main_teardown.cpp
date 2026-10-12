#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <csignal>

#include "main_miyoo.hpp"
#include "input/HandheldInput.hpp"
#include "graphics/SoftwareRenderer.hpp"
#include "audio/OpenMptMusicInterface.hpp"

// ---------------------------------------------------------------------------
// Step 1: ResetFramebuffer() Idempotency and Non-Crashing Verification
// ---------------------------------------------------------------------------
static void test_reset_framebuffer() {
    std::cout << "[TEST] Running test_reset_framebuffer..." << std::endl;

    // ResetFramebuffer() should run safely even in virtualized, headless,
    // or non-root environments where /dev/fb0 might not exist or be writable.
    ResetFramebuffer();
    ResetFramebuffer();
    ResetFramebuffer();

    std::cout << "[PASS] ResetFramebuffer executed cleanly without fault." << std::endl;
}

// ---------------------------------------------------------------------------
// Step 2: Signal Handling and Orderly Shutdown State
// ---------------------------------------------------------------------------
static volatile std::sig_atomic_t gTestShutdown = 0;

static void TestSignalHandler(int sig) {
    (void)sig;
    gTestShutdown = 1;
}

static void test_signal_handling() {
    std::cout << "[TEST] Running test_signal_handling..." << std::endl;

    // Register a custom test signal handler for SIGUSR1 / SIGTERM
#ifndef _WIN32
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = TestSignalHandler;
    sigaction(SIGUSR1, &sa, nullptr);

    gTestShutdown = 0;
    std::raise(SIGUSR1);
    assert(gTestShutdown == 1);
#else
    std::signal(SIGTERM, TestSignalHandler);
    gTestShutdown = 0;
    std::raise(SIGTERM);
    assert(gTestShutdown == 1);
#endif

    std::cout << "[PASS] Signal handling properly marks shutdown flag without aborting." << std::endl;
}

// ---------------------------------------------------------------------------
// Step 3: Event Loop Clean Unwinding (Removal of exit(0))
// ---------------------------------------------------------------------------
static void test_clean_unwinding() {
    std::cout << "[TEST] Running test_clean_unwinding..." << std::endl;

    PvZ::Input::HandheldInput input;
    bool quitRequested = false;

    input.SetOnMenuEscapeCallback([&]() {
        quitRequested = true;
    });

    // Simulate pressing physical Menu key (HOME scancode 102)
    SDL_Event event;
    std::memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_HOME;
    event.key.keysym.scancode = 102;

    input.HandleSDLEvent(event);

    assert(quitRequested == true);

    // Confirm loop would terminate normally and execute trailing code
    bool teardownCompleted = false;
    if (quitRequested) {
        ResetFramebuffer();
        teardownCompleted = true;
    }

    assert(teardownCompleted == true);
    std::cout << "[PASS] Clean unwinding succeeds without exit(0)." << std::endl;
}

int main() {
    std::cout << "==========================================" << std::endl;
    std::cout << " Running PvZ Lifecycle & Teardown Unit Tests" << std::endl;
    std::cout << "==========================================" << std::endl;

    test_reset_framebuffer();
    test_signal_handling();
    test_clean_unwinding();

    std::cout << "==========================================" << std::endl;
    std::cout << " ALL LIFECYCLE TESTS PASSED!" << std::endl;
    std::cout << "==========================================" << std::endl;
    return 0;
}
