# 踩坑记录（wine 11 线路）

这里记和"用发行版 wine 11 跑"直接相关的坑。和运行库无关的通用问题（安装、
prefix、CEF 参数、窗口 overlay、剪贴板图片、右键菜单这些）记在
`PITFALLS-common.md`。

## 1. 新 prefix 会弹 wine-mono 安装对话框

**现象**：`wineboot` 建新 prefix 时弹窗问要不要装 wine-mono，卡在那里等人点。

**结论**：点 Cancel。企业微信不用 .NET，装上只占地。

启动器里建 prefix 那一步已经带上 `WINEDLLOVERRIDES='mscoree,mshtml='`，不会再弹。

## 2. 上游 wine 不认 `WINE_WMCLASS`，WM_CLASS 只能是 `wxwork.exe`

**现象**：想给窗口一个 `wecom-win` 的 class（deepin 线路的做法），但窗口的
`WM_CLASS` 始终是 `("wxwork.exe", "wxwork.exe")`，导出 `WINE_WMCLASS=wecom-win`
毫无效果。

**原因**：`WINE_WMCLASS` 是 deepin 给 winex11 打的补丁。上游 wine 的
`dlls/winex11.drv/window.c` 里 `set_initial_wm_hints()` 直接用进程名（小写后的
exe 文件名）当 `res_name`/`res_class`，没有可配置项，也没有对应的注册表开关。

**影响**：

- 桌面入口的 `StartupWMClass` 必须写 `wxwork.exe`，否则面板归组和固定图标会错。
- `wecom-x11-fix` 本来就有按 `wxwork.exe` 兜底匹配的分支（应付不走启动器的实例），
  所以两套窗口修复不受影响。
- 想真正改 WM_CLASS 只能自己编译一份打过补丁的 wine，代价不成比例。

## 3. 补丁版 riched20 可以直接用 wine 11.22 的源码编

`wine-riched20-wecom.patch` 在 wine 11.22 上 `patch --dry-run` 全部通过，偏移最大
74 行（`editor.c` 前后加了东西）。构建方式不变：`--enable-archs=i386` 只编 32 位，
`--without-*` 关掉所有运行期依赖，编完 `strip --strip-debug`，最后把
`Wine builtin DLL` 签名抹成 0，让 wine 把它当普通 native DLL 加载。

## 4. `concrt140` 在 wine 11.22 里仍然是半成品

`dlls/concrt140/details.c` 里 `_TaskCollection` 相关还是 `FIXME stub`。所以
`WINEDLLOVERRIDES` 里的 `concrt140=n,b` 必须保留，让融合会议用企业微信自己目录里
那份原生 `concrt140.dll`（在 `WXWork/<ver>/WeMeet/<ver>/` 下，安装包自带）。

## 5. CEF 的 V8 `VirtualProtect` 断言，先按旧方案兜着

启动参数仍带 `--js-flags=--no-freeze-flags-after-init`。wine 11 有没有修好这个断言
还没验证，等登录后开 Industry News / Request for use 观察再决定要不要去掉。

## 6. 只给 `image/png` 的剪贴板粘不进输入框（未修）

deepin 的 `winex11.so` 自己链了 libpng，把 `image/png` 转成 `CF_DIB` 交给程序；
上游 wine 的 `dlls/winex11.drv/clipboard.c` 只把 `image/bmp` 映射成 `CF_DIB`，
`image/png` 只是原样注册成一个 "PNG" 格式。所以截图工具直粘这条路在 wine 11 上
是断的，而 Thunar 复制文件（`text/uri-list → CF_HDROP`）正常。

补法有两种，等实测确认哪种更稳：

1. 在 riched20 补丁里加一条：`CF_DIB` 没有时读 "PNG" 格式，用内置 gdiplus 解码成
   位图，再走现有的"当文件投放"逻辑。
2. 给 winex11 打补丁做 png→CF_DIB，等于把 deepin 那段搬过来，但要自己维护 wine。

## 7. 从 deepin-wine10 线路迁到本包：prefix 得手动换一次

两套 prefix 的目录结构不通用（deepin 那套带着自己的 wine 10.14 补丁库），
所以**不能直接让 wine 11 打开老的 `wecom-win` 目录**，得把目录整个换掉：

```bash
# 1. 关掉两个线路的进程，别在运行中搬目录
WINEPREFIX=$HOME/.local/share/wineprefixes/wecom-win   wineserver -k
WINEPREFIX=$HOME/.local/share/wineprefixes/wecom-win wineserver -k

# 2. 老的 deepin-wine10 prefix 挪走（确认新线路能用后再删）
mv ~/.local/share/wineprefixes/wecom-win{,.deepin-wine10.bak}

# 3. 过渡期 wine 11 的 prefix 改名成本包的正式 prefix
mv ~/.local/share/wineprefixes/wecom-win ~/.local/share/wineprefixes/wecom-win
```

`dosdevices` 里的盘符是相对符号链接，`*.reg` 里也没有写死 prefix 路径，所以
目录改名之后登录态、聊天记录、注册表补丁都还在。唯一要手改的是 prefix 里
`drive_c/users/<user>/Desktop/WeCom.desktop` 以及宿主桌面上同名快捷方式里
写死的 `WINEPREFIX=/.../wecom-win11`。

面板上钉过的旧条目（deepin deb 的 `com.qq.weixin.work.deepin.desktop`、过渡期的
`wecom-win11-bin.desktop`）卸包后会变成死图标，重新钉 `wecom-win-bin.desktop`。

## 8. Flutter 页面（Industry News / Bot 面板）掉出主窗口，变成独立无边框窗口

**现象**：点 Industry News 后，News 列表不再嵌在主窗口右侧聊天区，而是变成一个
没有标题栏、不能移动、不能最大化最小化的独立窗口。Bot 的 Request for use、
Approvals 同理。

**根因在 wine 11，不在企业微信**：

| 进程 | manifest 声明 | wine 里的 DPI context |
|---|---|---|
| `WXWork.exe` | 无 / SystemAware | `0x6011`，awareness=1（SYSTEM_AWARE） |
| `FlutterPlugins.exe` | `PerMonitorV2` | `0x12`，awareness=2（PER_MONITOR_AWARE） |

wine 11.16 引入提交 `730d06d7b798d57133eb6e98af0df159d5d6bcfe`
（`server: Check DPI awareness contexts when reparenting windows.`），
`server/window.c` 的 `set_parent` 多了一道校验：

```c
if (parent && !is_desktop_window( parent ) && !is_dpi_awareness_compatible( win, parent ))
    return set_error( STATUS_INVALID_STATE_TRANSITION );
```

`is_dpi_awareness_compatible` 只看 awareness 是否相等。企业微信跨进程
`SetParent()` 把 Flutter 窗口塞进 `WXWorkQtHostWindow_layered` 宿主时，
awareness 一个 1 一个 2，直接被拒。应用侧宿主窗口已经建好了（能看到
`0x70302` 这个 676x626 的空宿主），Flutter 窗口没挂进去，就留成了
`WS_POPUP` 顶层窗口。

**修法**：不改 wine，用 IFEO 在 wine 读取 exe manifest 之前覆盖 DPI 感知。

```text
HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\FlutterPlugins.exe
    dpiAwareness = REG_DWORD 1
```

包里的 `wecom-win-dpi.reg` 就是这个键，启动器每次 `ensure_prefix` 都会
`regedit /S` 一遍（幂等），全新机器装完即生效，不需要手动执行命令。
该值持久化在 prefix 里，也随 prefix 一起备份/迁移。

**验证方式**：Flutter 进程的 `dpi_context` 从 `0x12` 变成 `0x6011`，
`FLUTTER_RUNNER_WIN32_WINDOW` 不再作为顶层窗口出现，而是挂在
`WXWorkQtHostWindow_layered` 下面。

**注意**：注册表只影响新启动的进程。改完之后要等 `FlutterPlugins.exe`
重启（关掉再打开 Industry News 即可），不需要重启企业微信主程序、不用重新登录。

手工回滚：

```bash
WINEPREFIX=$HOME/.local/share/wineprefixes/wecom-win wine reg delete \
  'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\FlutterPlugins.exe' \
  /v dpiAwareness /f
```

## 9. 中文字体：DirectWrite 有回退，GDI 的 Tahoma 必须自己补汉字

两条渲染路径要分开看：

| 路径 | 用在哪 | wine 11 的行为 |
|---|---|---|
| DirectWrite | CEF 页面（聊天区、News、弹窗） | `dlls/dwrite/analyzer.c` 的 `system_fallback_config` 内置 CJK 回退，指向 `Noto Sans CJK SC/TC/KR/JP`；`dlls/dwrite/font.c` 的 `fontcollection_add_replacements()` 认 `HKCU\Software\Wine\Fonts\Replacements`，把 Noto 这些家族名别名到本机字体 |
| GDI | 主界面、RichEdit 输入框 | 走 `Replacements` 的家族没问题（微软雅黑、Noto 等都会落到文泉驿）；但 `Tahoma` 家族已经存在（wine 自带一份只有拉丁字的 `tahoma.ttf`），替换不生效，RichEdit 输入框因此画方框 |

所以包里同时做两件事：`wecom-win-fonts.reg` 写替换表把
`Noto Sans CJK *` 和企业微信会点名的那堆 Windows 字体名指向
`WenQuanYi Micro Hei`；构建期再用 `make-tahoma.py` 从文泉驿派生一份家族名为
`Tahoma` 的字体，由 `apply_fonts()` 放进 `drive_c/windows/Fonts` 并在
`HKLM\...\Fonts` 注册。

> 2026-11-16 的教训：`5.0.13.6005-4` 曾把派生 Tahoma 当成"wine 11 已经不需要"
> 删掉，结果主界面正常、输入框输中文又变方框。`5.0.13.6005-11` 起恢复。
> 主界面走 `TextOutW` 时确实有逐字回退，但 RichEdit 输入框依赖字形索引，
> 不能拿主界面的效果推断输入框。

**坑一：GDI 的替换不会覆盖已存在的家族。** `dlls/win32u/font.c` 的
`load_gdi_font_replacements()` 里有 `else TRACE("%s is available. Skip this
replacement.")`；而 wine 11 自带一份只有拉丁字的 `tahoma.ttf`
（`/usr/share/wine/fonts/tahoma.ttf`），所以 `"Tahoma"="WenQuanYi Micro Hei"`
这条对 GDI 是**不生效**的，DirectWrite 侧才生效。没有派生 Tahoma 时实测
`GetGlyphIndicesW` 对 4 个汉字返回 `0xFFFF`，输入框就是方框。

**坑二：`FontSubstitutes` 也救不了。** `find_matching_face_by_name()` 先按名字
找家族，找到就直接用，只有找不到才看替换名；Tahoma 家族存在，替换名根本没机会。

**验证方法**（`/tmp/dwritefb.exe`、`/tmp/gdifonttest.exe` 是当时写的一次性探针）：

```bash
export WINEPREFIX=$HOME/.local/share/wineprefixes/wecom-win

# DirectWrite：返回的字体必须有汉字字形索引
wine /tmp/dwritefb.exe        # U+4E2D glyph index 非 0

# GDI：Tahoma 不能是 ffff；派生字体铺好后应当返回真实字形索引
wine /tmp/gdifonttest.exe
```

`apply_fonts()` 每次启动都会把包里 `fonts/` 下的两个派生字体重新铺进 prefix
再导入注册表，幂等，老 prefix 升级后也不需要手工处理。`fontfix` 子命令可以
单独重跑这一步。

## 10. Plank 会把没装包时缓存的 fallback 图标一直记着

**现象**：包里的图标已经是企业微信的蓝橙 logo（`/usr/share/icons/hicolor/*/apps/wecom-win-bin.png`，
desktop 里 `Icon=wecom-win-bin` 也对），但 Plank 上显示的是一个齿轮/设置图标。

**原因**：图标文件缺失那次启动时，Plank 通过 `GtkIconTheme` 找不到图标，退回
主题里的 `preferences-system`。之后它把这张图缓存在自己的 dock item 里，只在
dock 条目重建时才重新查图标主题，`gtk-update-icon-cache` 并不会通知它。

**修法**：`pkgrel=5` 起 `post_install`/`post_upgrade` 会刷图标缓存和 desktop
database，图标本身也在构建期从 exe 里抠（`wrestool -x -t14 -n 103`）。已经缓存
错的那次需要重启一次 Plank 才会重新查图标：

```bash
killall plank && setsid plank >/dev/null 2>&1 &
```

**另一个坑**：`build()` 里调用 `wrestool` 之前必须 `cd "$srcdir"`。
`build_riched20()` 结束时停在 `$srcdir/wine-build`，不回目录的话 wrestool 在空
目录里找不到 exe，图标被静默跳过（`if ... 2>/dev/null` 把错误也吞了），装出来
的包就是没图标的。

## 11. 托盘图标黑底加边框：Wine 给的 bounding shape 被面板画成实心

**现象**：托盘里的企业微信图标是一块黑底方块，边上还多一圈边框，其他应用
（IBus、NetworkManager）的托盘图标正常。

**原因**：Wine 画分层窗口（托盘图标就是）时会按 alpha 通道给窗口设一个
`ShapeBounding` 区域，`dlls/winex11.drv/bitblt.c` 里 surface shape 那段：

```c
XShapeCombineMask( gdi_display, surface->window, ShapeBounding, 0, 0, shape, ShapeSet );
```

在没有合成器的桌面上这就是"透明处点击穿透"的实现。开了合成器以后本来不需要
它（窗口是 32 位 ARGB，透明处 alpha 就是 0），但 Xfce panel 是把托盘 socket
单独合成出来的，它不按 alpha 处理这个 shape，而是把 shape 圈住的区域整块填黑。

实测（用 `XShapeGetRectangles` 读窗口的 bounding 区域，再抓面板窗口的像素比对）：

| 窗口 | bounding shape | 面板里画出来 |
|---|---|---|
| 企业微信托盘窗口 | 5 个矩形（含越界的 `(22,0,106,3)`） | 330 个不透明像素里 256 个纯黑 |
| 去掉 shape 后 | 整窗 `(0,0,22,22)` | 145 个不透明像素，0 黑，和图标原始 RGBA 逐像素一致 |
| IBus / NM | 整窗 | 正常 |

**修法**：`no-shaped-systray.c` 编成 `LD_PRELOAD` 小库，在 wine 进程里拦
`XShapeCombineMask`/`XShapeCombineRectangles`，只在**有合成器**（`_NET_WM_CM_S0`
有 owner）且目标是那种不超过 64 像素的 32 位托盘窗口时，丢掉 `ShapeBounding`。
`ShapeInput` 一律不动，透明处的点击穿透靠 input shape，行为不变；`src == None`
（清 shape）和空区域（wine 用来藏窗口）都放过去，避免留下旧剪裁。

启动器里已经设好，不需要手工带：

```bash
export LD_PRELOAD="/usr/lib/wecom-win-bin/no-shaped-systray.so${LD_PRELOAD:+:$LD_PRELOAD}"
```

**注意几个试过没用的方向**：

- 注册表 `HKCU\Software\Wine\X11 Driver` 下那个 `ShapeLayeredWindows` 开关在
  wine 11.22 里只剩读配置的代码（`x11drv_main.c:485` 附近），驱动实际没再用它，
  写进去没效果。
- 图标数据本身是干净的（alpha 直方图里透明像素 alpha=0，没有整块不透明的黑），
  不是企业微信给的图有问题。
- 关掉面板的"方形图标"也只是换个画法，shape 还在，黑底还在。
