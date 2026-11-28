#!/bin/bash
# Runs inside the builder image (see Dockerfile). Cross-compiles Moonlight for
# aarch64 and assembles the PortMaster zip.
set -euo pipefail

PORT=moonlightembedded
SCRIPT="Moonlight Embedded.sh"
SRC=/src
OUT="$SRC/portmaster"
BUILD="$OUT/build"
STAGE="$BUILD/stage"
PORTDIR="$STAGE/$PORT"
LIBDIR="$PORTDIR/libs.aarch64"
SYSLIB=/usr/lib/aarch64-linux-gnu
STRIP=aarch64-linux-gnu-strip

rm -rf "$BUILD" "$OUT/dist"
mkdir -p "$BUILD/cmake" "$LIBDIR" "$PORTDIR/licenses" "$OUT/dist"

cmake -S "$SRC" -B "$BUILD/cmake" \
  -DCMAKE_TOOLCHAIN_FILE=/opt/toolchain-aarch64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_SKIP_RPATH=ON \
  -DCMAKE_C_FLAGS="-I/opt/deps/include" \
  -DCMAKE_EXE_LINKER_FLAGS="-L/opt/deps/lib" \
  -DCMAKE_SHARED_LINKER_FLAGS="-L/opt/deps/lib" \
  -DCURL_INCLUDE_DIR=/opt/deps/include \
  -DCURL_LIBRARY=/opt/deps/lib/libcurl.a \
  -DOPENSSL_ROOT_DIR=/opt/deps \
  -DOPENSSL_USE_STATIC_LIBS=TRUE \
  -DENABLE_X11=OFF -DENABLE_CEC=OFF -DENABLE_PULSE=OFF
cmake --build "$BUILD/cmake" -j"$(nproc)" --target moonlight

# Port files
cp -a "$SRC/portmaster/port/." "$STAGE/"
mv "$STAGE/README.md" "$PORTDIR/README.md"
mv "$STAGE/port.json" "$STAGE/gameinfo.xml" "$PORTDIR/"
install -m755 "$BUILD/cmake/moonlight" "$PORTDIR/moonlight.aarch64"

# Bundled libraries: our own, plus those not guaranteed on every CFW
# (Opus, Avahi client). OpenSSL is linked statically into libgamestream.
# Everything else comes from the device.
install -m644 "$BUILD/cmake/libgamestream/libgamestream.so.4" "$LIBDIR/"
install -m644 "$BUILD/cmake/libgamestream/libmoonlight-common.so.4" "$LIBDIR/"
for lib in libopus.so.0 libavahi-client.so.3 libavahi-common.so.3; do
  install -m644 "$(readlink -f "$SYSLIB/$lib")" "$LIBDIR/$lib"
done
$STRIP --strip-unneeded "$PORTDIR/moonlight.aarch64" "$LIBDIR"/*.so*

# Every NEEDED entry must be bundled or part of the base system of all CFWs.
SYSTEM_LIBS="ld-linux-aarch64.so.1 libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1
  libSDL2-2.0.so.0 libudev.so.1 libevdev.so.2 libuuid.so.1 libexpat.so.1 libdbus-1.so.3 libasound.so.2"
missing=0
for f in "$PORTDIR/moonlight.aarch64" "$LIBDIR"/*.so*; do
  for need in $(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
    if [ ! -e "$LIBDIR/$need" ] && ! grep -qw -- "$need" <<< "$SYSTEM_LIBS"; then
      echo "Unbundled dependency: $(basename "$f") -> $need" >&2
      missing=1
    fi
  done
done
[ "$missing" -eq 0 ]

echo "Highest glibc symbol version required:"
for f in "$PORTDIR/moonlight.aarch64" "$LIBDIR"/*.so*; do
  readelf -V "$f" | grep -o 'GLIBC_[0-9.]*'
done | sort -uV | tail -n 1

# Licenses
cp "$SRC/LICENSE" "$PORTDIR/licenses/LICENSE.moonlight-embedded.txt"
cp "$SRC/third_party/moonlight-common-c/LICENSE.txt" "$PORTDIR/licenses/LICENSE.moonlight-common-c.txt"
cp /opt/deps/share/licenses/* "$PORTDIR/licenses/"
for pkg in libopus0 libavahi-client3; do
  cp "/usr/share/doc/$pkg/copyright" "$PORTDIR/licenses/LICENSE.$pkg.txt"
done

(cd "$STAGE" && zip -r -9 -q "$OUT/dist/$PORT.zip" "$SCRIPT" "$PORT")
zip -sf "$OUT/dist/$PORT.zip"
