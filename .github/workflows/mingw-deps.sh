#!/usr/bin/env bash
# Cross-build the static libraries FujiNet-PC needs for Windows with MinGW-w64 into <prefix>,
# and write <prefix>/toolchain.cmake for the FujiNet-PC build. Run on Ubuntu with the
# mingw-w64, cmake, ninja, perl, git and curl packages installed.
set -euo pipefail

PREFIX=${1:?usage: mingw-deps.sh <prefix>}
mkdir -p "$PREFIX"
PREFIX=$(cd "$PREFIX" && pwd)
WORK=$PREFIX/src
JOBS=$(nproc)

TRIPLE=x86_64-w64-mingw32
# the posix thread model, which std::thread and std::mutex need
CC_WIN=$TRIPLE-gcc-posix
CXX_WIN=$TRIPLE-g++-posix

ZLIB_SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
EXPAT_SHA256=354552544b8f99012e5062f7d570ec77f14b412a3ff5c7d8d0dae62c0d217c30
MBEDTLS_SHA256=4a11f1777bb95bf4ad96721cac945a26e04bf19f57d905f241fe77ebeddf46d8
OPENSSL_COMMIT=636dfadc70ce26f2473870570bfd9ec352806b1d # openssl-3.5.0

cat > "$PREFIX/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $CC_WIN)
set(CMAKE_CXX_COMPILER $CXX_WIN)
set(CMAKE_RC_COMPILER $TRIPLE-windres)
set(CMAKE_FIND_ROOT_PATH /usr/$TRIPLE $PREFIX)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# fujinet_pc.cmake overrides CMAKE_EXE_LINKER_FLAGS, so -static and the library path ride here
set(CMAKE_C_STANDARD_LIBRARIES "-L$PREFIX/lib -static -lws2_32 -lcrypt32 -lbcrypt" CACHE STRING "")
set(CMAKE_CXX_STANDARD_LIBRARIES "-L$PREFIX/lib -static -lws2_32 -lcrypt32 -lbcrypt" CACHE STRING "")
EOF

mkdir -p "$WORK"
cd "$WORK"

fetch() { # <url> <sha256> <file>
    curl -fsSL -o "$3" "$1"
    echo "$2  $3" | sha256sum -c -
}

CMAKE_DEP=(-G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$PREFIX/toolchain.cmake"
           "-DCMAKE_INSTALL_PREFIX=$PREFIX")

fetch https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz "$ZLIB_SHA256" zlib.tar.gz
tar -xzf zlib.tar.gz
cmake -S zlib-1.3.1 -B zlib-build "${CMAKE_DEP[@]}"
cmake --build zlib-build -j"$JOBS"
cmake --install zlib-build
# zlib installs the shared import library too; keep the link static
rm -f "$PREFIX"/lib/libzlib.dll.a "$PREFIX"/bin/zlib1.dll
cp "$PREFIX/lib/libzlibstatic.a" "$PREFIX/lib/libz.a"

fetch https://github.com/libexpat/libexpat/releases/download/R_2_7_1/expat-2.7.1.tar.xz "$EXPAT_SHA256" expat.tar.xz
tar -xJf expat.tar.xz
cmake -S expat-2.7.1 -B expat-build "${CMAKE_DEP[@]}" -DEXPAT_SHARED_LIBS=OFF \
    -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TESTS=OFF \
    -DEXPAT_BUILD_DOCS=OFF
cmake --build expat-build -j"$JOBS"
cmake --install expat-build

fetch https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.5/mbedtls-3.6.5.tar.bz2 \
    "$MBEDTLS_SHA256" mbedtls.tar.bz2
tar -xjf mbedtls.tar.bz2
cmake -S mbedtls-3.6.5 -B mbedtls-build "${CMAKE_DEP[@]}" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF \
    -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DMBEDTLS_FATAL_WARNINGS=OFF
cmake --build mbedtls-build -j"$JOBS"
cmake --install mbedtls-build

git init -q openssl-src
git -C openssl-src fetch -q --depth 1 https://github.com/openssl/openssl.git "$OPENSSL_COMMIT"
git -C openssl-src checkout -q FETCH_HEAD
test "$(git -C openssl-src rev-parse HEAD)" = "$OPENSSL_COMMIT"
(
    cd openssl-src
    CC=$CC_WIN AR=$TRIPLE-ar RANLIB=$TRIPLE-ranlib RC=$TRIPLE-windres \
        ./Configure mingw64 no-shared no-tests --prefix="$PREFIX" --libdir=lib
    make -j"$JOBS" build_libs
    make install_dev
)

echo "MinGW dependencies installed in $PREFIX"
