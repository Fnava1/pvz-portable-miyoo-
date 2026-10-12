#include "input/HandheldInput.hpp"

#include <algorithm>
#include <cmath>

namespace PvZ {
namespace Input {

HandheldInput::HandheldInput() {
    // Center cursor on cell (0, 0) by default in LawnBattle mode
    GetCellCenter(mGridCol, mGridRow, mCursorX, mCursorY);
}

void HandheldInput::SetMode(InputMode mode) {
    mMode = mode;
    mHoldLeft = false;
    mHoldRight = false;
    mHoldUp = false;
    mHoldDown = false;
    mHoldDurationX = 0.0f;
    mHoldDurationY = 0.0f;
    mLastDirX = 0;
    mLastDirY = 0;
    mVelocityX = 0.0f;
    mVelocityY = 0.0f;

    if (mMode == InputMode::LawnBattle) {
        GetCellCenter(mGridCol, mGridRow, mCursorX, mCursorY);
        CheckHoverAutoCollect();
    }
}

bool HandheldInput::IsMenuKey(int sdlKey, int scancode) {
    if (scancode == MIYOO_SCANCODE_MENU_HOME || scancode == MIYOO_SCANCODE_MENU_RALT) {
        return true;
    }
    if (sdlKey == SDLK_HOME || sdlKey == SDLK_RALT) {
        return true;
    }
    return false;
}

int HandheldInput::TranslateSDLKeyToPopCap(int sdlKey, int scancode) {
    // Universal Menu key check: SDLK_HOME & SDLK_RALT -> KEYCODE_ESCAPE
    if (IsMenuKey(sdlKey, scancode)) {
        return Sexy::KeyCode::KEYCODE_ESCAPE;
    }

    if (sdlKey == SDLK_ESCAPE) {
        return Sexy::KeyCode::KEYCODE_ESCAPE;
    }
    if (sdlKey == SDLK_RETURN) {
        return Sexy::KeyCode::KEYCODE_RETURN;
    }
    if (sdlKey == SDLK_SPACE) {
        return Sexy::KeyCode::KEYCODE_SPACE;
    }
    if (sdlKey == SDLK_TAB) {
        return Sexy::KeyCode::KEYCODE_TAB;
    }
    if (sdlKey == SDLK_BACKSPACE) {
        return Sexy::KeyCode::KEYCODE_BACK;
    }
    if (sdlKey == SDLK_UP) {
        return Sexy::KeyCode::KEYCODE_UP;
    }
    if (sdlKey == SDLK_DOWN) {
        return Sexy::KeyCode::KEYCODE_DOWN;
    }
    if (sdlKey == SDLK_LEFT) {
        return Sexy::KeyCode::KEYCODE_LEFT;
    }
    if (sdlKey == SDLK_RIGHT) {
        return Sexy::KeyCode::KEYCODE_RIGHT;
    }

    // Letter keys
    if (sdlKey >= 'a' && sdlKey <= 'z') {
        return sdlKey - 'a' + 'A';
    }
    // Number keys
    if (sdlKey >= '0' && sdlKey <= '9') {
        return sdlKey;
    }

    return Sexy::KeyCode::KEYCODE_UNKNOWN;
}

HandheldButton HandheldInput::MapSDLKeyToHandheldButton(int sdlKey, int scancode) {
    if (IsMenuKey(sdlKey, scancode)) {
        return HandheldButton::Menu;
    }

    if (sdlKey == SDLK_UP) return HandheldButton::Up;
    if (sdlKey == SDLK_DOWN) return HandheldButton::Down;
    if (sdlKey == SDLK_LEFT) return HandheldButton::Left;
    if (sdlKey == SDLK_RIGHT) return HandheldButton::Right;

    // Miyoo Mini keyboard mapping conventions:
    // A button: Space or 'c'
    if (sdlKey == SDLK_SPACE || sdlKey == 'c' || sdlKey == 'C') {
        return HandheldButton::A;
    }
    // B button: LAlt or 'x'
    if (sdlKey == SDLK_LALT || sdlKey == 'x' || sdlKey == 'X') {
        return HandheldButton::B;
    }
    // X button: LShift or 'y'
    if (sdlKey == SDLK_LSHIFT || sdlKey == 'y' || sdlKey == 'Y') {
        return HandheldButton::X;
    }
    // Y button: LCtrl or 'a'
    if (sdlKey == SDLK_LCTRL || sdlKey == 'a' || sdlKey == 'A') {
        return HandheldButton::Y;
    }

    // Triggers L and R
    if (sdlKey == 'e' || sdlKey == 'E' || sdlKey == SDLK_TAB) {
        return HandheldButton::L;
    }
    if (sdlKey == 't' || sdlKey == 'T' || sdlKey == SDLK_BACKSPACE) {
        return HandheldButton::R;
    }

    // System buttons
    if (sdlKey == SDLK_RETURN) {
        return HandheldButton::Start;
    }
    if (sdlKey == SDLK_ESCAPE) {
        return HandheldButton::Select;
    }

    return HandheldButton::Unknown;
}

bool HandheldInput::HandleSDLEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN) {
        OnKeyDown(event.key.keysym.sym, event.key.keysym.scancode);
        return true;
    }
    if (event.type == SDL_KEYUP) {
        OnKeyUp(event.key.keysym.sym, event.key.keysym.scancode);
        return true;
    }
    if (event.type == SDL_JOYBUTTONDOWN) {
        switch (event.jbutton.button) {
            case 0: OnButtonDown(HandheldButton::A); break;
            case 1: OnButtonDown(HandheldButton::B); break;
            case 2: OnButtonDown(HandheldButton::X); break;
            case 3: OnButtonDown(HandheldButton::Y); break;
            case 4: OnButtonDown(HandheldButton::L); break;
            case 5: OnButtonDown(HandheldButton::R); break;
            case 6: OnButtonDown(HandheldButton::Select); break;
            case 7: OnButtonDown(HandheldButton::Start); break;
            case 8: OnButtonDown(HandheldButton::Menu); break;
            default: break;
        }
        return true;
    }
    if (event.type == SDL_JOYBUTTONUP) {
        switch (event.jbutton.button) {
            case 0: OnButtonUp(HandheldButton::A); break;
            case 1: OnButtonUp(HandheldButton::B); break;
            case 2: OnButtonUp(HandheldButton::X); break;
            case 3: OnButtonUp(HandheldButton::Y); break;
            case 4: OnButtonUp(HandheldButton::L); break;
            case 5: OnButtonUp(HandheldButton::R); break;
            case 6: OnButtonUp(HandheldButton::Select); break;
            case 7: OnButtonUp(HandheldButton::Start); break;
            case 8: OnButtonUp(HandheldButton::Menu); break;
            default: break;
        }
        return true;
    }
    return false;
}

void HandheldInput::OnKeyDown(int sdlKey, int scancode) {
    mLastPopCapKey = TranslateSDLKeyToPopCap(sdlKey, scancode);

    if (IsMenuKey(sdlKey, scancode)) {
        if (mMode == InputMode::Shop) {
            ShopExit();
        }
        if (mOnMenuEscapeCallback) {
            mOnMenuEscapeCallback();
        }
        return;
    }

    HandheldButton button = MapSDLKeyToHandheldButton(sdlKey, scancode);
    if (button != HandheldButton::Unknown) {
        OnButtonDown(button);
    }
}

void HandheldInput::OnKeyUp(int sdlKey, int scancode) {
    HandheldButton button = MapSDLKeyToHandheldButton(sdlKey, scancode);
    if (button != HandheldButton::Unknown) {
        OnButtonUp(button);
    }
}

void HandheldInput::OnButtonDown(HandheldButton button) {
    if (button == HandheldButton::Unknown) {
        return;
    }

    if (button == HandheldButton::Menu) {
        mLastPopCapKey = Sexy::KeyCode::KEYCODE_ESCAPE;
        if (mMode == InputMode::Shop) {
            ShopExit();
        }
        if (mOnMenuEscapeCallback) {
            mOnMenuEscapeCallback();
        }
        return;
    }

    if (mMode == InputMode::LawnBattle) {
        switch (button) {
            case HandheldButton::Right:
                if (mGridCol + 1 < mGridConfig.cols) {
                    mGridCol++;
                }
                GetCellCenter(mGridCol, mGridRow, mCursorX, mCursorY);
                CheckHoverAutoCollect();
                break;
            case HandheldButton::Left:
                if (mGridCol > 0) {
                    mGridCol--;
                }
                GetCellCenter(mGridCol, mGridRow, mCursorX, mCursorY);
                CheckHoverAutoCollect();
                break;
            case HandheldButton::Down:
                if (mGridRow + 1 < mGridConfig.rows) {
                    mGridRow++;
                }
                GetCellCenter(mGridCol, mGridRow, mCursorX, mCursorY);
                CheckHoverAutoCollect();
                break;
            case HandheldButton::Up:
                if (mGridRow > 0) {
                    mGridRow--;
                }
                GetCellCenter(mGridCol, mGridRow, mCursorX, mCursorY);
                CheckHoverAutoCollect();
                break;
            case HandheldButton::L:
                CycleSeedPacket(-1);
                break;
            case HandheldButton::R:
                CycleSeedPacket(1);
                break;
            case HandheldButton::A:
                PlantOrConfirm();
                break;
            case HandheldButton::B:
                CancelOrDrop();
                break;
            case HandheldButton::X:
                ToggleShovel();
                break;
            default:
                break;
        }
    } else if (mMode == InputMode::Shop) {
        switch (button) {
            case HandheldButton::Down:
                if (mShopItemIndex + 1 < mShopConfig.itemsPerPage) {
                    mShopItemIndex++;
                }
                break;
            case HandheldButton::Up:
                if (mShopItemIndex > 0) {
                    mShopItemIndex--;
                }
                break;
            case HandheldButton::Right:
                if (mShopPageIndex + 1 < mShopConfig.numPages) {
                    mShopPageIndex++;
                }
                break;
            case HandheldButton::Left:
                if (mShopPageIndex > 0) {
                    mShopPageIndex--;
                }
                break;
            case HandheldButton::A:
                ShopBuy();
                break;
            case HandheldButton::B:
                ShopExit();
                break;
            default:
                break;
        }
    } else if (mMode == InputMode::FreeCursor) {
        switch (button) {
            case HandheldButton::Right: mHoldRight = true; break;
            case HandheldButton::Left:  mHoldLeft = true; break;
            case HandheldButton::Down:  mHoldDown = true; break;
            case HandheldButton::Up:    mHoldUp = true; break;
            case HandheldButton::A:     PlantOrConfirm(); break;
            case HandheldButton::B:     CancelOrDrop(); break;
            case HandheldButton::X:     ToggleShovel(); break;
            default: break;
        }
    }
}

void HandheldInput::OnButtonUp(HandheldButton button) {
    if (button == HandheldButton::Unknown) {
        return;
    }
    if (mMode == InputMode::FreeCursor) {
        switch (button) {
            case HandheldButton::Right: mHoldRight = false; break;
            case HandheldButton::Left:  mHoldLeft = false; break;
            case HandheldButton::Down:  mHoldDown = false; break;
            case HandheldButton::Up:    mHoldUp = false; break;
            default: break;
        }
    }
}

void HandheldInput::Update(float deltaTimeSeconds) {
    if (deltaTimeSeconds <= 0.0f) {
        return;
    }

    if (mMode == InputMode::LawnBattle) {
        // Real-time hover auto-collection in LawnBattle mode for falling/moving suns and coins
        CheckHoverAutoCollect();
    } else if (mMode == InputMode::FreeCursor) {
        // X-axis velocity and acceleration
        int dirX = (mHoldRight ? 1 : 0) - (mHoldLeft ? 1 : 0);
        if (dirX != 0) {
            if (dirX != mLastDirX) {
                mHoldDurationX = 0.0f;
                mLastDirX = dirX;
            }
            mHoldDurationX += deltaTimeSeconds;
            float speed = mCursorConfig.baseVelocity + mCursorConfig.acceleration * mHoldDurationX;
            if (speed > mCursorConfig.maxVelocity) {
                speed = mCursorConfig.maxVelocity;
            }
            mVelocityX = static_cast<float>(dirX) * speed;
        } else {
            mLastDirX = 0;
            mHoldDurationX = 0.0f;
            if (mVelocityX > 0.0f) {
                mVelocityX = std::max(0.0f, mVelocityX - mCursorConfig.friction * deltaTimeSeconds);
            } else if (mVelocityX < 0.0f) {
                mVelocityX = std::min(0.0f, mVelocityX + mCursorConfig.friction * deltaTimeSeconds);
            }
        }

        // Y-axis velocity and acceleration
        int dirY = (mHoldDown ? 1 : 0) - (mHoldUp ? 1 : 0);
        if (dirY != 0) {
            if (dirY != mLastDirY) {
                mHoldDurationY = 0.0f;
                mLastDirY = dirY;
            }
            mHoldDurationY += deltaTimeSeconds;
            float speed = mCursorConfig.baseVelocity + mCursorConfig.acceleration * mHoldDurationY;
            if (speed > mCursorConfig.maxVelocity) {
                speed = mCursorConfig.maxVelocity;
            }
            mVelocityY = static_cast<float>(dirY) * speed;
        } else {
            mLastDirY = 0;
            mHoldDurationY = 0.0f;
            if (mVelocityY > 0.0f) {
                mVelocityY = std::max(0.0f, mVelocityY - mCursorConfig.friction * deltaTimeSeconds);
            } else if (mVelocityY < 0.0f) {
                mVelocityY = std::min(0.0f, mVelocityY + mCursorConfig.friction * deltaTimeSeconds);
            }
        }

        // Advance position
        mCursorX += mVelocityX * deltaTimeSeconds;
        mCursorY += mVelocityY * deltaTimeSeconds;

        // Clamp to screen
        mCursorX = std::max(0.0f, std::min(mScreenWidth, mCursorX));
        mCursorY = std::max(0.0f, std::min(mScreenHeight, mCursorY));

        CheckHoverAutoCollect();
    }
}

void HandheldInput::SetGridPosition(int col, int row) {
    mGridCol = std::max(0, std::min(mGridConfig.cols - 1, col));
    mGridRow = std::max(0, std::min(mGridConfig.rows - 1, row));
    if (mMode == InputMode::LawnBattle) {
        GetCellCenter(mGridCol, mGridRow, mCursorX, mCursorY);
        CheckHoverAutoCollect();
    }
}

void HandheldInput::GetCellCenter(int col, int row, float& outX, float& outY) const {
    outX = mGridConfig.startX + static_cast<float>(col) * mGridConfig.cellWidth + mGridConfig.cellWidth * 0.5f;
    outY = mGridConfig.startY + static_cast<float>(row) * mGridConfig.cellHeight + mGridConfig.cellHeight * 0.5f;
}

void HandheldInput::SetGridConfig(const LawnGridConfig& config) {
    mGridConfig = config;
    SetGridPosition(mGridCol, mGridRow);
}

void HandheldInput::SetCursorPosition(float x, float y) {
    mCursorX = std::max(0.0f, std::min(mScreenWidth, x));
    mCursorY = std::max(0.0f, std::min(mScreenHeight, y));
}

void HandheldInput::AddCollectible(const CollectibleItem& item) {
    mCollectibles.push_back(item);
}

void HandheldInput::UpdateCollectiblePosition(int id, float x, float y) {
    for (auto& item : mCollectibles) {
        if (item.id == id) {
            item.x = x;
            item.y = y;
            break;
        }
    }
}

void HandheldInput::ClearCollectibles() {
    mCollectibles.clear();
    mCollectedSunCount = 0;
}

bool HandheldInput::CollectSun(int id) {
    for (auto& item : mCollectibles) {
        if (item.id == id) {
            if (!item.collected) {
                item.collected = true;
                mCollectedSunCount++;
                if (mOnCollectSunCallback) {
                    mOnCollectSunCallback(id);
                }
                return true;
            }
            return false;
        }
    }

    // Direct invocation without prior AddCollectible
    mCollectedSunCount++;
    if (mOnCollectSunCallback) {
        mOnCollectSunCallback(id);
    }
    return true;
}

bool HandheldInput::CheckHoverAutoCollect() {
    bool collectedAny = false;
    const float radiusSq = mCursorConfig.hoverRadius * mCursorConfig.hoverRadius;

    for (auto& item : mCollectibles) {
        if (item.collected) {
            continue;
        }

        // Bounding box bounds (centered at item.x, item.y)
        float halfW = item.width * 0.5f;
        float halfH = item.height * 0.5f;
        float minX = item.x - halfW;
        float maxX = item.x + halfW;
        float minY = item.y - halfH;
        float maxY = item.y + halfH;

        // Closest point on AABB to cursor
        float clampX = std::max(minX, std::min(mCursorX, maxX));
        float clampY = std::max(minY, std::min(mCursorY, maxY));
        float dx = mCursorX - clampX;
        float dy = mCursorY - clampY;

        if ((dx * dx + dy * dy) <= radiusSq) {
            item.collected = true;
            mCollectedSunCount++;
            if (mOnCollectSunCallback) {
                mOnCollectSunCallback(item.id);
            }
            collectedAny = true;
        }
    }

    return collectedAny;
}

void HandheldInput::SetSelectedSeedIndex(int index) {
    if (mNumSeeds <= 0) return;
    mSelectedSeed = std::max(0, std::min(mNumSeeds - 1, index));
}

void HandheldInput::SetNumSeeds(int num) {
    mNumSeeds = std::max(1, num);
    if (mSelectedSeed >= mNumSeeds) {
        mSelectedSeed = mNumSeeds - 1;
    }
}

void HandheldInput::CycleSeedPacket(int delta) {
    if (mNumSeeds <= 0) return;
    mSelectedSeed = (mSelectedSeed + delta) % mNumSeeds;
    if (mSelectedSeed < 0) {
        mSelectedSeed += mNumSeeds;
    }
    if (mOnSeedSelectedCallback) {
        mOnSeedSelectedCallback(mSelectedSeed);
    }
}

void HandheldInput::ToggleShovel() {
    mShovelActive = !mShovelActive;
}

void HandheldInput::PlantOrConfirm() {
    if (mOnPlantConfirmCallback) {
        mOnPlantConfirmCallback(mGridCol, mGridRow, mSelectedSeed);
    }
}

void HandheldInput::CancelOrDrop() {
    if (mShovelActive) {
        mShovelActive = false;
    }
    if (mOnCancelDropCallback) {
        mOnCancelDropCallback();
    }
}

void HandheldInput::SetShopItemIndex(int item) {
    mShopItemIndex = std::max(0, std::min(mShopConfig.itemsPerPage - 1, item));
}

void HandheldInput::SetShopPageIndex(int page) {
    mShopPageIndex = std::max(0, std::min(mShopConfig.numPages - 1, page));
}

void HandheldInput::SetShopConfig(int itemsPerPage, int numPages) {
    mShopConfig.itemsPerPage = std::max(1, itemsPerPage);
    mShopConfig.numPages = std::max(1, numPages);
    SetShopItemIndex(mShopItemIndex);
    SetShopPageIndex(mShopPageIndex);
}

void HandheldInput::ShopBuy() {
    if (mOnShopBuyCallback) {
        mOnShopBuyCallback(mShopPageIndex, mShopItemIndex);
    }
}

void HandheldInput::ShopExit() {
    if (mOnShopExitCallback) {
        mOnShopExitCallback();
    }
}

} // namespace Input
} // namespace PvZ
