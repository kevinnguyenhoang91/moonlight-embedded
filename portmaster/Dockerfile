# Cross-compilation environment for the PortMaster (aarch64) build.
# Debian bullseye keeps the glibc requirement at 2.31 or lower, which loads on
# ArkOS/dArkOS, ROCKNIX, Knulli and muOS.
FROM debian:bullseye

ARG FFMPEG_VERSION=7.1.1
ARG OPENSSL_VERSION=3.5.9
# From the openssl/openssl GitHub release; verify against the .asc signature
# (OpenSSL, B146647E45A7B33947AB226B2A2C87D161692D40) when bumping.
ARG OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
ARG CURL_VERSION=8.23.0
# From https://curl.se/download/curl-${CURL_VERSION}.tar.xz; verify against the .asc
# signature (Daniel Stenberg, 27EDEAF22F3ABCEB50DB9A125CC908FDB71E12C2) when bumping.
ARG CURL_SHA256=8dadd7d2536a314f982ad05a651b713f426bf7d02cc2c76cffb9796f2218e6c5

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
      libexpat1-dev:arm64 libavahi-client-dev:arm64 \
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

# Static OpenSSL 3 (bullseye ships 1.1.1, which curl >= 8.18 no longer supports).
# Linked into libgamestream only, which exports it to the rest of the process.
# OPENSSLDIR is private to /opt/deps, so the device's openssl.cnf is never loaded.
RUN curl -fsSL -o /tmp/openssl.tar.gz \
      https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz \
 && echo "${OPENSSL_SHA256}  /tmp/openssl.tar.gz" | sha256sum -c - \
 && tar -xzf /tmp/openssl.tar.gz -C /tmp && rm /tmp/openssl.tar.gz \
 && cd /tmp/openssl-${OPENSSL_VERSION} \
 && ./Configure linux-aarch64 --cross-compile-prefix=aarch64-linux-gnu- \
      --prefix=/opt/deps --libdir=lib --openssldir=/opt/deps/ssl \
      no-shared no-module no-apps no-tests no-docs -fPIC \
 && make -j"$(nproc)" && make install_dev \
 && install -Dm644 LICENSE.txt /opt/deps/share/licenses/LICENSE.openssl.txt \
 && rm -rf /tmp/openssl-${OPENSSL_VERSION}

# Minimal static libcurl (HTTP/HTTPS over OpenSSL only), linked into libgamestream.
RUN curl -fsSL -o /tmp/curl.tar.xz https://curl.se/download/curl-${CURL_VERSION}.tar.xz \
 && echo "${CURL_SHA256}  /tmp/curl.tar.xz" | sha256sum -c - \
 && tar -xJf /tmp/curl.tar.xz -C /tmp && rm /tmp/curl.tar.xz \
 && cd /tmp/curl-${CURL_VERSION} \
 && ./configure --host=aarch64-linux-gnu --prefix=/opt/deps \
      --enable-static --disable-shared --with-pic \
      --with-openssl=/opt/deps --without-zlib --without-brotli --without-zstd \
      --without-libpsl --without-nghttp2 --without-libidn2 \
      --without-libssh2 --disable-ldap --disable-ldaps --disable-rtsp \
      --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
      --disable-imap --disable-smtp --disable-gopher \
      --disable-mqtt --disable-manual --disable-docs \
 && make -j"$(nproc)" && make install \
 && install -Dm644 COPYING /opt/deps/share/licenses/LICENSE.curl.txt \
 && rm -rf /tmp/curl-${CURL_VERSION}

COPY toolchain-aarch64.cmake /opt/toolchain-aarch64.cmake
