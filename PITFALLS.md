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

## 12. Emails / Document 面板点开全白：WeMail 也没挂进宿主窗口

**现象**：左侧 `Emails`、`Document` 点开后右侧是纯白；聊天、通讯录正常。
`WeMail.exe` 进程起来了，CEF 日志里 `WebQualityDetector` 报截图
`white_rate:100`。

**定位过程**：

窗口树（自己的 Win32 探针枚举）里 WeCom 的容器和宿主都在，尺寸也正常，
但宿主窗口下面没有任何子窗口：

```text
WeWorkWindow WeCom                          986x650 visible
  WXworkWindow - WeCom-Email                926x650 visible
    WXWorkQtHostWindow_NOT_BACKGROUND       926x650 visible   <- 空壳，无子窗口
```

而 WeMail 那一侧的整棵 UI（含 `CefBrowserWindow`，尺寸 376x545 是对的）
全挂在自建的隐藏顶层窗口下，停在屏幕外 `(-9120,-9526)`。

用 `GetWindowDpiAwarenessContext` 逐个窗口量，差异就出来了：

| 窗口 | DPI context | awareness |
|---|---|---|
| `WXWork.exe` 的宿主窗口 | `0x6011` | 1（SYSTEM_AWARE） |
| WeMail 的 `Email` / `CefBrowserWindow` | `0x12` | 2（PER_MONITOR_AWARE） |

和第 8 条同一个校验：

```c
if (parent && !is_desktop_window( parent ) && !is_dpi_awareness_compatible( win, parent ))
    return set_error( STATUS_INVALID_STATE_TRANSITION );
```

WeCom 跨进程 `SetParent(WeMail窗口, 宿主)` 直接被 wine 拒了，所以宿主永远
是空的。从探针进程手动调 `SetParent` 同样返回 0（而且 `GetLastError()` 还是
0，因为错误发生在窗口所属进程里，跨进程 `WM_WINE_SETPARENT` 只把 0 带回来）。

**修法**：给 `WeMail.exe` 也加一条 IFEO，和 Flutter 那条并排放在
`wecom-win-dpi.reg`：

```text
HKLM\Software\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\WeMail.exe
    dpiAwareness = REG_DWORD 1
```

**验证**：WeMail 的窗口 context 从 `0x12` 变成 `0x6011`；宿主窗口下面出现
`Qt51517QWindowIcon "Email"` → `WeMail` → … → `CefBrowserWindow`，面板内容
正常。Document 走同一个 `WeMail.exe`，一起修好。

**注意**：IFEO 只对新启动的进程生效，改完要把企业微信整个重启一次
（`wineserver -k` 再启动），只是关掉再点开面板不够 —— `WeMail.exe` 是常驻的。

## 13. Emails / Document 切回 Messages 后整块内容区残留

**现象**：Messages → Emails（或 Document）→ Messages 之后，顶部还留着邮件
那一排按钮和文字（`Reject`、`Reply`、`Search`），会话列表也停在邮件列表的
样子。不碰别的就一直挂着，鼠标划过或切个窗口又会自己消失。

**原因**：Emails / Document 不是主窗口里的一块区域，而是盖在 Messages 上面
的独立 Win32 子窗口，尺寸基本等于整个内容区。Wine 的 win32u 在隐藏子窗口时
走的是 `erase_now()`：给露出来的父窗口发 `WM_ERASEBKGND` 擦背景，就到此为
止了，**不会**顺手把父窗口客户区标成 invalid。原生 Windows 那边
`ShowWindow` 之后的失效处理是完整的，所以这事只在 wine 上出现。

父窗口既然没被标脏，`WM_PAINT` 就不会来，WeCom 自己也就没机会把 Messages
重画回去，面板原来盖住的那一整块（工具栏 + 会话列表）就一直是陈的，直到别的
事件（鼠标悬停重绘、切换窗口、resize）碰巧触发一次重绘。

**修法**：`wecom-repaint-watch.c`，一个跑在 wine 里的 Windows 小程序，由
启动器 `setsid` 拉起，和主程序同 prefix 但不共享 `LD_PRELOAD`（它不建托盘
窗口，不需要 `no-shaped-systray.so`）。

主循环每 250ms 扫一次两个面板窗口的句柄和可见性，同时用
`SetWinEventHook` 监听 `EVENT_OBJECT_SHOW` / `EVENT_OBJECT_HIDE`。看到
visible → hidden 后，等面板安静 700ms，再按面板原来的位置把主窗口那一块
整块标脏，300ms 后补第二枪：

```c
RedrawWindow(main_wnd, &panel_rect, NULL,
             RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE | RDW_ALLCHILDREN);
```

几个关键点：

- **重画范围必须是整个面板矩形，不是顶部一条窄带**。第一版只打了顶部
  60px，工具栏是干净了，会话列表还停在邮件列表的样子——因为被盖住的整块
  都是陈的。面板矩形按 `GetWindowRect` 取，再用 `ScreenToClient` 换算，
  拿不到句柄时退回整块客户区。
- **`RDW_NOERASE` 不能省**：带擦背景的话主窗口会闪一下白，聊天区也会跟着抖。
  只 invalidate 不加 erase，WeCom 收到 `WM_PAINT` 会自己把真实内容画回来。
- **`RDW_ALLCHILDREN` 也要带**：会话列表是主窗口的子窗口，只标父窗口它不会
  跟着重画。
- **不能在面板刚隐藏时立刻重画**：切换后 WeCom 自己还会补画约 0.6 秒。
  250ms 就重画会先画出 Messages，再被 WeCom 的旧邮件页盖回去，看起来就是
  「消息 → 旧邮件 → 消息」来回闪。实测把首枪放到面板安静 700ms 后，旧页
  不再反扑；第二枪在 300ms 后兜底偶发的迟到绘制。
- **SHOW 必须取消尚未执行的重绘**：第一次用轮询时，Document 的 HIDE/SHOW
  对实测只隔 3ms，整个落在同一个 250ms tick 里，轮询根本看不见。事件钩子
  看到任何面板 SHOW 就清空重绘队列，避免把 Messages 盖到刚打开的 Document
  或 Emails 上。钩子期间收到的 HIDE 则重新开始计算 700ms 安静期。
- **面板句柄是懒创建、还会被销毁**：冷启动时 Emails / Document 的窗口还不
  存在，点到左边入口才建；切走时它可能被整个销毁。所以"句柄没了"必须当成
  "收起了"，**不能**把它当作新基线重置掉——第一版就是这么漏掉 Document
  那一路切换的。
- **一个 session 只跑一个**：用命名 mutex 占位。旧实例在主窗口消失后还要守
  5 秒才退出，所以新实例的 mutex 等待时间（32 × 250ms = 8s）必须比它长，
  不然快速重启时两个都退出，没人接手。
- **WinEvent 回调靠消息循环派发**：轮询等待要用
  `MsgWaitForMultipleObjectsEx` + `PeekMessage` 泵消息。只 `Sleep` 的话钩子
  收得到事件也进不了回调，表现和没装钩子一样。

**验证**：录 10fps 的窗口画面，按真实截图时间逐帧分类（页面头部
`x=76..968, y=20..60` 对三张参考图的平均差分别约 0/1.5/8+，足以稳定区分
Messages / Emails / Document）。

- Emails → Messages 连测 5 轮：最终都在点击后约 0.8 秒收敛到 Messages，
  没有永久残留，也没有 watcher 自己制造的来回闪。
- Document → Messages 连测 3 轮：都在 0.1 秒内直接到 Messages，无残留。
- 快速互切（Messages → Document → Messages → Emails，间隔 200–300ms）：
  日志能看到 Document 的 HIDE/SHOW 对只隔 3ms，SHOW 时 watcher 取消了 2–3
  个待执行重绘，新面板没有再被 Messages 盖回去。
- WeCom 自己偶尔会在切换瞬间先画一帧旧邮件头，再被 watcher 的 700ms 重绘
  纠正。这段应用自身的短暂反扑无法靠重绘时序完全消除；能保证的是不再永久
  残留，也不会因补画时机不当产生多次来回闪。

## 14. 托盘图标闪烁时，图标位置变成纯黑方块

**现象**：企业微信收到消息时托盘图标会闪，亮的那一帧正常，暗的那一帧原来
的图标位置变成一块 16x16 的纯黑色方块，周围 22x22 的窗口区域仍是透明的。

**原因**：在 `programs/explorer/systray.c` 的 `paint_layered_icon` 里，wine 靠
“扫一遍像素有没有非零 alpha”来判断这张图带不带 alpha 通道：

```c
for (i = 0, ptr = color_bits; i < size.cx * size.cy; i++)
    if ((has_alpha = (ptr[i] & 0xff000000) != 0)) break;
```

企业微信闪烁的隐藏帧是一张 32 位全透明的图，一个非零 alpha 都没有，于是被
判成“没有 alpha 通道”，掉进 AND mask 分支：建一张 1bpp 掩码、先整体填 0xff，
再用 `DI_MASK` 把图标画上去。这张图的 AND mask 是全 0（全透明），掩码里没有
任何一位被清掉，循环里就把**每个**像素的 alpha 都置成 `0xff` 且颜色保持 0，
结果就是整块不透明黑。真实 Windows 上这张帧就是全透明，看不到任何东西。

实测帧（`XGetImage` 统计托盘窗口 22x22 的像素）：256 个不透明 `#ff000000`
落在中间 16x16，其余 228 个像素 alpha 为 0，窗口的 bounding shape 是整窗。

**修法**：`wine-explorer-tray-blank.patch` 在 `has_alpha` 判断后面补一条：
DIB 是 32 位（`GetIconInfo` + `GetObjectW` 看 `bmBitsPixel`）或者是 `NULL`
图标时，即使一个非零 alpha 都没有，也按“有 alpha 通道”处理。这样全透明帧
走正常的 `ULW_ALPHA` 路径，窗口内容是真正的 alpha=0，面板合成出来就是透明。

**这个补丁要换掉 wine 自带的 `explorer.exe`**，不是 `riched20` 那种往应用
目录塞一个 DLL 就完事的做法，原因和落地方式：

- 托盘窗口是 explorer.exe 建的，`paint_layered_icon` 就在它里面，`win32u` /
  `winex11.drv` 那层拿不到“这张图标有没有 alpha 通道”这个信息；
- PKGBUILD 里 `build_explorer` 用同一份 wine 源码编 64 位 `explorer.exe`，
  链接时要去掉 `-Wl,--wine-builtin`（带这个标记的 PE 会被当成 builtin，用
  `WINEDLLOVERRIDES` 强制 native 加载会以 `c0000135` 失败）；
- 启动器把它同步到 `<prefix>/drive_c/windows/system32/explorer.exe`，并给
  `WINEDLLOVERRIDES` 加上 `explorer=n,b`（builtin 兜底）。`wineboot` 建
  prefix 时会往 system32 放一份 builtin 副本，同步时用“写临时文件再改名”，
  避免正在运行的 wine 进程映射的旧文件被截断；
- 同步之后必须让这个 prefix 的 wine 进程全部退出一次（`wineserver -k`），
  托盘窗口才会由新的 explorer 建出来。

**验证**（`/tmp` 下的 `traytest.exe` + `trackwin`，独立 prefix，
`WINEDLLOVERRIDES=explorer.exe=n`）：四个阶段（实心 / 全透明+空 mask /
`NULL` 图标 / 全透明+全 1 mask）来回切，隐藏帧里 `alpha=0` 的像素 484 个、
不透明黑 0 个，屏幕同一块区域的像素也跟着回到面板底色，不再残留上一帧的
图标；切回实心帧时 256 个图标像素（`#00c000`）正常回来。

**顺带确认**：`no-shaped-systray.so` 丢掉 bounding shape 之后，隐藏帧的
重绘不需要额外补一刀。窗口内容变成全透明本身就会让面板重画那一块；早先
观察到“旧图标留在屏幕上”是 AND mask 画出来的不透明黑导致的，不是合成器
不重绘。

