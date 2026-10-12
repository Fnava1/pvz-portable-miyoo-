#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include <SDL.h>

#include "input/HandheldInput.hpp"

using namespace PvZ::Input;

static bool float_near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

// ---------------------------------------------------------------------------
// Step 1: Universal Menu Key Translation (SDLK_HOME & SDLK_RALT -> KEYCODE_ESCAPE)
// ---------------------------------------------------------------------------
static void test_menu_key_translation() {
    std::cout << "[TEST] Running test_menu_key_translation..." << std::endl;

    HandheldInput input;

    // Test static translation functions
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_HOME, 102) == Sexy::KeyCode::KEYCODE_ESCAPE);
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_RALT, 100) == Sexy::KeyCode::KEYCODE_ESCAPE);
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_ESCAPE, 1) == Sexy::KeyCode::KEYCODE_ESCAPE);

    // Verify scancode fallback even if sym is different
    assert(HandheldInput::TranslateSDLKeyToPopCap(0, HandheldInput::MIYOO_SCANCODE_MENU_HOME) == Sexy::KeyCode::KEYCODE_ESCAPE);
    assert(HandheldInput::TranslateSDLKeyToPopCap(0, HandheldInput::MIYOO_SCANCODE_MENU_RALT) == Sexy::KeyCode::KEYCODE_ESCAPE);

    // Standard arrow keys and action keys
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_UP, 0) == Sexy::KeyCode::KEYCODE_UP);
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_DOWN, 0) == Sexy::KeyCode::KEYCODE_DOWN);
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_LEFT, 0) == Sexy::KeyCode::KEYCODE_LEFT);
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_RIGHT, 0) == Sexy::KeyCode::KEYCODE_RIGHT);
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_RETURN, 0) == Sexy::KeyCode::KEYCODE_RETURN);
    assert(HandheldInput::TranslateSDLKeyToPopCap(SDLK_SPACE, 0) == Sexy::KeyCode::KEYCODE_SPACE);

    // Test callback & event interception for physical Menu key
    bool menuEscaped = false;
    input.SetOnMenuEscapeCallback([&menuEscaped]() {
        menuEscaped = true;
    });

    // Send SDLK_HOME
    menuEscaped = false;
    input.OnKeyDown(SDLK_HOME, HandheldInput::MIYOO_SCANCODE_MENU_HOME);
    assert(menuEscaped == true);
    assert(input.GetLastPopCapKeyCode() == Sexy::KeyCode::KEYCODE_ESCAPE);

    // Send SDLK_RALT
    menuEscaped = false;
    input.OnKeyDown(SDLK_RALT, HandheldInput::MIYOO_SCANCODE_MENU_RALT);
    assert(menuEscaped == true);
    assert(input.GetLastPopCapKeyCode() == Sexy::KeyCode::KEYCODE_ESCAPE);

    std::cout << "  -> PASS: Physical Menu keys (SDLK_HOME / SDLK_RALT) trigger KEYCODE_ESCAPE (27)" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 2: 9x5 Grid Snapping in Garden Battle Mode
// ---------------------------------------------------------------------------
static void test_lawn_grid_snapping() {
    std::cout << "[TEST] Running test_lawn_grid_snapping..." << std::endl;

    HandheldInput input;
    input.SetMode(InputMode::LawnBattle);

    // Initial position should be (0, 0)
    assert(input.GetGridCol() == 0);
    assert(input.GetGridRow() == 0);

    // Discrete cell center check
    float expectedX0 = 0.0f, expectedY0 = 0.0f;
    input.GetCellCenter(0, 0, expectedX0, expectedY0);
    assert(float_near(input.GetCursorX(), expectedX0));
    assert(float_near(input.GetCursorY(), expectedY0));

    // Move D-pad Right 8 times across the 9 columns (0..8)
    for (int col = 1; col <= 8; ++col) {
        input.OnButtonDown(HandheldButton::Right);
        input.OnButtonUp(HandheldButton::Right);
        assert(input.GetGridCol() == col);
        assert(input.GetGridRow() == 0);

        float cx = 0.0f, cy = 0.0f;
        input.GetCellCenter(col, 0, cx, cy);
        assert(float_near(input.GetCursorX(), cx));
        assert(float_near(input.GetCursorY(), cy));
    }

    // Attempting to move past column 8 must clamp at 8
    input.OnButtonDown(HandheldButton::Right);
    input.OnButtonUp(HandheldButton::Right);
    assert(input.GetGridCol() == 8);

    // Move D-pad Down 4 times across the 5 rows (0..4)
    for (int row = 1; row <= 4; ++row) {
        input.OnButtonDown(HandheldButton::Down);
        input.OnButtonUp(HandheldButton::Down);
        assert(input.GetGridCol() == 8);
        assert(input.GetGridRow() == row);

        float cx = 0.0f, cy = 0.0f;
        input.GetCellCenter(8, row, cx, cy);
        assert(float_near(input.GetCursorX(), cx));
        assert(float_near(input.GetCursorY(), cy));
    }

    // Attempting to move past row 4 must clamp at 4
    input.OnButtonDown(HandheldButton::Down);
    input.OnButtonUp(HandheldButton::Down);
    assert(input.GetGridRow() == 4);

    // Move Left back to column 0
    for (int col = 7; col >= 0; --col) {
        input.OnButtonDown(HandheldButton::Left);
        input.OnButtonUp(HandheldButton::Left);
        assert(input.GetGridCol() == col);
    }
    // Clamping at column 0
    input.OnButtonDown(HandheldButton::Left);
    input.OnButtonUp(HandheldButton::Left);
    assert(input.GetGridCol() == 0);

    // Move Up back to row 0
    for (int row = 3; row >= 0; --row) {
        input.OnButtonDown(HandheldButton::Up);
        input.OnButtonUp(HandheldButton::Up);
        assert(input.GetGridRow() == row);
    }
    // Clamping at row 0
    input.OnButtonDown(HandheldButton::Up);
    input.OnButtonUp(HandheldButton::Up);
    assert(input.GetGridRow() == 0);

    std::cout << "  -> PASS: 9x5 Lawn Grid increments discretely cell-by-cell within (0..8, 0..4)" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 3: Sun / Coin Hover Auto-Collection
// ---------------------------------------------------------------------------
static void test_sun_coin_hover_auto_collect() {
    std::cout << "[TEST] Running test_sun_coin_hover_auto_collect..." << std::endl;

    HandheldInput input;
    input.SetMode(InputMode::LawnBattle);

    // Set up collectible items:
    // Item 101: Sun at cell (2, 1) center
    float sunX1 = 0.0f, sunY1 = 0.0f;
    input.GetCellCenter(2, 1, sunX1, sunY1);
    CollectibleItem sun1;
    sun1.id = 101;
    sun1.x = sunX1;
    sun1.y = sunY1;
    sun1.width = 40.0f;
    sun1.height = 40.0f;
    sun1.collected = false;
    sun1.isCoin = false;
    input.AddCollectible(sun1);

    // Item 102: Falling sun between cells at (360, 180)
    CollectibleItem sun2;
    sun2.id = 102;
    sun2.x = 360.0f;
    sun2.y = 180.0f;
    sun2.width = 40.0f;
    sun2.height = 40.0f;
    sun2.collected = false;
    sun2.isCoin = false;
    input.AddCollectible(sun2);

    // Item 201: Coin at cell (4, 3) center
    float coinX = 0.0f, coinY = 0.0f;
    input.GetCellCenter(4, 3, coinX, coinY);
    CollectibleItem coin1;
    coin1.id = 201;
    coin1.x = coinX;
    coin1.y = coinY;
    coin1.width = 30.0f;
    coin1.height = 30.0f;
    coin1.collected = false;
    coin1.isCoin = true;
    input.AddCollectible(coin1);

    // Item 301: Distant sun at cell (8, 4) center
    float sunFarX = 0.0f, sunFarY = 0.0f;
    input.GetCellCenter(8, 4, sunFarX, sunFarY);
    CollectibleItem sunFar;
    sunFar.id = 301;
    sunFar.x = sunFarX;
    sunFar.y = sunFarY;
    sunFar.width = 40.0f;
    sunFar.height = 40.0f;
    sunFar.collected = false;
    sunFar.isCoin = false;
    input.AddCollectible(sunFar);

    std::vector<int> collectedIds;
    input.SetOnCollectSunCallback([&collectedIds](int id) {
        collectedIds.push_back(id);
    });

    // At (0, 0), no sun should be collected
    assert(collectedIds.empty());
    assert(input.GetCollectedSunCount() == 0);

    // Move to (1, 0) -> not within radius of any sun
    input.OnButtonDown(HandheldButton::Right);
    input.OnButtonUp(HandheldButton::Right);
    assert(collectedIds.empty());

    // Move to (2, 0)
    input.OnButtonDown(HandheldButton::Right);
    input.OnButtonUp(HandheldButton::Right);

    // Move Down to (2, 1) -> cursor now directly overlaps sun 101!
    input.OnButtonDown(HandheldButton::Down);
    input.OnButtonUp(HandheldButton::Down);

    // Hover auto-collection must fire immediately without pressing button
    assert(collectedIds.size() == 1);
    assert(collectedIds[0] == 101);
    assert(input.GetCollectedSunCount() == 1);

    // Moving off and returning must NOT re-collect the same sun
    input.OnButtonDown(HandheldButton::Left);
    input.OnButtonUp(HandheldButton::Left);
    input.OnButtonDown(HandheldButton::Right);
    input.OnButtonUp(HandheldButton::Right);
    assert(collectedIds.size() == 1);

    // Now test free-falling sun collection via cursor update / hover
    input.SetCursorPosition(360.0f, 180.0f);
    input.CheckHoverAutoCollect();
    assert(collectedIds.size() == 2);
    assert(collectedIds[1] == 102);

    // Move to coin at (4, 3)
    input.SetGridPosition(4, 3);
    assert(collectedIds.size() == 3);
    assert(collectedIds[2] == 201);

    // Far sun (301) must still remain uncollected
    const auto& items = input.GetCollectibles();
    for (const auto& item : items) {
        if (item.id == 301) {
            assert(!item.collected);
        }
    }

    std::cout << "  -> PASS: Cursor hover overlap auto-collects suns and coins without button presses" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 4: Seed Packet Cycle (L/R) & Action Buttons (A/B/X)
// ---------------------------------------------------------------------------
static void test_seed_packet_cycling_and_actions() {
    std::cout << "[TEST] Running test_seed_packet_cycling_and_actions..." << std::endl;

    HandheldInput input;
    input.SetMode(InputMode::LawnBattle);
    input.SetNumSeeds(6);
    input.SetSelectedSeedIndex(0);

    assert(input.GetSelectedSeedIndex() == 0);

    // Trigger R shifts forward: (0 + 1) % 6 = 1
    input.OnButtonDown(HandheldButton::R);
    input.OnButtonUp(HandheldButton::R);
    assert(input.GetSelectedSeedIndex() == 1);

    input.OnButtonDown(HandheldButton::R);
    input.OnButtonUp(HandheldButton::R);
    assert(input.GetSelectedSeedIndex() == 2);

    input.OnButtonDown(HandheldButton::R);
    input.OnButtonUp(HandheldButton::R);
    assert(input.GetSelectedSeedIndex() == 3);

    input.OnButtonDown(HandheldButton::R);
    input.OnButtonUp(HandheldButton::R);
    assert(input.GetSelectedSeedIndex() == 4);

    input.OnButtonDown(HandheldButton::R);
    input.OnButtonUp(HandheldButton::R);
    assert(input.GetSelectedSeedIndex() == 5);

    // Wraps around: (5 + 1) % 6 = 0
    input.OnButtonDown(HandheldButton::R);
    input.OnButtonUp(HandheldButton::R);
    assert(input.GetSelectedSeedIndex() == 0);

    // Trigger L shifts backward: (0 - 1 + 6) % 6 = 5
    input.OnButtonDown(HandheldButton::L);
    input.OnButtonUp(HandheldButton::L);
    assert(input.GetSelectedSeedIndex() == 5);

    input.OnButtonDown(HandheldButton::L);
    input.OnButtonUp(HandheldButton::L);
    assert(input.GetSelectedSeedIndex() == 4);

    // Test Action Buttons
    bool planted = false;
    int plantCol = -1, plantRow = -1, plantSeed = -1;
    input.SetOnPlantConfirmCallback([&](int col, int row, int seed) {
        planted = true;
        plantCol = col;
        plantRow = row;
        plantSeed = seed;
    });

    input.SetGridPosition(3, 2);
    input.SetSelectedSeedIndex(4);

    // Button A plants/confirms
    input.OnButtonDown(HandheldButton::A);
    input.OnButtonUp(HandheldButton::A);
    assert(planted == true);
    assert(plantCol == 3 && plantRow == 2 && plantSeed == 4);

    // Button B cancels/drops
    bool canceled = false;
    input.SetOnCancelDropCallback([&canceled]() {
        canceled = true;
    });
    input.OnButtonDown(HandheldButton::B);
    input.OnButtonUp(HandheldButton::B);
    assert(canceled == true);

    // Button X toggles shovel tool
    assert(!input.IsShovelActive());
    input.OnButtonDown(HandheldButton::X);
    input.OnButtonUp(HandheldButton::X);
    assert(input.IsShovelActive());

    // Pressing X again toggles shovel off
    input.OnButtonDown(HandheldButton::X);
    input.OnButtonUp(HandheldButton::X);
    assert(!input.IsShovelActive());

    // When shovel is active, pressing B drops shovel
    input.OnButtonDown(HandheldButton::X);
    input.OnButtonUp(HandheldButton::X);
    assert(input.IsShovelActive());
    input.OnButtonDown(HandheldButton::B);
    input.OnButtonUp(HandheldButton::B);
    assert(!input.IsShovelActive());

    std::cout << "  -> PASS: L/R cycles seed packets cyclically, A plants, B cancels, X toggles shovel" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 5: Crazy Dave's Shop Navigation
// ---------------------------------------------------------------------------
static void test_crazy_dave_shop_navigation() {
    std::cout << "[TEST] Running test_crazy_dave_shop_navigation..." << std::endl;

    HandheldInput input;
    input.SetMode(InputMode::Shop);
    input.SetShopConfig(4, 3); // 4 items per shelf/page, 3 pages

    assert(input.GetShopItemIndex() == 0);
    assert(input.GetShopPageIndex() == 0);

    // Down moves item index down: 0 -> 1 -> 2 -> 3
    for (int i = 1; i <= 3; ++i) {
        input.OnButtonDown(HandheldButton::Down);
        input.OnButtonUp(HandheldButton::Down);
        assert(input.GetShopItemIndex() == i);
        assert(input.GetShopPageIndex() == 0);
    }

    // Clamps at item 3
    input.OnButtonDown(HandheldButton::Down);
    input.OnButtonUp(HandheldButton::Down);
    assert(input.GetShopItemIndex() == 3);

    // Up moves item index up: 3 -> 2 -> 1 -> 0
    for (int i = 2; i >= 0; --i) {
        input.OnButtonDown(HandheldButton::Up);
        input.OnButtonUp(HandheldButton::Up);
        assert(input.GetShopItemIndex() == i);
    }
    // Clamps at item 0
    input.OnButtonDown(HandheldButton::Up);
    input.OnButtonUp(HandheldButton::Up);
    assert(input.GetShopItemIndex() == 0);

    // Right switches pages: 0 -> 1 -> 2
    for (int p = 1; p <= 2; ++p) {
        input.OnButtonDown(HandheldButton::Right);
        input.OnButtonUp(HandheldButton::Right);
        assert(input.GetShopPageIndex() == p);
    }
    // Clamps at page 2
    input.OnButtonDown(HandheldButton::Right);
    input.OnButtonUp(HandheldButton::Right);
    assert(input.GetShopPageIndex() == 2);

    // Left switches pages back: 2 -> 1 -> 0
    for (int p = 1; p >= 0; --p) {
        input.OnButtonDown(HandheldButton::Left);
        input.OnButtonUp(HandheldButton::Left);
        assert(input.GetShopPageIndex() == p);
    }
    // Clamps at page 0
    input.OnButtonDown(HandheldButton::Left);
    input.OnButtonUp(HandheldButton::Left);
    assert(input.GetShopPageIndex() == 0);

    // Button A buys selected item
    bool bought = false;
    int boughtPage = -1, boughtItem = -1;
    input.SetOnShopBuyCallback([&](int page, int item) {
        bought = true;
        boughtPage = page;
        boughtItem = item;
    });

    input.SetShopPageIndex(1);
    input.SetShopItemIndex(2);
    input.OnButtonDown(HandheldButton::A);
    input.OnButtonUp(HandheldButton::A);
    assert(bought == true);
    assert(boughtPage == 1 && boughtItem == 2);

    // Button B exits shop
    bool exited = false;
    input.SetOnShopExitCallback([&exited]() {
        exited = true;
    });
    input.OnButtonDown(HandheldButton::B);
    input.OnButtonUp(HandheldButton::B);
    assert(exited == true);

    std::cout << "  -> PASS: Crazy Dave Shop list navigation (Up/Down/Left/Right/A/B) verified" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 6: Contextual Smooth Virtual Cursor with Progressive Acceleration Curve
// ---------------------------------------------------------------------------
static void test_contextual_smooth_cursor_and_velocity_curves() {
    std::cout << "[TEST] Running test_contextual_smooth_cursor_and_velocity_curves..." << std::endl;

    HandheldInput input;
    input.SetMode(InputMode::FreeCursor);
    input.SetCursorPosition(400.0f, 300.0f);

    assert(float_near(input.GetCursorX(), 400.0f));
    assert(float_near(input.GetCursorY(), 300.0f));
    assert(float_near(input.GetCursorVelocityX(), 0.0f));
    assert(float_near(input.GetCursorVelocityY(), 0.0f));

    // Press and hold Right
    input.OnButtonDown(HandheldButton::Right);

    // Step 1: Small delta time t = 0.05s
    input.Update(0.05f);
    float v1 = input.GetCursorVelocityX();
    float x1 = input.GetCursorX();
    assert(v1 > 0.0f);
    assert(x1 > 400.0f);

    // Step 2: Next delta time t = 0.15s (total hold 0.20s)
    input.Update(0.15f);
    float v2 = input.GetCursorVelocityX();
    float x2 = input.GetCursorX();
    // Progressive acceleration curve: velocity must increase over time!
    assert(v2 > v1);
    assert(x2 > x1);

    // Step 3: Next delta time t = 0.30s (total hold 0.50s)
    input.Update(0.30f);
    float v3 = input.GetCursorVelocityX();
    float x3 = input.GetCursorX();
    assert(v3 > v2);
    assert(x3 > x2);

    // Step 4: Long hold t = 2.0s -> Velocity must clamp at maxVelocity
    input.Update(2.0f);
    float vMax = input.GetCursorVelocityX();
    assert(float_near(vMax, input.GetCursorConfig().maxVelocity, 1.0f));

    // Release D-pad Right
    input.OnButtonUp(HandheldButton::Right);

    // Deceleration when released
    input.Update(0.1f);
    float vDecel = input.GetCursorVelocityX();
    assert(vDecel < vMax);

    input.Update(1.0f);
    assert(float_near(input.GetCursorVelocityX(), 0.0f, 0.01f));

    // Test boundary clamping (logical space 800x600)
    input.SetCursorPosition(790.0f, 590.0f);
    input.OnButtonDown(HandheldButton::Right);
    input.OnButtonDown(HandheldButton::Down);
    input.Update(2.0f);
    assert(input.GetCursorX() <= 800.0f);
    assert(input.GetCursorY() <= 600.0f);

    input.OnButtonUp(HandheldButton::Right);
    input.OnButtonUp(HandheldButton::Down);

    input.SetCursorPosition(10.0f, 10.0f);
    input.OnButtonDown(HandheldButton::Left);
    input.OnButtonDown(HandheldButton::Up);
    input.Update(2.0f);
    assert(input.GetCursorX() >= 0.0f);
    assert(input.GetCursorY() >= 0.0f);

    std::cout << "  -> PASS: Virtual cursor progressive acceleration curve and velocity verified" << std::endl;
}

// ---------------------------------------------------------------------------
// Step 7: SDL_Event Integration
// ---------------------------------------------------------------------------
static void test_sdl_event_handling_integration() {
    std::cout << "[TEST] Running test_sdl_event_handling_integration..." << std::endl;

    HandheldInput input;
    input.SetMode(InputMode::LawnBattle);

    // 1. SDL_KEYDOWN for Menu Key (SDLK_HOME)
    bool menuTriggered = false;
    input.SetOnMenuEscapeCallback([&menuTriggered]() {
        menuTriggered = true;
    });

    SDL_Event ev;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_HOME;
    ev.key.keysym.scancode = 102;
    input.HandleSDLEvent(ev);
    assert(menuTriggered == true);
    assert(input.GetLastPopCapKeyCode() == Sexy::KeyCode::KEYCODE_ESCAPE);

    // 2. SDL_KEYDOWN for Menu Key (SDLK_RALT)
    menuTriggered = false;
    std::memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_RALT;
    ev.key.keysym.scancode = 100;
    input.HandleSDLEvent(ev);
    assert(menuTriggered == true);
    assert(input.GetLastPopCapKeyCode() == Sexy::KeyCode::KEYCODE_ESCAPE);

    // 3. SDL_KEYDOWN for D-pad Right
    assert(input.GetGridCol() == 0);
    std::memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_RIGHT;
    input.HandleSDLEvent(ev);
    assert(input.GetGridCol() == 1);

    // 4. SDL_KEYDOWN for Seed Trigger R (e.g. SDLK_t)
    int initialSeed = input.GetSelectedSeedIndex();
    std::memset(&ev, 0, sizeof(ev));
    ev.type = SDL_KEYDOWN;
    ev.key.keysym.sym = SDLK_t;
    input.HandleSDLEvent(ev);
    assert(input.GetSelectedSeedIndex() == (initialSeed + 1) % input.GetNumSeeds());

    std::cout << "  -> PASS: SDL_Event integration handles Miyoo physical mapping and PopCap translation" << std::endl;
}

// ---------------------------------------------------------------------------
// Main test runner
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "==================================================" << std::endl;
    std::cout << " Running PvZ Handheld Input Mapping Tests" << std::endl;
    std::cout << "==================================================" << std::endl;

    test_menu_key_translation();
    test_lawn_grid_snapping();
    test_sun_coin_hover_auto_collect();
    test_seed_packet_cycling_and_actions();
    test_crazy_dave_shop_navigation();
    test_contextual_smooth_cursor_and_velocity_curves();
    test_sdl_event_handling_integration();

    std::cout << "==================================================" << std::endl;
    std::cout << " ALL HANDHELD INPUT TESTS PASSED!" << std::endl;
    std::cout << "==================================================" << std::endl;

    return 0;
}
