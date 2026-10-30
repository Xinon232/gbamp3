#!/usr/bin/env bash
# Halim Jarrar, 2026. GPL-3.0-or-later with the additional permission described in NOTICES.md (linking with the Helix MP3 decoder).
# Native-only, offline build from already verified and unpacked sources.
set -euo pipefail
work=$1
if [[ "${MSYSTEM:-}" == UCRT64 ]]; then work=$(cygpath -u "$work"); fi
cd "$work"
work=$PWD
case "$work" in *' '*) printf 'Build directory must not contain spaces\n' >&2; exit 1;; esac
prefix="$work/prefix"
mkdir -p "$prefix" logs
exec > >(tee logs/build.log) 2>&1
set -x
unset PKG_CONFIG_PATH CPATH LIBRARY_PATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH
export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig"
export LC_ALL=C
export CFLAGS='-O2'
export CPPFLAGS=''
export LDFLAGS=''
jobs=${BUILD_JOBS:-2}
extra=()
# Bash 3.2 (macOS /bin/bash) rejects empty arrays with nounset. Keep one
# common configure option here so native Intel builds never expand an empty array.
lame_host=(--disable-frontend)
ldflags="-L$prefix/lib"
case "$(uname -s)" in
  Linux) export CC=gcc; extra+=(--arch=x86_64);;
  Darwin)
    export CC=clang
    export MACOSX_DEPLOYMENT_TARGET=${MACOSX_DEPLOYMENT_TARGET:-14.0}
    if [[ "$(uname -m)" == arm64 ]]; then
      arch=arm64; lame_host+=(--build=aarch64-apple-darwin --host=aarch64-apple-darwin)
    else arch=x86_64; fi
    export CFLAGS="$CFLAGS -arch $arch -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
    export LDFLAGS="-arch $arch -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET"
    ldflags="$ldflags $LDFLAGS"
    extra+=(--arch="$arch");;
  MINGW*|MSYS*)
    [[ "${MSYSTEM:-}" == UCRT64 ]] || { printf 'UCRT64 required\n' >&2; exit 1; }
    export CC=gcc
    lame_host+=(--build=x86_64-w64-mingw32 --host=x86_64-w64-mingw32)
    extra+=(--target-os=mingw32 --arch=x86_64 --disable-pthreads --enable-w32threads)
    ldflags="$ldflags -static -static-libgcc";;
  *) exit 1;;
esac
{
  uname -a
  "$CC" --version
  "$CC" -print-search-dirs
  make --version
  pkg-config --version
  if command -v pacman >/dev/null; then
    pacman -Q
    # Preserve actual compiler/CRT package notices rather than guessing versions.
    mkdir -p "$work/toolchain-notices"
    for d in /ucrt64/share/licenses/*; do
      [[ ! -d "$d" ]] || cp -R "$d" "$work/toolchain-notices/"
    done
  fi
  if command -v dpkg-query >/dev/null; then dpkg-query -W; fi
  if [[ "$(uname -s)" == Darwin ]]; then
    xcodebuild -version; xcrun --show-sdk-path; xcrun --show-sdk-version; sw_vers
  fi
  printf 'MACOSX_DEPLOYMENT_TARGET=%s\n' "${MACOSX_DEPLOYMENT_TARGET:-not-applicable}"
} > logs/toolchain.txt 2>&1
cd "$work/lame"
./configure --prefix="$prefix" --disable-shared --enable-static \
  --disable-decoder --disable-nasm "${lame_host[@]}"
make -j"$jobs"
make install
cd "$work/zlib"
if [[ "${MSYSTEM:-}" == UCRT64 ]]; then
  make -f win32/Makefile.gcc -j"$jobs" libz.a
  mkdir -p "$prefix/lib" "$prefix/include"
  cp libz.a "$prefix/lib/"
  cp zlib.h zconf.h "$prefix/include/"
else
  ./configure --prefix="$prefix" --static
  make -j"$jobs"
  make install
fi
cd "$work/ffmpeg"
./configure --prefix="$prefix" --cc="$CC" \
  --disable-autodetect --disable-everything --disable-programs --enable-ffmpeg \
  --disable-doc --disable-debug --disable-network --disable-shared --enable-static \
  --enable-small --disable-gpl --disable-version3 --disable-nonfree --disable-postproc \
  --disable-x86asm --enable-avcodec --enable-avformat --enable-avfilter --enable-avdevice \
  --enable-swresample --enable-swscale --enable-libmp3lame --enable-zlib \
  --extra-cflags="-I$prefix/include $CFLAGS" --extra-ldflags="$ldflags" \
  --pkg-config-flags=--static --enable-protocol=file,pipe --enable-indev=lavfi \
  --enable-demuxer=mp3,wav,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,image2,image2pipe,image_png_pipe,image_jpeg_pipe \
  --enable-muxer=mp3,wav,pcm_s16le,pcm_f32le,rawvideo,framehash,ffmetadata,image2,image2pipe,null \
  --enable-decoder=mp3,mp3float,pcm_s16le,pcm_s24le,pcm_s32le,pcm_f32le,pcm_f64le,pcm_u8,png,mjpeg,rawvideo,wrapped_avframe \
  --enable-encoder=libmp3lame,pcm_s16le,pcm_f32le,png,mjpeg,rawvideo,wrapped_avframe \
  --enable-parser=mpegaudio,png,mjpeg \
  --enable-filter=aresample,apad,atrim,aformat,anull,asetpts,scale,pad,format,null,sine,color,aevalsrc \
  "${extra[@]}"
make -j"$jobs" V=1
if [[ "${MSYSTEM:-}" == UCRT64 ]]; then
  objdump -p ffmpeg.exe > "$work/logs/dependencies.txt"
  cp ffmpeg.exe "$work/../ffmpeg.exe"
elif [[ "$(uname -s)" == Darwin ]]; then
  otool -L ffmpeg > "$work/logs/dependencies.txt"
  cp ffmpeg "$work/../ffmpeg"
else
  ldd ffmpeg > "$work/logs/dependencies.txt"
  readelf --version-info ffmpeg > "$work/logs/elf-versions.txt"
  cp ffmpeg "$work/../ffmpeg"
fi
