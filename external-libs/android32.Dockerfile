# ============================================================
# Monero Android (armeabi-v7a) build image
# ============================================================
FROM debian:bullseye

ENV DEBIAN_FRONTEND=noninteractive
ENV API_LEVEL=33
ENV WORK_DIR=/work
ENV ANDROID_NDK_HOME=${WORK_DIR}/android-ndk
ENV ANDROID_NDK_ROOT=${ANDROID_NDK_HOME}
ENV TOOLCHAIN=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64
ENV PATH="${TOOLCHAIN}/bin:${PATH}"
ENV TARGET=armv7a-linux-androideabi
ENV AR=${TOOLCHAIN}/bin/llvm-ar
ENV AS=${TOOLCHAIN}/bin/llvm-as
ENV CC=${TOOLCHAIN}/bin/${TARGET}${API_LEVEL}-clang
ENV CXX=${TOOLCHAIN}/bin/${TARGET}${API_LEVEL}-clang++
ENV LD=${TOOLCHAIN}/bin/ld
ENV RANLIB=${TOOLCHAIN}/bin/llvm-ranlib
ENV STRIP=${TOOLCHAIN}/bin/llvm-strip
ENV PKG_CONFIG_PATH=${WORK_DIR}/local/lib/pkgconfig
ENV NPROC=$(nproc)
ENV BOOST_ROOT=${WORK_DIR}/local/boost
ENV PREFIX=${WORK_DIR}/local

# ------------------------------------------------------------
# Base dependencies
# ------------------------------------------------------------
RUN apt-get update && apt-get install -y \
    autoconf automake libtool pkg-config git curl unzip make cmake ninja-build \
    g++ perl flex bison wget python3 python3-distutils build-essential zlib1g-dev ccache \
    gettext ca-certificates autoconf-archive m4 sed file \
    && rm -rf /var/lib/apt/lists/*

WORKDIR ${WORK_DIR}

# ------------------------------------------------------------
# Android NDK (copy from build context)
# ------------------------------------------------------------
COPY ndk29 ${ANDROID_NDK_HOME}

# ------------------------------------------------------------
# Boost for Android ARM32
# ------------------------------------------------------------
COPY boost_1_81_0.tar.gz ${WORK_DIR}/boost/
WORKDIR ${WORK_DIR}/boost
RUN set -eux; \
    if [ -f ${BOOST_ROOT}/lib/libboost_system.a ]; then \
        echo "[OK] Boost already built at ${BOOST_ROOT}, skipping"; \
    else \
        echo "[BUILD] Building Boost for Android ARM32..."; \
        tar xf boost_1_81_0.tar.gz && cd boost_1_81_0; \
        echo "import os ;" > user-config.jam; \
        echo "using clang : android" >> user-config.jam; \
        echo ":" >> user-config.jam; \
        echo "${TOOLCHAIN}/bin/armv7a-linux-androideabi${API_LEVEL}-clang++" >> user-config.jam; \
        echo ":" >> user-config.jam; \
        echo "<compileflags>-fPIC" >> user-config.jam; \
        echo "<compileflags>-std=c++17" >> user-config.jam; \
        echo "<compileflags>-DANDROID" >> user-config.jam; \
        echo "<compileflags>-D__ANDROID_API__=${API_LEVEL}" >> user-config.jam; \
        echo "<compileflags>--target=armv7a-linux-androideabi${API_LEVEL}" >> user-config.jam; \
        echo "<compileflags>-march=armv7-a" >> user-config.jam; \
        echo "<compileflags>-mfloat-abi=softfp" >> user-config.jam; \
        echo "<compileflags>-mfpu=neon" >> user-config.jam; \
        echo "<archiver>${TOOLCHAIN}/bin/llvm-ar" >> user-config.jam; \
        echo "<ranlib>${TOOLCHAIN}/bin/llvm-ranlib" >> user-config.jam; \
        echo ";" >> user-config.jam; \
        ./bootstrap.sh --with-toolset=clang; \
        ./b2 install \
            --prefix=${BOOST_ROOT} \
            --user-config=user-config.jam \
            --build-type=minimal \
            toolset=clang-android \
            architecture=arm \
            address-model=32 \
            binary-format=elf \
            abi=aapcs \
            target-os=android \
            threading=multi \
            link=static \
            variant=release \
            cxxflags="-fPIC -std=c++17 -DANDROID -march=armv7-a -mfloat-abi=softfp -mfpu=neon" \
            cflags="-fPIC -DANDROID -march=armv7-a -mfloat-abi=softfp -mfpu=neon" \
            --with-system \
            --with-filesystem \
            --with-thread \
            --with-chrono \
            --with-date_time \
            --with-regex \
            --with-serialization \
            --with-program_options \
            --with-locale \
            -sNO_BZIP2=1 \
            -sICU_PATH= \
            -j$(nproc); \
        echo "[OK] Boost build complete"; \
        echo "Verifying Boost libraries architecture..."; \
        file ${BOOST_ROOT}/lib/libboost_system.a; \
        echo "Installed Boost libraries:"; \
        find ${BOOST_ROOT}/lib -name "*.a" -ls; \
    fi
    
# Verify Boost was built for correct architecture
RUN set -eux; \
    echo "=== Verifying Boost architecture ==="; \
    ${AR} t ${BOOST_ROOT}/lib/libboost_system.a | head -1 | xargs -I {} sh -c "${AR} p ${BOOST_ROOT}/lib/libboost_system.a {} | file -" | grep -qi "arm\|armv7" || \
        (echo "[ERROR] Boost not built for ARM architecture!"; \
         echo "Attempting detailed check..."; \
         cd /tmp && ${AR} x ${BOOST_ROOT}/lib/libboost_system.a && file *.o | head -5 && exit 1); \
    echo "[OK] Boost correctly built for Android ARM32"
    
# ------------------------------------------------------------
# Monero source: copy from build context
# ------------------------------------------------------------
WORKDIR ${WORK_DIR}/monero
COPY monero ${WORK_DIR}/monero

# Ensure submodules are initialized (if needed)
RUN set -eux; \
    echo "================================="; \
    echo "Setting up Monero source..."; \
    echo "================================="; \
    cd ${WORK_DIR}/monero; \
    git submodule update --init --force --recursive || true; \
    echo "Current version:"; \
    git describe --tags --always 2>/dev/null || echo "No git tags available"; \
    echo "================================="
                    
# ------------------------------------------------------------
# OpenSSL (android-arm)
# ------------------------------------------------------------
WORKDIR ${WORK_DIR}/openssl
RUN set -eux; \
    if [ -f ${PREFIX}/openssl/arm/lib/libcrypto.a ] && [ -f ${PREFIX}/openssl/arm/lib/libssl.a ]; then \
        echo "[OK] OpenSSL already built at ${PREFIX}/openssl/arm, skipping"; \
    else \
        echo "[BUILD] Building OpenSSL..."; \
        if [ ! -d openssl-src ]; then \
            git clone --depth=1 https://github.com/openssl/openssl.git openssl-src; \
        fi; \
        cd openssl-src; \
        export PATH=${TOOLCHAIN}/bin:$PATH; \
        ./Configure android-arm no-shared no-dso no-tests no-ssl3 no-comp no-hw no-engine \
            --prefix=${PREFIX}/openssl/arm; \
        make -j$(nproc) && make install_sw; \
        echo "[OK] OpenSSL build complete"; \
        ls -lh ${PREFIX}/openssl/arm/lib/libcrypto.a; \
        ls -lh ${PREFIX}/openssl/arm/lib/libssl.a; \
    fi

# ------------------------------------------------------------
# Expat (static)
# ------------------------------------------------------------
WORKDIR ${WORK_DIR}/expat
RUN set -eux; \
    if [ -f ${PREFIX}/expat/arm/lib/libexpat.a ]; then \
        echo "[OK] Expat already built at ${PREFIX}/expat/arm, skipping"; \
    else \
        echo "[BUILD] Building Expat..."; \
        if [ ! -d expat-src ]; then \
            git clone --depth=1 https://github.com/libexpat/libexpat.git expat-src; \
        fi; \
        cd expat-src/expat; \
        cmake -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake \
            -DANDROID_ABI=armeabi-v7a \
            -DANDROID_PLATFORM=android-${API_LEVEL} \
            -DEXPAT_SHARED_LIBS=OFF \
            -DEXPAT_BUILD_EXAMPLES=OFF \
            -DEXPAT_BUILD_TESTS=OFF \
            -DEXPAT_BUILD_TOOLS=OFF \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX=${PREFIX}/expat/arm; \
        ninja install; \
        echo "[OK] Expat build complete"; \
        ls -lh ${PREFIX}/expat/arm/lib/libexpat.a; \
    fi

# ------------------------------------------------------------
# Unbound (static)
# ------------------------------------------------------------
WORKDIR ${WORK_DIR}/unbound
RUN set -eux; \
    if [ -f ${PREFIX}/unbound/arm/lib/libunbound.a ]; then \
        echo "[OK] Unbound already built at ${PREFIX}/unbound/arm, skipping"; \
    else \
        echo "[BUILD] Building Unbound..."; \
        if [ ! -d unbound-src ]; then \
            git clone --depth=1 https://github.com/NLnetLabs/unbound.git unbound-src; \
        fi; \
        cd unbound-src; \
        autoreconf -fi || true; \
        sed -i 's/#ifdef USE_GOST/#if defined(USE_GOST) \&\& OPENSSL_VERSION_NUMBER < 0x30000000L/' sldns/keyraw.c || true; \
        export CC=${TOOLCHAIN}/bin/armv7a-linux-androideabi${API_LEVEL}-clang; \
        export AR=${TOOLCHAIN}/bin/llvm-ar; \
        export RANLIB=${TOOLCHAIN}/bin/llvm-ranlib; \
        export LD=${TOOLCHAIN}/bin/ld; \
        export CFLAGS="-fPIC -O2 --target=armv7a-linux-androideabi${API_LEVEL} -march=armv7-a -mthumb -mfpu=neon -mfloat-abi=softfp"; \
        ./configure \
            --host=arm-linux-androideabi \
            --prefix=${PREFIX}/unbound/arm \
            --with-libexpat=${PREFIX}/expat/arm \
            --with-ssl=${PREFIX}/openssl/arm \
            --disable-shared --enable-static --disable-flto --disable-gost --without-pyunbound; \
        make -j$(nproc) && make install; \
        echo "[OK] Unbound build complete"; \
        ls -lh ${PREFIX}/unbound/arm/lib/libunbound.a; \
    fi

# ------------------------------------------------------------
# libsodium (static) - with verification
# ------------------------------------------------------------
WORKDIR ${WORK_DIR}/sodium
RUN set -eux; \
    if [ -f ${PREFIX}/sodium/arm/lib/libsodium.a ]; then \
        echo "[OK] libsodium already built at ${PREFIX}/sodium/arm, skipping"; \
    else \
        echo "[BUILD] Building libsodium..."; \
        if [ ! -d libsodium-src ]; then \
            git clone --depth=1 --branch=stable https://github.com/jedisct1/libsodium.git libsodium-src; \
        fi; \
        cd libsodium-src; \
        ./autogen.sh; \
        export CC=${TOOLCHAIN}/bin/armv7a-linux-androideabi${API_LEVEL}-clang; \
        export AR=${TOOLCHAIN}/bin/llvm-ar; \
        export RANLIB=${TOOLCHAIN}/bin/llvm-ranlib; \
        export CFLAGS="-fPIC -O2 --target=armv7a-linux-androideabi${API_LEVEL} -march=armv7-a -mthumb -mfpu=neon -mfloat-abi=softfp"; \
        ./configure \
            --host=arm-linux-androideabi \
            --prefix=${PREFIX}/sodium/arm \
            --disable-shared --enable-static; \
        make -j$(nproc) && make install; \
        echo "[OK] libsodium build complete"; \
    fi

# Verify libsodium installation
RUN set -eux; \
    echo "=== Verifying libsodium installation ==="; \
    if [ ! -f ${PREFIX}/sodium/arm/lib/libsodium.a ]; then \
        echo "[ERROR] libsodium.a not found!"; \
        exit 1; \
    fi; \
    if [ ! -f ${PREFIX}/sodium/arm/include/sodium.h ]; then \
        echo "[ERROR] sodium.h not found!"; \
        exit 1; \
    fi; \
    ls -lh ${PREFIX}/sodium/arm/lib/libsodium.a; \
    ls -lh ${PREFIX}/sodium/arm/include/sodium.h; \
    echo "Checking sodium header structure:"; \
    ls -lh ${PREFIX}/sodium/arm/include/sodium/ || echo "No sodium/ subdirectory"; \
    echo "[OK] libsodium verified successfully"

# Create pkg-config file for libsodium
RUN mkdir -p ${PREFIX}/sodium/arm/lib/pkgconfig && \
    echo "prefix=${PREFIX}/sodium/arm" > ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "exec_prefix=\${prefix}" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "libdir=\${prefix}/lib" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "includedir=\${prefix}/include" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "Name: libsodium" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "Description: A modern and easy-to-use crypto library" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "Version: 1.0.18" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "Libs: -L\${libdir} -lsodium" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "Libs.private: -pthread" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc && \
    echo "Cflags: -I\${includedir}" >> ${PREFIX}/sodium/arm/lib/pkgconfig/libsodium.pc

# ------------------------------------------------------------
# libzmq (ZeroMQ) - static library for Android
# ------------------------------------------------------------
WORKDIR ${WORK_DIR}/libzmq
RUN set -eux; \
    if [ -f ${PREFIX}/libzmq/arm/lib/libzmq.a ]; then \
        echo "[OK] libzmq already built at ${PREFIX}/libzmq/arm, skipping"; \
    else \
        echo "[BUILD] Building libzmq..."; \
        if [ ! -d libzmq-src ]; then \
            git clone --depth=1 --branch=v4.3.5 https://github.com/zeromq/libzmq.git libzmq-src; \
        fi; \
        cd libzmq-src; \
        mkdir -p build && cd build; \
        cmake -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake \
            -DANDROID_ABI=armeabi-v7a \
            -DANDROID_PLATFORM=android-${API_LEVEL} \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_CXX_STANDARD=11 \
            -DBUILD_SHARED=OFF \
            -DBUILD_STATIC=ON \
            -DBUILD_TESTS=OFF \
            -DENABLE_CPACK=OFF \
            -DWITH_DOCS=OFF \
            -DENABLE_WS=OFF \
            -DWITH_LIBSODIUM=OFF \
            -DCMAKE_INSTALL_PREFIX=${PREFIX}/libzmq/arm \
            ..; \
        ninja install; \
        echo "[OK] libzmq build complete"; \
        ls -lh ${PREFIX}/libzmq/arm/lib/libzmq.a; \
    fi

# Create pkg-config file for libzmq
RUN mkdir -p ${PREFIX}/libzmq/arm/lib/pkgconfig && \
    echo "prefix=${PREFIX}/libzmq/arm" > ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "exec_prefix=\${prefix}" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "libdir=\${prefix}/lib" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "includedir=\${prefix}/include" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "Name: libzmq" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "Description: 0MQ c++ library" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "Version: 4.3.4" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "Libs: -L\${libdir} -lzmq" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "Libs.private: -pthread" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "Cflags: -I\${includedir}" >> ${PREFIX}/libzmq/arm/lib/pkgconfig/libzmq.pc && \
    echo "[OK] libzmq pkg-config file created"

# ------------------------------------------------------------
# Verify installed static artifacts
# ------------------------------------------------------------
RUN echo "=== Static libs overview ==="; \
    find ${PREFIX} -type f -name "*.a" -ls 2>/dev/null || echo "No static libs found yet"; \
    echo "============================"

# ------------------------------------------------------------
# Configure & build Monero wallet_api
# ------------------------------------------------------------
RUN set -eux; \
    git -C ${WORK_DIR}/monero submodule update --init --force --recursive || true; \
    echo "[BUILD] Building Monero wallet_api..."; \
    echo "=== Checking dependencies ==="; \
    ls -la ${PREFIX}/unbound/arm/lib/libunbound.a || echo "[WARN] libunbound.a not found!"; \
    ls -la ${PREFIX}/openssl/arm/lib/libcrypto.a || echo "[WARN] libcrypto.a not found!"; \
    find ${PREFIX}/expat -name "*.a" -ls || echo "[WARN] No expat libs found"; \
    ls -la ${BOOST_ROOT}/include || echo "[WARN] Boost includes not found!"; \
    ls -la ${PREFIX}/sodium/arm/lib/libsodium.a || echo "[WARN] libsodium.a not found!"; \
    ls -la ${PREFIX}/libzmq/arm/lib/libzmq.a || echo "[WARN] libzmq.a not found!"; \
    ls -la ${PREFIX}/libzmq/arm/include/zmq.h || echo "[WARN] zmq.h not found!"; \
    echo "============================"; \
    mkdir -p ${WORK_DIR}/monero/build/android-arm; \
    cd ${WORK_DIR}/monero/build/android-arm; \
    \
    echo "Patching FindSodium.cmake to bypass detection..."; \
    echo '# Bypass FindSodium - manually set all required variables' > ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(sodium_FOUND TRUE)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(Sodium_FOUND TRUE)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(sodium_INCLUDE_DIR /work/local/sodium/arm/include)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(sodium_LIBRARY_RELEASE /work/local/sodium/arm/lib/libsodium.a)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(sodium_LIBRARY_DEBUG /work/local/sodium/arm/lib/libsodium.a)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(sodium_LIBRARY /work/local/sodium/arm/lib/libsodium.a)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(Sodium_INCLUDE_DIR /work/local/sodium/arm/include)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'set(Sodium_LIBRARY /work/local/sodium/arm/lib/libsodium.a)' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    echo 'message(STATUS "Found Sodium (patched): /work/local/sodium/arm/lib/libsodium.a")' >> ${WORK_DIR}/monero/cmake/FindSodium.cmake; \
    \
    echo "Patching FindZeroMQ.cmake to bypass detection..."; \
    echo '# Bypass FindZeroMQ - manually set all required variables' > ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZeroMQ_FOUND TRUE)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZMQ_FOUND TRUE)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZeroMQ_INCLUDE_DIR /work/local/libzmq/arm/include)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZMQ_INCLUDE_DIR /work/local/libzmq/arm/include)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZMQ_INCLUDE_DIRS /work/local/libzmq/arm/include)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZMQ_INCLUDE_PATH /work/local/libzmq/arm/include)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZeroMQ_LIBRARY /work/local/libzmq/arm/lib/libzmq.a)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZMQ_LIB /work/local/libzmq/arm/lib/libzmq.a)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZMQ_LIBRARY /work/local/libzmq/arm/lib/libzmq.a)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'set(ZMQ_LIBRARIES /work/local/libzmq/arm/lib/libzmq.a)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'mark_as_advanced(ZMQ_INCLUDE_PATH ZMQ_INCLUDE_DIR ZMQ_INCLUDE_DIRS ZeroMQ_INCLUDE_DIR)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'mark_as_advanced(ZMQ_LIB ZMQ_LIBRARIES ZeroMQ_LIBRARY)' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    echo 'message(STATUS "Found ZeroMQ (patched): /work/local/libzmq/arm/lib/libzmq.a")' >> ${WORK_DIR}/monero/cmake/FindZeroMQ.cmake; \
    \
    echo "Patching CMakeLists.txt to skip zmq.h header check..."; \
    sed -i 's/message(FATAL_ERROR "Could not find required header zmq.h")/message(STATUS "ZMQ header check bypassed for cross-compilation")/g' ${WORK_DIR}/monero/CMakeLists.txt; \
    \

    echo "Bypassing sodium header verification in CMakeLists.txt..."; \
    sed -i 's/check_include_file_cxx("sodium\/crypto_verify_32.h"/# check_include_file_cxx("sodium\/crypto_verify_32.h"/g' ${WORK_DIR}/monero/CMakeLists.txt; \
    sed -i '/# check_include_file_cxx("sodium\/crypto_verify_32.h"/a set(HAVE_SODIUM_CRYPTO_VERIFY_32_H TRUE)' ${WORK_DIR}/monero/CMakeLists.txt; \
    sed -i 's/if(NOT HAVE_SODIUM_CRYPTO_VERIFY_32_H)/if(FALSE) # if(NOT HAVE_SODIUM_CRYPTO_VERIFY_32_H)/g' ${WORK_DIR}/monero/CMakeLists.txt; \
    sed -i 's/message(FATAL_ERROR "Could not find required sodium/# message(FATAL_ERROR "Could not find required sodium/g' ${WORK_DIR}/monero/CMakeLists.txt; \
    \
    echo "Disabling translation header generation for cross-compilation..."; \
    mkdir -p ${WORK_DIR}/monero/translations; \
    mkdir -p ${WORK_DIR}/monero/build/android-arm/translations; \
    echo '// Stub translation file for cross-compilation' > ${WORK_DIR}/monero/translations/translation_files.h; \
    echo '#pragma once' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    echo '#include <string>' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    echo '#include <cstring>' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    echo '' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    echo 'static inline bool find_embedded_file(const std::string& filename, std::string& contents) {' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    echo '    // No embedded translations for cross-compilation' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    echo '    return false;' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    echo '}' >> ${WORK_DIR}/monero/translations/translation_files.h; \
    \
    echo 'project(translations LANGUAGES C CXX)' > ${WORK_DIR}/monero/translations/CMakeLists.txt; \
    echo 'message(STATUS "Translations disabled for cross-compilation")' >> ${WORK_DIR}/monero/translations/CMakeLists.txt; \
    echo 'add_custom_target(generate_translations_header ALL' >> ${WORK_DIR}/monero/translations/CMakeLists.txt; \
    echo '    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/translation_files.h ${CMAKE_CURRENT_BINARY_DIR}/translation_files.h' >> ${WORK_DIR}/monero/translations/CMakeLists.txt; \
    echo '    COMMENT "Copying stub translation_files.h for cross-compilation"' >> ${WORK_DIR}/monero/translations/CMakeLists.txt; \
    echo '    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/translation_files.h' >> ${WORK_DIR}/monero/translations/CMakeLists.txt; \
    echo ')' >> ${WORK_DIR}/monero/translations/CMakeLists.txt; \
    \
    export PKG_CONFIG_PATH="${PREFIX}/libzmq/arm/lib/pkgconfig:${PREFIX}/sodium/arm/lib/pkgconfig:${PREFIX}/expat/arm/lib/pkgconfig:${PREFIX}/openssl/arm/lib/pkgconfig:${PKG_CONFIG_PATH}"; \
    export sodium_DIR="${PREFIX}/sodium/arm"; \
    export sodium_ROOT_DIR="${PREFIX}/sodium/arm"; \
    export ZeroMQ_DIR="${PREFIX}/libzmq/arm"; \
    \
    cmake -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_ABI=armeabi-v7a \
        -DANDROID_PLATFORM=android-${API_LEVEL} \
        -DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake \
        -DANDROID_STL=c++_static \
        -DANDROID_ARM_NEON=TRUE \
        -DBUILD_GUI_DEPS=ON \
        -DBUILD_TESTS=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_DOCUMENTATION=OFF \
        -DBUILD_TAG=android \
        -DTRANSLATIONS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_CXX_STANDARD=17 \
        -DCMAKE_CXX_FLAGS="-fPIC -O2 -DANDROID -DBOOST_ASIO_DISABLE_EPOLL -I${PREFIX}/sodium/arm/include -I${PREFIX}/libzmq/arm/include" \
        -DCMAKE_C_FLAGS="-I${PREFIX}/libzmq/arm/include" \
        -DSTATIC=ON \
        -DCMAKE_DISABLE_FIND_PACKAGE_Qt5LinguistTools=TRUE \
        -DBOOST_ROOT=${BOOST_ROOT} \
        -DBoost_INCLUDE_DIR=${BOOST_ROOT}/include \
        -DBoost_LIBRARY_DIR=${BOOST_ROOT}/lib \
        -DBoost_NO_SYSTEM_PATHS=ON \
        -DUSE_DEVICE_TREZOR=OFF \
        -DUSE_DEVICE_LEDGER=OFF \
        -DUSE_UNBOUND=ON \
        -DUNBOUND_INCLUDE_DIR=${PREFIX}/unbound/arm/include \
        -DUNBOUND_LIBRARIES=${PREFIX}/unbound/arm/lib/libunbound.a \
        -DOPENSSL_ROOT_DIR=${PREFIX}/openssl/arm \
        -DOPENSSL_INCLUDE_DIR=${PREFIX}/openssl/arm/include \
        -DOPENSSL_CRYPTO_LIBRARY=${PREFIX}/openssl/arm/lib/libcrypto.a \
        -DOPENSSL_SSL_LIBRARY=${PREFIX}/openssl/arm/lib/libssl.a \
        -DOPENSSL_USE_STATIC_LIBS=TRUE \
        -DEXPAT_INCLUDE_DIR=${PREFIX}/expat/arm/include \
        -DEXPAT_LIBRARY=${PREFIX}/expat/arm/lib/libexpat.a \
        -DSodium_USE_STATIC_LIBS=ON \
        -Dsodium_USE_STATIC_LIBS=ON \
        -DSODIUM_INCLUDE_DIR=${PREFIX}/sodium/arm/include \
        -DSODIUM_LIBRARY=${PREFIX}/sodium/arm/lib/libsodium.a \
        -DSODIUM_LIBRARY_RELEASE=${PREFIX}/sodium/arm/lib/libsodium.a \
        -DSODIUM_LIBRARY_DEBUG=${PREFIX}/sodium/arm/lib/libsodium.a \
        -Dsodium_INCLUDE_DIR=${PREFIX}/sodium/arm/include \
        -Dsodium_LIBRARY=${PREFIX}/sodium/arm/lib/libsodium.a \
        -Dsodium_LIBRARY_RELEASE=${PREFIX}/sodium/arm/lib/libsodium.a \
        -Dsodium_LIBRARY_DEBUG=${PREFIX}/sodium/arm/lib/libsodium.a \
        -Dsodium_DIR=${PREFIX}/sodium/arm \
        -Dsodium_ROOT_DIR=${PREFIX}/sodium/arm \
        -DZMQ_INCLUDE_DIR=${PREFIX}/libzmq/arm/include \
        -DZMQ_INCLUDE_PATH=${PREFIX}/libzmq/arm/include \
        -DZMQ_LIB=${PREFIX}/libzmq/arm/lib/libzmq.a \
        -DZeroMQ_INCLUDE_DIR=${PREFIX}/libzmq/arm/include \
        -DZeroMQ_LIBRARY=${PREFIX}/libzmq/arm/lib/libzmq.a \
        -DMANUAL_SUBMODULES=1 \
        ${WORK_DIR}/monero; \
    \
    echo "=== Checking available targets ==="; \
    ninja -t targets 2>&1 | grep -E "wallet|api" | head -20 || echo "No wallet targets found"; \
    \
    echo "=== Building wallet_api target ==="; \
    ninja -j$(nproc) wallet_api || (echo "[ERROR] wallet_api build failed!" && exit 1); \
    \
    echo "[OK] wallet_api build complete"; \
    echo "=== Verifying wallet libraries were built ==="; \
    ls -la ${WORK_DIR}/monero/build/android-arm/lib/ || echo "[ERROR] lib directory not found!"; \
    find ${WORK_DIR}/monero/build/android-arm -name "*.a" | grep -i wallet || echo "[ERROR] No wallet .a files!"; \
    echo "=== Build complete ===" \

# ------------------------------------------------------------
# Package output artifacts for Android integration
# ------------------------------------------------------------
WORKDIR ${WORK_DIR}/output
RUN set -eux; \
    echo "[PACKAGE] Collecting output artifacts for Android integration..."; \
    BUILD_DIR="${WORK_DIR}/monero/build/android-arm"; \
    \
    mkdir -p ${WORK_DIR}/output/armeabi-v7a/lib; \
    mkdir -p ${WORK_DIR}/output/include/wallet; \
    mkdir -p ${WORK_DIR}/output/include/monero; \
    \
    echo "=== Copying Monero wallet libraries ==="; \
    # Core wallet libraries 
    cp ${BUILD_DIR}/lib/libwallet_api.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || echo "[WARN] libwallet_api.a not found"; \
    cp ${BUILD_DIR}/lib/libwallet.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || echo "[WARN] libwallet.a not found"; \
    \
    # Monero core libraries 
    cp ${BUILD_DIR}/src/cryptonote_core/libcryptonote_core.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/cryptonote_basic/libcryptonote_basic.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/cryptonote_protocol/libcryptonote_protocol.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/crypto/libcncrypto.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/ringct/libringct.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/ringct/libringct_basic.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/mnemonics/libmnemonics.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/common/libcommon.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/device/libdevice.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/checkpoints/libcheckpoints.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/blockchain_db/libblockchain_db.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    \
    # Additional Monero libraries 
    cp ${BUILD_DIR}/external/db_drivers/liblmdb/liblmdb.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/blocks/libblocks.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/device_trezor/libdevice_trezor.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/multisig/libmultisig.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/net/libnet.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/hardforks/libhardforks.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/rpc/librpc_base.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/cryptonote_basic/libcryptonote_format_utils_basic.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/src/libversion.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || echo "[WARN] libversion.a missing"; \
    \
    # Critical missing libraries 
    echo "=== Copying additional critical libraries ==="; \
    # RandomX library 
    cp ${BUILD_DIR}/external/randomx/librandomx.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || echo "[WARN] librandomx.a not found"; \
    # Wallet crypto library 
    cp ${BUILD_DIR}/src/crypto/wallet/libwallet-crypto.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || echo "[WARN] libwallet-crypto.a not found"; \
    # Character set library (from external libs) 
    find ${BUILD_DIR}/external -name "libcharset.a" -exec cp {} ${WORK_DIR}/output/armeabi-v7a/lib/ \; 2>/dev/null || echo "[WARN] libcharset.a not found"; \
    \
    # External libraries 
    cp ${BUILD_DIR}/external/easylogging++/libeasylogging.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    cp ${BUILD_DIR}/contrib/epee/src/libepee.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || true; \
    \
    echo "=== Copying dependency libraries ==="; \
    # Copy all dependency static libraries
    cp ${PREFIX}/openssl/arm/lib/libssl.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${PREFIX}/openssl/arm/lib/libcrypto.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${PREFIX}/unbound/arm/lib/libunbound.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${PREFIX}/expat/arm/lib/libexpat.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${PREFIX}/sodium/arm/lib/libsodium.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${PREFIX}/libzmq/arm/lib/libzmq.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    \
    # Copy Boost libraries
    cp ${BOOST_ROOT}/lib/libboost_system.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_filesystem.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_thread.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_chrono.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_date_time.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_regex.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_serialization.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_program_options.a ${WORK_DIR}/output/armeabi-v7a/lib/; \
    cp ${BOOST_ROOT}/lib/libboost_wserialization.a ${WORK_DIR}/output/armeabi-v7a/lib/ 2>/dev/null || echo "[WARN] libboost_wserialization.a missing"; \
    \
    echo "=== Copying header files ==="; \
    # Copy wallet API headers
    cp -r ${WORK_DIR}/monero/src/wallet/api/*.h ${WORK_DIR}/output/include/wallet/ 2>/dev/null || true; \
    \
    # Copy essential Monero headers for JNI integration
    cp -r ${WORK_DIR}/monero/src/cryptonote_basic ${WORK_DIR}/output/include/monero/ 2>/dev/null || true; \
    cp -r ${WORK_DIR}/monero/src/crypto ${WORK_DIR}/output/include/monero/ 2>/dev/null || true; \
    cp -r ${WORK_DIR}/monero/src/common ${WORK_DIR}/output/include/monero/ 2>/dev/null || true; \
    \
    echo "=== Creating build info file ==="; \
    cat > ${WORK_DIR}/output/BUILD_INFO.txt << 'EOF'
Monero Android ARM32 (armeabi-v7a) Build
========================================

Architecture: armeabi-v7a (32-bit ARM)
API Level: 33
NDK: r29
Build Date: $(date)

Output Structure:
-----------------
armeabi-v7a/lib/          - All static libraries (.a files)
include/wallet/           - Wallet API headers
include/monero/           - Monero core headers

Integration Instructions:
-------------------------
1. Copy armeabi-v7a/lib/*.a to your Android project's jniLibs/armeabi-v7a/ directory
2. Copy include/ headers to your Android project's cpp/ directory
3. Create a CMakeLists.txt in your Android project to:
   - Import these prebuilt static libraries
   - Compile your JNI wrapper (e.g., native-lib.cpp)
   - Link against these libraries to create the final .so

Example CMakeLists.txt snippet:
-------------------------------
add_library(wallet_api STATIC IMPORTED)
set_target_properties(wallet_api PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/jniLibs/${ANDROID_ABI}/libwallet_api.a)

add_library(native-lib SHARED native-lib.cpp)
target_link_libraries(native-lib wallet_api ... other_libs)

Libraries included:
-------------------
EOF

# Complete packaging and verification
RUN set -eux; \
    ls -1 ${WORK_DIR}/output/armeabi-v7a/lib/ >> ${WORK_DIR}/output/BUILD_INFO.txt; \
    \
    echo "=== Output summary ==="; \
    echo "Total libraries built: $(ls -1 ${WORK_DIR}/output/armeabi-v7a/lib/*.a 2>/dev/null | wc -l)"; \
    echo "Total headers copied: $(find ${WORK_DIR}/output/include -name "*.h" 2>/dev/null | wc -l)"; \
    du -sh ${WORK_DIR}/output; \
    \
    echo "=== Verifying critical libraries ==="; \
    for lib in libwallet_api.a libwallet.a libcryptonote_core.a libssl.a libcrypto.a librandomx.a libwallet-crypto.a; do \
        if [ -f ${WORK_DIR}/output/armeabi-v7a/lib/$lib ]; then \
            echo "[OK] $lib - $(ls -lh ${WORK_DIR}/output/armeabi-v7a/lib/$lib | awk '{print $5}')"; \
        else \
            echo "[MISSING] $lib"; \
        fi; \
    done; \
    \
    echo ""; \
    echo "========================================"; \
    echo "Build complete! Artifacts ready in:"; \
    echo "${WORK_DIR}/output"; \
    echo "========================================"; \
    echo ""; \
    echo "To extract artifacts, run:"; \
    echo "docker cp <container_id>:/work/output ./monero-android-arm32-libs"; \
    echo "========================================"

# Create extraction script
RUN echo '#!/bin/bash' > /extract-libs.sh && \
    echo 'set -e' >> /extract-libs.sh && \
    echo 'DEST=${1:-/host-output}' >> /extract-libs.sh && \
    echo 'echo "Extracting Monero Android libraries to: $DEST"' >> /extract-libs.sh && \
    echo 'mkdir -p "$DEST"' >> /extract-libs.sh && \
    echo 'cp -r /work/output/* "$DEST/"' >> /extract-libs.sh && \
    echo 'echo ""' >> /extract-libs.sh && \
    echo 'echo "Extraction complete!"' >> /extract-libs.sh && \
    echo 'echo "Files copied:"' >> /extract-libs.sh && \
    echo 'ls -lh "$DEST"' >> /extract-libs.sh && \
    echo 'echo ""' >> /extract-libs.sh && \
    echo 'echo "Library count: $(ls -1 $DEST/armeabi-v7a/lib/*.a 2>/dev/null | wc -l)"' >> /extract-libs.sh && \
    echo 'echo "Total size: $(du -sh $DEST | cut -f1)"' >> /extract-libs.sh && \
    chmod +x /extract-libs.sh

WORKDIR ${WORK_DIR}/output
CMD ["/bin/bash"]
