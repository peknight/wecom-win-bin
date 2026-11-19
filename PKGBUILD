# Maintainer: Peknight <JKpeknight@gmail.com>

pkgname=wecom-win-bin
pkgver=5.0.13.6005
pkgrel=22
pkgdesc='企业微信 Windows 客户端，基于发行版 wine 11 运行 | Tencent WeCom Windows build on upstream wine'
arch=('x86_64')
url='https://work.weixin.qq.com/'
license=('LicenseRef-proprietary')
options=(!debug !strip)

# 本包早期是 deepin-wine10 线路，5.0.13.6005-10 起换成发行版 wine 11，
# 同时从临时的 wecom-win11-bin 包名收回 wecom-win-bin。老包同名、新版换
# 运行库，靠下面两条让 pacman -Su 直接完成迁移。
conflicts=('wecom-win11-bin')
replaces=('wecom-win11-bin')

depends=(
    'wine-fix'      # 按进程挂载的 wine 补丁，目前只用到 winex11 那份（XIM 光标位置）
    'fontconfig'
    'hicolor-icon-theme'
    'libx11'        # 托盘图标修复用的 LD_PRELOAD 小库
    'libxext'
    'wqy-microhei'  # 中文字形来源，替换表把它填给 Tahoma / Noto Sans CJK 等家族
)
optdepends=(
    'wecom-x11-fix: 修复窗口按钮穿透与拖动时的白边残留'
    'workbuddy-win-bin: 企业微信里的 WorkBuddy 入口跳转到本机 WorkBuddy'
)
makedepends=(
    'icoutils'
    'python-fonttools' # 派生带汉字的 Tahoma，给 GDI RichEdit 输入框用
    'mingw-w64-gcc' # 编译打过补丁的 32 位 riched20.dll
    'flex'
    'bison'
)

# 补丁 DLL 用哪份 wine 源码编译。跟着发行版 wine 走，PE 层的 riched20 不挑小版本。
_winever='11.22'

# prefix 名和可执行文件名。WM_CLASS 跟不了这两个值，上游 wine 只认进程名。
_app_id='wecom-win'
_bin='wecom-win'
_app_name='企业微信'

source=(
    "WeCom_${pkgver}.exe::https://dldir1.qq.com/wework/work_weixin/WeCom_${pkgver}.exe"
    "wine-${_winever}.tar.xz::https://dl.winehq.org/wine/source/${_winever%.*}.x/wine-${_winever}.tar.xz"
    'wine-riched20-wecom.patch'
    'wine-explorer-tray-blank.patch'
    'make-tahoma.py'
    'wecom-win.sh'
    'wecom-win.desktop'
    'wecom-win-fonts.reg'
    'wecom-win-protocols.reg'
    'wecom-win-dpi.reg'
    'no-shaped-systray.c'
    'wecom-repaint-watch.c'
    'slwga.c'
    'slwga.def'
)
noextract=("WeCom_${pkgver}.exe")
sha256sums=(
    '5df50a240038a2de7c3b17e4f25f4747e74d97f9028be9baec6f7379fc97e1be'
    'f172e9c1be73e862057deae1f0e03ce95919986c5df8a18e1c0829c34863c9ee'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
    'SKIP'
)

# 只为 riched20 编一份 DLL：wine 自带的那个不认剪贴板里的 CF_DIB，
# 从浏览器/截图工具复制图片粘不进输入框。详见 PITFALLS-common.md 第 14 条。
build_riched20() {
    cd "$srcdir/wine-${_winever}"
    # 重复构建时源码树可能已经打过补丁了
    grep -q paste_dib dlls/riched20/editor.c ||
        patch -Np1 -i "$srcdir/wine-riched20-wecom.patch"

    mkdir -p "$srcdir/wine-build"
    cd "$srcdir/wine-build"
    # 只编 32 位；把 X11、音频、网络这些运行期的依赖全关掉，只求能把
    # riched20 编出来，整条链路半分钟左右
    "$srcdir/wine-${_winever}/configure" \
        --enable-archs=i386,x86_64 \
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

# 托盘图标闪烁时，隐藏的那一帧在 wine 里被画成纯黑方块：explorer 的 systray
# 靠扫描像素 alpha 判断图标有没有 alpha 通道，全透明的一帧一个非零 alpha 都
# 没有，就被当成"没有 alpha 通道"，退回 AND mask 画法；而这张图的 mask 全 0，
# AND 的结果是整块不透明。补丁改成按 DIB 位数判断有没有 alpha 通道，全透明
# 就真的透明。详见 PITFALLS.md 第 14 条。
build_explorer() {
    cd "$srcdir/wine-${_winever}"
    grep -q icon_has_alpha_channel programs/explorer/systray.c ||
        patch -Np1 -i "$srcdir/wine-explorer-tray-blank.patch"

    cd "$srcdir/wine-build"
    # 顶层 Makefile 给 explorer 的链接命令带了 -Wl,--wine-builtin（给 wine 自己
    # 的安装目录用）。带这个标记的 PE 会被当成 builtin，用 WINEDLLOVERRIDES
    # 强制 native 加载会以 c0000135 失败，所以重链一次去掉它。
    sed -i '/-mwindows -municode programs\/explorer\// s/ -Wl,--wine-builtin//' Makefile
    make -j"$(nproc)" programs/explorer/x86_64-windows/explorer.exe

    install -Dm755 programs/explorer/x86_64-windows/explorer.exe "$srcdir/explorer.exe"
    # 1M 里大半是调试信息
    x86_64-w64-mingw32-strip -s "$srcdir/explorer.exe"
}

build() {
    cd "$srcdir"

    # 输入框（GDI RichEdit）按家族名 Tahoma 取字，而 wine 自带的 tahoma.ttf
    # 没有汉字；GDI 的 Replacements 又不会覆盖已存在的家族，所以只能让
    # Tahoma 本身带汉字。DirectWrite 那条路不需要，它有自己的回退链。
    python make-tahoma.py \
        '/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc' fonts

    build_riched20
    build_explorer

    # build_riched20 结束时停在 $srcdir/wine-build，不回 $srcdir 的话
    # 下面的 wrestool 会在空目录里找不到 exe，图标就被静默跳过了。
    cd "$srcdir"

    # 托盘图标黑底：编一个 LD_PRELOAD 小库，在 wine 进程里拦掉
    # 托盘窗口的 bounding shape，详见 no-shaped-systray.c 开头
    cc -shared -fPIC -O2 -Wall -Wextra \
        -o no-shaped-systray.so no-shaped-systray.c -ldl -lX11 -lXext

    # Emails / Document 面板收起后残留的邮件工具栏：编一个 Windows 小程序，
    # 由启动器拉起，盯面板可见性并补一次主窗口重绘。见 wecom-repaint-watch.c
    x86_64-w64-mingw32-gcc -O2 -s -mwindows -Wall -Wextra \
        -o wecom-repaint-watch.exe wecom-repaint-watch.c

    # 融合会议加载链上的 SLWGA.dll（Windows 正版校验）存根，
    # 32 位、导出名不能带 @12 修饰。见 slwga.c 开头
    i686-w64-mingw32-gcc -shared -O2 -s -nostartfiles \
        -o SLWGA.dll slwga.c slwga.def

    # 图标只是锦上添花，抠不出来不该让整个构建失败
    mkdir -p icons
    # exe 里只有一个 1033/103 图标组，显式指定，免得 wrestool 挑错资源
    if wrestool -x -t14 -n 103 "WeCom_${pkgver}.exe" > wecom.ico 2>/dev/null && [[ -s wecom.ico ]]; then
        icotool -x -o icons wecom.ico || warning '图标转换失败，跳过'
    else
        warning '没从 exe 里抠到图标，跳过'
    fi
}

_update_caches() {
    gtk-update-icon-cache -q -t -f /usr/share/icons/hicolor 2>/dev/null || true
    update-desktop-database -q /usr/share/applications 2>/dev/null || true
}

post_install() {
    _update_caches
}

post_upgrade() {
    _update_caches
}

package() {
    local share="$pkgdir/usr/share/$pkgname"

    # 版本号和标识在构建期固定进启动器，避免装完包和安装包版本不一致
    install -Dm755 "$srcdir/wecom-win.sh" "$pkgdir/usr/bin/$_bin"
    sed -i "s/@PKGNAME@/$pkgname/g; s/@PKGVER@/$pkgver/g; s/@APP_ID@/$_app_id/g; s/@BIN@/$_bin/g" \
        "$pkgdir/usr/bin/$_bin"

    install -Dm644 "$srcdir/wecom-win.desktop" \
        "$pkgdir/usr/share/applications/$pkgname.desktop"
    sed -i "s/@PKGNAME@/$pkgname/g; s/@APP_ID@/$_app_id/g; s/@BIN@/$_bin/g; s/@APP_NAME@/$_app_name/g" \
        "$pkgdir/usr/share/applications/$pkgname.desktop"

    install -Dm644 "$srcdir/WeCom_${pkgver}.exe" "$share/WeCom_${pkgver}.exe"
    install -Dm644 "$srcdir/riched20.dll" "$share/riched20.dll"
    install -Dm644 "$srcdir/fonts/tahoma.ttf" "$share/fonts/tahoma.ttf"
    install -Dm644 "$srcdir/fonts/tahomabd.ttf" "$share/fonts/tahomabd.ttf"
    install -Dm755 "$srcdir/no-shaped-systray.so" \
        "$pkgdir/usr/lib/$pkgname/no-shaped-systray.so"
    install -Dm755 "$srcdir/explorer.exe" \
        "$pkgdir/usr/lib/$pkgname/explorer.exe"
    install -Dm644 "$srcdir/wecom-repaint-watch.exe" "$share/wecom-repaint-watch.exe"
    install -Dm644 "$srcdir/SLWGA.dll" "$share/SLWGA.dll"
    install -Dm644 "$srcdir/wecom-win-fonts.reg" "$share/wecom-win-fonts.reg"
    install -Dm644 "$srcdir/wecom-win-protocols.reg" "$share/wecom-win-protocols.reg"
    install -Dm644 "$srcdir/wecom-win-dpi.reg" "$share/wecom-win-dpi.reg"

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
