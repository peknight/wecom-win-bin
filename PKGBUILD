# Maintainer: Peknight <JKpeknight@gmail.com>

pkgname=wecom-win-bin
pkgver=5.0.13.6005
pkgrel=7
pkgdesc='企业微信 Windows 客户端，基于 deepin-wine10 运行 | Tencent WeCom Windows build on deepin-wine'
arch=('x86_64')
url='https://work.weixin.qq.com/'
license=('LicenseRef-proprietary')
options=(!debug !strip)

# 补丁 DLL 要能和运行库对上，跟着 deepin-wine10-stable 的 wine 版本走
_winever='10.14'

depends=(
    'deepin-wine10-stable'
    'fontconfig'
    'hicolor-icon-theme'
)
optdepends=(
    'wecom-x11-fix: 修复窗口按钮穿透与拖动时的白边残留'
)
makedepends=(
    'icoutils'
    'python-fonttools'
    'wqy-microhei'  # 派生 Tahoma 用的字体源，每次构建都现生成，不随包分发
    'mingw-w64-gcc' # 编译打过补丁的 32 位 riched20.dll
    'flex'
    'bison'
)

source=(
    "WeCom_${pkgver}.exe::https://dldir1.qq.com/wework/work_weixin/WeCom_${pkgver}.exe"
    "wine-${_winever}.tar.xz::https://dl.winehq.org/wine/source/${_winever%.*}.x/wine-${_winever}.tar.xz"
    'wine-riched20-wecom.patch'
    'make-tahoma.py'
    'wecom-win.sh'
    'wecom-win.desktop'
    'wecom-win-fonts.reg'
    'wecom-win-protocols.reg'
)
noextract=("WeCom_${pkgver}.exe")
sha256sums=(
    '5df50a240038a2de7c3b17e4f25f4747e74d97f9028be9baec6f7379fc97e1be'
    'a4fa3b5aefe1c0b7391a91a2c3c733b8dfd04bb315c8eabcfb2af413969e5e4b'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
)

# 只为 riched20 编一份 DLL：wine 自带的那个不认剪贴板里的 CF_DIB，
# 从浏览器/截图工具复制图片粘不进输入框。详见 PITFALLS.md 第 14 条。
build_riched20() {
    cd "$srcdir/wine-${_winever}"
    # 重复构建时源码树可能已经打过补丁了
    grep -q paste_dib dlls/riched20/editor.c ||
        patch -Np1 -i "$srcdir/wine-riched20-wecom.patch"

    mkdir -p "$srcdir/wine-build"
    cd "$srcdir/wine-build"
    # 只编 32 位；把 X11、音频、网络这些运行期的依赖全关掉，只求能把
    # riched20 编出来，整条链路二十来秒
    "$srcdir/wine-${_winever}/configure" \
        --enable-archs=i386 \
        --without-x --without-freetype --without-fontconfig \
        --without-gnutls --without-cups --without-dbus --without-udev \
        --without-sane --without-pulse --without-alsa --without-oss \
        --without-coreaudio --without-krb5 --without-netapi --without-pcap \
        --without-v4l2 --without-gstreamer --without-vulkan --without-wayland \
        >/dev/null
    make -j"$(nproc)" dlls/riched20/i386-windows/riched20.dll

    install -Dm644 dlls/riched20/i386-windows/riched20.dll "$srcdir/riched20.dll"
    # 2.7M 里大半是调试信息，strip 掉省事（顺带少带点构建路径）
    i686-w64-mingw32-strip --strip-debug "$srcdir/riched20.dll"
    # 去掉 winebuild 盖的 builtin 签名。留着它 wine 会去找配套的
    # riched20.so 而不是把这个 PE 当普通 native DLL 加载，LoadLibrary 直接 126。
    # 注意 strip 之后签名还在，所以这一步放在最后。
    local offset
    offset=$(grep -abo 'Wine builtin DLL' "$srcdir/riched20.dll" | head -1 | cut -d: -f1)
    [[ -n $offset ]] || error 'riched20.dll 里找不到 builtin 签名'
    head -c 32 /dev/zero | dd of="$srcdir/riched20.dll" bs=1 seek="$offset" conv=notrunc status=none
}

build() {
    cd "$srcdir"

    # 输入框由 CEF 按 Tahoma 取字，而这套 wine 的 dwrite 没有中文回退链，
    # 只能让 Tahoma 本身带汉字。详见 PITFALLS.md。
    python make-tahoma.py \
        '/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc' fonts

    build_riched20

    # 图标只是锦上添花，抠不出来不该让整个构建失败
    mkdir -p icons
    if wrestool -x -t14 "WeCom_${pkgver}.exe" > wecom.ico 2>/dev/null && [[ -s wecom.ico ]]; then
        icotool -x -o icons wecom.ico || warning '图标转换失败，跳过'
    else
        warning '没从 exe 里抠到图标，跳过'
    fi
}

package() {
    local share="$pkgdir/usr/share/$pkgname"

    # 版本号在构建期固定进启动器，避免装完包和安装包版本不一致
    install -Dm755 "$srcdir/wecom-win.sh" "$pkgdir/usr/bin/wecom-win"
    sed -i "s/@PKGNAME@/$pkgname/g; s/@PKGVER@/$pkgver/g" "$pkgdir/usr/bin/wecom-win"

    install -Dm644 "$srcdir/wecom-win.desktop" \
        "$pkgdir/usr/share/applications/$pkgname.desktop"

    install -Dm644 "$srcdir/WeCom_${pkgver}.exe" "$share/WeCom_${pkgver}.exe"
    install -Dm644 "$srcdir/riched20.dll" "$share/riched20.dll"
    install -Dm644 "$srcdir/fonts/tahoma.ttf" "$share/fonts/tahoma.ttf"
    install -Dm644 "$srcdir/fonts/tahomabd.ttf" "$share/fonts/tahomabd.ttf"
    install -Dm644 "$srcdir/wecom-win-fonts.reg" "$share/wecom-win-fonts.reg"
    install -Dm644 "$srcdir/wecom-win-protocols.reg" "$share/wecom-win-protocols.reg"

    local png size
    for png in "$srcdir"/icons/*.png; do
        [[ -e $png ]] || continue
        size=$(basename "$png" | grep -oE '[0-9]+x[0-9]+x[0-9]+' | head -1)
        [[ -n $size ]] || continue
        install -Dm644 "$png" \
            "$pkgdir/usr/share/icons/hicolor/${size%x*}/apps/$pkgname.png"
    done

    install -d "$pkgdir/usr/share/licenses/$pkgname"
    echo 'https://work.weixin.qq.com/' >"$pkgdir/usr/share/licenses/$pkgname/ToS.txt"
    # 包内 tahoma.ttf/tahomabd.ttf 是文泉驿微米黑的派生物，附上原字体许可
    install -Dm644 /usr/share/licenses/wqy-microhei/FONTEXCEPTION \
        "$pkgdir/usr/share/licenses/$pkgname/FONTEXCEPTION"
}
