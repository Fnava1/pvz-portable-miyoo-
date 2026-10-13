#!/usr/bin/env bash
set -euo pipefail
source /root/setup-env.sh 2>/dev/null || true

PREFIX="/tmp/deps"
BUILD_DIR="/tmp/build_deps"
rm -rf "$BUILD_DIR" "$PREFIX"
mkdir -p "$BUILD_DIR" "$PREFIX"
cd "$BUILD_DIR"

echo "=== 1. Building libogg ==="
wget -q https://downloads.xiph.org/releases/ogg/libogg-1.3.5.tar.gz
tar -xzf libogg-1.3.5.tar.gz
cd libogg-1.3.5
./configure --host=arm-linux-gnueabihf --prefix="$PREFIX" --enable-static --disable-shared
make -j4
make install
cd "$BUILD_DIR"

echo "=== 2. Building libvorbis ==="
wget -q https://downloads.xiph.org/releases/vorbis/libvorbis-1.3.7.tar.gz
tar -xzf libvorbis-1.3.7.tar.gz
cd libvorbis-1.3.7
./configure --host=arm-linux-gnueabihf --prefix="$PREFIX" --with-ogg="$PREFIX" --enable-static --disable-shared
make -j4
make install
cd "$BUILD_DIR"

echo "=== 3. Building libmpg123 ==="
wget -q https://downloads.sourceforge.net/project/mpg123/mpg123/1.32.5/mpg123-1.32.5.tar.bz2
tar -xjf mpg123-1.32.5.tar.bz2
cd mpg123-1.32.5
./configure --host=arm-linux-gnueabihf --prefix="$PREFIX" --enable-static --disable-shared --with-cpu=arm_fpu --with-audio=dummy --with-default-audio=dummy --disable-modules
make -j4
make install
cd "$BUILD_DIR"

echo "=== 4. Building libopenmpt ==="
wget -q https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.7.13+release.autotools.tar.gz
tar -xzf libopenmpt-0.7.13+release.autotools.tar.gz
cd libopenmpt-0.7.13+release.autotools
PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig" ./configure \
  --host=arm-linux-gnueabihf \
  --prefix="$PREFIX" \
  --enable-static \
  --disable-shared \
  --without-portaudio \
  --without-portaudiocpp \
  --without-pulseaudio \
  --without-sndfile \
  --without-flac \
  --disable-openmpt123 \
  CPPFLAGS="-I$PREFIX/include" \
  LDFLAGS="-L$PREFIX/lib"
make -j4
make install

echo "=== 5. Merging static libraries into single libopenmpt.a ==="
cd "$PREFIX/lib"
cat << 'EOF' > merge.mri
create /root/workspace/3rdparty/libopenmpt/lib/libopenmpt.a
addlib libopenmpt.a
addlib libvorbisfile.a
addlib libvorbis.a
addlib libogg.a
addlib libmpg123.a
save
end
EOF
arm-linux-gnueabihf-ar -M < merge.mri
arm-linux-gnueabihf-ranlib /root/workspace/3rdparty/libopenmpt/lib/libopenmpt.a
arm-linux-gnueabihf-strip --strip-debug /root/workspace/3rdparty/libopenmpt/lib/libopenmpt.a
ls -lh /root/workspace/3rdparty/libopenmpt/lib/libopenmpt.a
echo "ALL_DEPS_BUILT_AND_MERGED_SUCCESSFULLY"
