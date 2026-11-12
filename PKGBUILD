# Maintainer: Peknight <JKpeknight@gmail.com>

pkgname=wecom-win-bin
pkgver=5.0.13.6005
pkgrel=1
pkgdesc='企业微信 Windows 客户端，基于 deepin-wine10 运行 | Tencent WeCom Windows build on deepin-wine'
arch=('x86_64')
url='https://work.weixin.qq.com/'
license=('LicenseRef-proprietary')
options=(!debug !strip)

depends=(
    'deepin-wine10-stable'
    'com.qq.weixin.work.deepin'  # 提供 WINEPREDLL 用的 deepin 补丁 dll
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
)

source=(
    "WeCom_${pkgver}.exe::https://dldir1.qq.com/wework/work_weixin/WeCom_${pkgver}.exe"
    'make-tahoma.py'
    'wecom-win.sh'
    'wecom-win.desktop'
    'wecom-win-fonts.reg'
)
noextract=("WeCom_${pkgver}.exe")
sha256sums=(
    '5df50a240038a2de7c3b17e4f25f4747e74d97f9028be9baec6f7379fc97e1be'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
)

build() {
    cd "$srcdir"

    # 输入框由 CEF 按 Tahoma 取字，而这套 wine 的 dwrite 没有中文回退链，
    # 只能让 Tahoma 本身带汉字。详见 PITFALLS.md。
    python make-tahoma.py \
        '/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc' fonts

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
    install -Dm644 "$srcdir/fonts/tahoma.ttf" "$share/fonts/tahoma.ttf"
    install -Dm644 "$srcdir/fonts/tahomabd.ttf" "$share/fonts/tahomabd.ttf"
    install -Dm644 "$srcdir/wecom-win-fonts.reg" "$share/wecom-win-fonts.reg"

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
