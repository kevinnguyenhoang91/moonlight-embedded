# Cross-compilation environment for the PortMaster (aarch64) build.
# Debian bullseye keeps the glibc requirement at 2.31 or lower, which loads on
# ArkOS/dArkOS, ROCKNIX, Knulli and muOS.
FROM debian:bullseye

ARG FFMPEG_VERSION=7.1.1
ARG CURL_VERSION=8.11.1

ENV DEBIAN_FRONTEND=noninteractive

# Bullseye is EOL: its packages now live only on archive.debian.org.
RUN printf '%s\n' \
      'deb http://archive.debian.org/debian bullseye main' \
      'deb http://archive.debian.org/debian bullseye-updates main' \
      'deb http://archive.debian.org/debian-security bullseye-security main' \
      > /etc/apt/sources.list \
 && dpkg --add-architecture arm64 \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      ca-certificates curl xz-utils build-essential cmake pkg-config perl zip file \
      crossbuild-essential-arm64 \
      libssl-dev:arm64 libexpat1-dev:arm64 libavahi-client-dev:arm64 \
      uuid-dev:arm64 libevdev-dev:arm64 libudev-dev:arm64 libopus-dev:arm64 \
      libsdl2-dev:arm64 \
 && rm -rf /var/lib/apt/lists/*

ENV PKG_CONFIG_LIBDIR=/opt/deps/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig

# Minimal static FFmpeg: H.264/HEVC software decoders only.
RUN curl -fsSL https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz | tar -xJ -C /tmp \
 && cd /tmp/ffmpeg-${FFMPEG_VERSION} \
 && ./configure --prefix=/opt/deps \
      --enable-cross-compile --cross-prefix=aarch64-linux-gnu- \
      --arch=aarch64 --target-os=linux --pkg-config=pkg-config \
      --enable-static --disable-shared --enable-pic \
      --disable-autodetect --disable-everything --disable-programs --disable-doc \
      --disable-network --disable-avdevice --disable-avformat --disable-avfilter \
      --disable-swscale --disable-swresample \
      --enable-decoder=h264,hevc --enable-parser=h264,hevc \
 && make -j"$(nproc)" && make install \
 && install -Dm644 COPYING.LGPLv2.1 /opt/deps/share/licenses/LICENSE.ffmpeg.txt \
 && rm -rf /tmp/ffmpeg-${FFMPEG_VERSION}

# Minimal static libcurl (HTTP/HTTPS over OpenSSL only), linked into libgamestream.
RUN curl -fsSL https://curl.se/download/curl-${CURL_VERSION}.tar.xz | tar -xJ -C /tmp \
 && cd /tmp/curl-${CURL_VERSION} \
 && ./configure --host=aarch64-linux-gnu --prefix=/opt/deps \
      --enable-static --disable-shared --with-pic \
      --with-openssl --without-zlib --without-brotli --without-zstd \
      --without-libpsl --without-nghttp2 --without-libidn2 --without-librtmp \
      --without-libssh2 --disable-ldap --disable-ldaps --disable-rtsp \
      --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
      --disable-imap --disable-smb --disable-smtp --disable-gopher \
      --disable-mqtt --disable-manual --disable-docs \
 && make -j"$(nproc)" && make install \
 && install -Dm644 COPYING /opt/deps/share/licenses/LICENSE.curl.txt \
 && rm -rf /tmp/curl-${CURL_VERSION}

COPY toolchain-aarch64.cmake /opt/toolchain-aarch64.cmake
