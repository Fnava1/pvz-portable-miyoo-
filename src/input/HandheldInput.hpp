#ifndef PVZ_INPUT_HANDHELDINPUT_HPP
#define PVZ_INPUT_HANDHELDINPUT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>

#if defined(__has_include)
  #if __has_include(<SDL.h>)
    #include <SDL.h>
  #elif __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #elif __has_include(<SDL/SDL.h>)
    #include <SDL/SDL.h>
  #endif
#else
  #include <SDL.h>
#endif

namespace Sexy {

/**
 * Standard PopCap key codes used across SexyAppFramework.
 */
namespace KeyCode {
    constexpr int KEYCODE_UNKNOWN = 0;
    constexpr int KEYCODE_BACK    = 8;
    constexpr int KEYCODE_TAB     = 9;
    constexpr int KEYCODE_RETURN  = 13;
    constexpr int KEYCODE_ESCAPE  = 27;
    constexpr int KEYCODE_SPACE   = 32;
    constexpr int KEYCODE_LEFT    = 37;
    constexpr int KEYCODE_UP      = 38;
    constexpr int KEYCODE_RIGHT   = 39;
    constexpr int KEYCODE_DOWN    = 40;
}

} // namespace Sexy

namespace PvZ {
namespace Input {

/**
 * Handheld input modes reflecting game state context.
 */
enum class InputMode {
    LawnBattle,   // 9x5 discrete grid snapping, hover auto-collect, L/R seed cycle
    Shop,         // Crazy Dave's shop: Up/Down items, Left/Right pages/shelves
    FreeCursor,   // Contextual smooth cursor with progressive acceleration curve
    CustomModal   // Dialogs and fallback
};

// Mode aliases for minigames
constexpr InputMode InputMode_ZenGarden     = InputMode::FreeCursor;
constexpr InputMode InputMode_Zombatar      = InputMode::FreeCursor;
constexpr InputMode InputMode_WhackAZombie  = InputMode::FreeCursor;
constexpr InputMode InputMode_Zombiquarium  = InputMode::FreeCursor;

/**
 * Physical handheld console buttons.
 */
enum class HandheldButton {
    Unknown = 0,
    Up,
    Down,
    Left,
    Right,
    A,         // Plant / Confirm / Buy
    B,         // Cancel / Drop / Exit
    X,         // Toggle Shovel tool
    Y,         // Inspect / Secondary
    L,         // Previous seed / Prev page
    R,         // Next seed / Next page
    Select,    // Select
    Start,     // Start / Pause
    Menu       // Miyoo Mini physical Menu key
};

/**
 * Collectible sun or coin item for hover auto-collection.
 */
struct CollectibleItem {
    int id = 0;
    float x = 0.0f;        // Center X coordinate in 800x600 space
    float y = 0.0f;        // Center Y coordinate in 800x600 space
    float width = 40.0f;   // Bounding box width
    float height = 40.0f;  // Bounding box height
    bool collected = false;
    bool isCoin = false;
};

/**
 * Virtual cursor physics and acceleration tuning.
 */
struct CursorConfig {
    float baseVelocity = 180.0f;     // Initial pixels/second when direction engaged
    float maxVelocity = 650.0f;      // Maximum speed limit
    float acceleration = 950.0f;     // Rate of speed ramp per second
    float friction = 1200.0f;        // Deceleration rate when released
    float hoverRadius = 45.0f;       // Bounding radius for hover auto-collect
};

/**
 * Standard 9x5 garden battle lawn grid layout (800x600 logical space).
 */
struct LawnGridConfig {
    int cols = 9;
    int rows = 5;
    float startX = 80.0f;
    float startY = 80.0f;
    float cellWidth = 80.0f;
    float cellHeight = 100.0f;
};

/**
 * Crazy Dave's shop layout configuration.
 */
struct ShopConfig {
    int itemsPerPage = 4;
    int numPages = 3;
};

/**
 * Hybrid Handheld Controller Subsystem for Miyoo Mini Plus.
 */
class HandheldInput {
public:
    // Miyoo Mini Linux internal driver scancodes for physical Menu key
    static constexpr int MIYOO_SCANCODE_MENU_HOME = 102;
    static constexpr int MIYOO_SCANCODE_MENU_RALT = 100;

    // Callbacks
    using MenuEscapeCallback   = std::function<void()>;
    using CollectSunCallback   = std::function<void(int id)>;
    using PlantConfirmCallback = std::function<void(int col, int row, int seed)>;
    using CancelDropCallback   = std::function<void()>;
    using SeedSelectedCallback = std::function<void(int seed)>;
    using ShopBuyCallback      = std::function<void(int page, int item)>;
    using ShopExitCallback     = std::function<void()>;

    HandheldInput();
    ~HandheldInput() = default;

    // --- Mode Management ---
    void SetMode(InputMode mode);
    InputMode GetMode() const { return mMode; }

    // --- Static Key & Button Translation ---
    static int TranslateSDLKeyToPopCap(int sdlKey, int scancode = 0);
    static bool IsMenuKey(int sdlKey, int scancode = 0);
    static HandheldButton MapSDLKeyToHandheldButton(int sdlKey, int scancode = 0);

    // --- Event & Direct Input Feeds ---
    bool HandleSDLEvent(const SDL_Event& event);
    void OnKeyDown(int sdlKey, int scancode = 0);
    void OnKeyUp(int sdlKey, int scancode = 0);
    void OnButtonDown(HandheldButton button);
    void OnButtonUp(HandheldButton button);
    void Update(float deltaTimeSeconds);

    int GetLastPopCapKeyCode() const { return mLastPopCapKey; }

    // --- 9x5 Lawn Grid Snapping ---
    int GetGridCol() const { return mGridCol; }
    int GetGridRow() const { return mGridRow; }
    void SetGridPosition(int col, int row);
    void GetCellCenter(int col, int row, float& outX, float& outY) const;
    const LawnGridConfig& GetGridConfig() const { return mGridConfig; }
    void SetGridConfig(const LawnGridConfig& config);

    // --- Virtual Cursor Properties ---
    float GetCursorX() const { return mCursorX; }
    float GetCursorY() const { return mCursorY; }
    float GetCursorVelocityX() const { return mVelocityX; }
    float GetCursorVelocityY() const { return mVelocityY; }
    void SetCursorPosition(float x, float y);
    const CursorConfig& GetCursorConfig() const { return mCursorConfig; }
    void SetCursorConfig(const CursorConfig& config) { mCursorConfig = config; }
    void SetScreenDimensions(float width, float height) { mScreenWidth = width; mScreenHeight = height; }

    // --- Sun / Coin Hover Auto-Collection ---
    void AddCollectible(const CollectibleItem& item);
    void UpdateCollectiblePosition(int id, float x, float y);
    void ClearCollectibles();
    const std::vector<CollectibleItem>& GetCollectibles() const { return mCollectibles; }
    bool CollectSun(int id);
    int GetCollectedSunCount() const { return mCollectedSunCount; }
    bool CheckHoverAutoCollect();

    // --- Seed Packet Selection & Actions ---
    int GetSelectedSeedIndex() const { return mSelectedSeed; }
    void SetSelectedSeedIndex(int index);
    int GetNumSeeds() const { return mNumSeeds; }
    void SetNumSeeds(int num);
    void CycleSeedPacket(int delta);
    bool IsShovelActive() const { return mShovelActive; }
    void SetShovelActive(bool active) { mShovelActive = active; }
    void ToggleShovel();
    void PlantOrConfirm();
    void CancelOrDrop();

    // --- Crazy Dave Shop Navigation ---
    int GetShopItemIndex() const { return mShopItemIndex; }
    int GetShopPageIndex() const { return mShopPageIndex; }
    void SetShopItemIndex(int item);
    void SetShopPageIndex(int page);
    void SetShopConfig(int itemsPerPage, int numPages);
    void ShopBuy();
    void ShopExit();

    // --- Callback Registration ---
    void SetOnMenuEscapeCallback(MenuEscapeCallback cb)     { mOnMenuEscapeCallback = cb; }
    void SetOnCollectSunCallback(CollectSunCallback cb)     { mOnCollectSunCallback = cb; }
    void SetOnPlantConfirmCallback(PlantConfirmCallback cb) { mOnPlantConfirmCallback = cb; }
    void SetOnCancelDropCallback(CancelDropCallback cb)     { mOnCancelDropCallback = cb; }
    void SetOnSeedSelectedCallback(SeedSelectedCallback cb) { mOnSeedSelectedCallback = cb; }
    void SetOnShopBuyCallback(ShopBuyCallback cb)           { mOnShopBuyCallback = cb; }
    void SetOnShopExitCallback(ShopExitCallback cb)         { mOnShopExitCallback = cb; }

private:
    InputMode mMode = InputMode::LawnBattle;

    // Grid coordinates
    LawnGridConfig mGridConfig;
    int mGridCol = 0;
    int mGridRow = 0;

    // Virtual cursor
    CursorConfig mCursorConfig;
    float mCursorX = 0.0f;
    float mCursorY = 0.0f;
    float mVelocityX = 0.0f;
    float mVelocityY = 0.0f;
    float mHoldDurationX = 0.0f;
    float mHoldDurationY = 0.0f;
    int mLastDirX = 0;
    int mLastDirY = 0;
    bool mHoldLeft = false;
    bool mHoldRight = false;
    bool mHoldUp = false;
    bool mHoldDown = false;
    float mScreenWidth = 800.0f;
    float mScreenHeight = 600.0f;

    // Collectibles & auto-hover
    std::vector<CollectibleItem> mCollectibles;
    int mCollectedSunCount = 0;

    // Seed packets & tools
    int mSelectedSeed = 0;
    int mNumSeeds = 8;
    bool mShovelActive = false;

    // Shop navigation
    ShopConfig mShopConfig;
    int mShopItemIndex = 0;
    int mShopPageIndex = 0;

    // PopCap key mapping state
    int mLastPopCapKey = Sexy::KeyCode::KEYCODE_UNKNOWN;

    // Callbacks
    MenuEscapeCallback   mOnMenuEscapeCallback;
    CollectSunCallback   mOnCollectSunCallback;
    PlantConfirmCallback mOnPlantConfirmCallback;
    CancelDropCallback   mOnCancelDropCallback;
    SeedSelectedCallback mOnSeedSelectedCallback;
    ShopBuyCallback      mOnShopBuyCallback;
    ShopExitCallback     mOnShopExitCallback;
};

} // namespace Input
} // namespace PvZ

#endif // PVZ_INPUT_HANDHELDINPUT_HPP
