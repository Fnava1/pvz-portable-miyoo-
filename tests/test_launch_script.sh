#!/usr/bin/env bash
# ==============================================================================
# Integration Test: OnionOS launch.sh and .port lifecycle traps & packaging
# ==============================================================================
set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LAUNCH_SH="${REPO_ROOT}/ports/Games/Plants vs. Zombies/launch.sh"
PORT_FILE="${REPO_ROOT}/ports/Plants vs. Zombies.port"
PKG_LAUNCH_SH="${REPO_ROOT}/packaging/Roms/PORTS/Games/Plants vs. Zombies/launch.sh"
PKG_PORT_FILE="${REPO_ROOT}/packaging/Roms/PORTS/Shortcuts/Strategy/Plants vs. Zombies.port"

FAILED=0
TEST_COUNT=0

pass() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo "[PASS] $1"
}

fail() {
    TEST_COUNT=$((TEST_COUNT + 1))
    echo "[FAIL] $1"
    FAILED=$((FAILED + 1))
}

assert_file_exists() {
    local file="$1"
    local desc="$2"
    if [ -f "$file" ]; then
        pass "File exists: $desc ($file)"
    else
        fail "File missing: $desc ($file)"
    fi
}

assert_syntax() {
    local file="$1"
    local desc="$2"
    if bash -n "$file" 2>/dev/null; then
        pass "Syntax valid (bash -n): $desc"
    else
        fail "Syntax error in $desc"
    fi
}

assert_pattern() {
    local file="$1"
    local pattern="$2"
    local desc="$3"
    if grep -E -q "$pattern" "$file"; then
        pass "Pattern matched ($desc): '$pattern'"
    else
        fail "Pattern missing ($desc): '$pattern' in $file"
    fi
}

echo "=========================================================="
echo " Running OnionOS Packaging & Lifecycle Integration Tests"
echo "=========================================================="

# ------------------------------------------------------------------------------
# 1. Existence and Syntax Checks
# ------------------------------------------------------------------------------
assert_file_exists "$LAUNCH_SH" "ports launch.sh"
assert_file_exists "$PORT_FILE" "ports shortcut .port"
assert_file_exists "$PKG_LAUNCH_SH" "packaging launch.sh"
assert_file_exists "$PKG_PORT_FILE" "packaging shortcut .port"

assert_syntax "$LAUNCH_SH" "ports launch.sh"
assert_syntax "$PORT_FILE" "ports shortcut .port"
assert_syntax "$PKG_LAUNCH_SH" "packaging launch.sh"
assert_syntax "$PKG_PORT_FILE" "packaging shortcut .port"

# ------------------------------------------------------------------------------
# 2. Static Analysis: Lifecycle Traps, Overclock & Environment
# ------------------------------------------------------------------------------
for script in "$LAUNCH_SH" "$PKG_LAUNCH_SH"; do
    script_name="$(basename "$(dirname "$script")")/$(basename "$script")"
    assert_pattern "$script" 'trap[[:space:]]+cleanup[[:space:]]+.*EXIT.*INT.*TERM.*HUP' "${script_name} traps EXIT INT TERM HUP"
    assert_pattern "$script" 'sync' "${script_name} invokes sync on cleanup"
    assert_pattern "$script" 'cpuclock[[:space:]]+1200' "${script_name} restores CPU clock to 1200 MHz"
    assert_pattern "$script" 'IS_PLUS' "${script_name} performs hardware model detection"
    assert_pattern "$script" 'CPU_FREQ=1800' "${script_name} sets 1800 MHz for Plus"
    assert_pattern "$script" 'CPU_FREQ=1400' "${script_name} sets 1400 MHz for non-Plus"
    assert_pattern "$script" 'cpuclock[[:space:]]+\$CPU_FREQ' "${script_name} applies dynamic CPU clock"
    assert_pattern "$script" 'ondemand' "${script_name} restores ondemand governor"
    assert_pattern "$script" 'audioserver[[:space:]]*&' "${script_name} restarts audioserver in background"
    assert_pattern "$script" 'killall[[:space:]]+-9[[:space:]]+audioserver' "${script_name} isolates audio via killall audioserver"
    assert_pattern "$script" 'SDL_MMIYOO_TEXTURE_POOL=1' "${script_name} exports SDL_MMIYOO_TEXTURE_POOL=1"
    assert_pattern "$script" 'SDL_MMIYOO_TEXTURE_POOL_MAX_BYTES=16777216' "${script_name} exports 16MB texture pool"
    assert_pattern "$script" 'SDL_MMIYOO_VSYNC_MODE=adaptive' "${script_name} exports SDL_MMIYOO_VSYNC_MODE=adaptive"
    assert_pattern "$script" 'PVZ_SMOOTH_SCALE=0' "${script_name} exports PVZ_SMOOTH_SCALE=0"
    assert_pattern "$script" 'setup_miyoo' "${script_name} handles setup_miyoo lifecycle"
    assert_pattern "$script" 'main_mmp\.pak' "${script_name} detects main_mmp.pak"
done

# ------------------------------------------------------------------------------
# 3. Static Analysis: Shortcut Script Resolution
# ------------------------------------------------------------------------------
for port in "$PORT_FILE" "$PKG_PORT_FILE"; do
    port_name="$(basename "$port")"
    assert_pattern "$port" 'Games/Plants vs\. Zombies' "${port_name} references Games/Plants vs. Zombies"
    assert_pattern "$port" '/mnt/SDCARD/Roms/PORTS/Games/Plants vs\. Zombies' "${port_name} contains /mnt/SDCARD fallback"
    assert_pattern "$port" 'launch_standalone\.sh' "${port_name} checks launch_standalone.sh fallback"
    assert_pattern "$port" 'GameDataFile="main\*\.pak"' "${port_name} uses single wildcard GameDataFile pattern"
    
    # Verify exactly one GameDataFile= line exists to prevent OnionOS ~Import ports parsing failures
    gdf_count=$(grep -c "GameDataFile=" "$port" || true)
    if [ "$gdf_count" -eq 1 ]; then
        pass "${port_name} contains exactly one GameDataFile= assignment"
    else
        fail "${port_name} contains ${gdf_count} GameDataFile= assignments (expected 1)"
    fi
done

# ------------------------------------------------------------------------------
# 4. Behavioral Sandbox Tests for launch.sh
# ------------------------------------------------------------------------------
export SANDBOX_DIR="$(mktemp -d -t pvz_launch_test_XXXXXX)"
MOCK_BIN_DIR="${SANDBOX_DIR}/mock_bin"
MOCK_GAME_DIR="${SANDBOX_DIR}/game"
mkdir -p "$MOCK_BIN_DIR" "$MOCK_GAME_DIR"

# Create mock system commands
cat <<EOF > "${MOCK_BIN_DIR}/sync"
#!/bin/sh
echo "MOCK_SYNC_CALLED" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${MOCK_BIN_DIR}/sync"

cat <<EOF > "${MOCK_BIN_DIR}/cpuclock"
#!/bin/sh
echo "MOCK_CPUCLOCK_\$1" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${MOCK_BIN_DIR}/cpuclock"

cat <<EOF > "${MOCK_BIN_DIR}/killall"
#!/bin/sh
echo "MOCK_KILLALL_\$@" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${MOCK_BIN_DIR}/killall"

# Create mock audioserver and sysfs scaling_governor
mkdir -p "${SANDBOX_DIR}/customer/app"
cat <<EOF > "${SANDBOX_DIR}/customer/app/audioserver"
#!/bin/sh
echo "MOCK_AUDIOSERVER_STARTED" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${SANDBOX_DIR}/customer/app/audioserver"

# Helper to run launch.sh in sandbox
run_sandbox_test() {
    local scenario="$1"
    rm -rf "${MOCK_GAME_DIR:?}"/* "${MOCK_GAME_DIR}"/.[!.]* "${SANDBOX_DIR}/mock_log.txt" 2>/dev/null || true
    cp "$LAUNCH_SH" "${MOCK_GAME_DIR}/launch.sh"
    chmod +x "${MOCK_GAME_DIR}/launch.sh"
}

# --- Scenario A: main_mmp.pak exists + setup_miyoo present -> remove setup_miyoo ---
run_sandbox_test "Scenario A"
touch "${MOCK_GAME_DIR}/main_mmp.pak"
touch "${MOCK_GAME_DIR}/setup_miyoo"
cat <<EOF > "${MOCK_GAME_DIR}/PvZ"
#!/bin/sh
echo "MOCK_PVZ_RUNNING" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${MOCK_GAME_DIR}/PvZ"

(
    export PATH="${MOCK_BIN_DIR}:$PATH"
    cd "${MOCK_GAME_DIR}"
    ./launch.sh >/dev/null 2>&1 || true
)

if [ ! -f "${MOCK_GAME_DIR}/setup_miyoo" ]; then
    pass "Scenario A: setup_miyoo removed when main_mmp.pak exists"
else
    fail "Scenario A: setup_miyoo still present despite main_mmp.pak"
fi

if grep -q "MOCK_PVZ_RUNNING" "${SANDBOX_DIR}/mock_log.txt"; then
    pass "Scenario A: PvZ executable ran successfully"
else
    fail "Scenario A: PvZ executable did not run"
fi

# --- Scenario B: main_mmp.pak exists + .installed exists + setup_miyoo present -> remove setup_miyoo ---
run_sandbox_test "Scenario B"
touch "${MOCK_GAME_DIR}/main_mmp.pak"
touch "${MOCK_GAME_DIR}/.installed"
touch "${MOCK_GAME_DIR}/setup_miyoo"
cat <<EOF > "${MOCK_GAME_DIR}/PvZ"
#!/bin/sh
echo "MOCK_PVZ_RUNNING" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${MOCK_GAME_DIR}/PvZ"

(
    export PATH="${MOCK_BIN_DIR}:$PATH"
    cd "${MOCK_GAME_DIR}"
    ./launch.sh >/dev/null 2>&1 || true
)

if [ ! -f "${MOCK_GAME_DIR}/setup_miyoo" ]; then
    pass "Scenario B: setup_miyoo removed when main_mmp.pak exists"
else
    fail "Scenario B: setup_miyoo still present despite main_mmp.pak"
fi

# --- Scenario C: Only raw main.pak exists -> run setup_miyoo, create .installed, delete setup_miyoo ---
run_sandbox_test "Scenario C"
touch "${MOCK_GAME_DIR}/main.pak"
cat <<EOF > "${MOCK_GAME_DIR}/setup_miyoo"
#!/bin/sh
echo "MOCK_SETUP_MIYOO_EXECUTED" >> "${SANDBOX_DIR}/mock_log.txt"
touch main_mmp.pak
exit 0
EOF
chmod +x "${MOCK_GAME_DIR}/setup_miyoo"

cat <<EOF > "${MOCK_GAME_DIR}/PvZ"
#!/bin/sh
echo "MOCK_PVZ_RUNNING" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${MOCK_GAME_DIR}/PvZ"

(
    export PATH="${MOCK_BIN_DIR}:$PATH"
    cd "${MOCK_GAME_DIR}"
    ./launch.sh >/dev/null 2>&1 || true
)

if grep -q "MOCK_SETUP_MIYOO_EXECUTED" "${SANDBOX_DIR}/mock_log.txt"; then
    pass "Scenario C: setup_miyoo was executed for raw main.pak"
else
    fail "Scenario C: setup_miyoo was not executed for raw main.pak"
fi

if [ ! -f "${MOCK_GAME_DIR}/setup_miyoo" ]; then
    pass "Scenario C: setup_miyoo was deleted after successful conversion"
else
    fail "Scenario C: setup_miyoo was not deleted after conversion"
fi

if [ -f "${MOCK_GAME_DIR}/.installed" ]; then
    pass "Scenario C: .installed flag was created after setup"
else
    fail "Scenario C: .installed flag was not created"
fi

# --- Scenario E: Stray .installed exists + raw main.pak exists (main_mmp.pak missing) -> MUST run setup_miyoo ---
run_sandbox_test "Scenario E"
touch "${MOCK_GAME_DIR}/.installed"
touch "${MOCK_GAME_DIR}/main.pak"
cat <<EOF > "${MOCK_GAME_DIR}/setup_miyoo"
#!/bin/sh
echo "MOCK_SETUP_MIYOO_EXECUTED_SCENARIO_E" >> "${SANDBOX_DIR}/mock_log.txt"
touch main_mmp.pak
exit 0
EOF
chmod +x "${MOCK_GAME_DIR}/setup_miyoo"

cat <<EOF > "${MOCK_GAME_DIR}/PvZ"
#!/bin/sh
echo "MOCK_PVZ_RUNNING" >> "${SANDBOX_DIR}/mock_log.txt"
EOF
chmod +x "${MOCK_GAME_DIR}/PvZ"

(
    export PATH="${MOCK_BIN_DIR}:$PATH"
    cd "${MOCK_GAME_DIR}"
    ./launch.sh >/dev/null 2>&1 || true
)

if grep -q "MOCK_SETUP_MIYOO_EXECUTED_SCENARIO_E" "${SANDBOX_DIR}/mock_log.txt"; then
    pass "Scenario E: setup_miyoo executed even when stray .installed is present"
else
    fail "Scenario E: stray .installed incorrectly blocked setup_miyoo execution"
fi

# --- Scenario D: Trap cleanup verification (sync, cpuclock 1200, audioserver) ---
if grep -q "MOCK_SYNC_CALLED" "${SANDBOX_DIR}/mock_log.txt"; then
    pass "Scenario D: cleanup trap called sync"
else
    fail "Scenario D: cleanup trap did not call sync"
fi

if grep -q "MOCK_CPUCLOCK_1200" "${SANDBOX_DIR}/mock_log.txt"; then
    pass "Scenario D: cleanup trap restored CPU clock to 1200"
else
    fail "Scenario D: cleanup trap did not restore CPU clock to 1200"
fi

# Cleanup sandbox
rm -rf "${SANDBOX_DIR}"

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
echo "=========================================================="
if [ "$FAILED" -eq 0 ]; then
    echo " ALL $TEST_COUNT TESTS PASSED!"
    echo "=========================================================="
    exit 0
else
    echo " $FAILED OF $TEST_COUNT TESTS FAILED."
    echo "=========================================================="
    exit 1
fi
