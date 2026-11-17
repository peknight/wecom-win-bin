#!/usr/bin/env bash
# 重建 explorer-systray-blank.exe。
#
# 包里这份 explorer.exe 来自 wine 11.22 源码，只改了 programs/explorer/systray.c
# 一处（见 wine-explorer-tray-blank.patch），并且按 native 模块链接——发行版
# 自带的 explorer.exe 带 "Wine builtin DLL" 标记，被 WINEDLLOVERRIDES 强制按
# native 加载时会以 c0000135 失败。原因见 PITFALLS.md 第 11 条补充说明。
#
# 依赖：gcc、flex、bison、x86_64-w64-mingw32-gcc。
# 用法：./build-explorer.sh [输出路径]，默认输出到当前目录。国内访问
# dl.winehq.org 慢时，可用 WINE_TARBALL / WINE_TARBALL_URL 指向镜像。
set -euo pipefail

readonly WINE_VERSION='11.22'
readonly WINE_TARBALL="${WINE_TARBALL:-wine-${WINE_VERSION}.tar.xz}"
readonly WINE_URL="${WINE_TARBALL_URL:-https://dl.winehq.org/wine/source/11.x/${WINE_TARBALL}}"
readonly HERE=$(cd "$(dirname "$0")" && pwd)
readonly OUTPUT=${1:-$PWD/explorer-systray-blank.exe}

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT
cd "$workdir"

[[ -f ${WINE_TARBALL} ]] || curl -fL -o "${WINE_TARBALL}" "${WINE_URL}"
tar xf "${WINE_TARBALL}"

cd "wine-${WINE_VERSION}"
patch -p1 <"${HERE}/wine-explorer-tray-blank.patch"

# explorer 只需要 PE 侧编译，X11 / 音频这些后端全部关掉，configure 快很多
mkdir -p build
cd build
../configure --enable-archs=x86_64 \
	--without-x --without-freetype --without-fontconfig --without-gnutls \
	--without-cups --without-dbus --without-udev --without-sane \
	--without-pulse --without-alsa --without-oss --without-coreaudio \
	--without-krb5 --without-netapi --without-pcap --without-v4l2 \
	--without-gstreamer --without-vulkan --without-wayland

make -j"$(nproc)" programs/explorer/x86_64-windows/explorer.exe

# 顶层 Makefile 给 explorer 的链接命令带了 -Wl,--wine-builtin（给 wine 自己的
# 安装目录用）。这里只改 explorer 这一条链接规则，去掉标记重链一次，得到的
# 才是能放进 prefix 的 native PE。
sed -i '/-mwindows -municode programs\/explorer\// s/ -Wl,--wine-builtin//' Makefile
rm -f programs/explorer/x86_64-windows/explorer.exe
if make -n programs/explorer/x86_64-windows/explorer.exe | grep -q -- '--wine-builtin'; then
	echo 'explorer link rule still carries --wine-builtin' >&2
	exit 1
fi
make -j"$(nproc)" programs/explorer/x86_64-windows/explorer.exe

install -Dm755 programs/explorer/x86_64-windows/explorer.exe "$OUTPUT"
x86_64-w64-mingw32-strip -s "$OUTPUT"
printf 'built %s\n' "$OUTPUT"
