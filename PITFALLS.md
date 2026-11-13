# 踩坑记录

装这个包时遇到的问题，以及每个坑为什么那样填。改动之前建议先读一遍。

## 1. 输入框里的汉字显示成方框

**现象**：界面大部分地方中文正常，只有聊天窗口的输入框里打出来的汉字是方框；
发出去的消息却是正常的。

**原因**：主界面和输入框走的是两套字体路径。

- 主界面是 GDI，读 `HKCU\Software\Wine\Fonts\Replacements`，把
  `Microsoft YaHei` / `SimSun` / `Microsoft Sans Serif` 之类的族名替换成
  prefix 里真实存在的字体就能显示。
- 输入框是 CEF（Chromium），走 DirectWrite。CEF 给这段文字指定了 Tahoma，
  而 Deepin 这套 wine 10.14 的 `dwrite.dll` 不按族名替换，只在缺字时去查
  “系统字体回退表”。这张表由注册表
  `HKLM\Software\Microsoft\Windows NT\CurrentVersion\Fonts` 里登记的字体构建，
  干净 prefix 里登记的只有 `tahoma.ttf`、`marlett.ttf`、几个 `.fon` 点阵字体，
  没有一个带汉字。回退表对中日韩为空，于是缺字直接画 `.notdef` 方框。

实测依据：`strings /opt/deepin-wine10-stable/lib/wine/x86_64-windows/dwrite.dll`
里只有 `Software\Microsoft\Windows NT\CurrentVersion\Fonts` 这一条字体相关
字符串，`YaHei` / `SimSun` / `DengXian` / `Gothic` / `Malgun` / `Noto` 一个都
没有。也就是说它自己不内置任何 CJK 回退族名，完全依赖 prefix 里注册了什么。

**试过但无效**：

- 改 `~/.config/fontconfig/fonts.conf`：那是 Linux 侧字体栈，CEF 在 wine 里
  走的是 Wine 的字体接口，碰不到。
- 改 `HKCU\Software\Wine\Fonts\Replacements` 加 `Tencent Sans` 之类别名：对
  GDI 有效，对 DirectWrite 无效。
- 往 prefix 的 `drive_c/windows/Fonts/` 里丢一个中文 TTF 但不注册：DirectWrite
  不回退到它，因为没进回退表。

**解法**：让 Tahoma 本身带汉字。`make-tahoma.py` 把文泉驿微米黑
（`/usr/share/fonts/wenquanyi/wqy-microhei/wqy-microhei.ttc`，fontNumber=0，
来自 `wqy-microhei` 包）的内部族名改成 `Tahoma`，另存出
`tahoma.ttf`（Regular, weight 400）和 `tahomabd.ttf`（Bold, weight 700），
放进 prefix 的 `drive_c/windows/Fonts/`，并在 HKLM 和 Wow6432Node 两处把
`Tahoma (TrueType)` / `Tahoma Bold (TrueType)` 指过去。

字体在构建期现生成而不是塞进源码树，一是避免把一个 4.6M 的二进制放在仓库里，
二是字体源跟着 `wqy-microhei` 包走，不用自己维护 URL 和校验和。派生物随包分发，
所以包里同时装了原字体的 `FONTEXCEPTION` 许可。

这样 CEF 请求 Tahoma 拿到的是带 49531 个字形、34600 个码点、汉字齐全的字体，
不用去回退，方框自然没了。影响面也比换整个 prefix 的默认字体小得多。

验证方式（改完之后输入框不再有方框时，可以这样确认走的是我们的字体）：

```bash
grep -l 'tahoma.ttf' /proc/$(pgrep -f WXWorkWeb.exe | head -1)/maps
```

应该指向 prefix 里的 `drive_c/windows/Fonts/tahoma.ttf`。

### 1.1 派生字体时要去掉排版族名

`name` 表里 nameID 16/17/18/21/22 是“排版族名/排版子族名”那一套。留着它们，
DirectWrite 会把字体重新组织成 `Tahoma / Regular` 两个独立族，和我们要的固定
映射冲突。`make-tahoma.py` 里 `removeNames(nameID=...)` 就是在处理这个，别删。

## 2. 静默安装成功与否必须查文件

安装包是 NSIS，`/S` 可以静默装。但**返回码不可靠**：空间不足之类的失败也可能
给出看起来正常的退出码，只是少装文件。所以启动器不信任 `$?`，装完必须确认
`drive_c/Program Files (x86)/WXWork/WXWork.exe` 和版本目录 `WXWork/<版本号>/`
都在，缺一个就报错退出。

版本目录这一条同时也是升级开关：只看 `WXWork.exe` 的话，包升到新版本后启动器会
认为已经装好，继续启动旧程序。加上版本目录判断，`pkgver` 一改就会重新跑一次安装
包。注意 NSIS 是就地升级，旧版本目录通常还留着，不会自动清。

装完应该能看到 `WXWork.exe`、`Uninstall.exe`、`WXWorkUpgrader` 和
`5.0.13.6005/` 这些。正常完整安装约 1.9G。

prefix 整体会比这大：CEF 缓存、日志都在里面，跑几天后 3~4G 是正常水位。

## 3. 不需要 `WINEPREDLL`

早期拿裸 prefix 试装时，直接跑 exe 起不来，当时是靠 deepin deb 包里的那套补丁
dll 才启动成功的：

```bash
WINEPREDLL=/opt/apps/com.qq.weixin.work.deepin/files/dlls   # 现在不需要
```

那套 dll 里有打过补丁的 ntdll/winex11.drv/user32 等。后来用本包的默认 prefix
（`wineboot -u` + NSIS 静默安装 + 字体补丁）重做了一遍，去掉 `WINEPREDLL` 也能
正常启动、登录、收发消息，所以 1.0.0 从 `pkgrel=2` 起不再导出它，也不再依赖
`com.qq.weixin.work.deepin`。

推测当初起不来是 prefix 初始化方式的问题（没跑 `wineboot`、缺组件），不是 exe
真的需要那套补丁 dll。如果以后换 wine 版本又出现起不来，先回头看这个变量。

## 4. `WINE_WMCLASS` 决定 `wecom-x11-fix` 能不能认出窗口

启动器导出 `WINE_WMCLASS=wecom-win`，wine 会把它写进窗口 `WM_CLASS` 的第二段：

```
WM_CLASS(STRING) = "wxwork.exe", "wecom-win"
```

`wecom-x11-fix` 拿 `XGetClassHint()` 的 `res_class` 跟自己的白名单比。**这个环境
变量不能去掉**：没有它，class 会退化成 exe 名 `wxwork.exe`，修复认不出窗口、静默
失效，窗口按钮穿透和拖动白边就都回来了。

这里刻意没有沿用 `com.qq.weixin.work.deepin`（deepin deb 那条线路的标识），两条
线路的窗口可以分开识别。代价是 `wecom-x11-fix` 必须同时认这两个值，`1.0.0-1`
只认旧的那个，所以**先装 `1.0.0-2` 或更新的再换这个 id**，否则窗口失去修复。

`wecom-win.desktop` 里的 `StartupWMClass` 是另一回事：它只给桌面环境/面板做窗口
归组（任务栏分组、图标、运行状态），修复程序不读它。这里跟 `WINE_WMCLASS` 保持
一致，是因为那本来就是窗口真实的 class。

顺带一提，钉在面板上的 `com.qq.weixin.work.deepin.desktop` 是 deb 线路的条目，
换成 exe 线路后要点 `wecom-win-bin.desktop`，否则面板图标不会显示“运行中”。

## 5. `wxworkweb.reg` 不需要

deepin 的 `run.sh` 会导入一个 `wxworkweb.reg`，内容只有
`HKCU\Software\Wine\AppDefaults\WXWorkWeb.exe\Direct3D` 的 `renderer=gdi`。

实测 5.0.13 的 prefix 里没有这一项，程序运行和渲染都正常，所以本包没导。
如果哪天内嵌页面出现黑屏/空白，可以先试着手工加上这一项。

## 6. 登录态和 prefix 绑死

登录态、聊天记录都在 prefix 里。删 prefix = 重新登录 + 丢本地记录。所以：

- 启动器优先复用已存在的 `wecom-win-5.0.13` / `wecom-win`，避免升级包装完
  又建个新 prefix（调试期在这上面吃过一次亏：手机上被踢下线了）。
- 调试期间不要反复重装、反复换 prefix。短时间内多次登录会触发风控，把手机端
  也挤下线。

## 7. 版本号在构建期烧进启动器

`wecom-win.sh` 里的 `@PKGNAME@` / `@PKGVER@` 是 `package()` 用 `sed` 替换的。
改包名或版本时只需要动 `PKGBUILD` 的变量，别手改脚本里的占位符。

## 8. 右上角的 `- 口 X` 和 `...` 都是独立的 override-redirect 窗口

**现象**：弹窗盖住主窗口右上角时，标题栏的 `- 口 X`、以及标题栏右侧的 `...`
（更多）按钮会画在弹窗上面；点它们还会真的触发企业微信的操作。1.0.0-2 的
`wecom-x11-fix` 只挡住了前者，`...` 还会透出来。

**原因**：Wine 把 client-side 的窗口按钮画在透明的 override-redirect ARGB
窗口里，各自独立：标题栏条是 986×28，`...` 按钮是 28×28，都是 `depth=32`、
同 pid、带圆角 bounding shape。override-redirect 的窗口不归窗口管理器管，
restack 时永远在最上层，WM 没法把它们压到弹窗下面。

**解法**：`wecom-x11-fix` 1.0.0-3 起把这类窗口都当 chrome overlay 跟踪，按
stack 里压在所属窗口之上的窗口裁掉 bounding + input，弹窗关掉后再恢复原来的
圆角形状。只对“和主窗口同宽、贴在顶边”的条状窗口做拖动粘合，28×28 的小按钮
不动。

顺带一条经验：如果调试时把形状裁成空又没恢复（比如 daemon 被 kill -9），
按钮会彻底消失；新版本把“bounding 为空”当成未裁形状处理，重启 daemon 即可
恢复。

## 9. 从 deepin deb 版迁移的顺序不能反

`pkgrel=1` 时本包依赖 `com.qq.weixin.work.deepin`，所以**必须先升到 `-2`
再卸旧包**。顺序反了，`pacman -Rns com.qq.weixin.work.deepin` 会把本包当孤儿
一起带走，连带 prefix 之外的东西全清掉。`-2` 起没有这个依赖，先卸后装都行。

旧包自己的依赖栈也是白占地方：`spark-dwine-helper` 会再拉
`deepin-wine8-stable`（326M）。卸完旧包可以把这两个也删了，`deepin-wine10-stable`
要留着，本包在用它。

旧版的 prefix 在 `~/.deepinwine/com.qq.weixin.work.deepin`，和本包的
`~/.local/share/wineprefixes/wecom-win` 完全独立，可以直接删；删之前确认没有
残留 wine 进程还开着那个目录。`~/.deepinwine/Deepin-WXWork` 是更早的 deepin
prefix 名，同理。

最后一条：面板上钉的 `com.qq.weixin.work.deepin.desktop` 卸包后会变成死图标，
要重新钉 `wecom-win-bin.desktop`。

## 10. CEF 日志里的 ERROR 是噪音

启动后 stderr 会刷一片
`ERROR:navigation_request.cc(...)] NavigationRequest::ShareSameRenderProcess`，
每次启动都有，和登录态、渲染、崩溃都无关，不影响使用。

启动器已经导出了 `WINEDEBUG=-all` 压掉 wine 自己的调试输出；CEF 这部分是程序
自身日志，压不掉，排查问题时直接忽略即可。

## 11. 点 WorkBuddy 提示“没有下载”：Wine 里缺 `workbuddy://` 协议

**现象**：企业微信里点 WorkBuddy 入口，弹窗说没有下载，但宿主机上
`/usr/bin/workbuddy` 装着、`x-scheme-handler/workbuddy` 也关联到了
`workbuddy.desktop`。deepin deb 那版没这个问题。

**原因**：这个入口是 CEF 用 ShellExecute 打开 `workbuddy://...`，查询的是
Windows 侧的协议关联。Wine 只内置 `http` / `https` / `mailto` 这类常见协议，
`HKCR\workbuddy` 不存在，ShellExecuteEx 直接失败，页面上就显示成“没有下载”。
宿主的 xdg 关联它看不到。

**解法**：在 prefix 里注册 `workbuddy` 协议，命令指向 `winebrowser.exe`。
winebrowser 会把它交给宿主的 `xdg-open`，再落到真正的 WorkBuddy。
注册表内容在 `wecom-win-protocols.reg`，启动器的 `setup` 阶段每次都会应用。

**注意写在 HKLM 而不是 HKCU**：实测只写
`HKCU\Software\Classes\workbuddy` 时，`reg query HKCR\workbuddy` 查不到，
`wine start workbuddy://...` 仍然报 `ShellExecuteEx failed: File not found`；
写到 `HKLM\Software\Classes\workbuddy` 后才正常解析（和内置的 `https` 在同一个
位置）。两个都写反而会掩盖这个区别，所以只留 HKLM。

**验证**：

```bash
export WINEPREFIX="$HOME/.local/share/wineprefixes/wecom-win"
export WINEDLLPATH=/opt/deepin-wine10-stable/lib:/opt/deepin-wine10-stable/lib64
/opt/deepin-wine10-stable/bin/wine start 'workbuddy://discover'
```

宿主 WorkBuddy 的 `~/.workbuddy/logs/main.log` 里应出现：

```
[DeepLink] dispatching open url to renderer ... "scheme":"workbuddy","host":"discover"
```

对比：不注册时命令返回
`Application could not be started, or no application associated with the specified file.`

winebrowser 会顺带打印一行
`xdg-settings: default-url-scheme-handler not implemented for xfce`，
那是它在 xfce 下查默认处理器的噪音，后面的 `xdg-open` 仍然成功，可以忽略。

## 12. CEF 弹窗（Industry News / Request for use / Approvals）白屏

**现象**：主界面和聊天正常，点开这几个独立弹窗后内容区纯白；有时是“闪白”，
切焦点时变白、点一下鼠标又出来一点；`Workspace` 却有内容。

**原因**：不是网络，也不是页面问题，是 renderer 启动即崩。Wine 把 PE 的可写
数据节映射成 `PAGE_WRITECOPY`（`0x08`），而 V8 初始化结尾的 `FreezeFlags()`
会调 `OS::SetDataReadOnly()`，里面有两句：

```cpp
CHECK(VirtualProtect(address, size, PAGE_READONLY, &old_protection));
CHECK_EQ(PAGE_READWRITE, old_protection);
```

旧保护属性是 `0x08` 而不是 `0x04`，第二句失败 → `IMMEDIATE_CRASH()` →
`STATUS_BREAKPOINT`，dump 里看到的偏移固定是 `libcef.dll+0x16a59ac`
（文件偏移 `0x16a4dac`，`cc 0f 0b` 崩溃桩）。renderer 死了，宿主不停重建，
窗口就一直白。

**解法**：启动时给 CEF 传 V8 参数

```
--js-flags=--no-freeze-flags-after-init
```

`wecom-win.sh` 的 `launch()` 已默认带上；如果用户命令行里已经有
`--js-flags=...`，就不再加，避免覆盖。

**别做的事**：

- 别去 patch `libcef.dll` 的 `jne`。09:10 试过把 `0x16a49eb` 的 6 字节 `jne`
  换成 NOP，结果整进程 unhandled page fault，比白屏更糟。
- 别用 `--single-process`。CEF 会 `Cannot use V8 Proxy resolver in single
  process mode.`，然后 browser 级崩溃循环。
- 别关合成器/改 xfwm4。跟本问题无关，还会引出窗口黑毛边。

**代价**：跳过 flag 冻结只少掉 V8 自身的一层防御性只读保护（前提是攻击者已经
能在进程内任意写），不影响功能与沙箱。排查过程与证据见
`DEBUG-CEF-WHITEPAGE.md`。

**验证**：启动后 `Crashpad/reports` 不再新增 dump、renderer PID 不再起落；
用 CDP 在企业微信页面里 `window.open` 一篇 `mp.weixin.qq.com` 文章，能画出正文
（148 行文字像素、微信红 `#fa5151`）而不是纯白。2026-11-13 用户在实际入口
确认三个弹窗均正常，此后 Crashpad 没有新增 dump。

## 13. 点“加入快速会议”转圈后闪退：Wine 自带 concrt140 是半成品

**现象**：点“加入快速会议”，转几圈后没有会议窗口；老的 deepin deb 版能正常
拉起会议。Crashpad 里 `wwmapp.exe`（`WeMeet/3.26.511.637/`，进程名“企微融合
会议”）留下 dump，两次触发的异常记录完全一致：

```
code=0x80000100 flags=0x1 addr=0x7a048703 npar=2
params 0x765d2000 0x765d214c
```

`0x80000100` 是 Wine 的 `EXCEPTION_WINE_STUB`，两个参数指向模块名和函数名。
dump 里模块表显示 `0x765c0000` 的 `concrt140.dll`，而 `0x765d2xxx` 正好落在
它的 `.text` 里（winebuild 生成的 stub 把字符串就放在桩代码后面），说明命中的
是 **Wine 自带的 concrt140**，不是企业微信目录里的原生 DLL。

**复现**：从 dump 里抠出 `wwmapp.exe` 的完整命令行（PEB 里能读到 `--scheme=`
是 `wemeet://page/inmeeting?...`），把会议号换成无效值单独跑，加
`WINEDEBUG=+seh`，stderr 直接给出函数名：

```
wine: Call from 7A048703 to unimplemented function
concrt140.dll.??0_TaskCollection@details@Concurrency@@QAE@XZ, aborting
```

即 MSVC 并发库 `Concurrency::details::_TaskCollection` 的构造函数在 Wine 的
builtin 里没有实现。`wwmapp` 一初始化线程池就踩到，SEH 没人接，进程退出。

**解法**：启动器里优先原生：

```
export WINEDLLOVERRIDES='concrt140=n,b'
```

企业微信的 `WeMeet/3.26.511.637/concrt140.dll` 是完整的 MSVC 运行时；
`MSVCP140.dll`、`vcruntime140.dll` 本来就默认走原生。加 override 后同样的
命令行不再出现 `EXCEPTION_WINE_STUB`，进程能活到创建 D3D swapchain、
正常画会议窗口。写成 `n,b` 而不是只写 `n`，是为了让搜索路径里没有原生
`concrt140.dll` 的其他组件还能回退到 builtin，不至于整体起不来。

**注意**：别去补 Wine 的 concrt140 实现，也别改 `wwmapp` 的调用点；这个
构造函数属于 MSVC 内部 ABI，用微软原版 DLL 才是正路。

## 14. 从浏览器/截图工具复制的图片粘不进输入框

**现象**：复制文字能粘进聊天输入框，复制图片（截图工具、浏览器里右键复制图片）
粘进去什么都不出现，`Ctrl+V` 连个缩略图都没有。老的 deepin deb 版没这个问题。

**原因**：三层叠在一起。

1. X11 驱动往剪贴板里放的图片只有 `CF_DIB`、`CF_DIBV5`、`CF_BITMAP`，
   不会有 `CF_ENHMETAFILE`（`xclip -o -t TARGETS` 能看到）。
2. wine 的 `ole32` 对 `CF_DIB` 的 `FORMATETC` 返回 `TYMED_NULL`，也就是
   没法通过 `IDataObject` 把它交出去。所以 `WM_PASTE` 走 OLE 一遭，
   所有格式都对不上，直接判定“没东西可粘”。
3. `riched20` 的 `paste_formats[]` 只有三个：RTF、`CF_UNICODETEXT`、
   `CF_ENHMETAFILE`。文字能粘就是因为第二个命中；图片一个都不命中。

顺带查出同一条链上的第二个 bug：`paste_emf()` 调 `insert_static_object()`
时尺寸写死成 `{0, 0}`，即使 EMF 路径命中也会插入一个 0×0 的不可见对象
（`editor_insert_oleobj()` 只在尺寸为 0 时才去问 `IOleObject_GetExtent`，
而 wine 的 default handler 给不出结果，最后还是 0×0）。

**解法**：`wine-riched20-paste-dib.patch`，改的是 `dlls/riched20/editor.c`：

- 新增 `paste_dib()`：直接用 user32 的 `GetClipboardData(CF_DIB)` 取位图，
  `CreateDIBSection` 拷一份，按 96 dpi 换算成 twips，再走
  `insert_static_object(editor, NULL, hbmp, &sz)` 插成静态对象。
- `paste_special()` 在所有 OLE 格式都不匹配后兜底调它；`check_only`
  （`EM_CANPASTE`）分支只看 `IsClipboardFormatAvailable(CF_DIB)`，不再误报 0。
- `paste_emf()` 从 `ENHMETAHEADER.rclFrame`（0.01mm）换算 twips 当对象尺寸，
  修掉 0×0。

**打包方式**：不打包整个 wine，只从对应版本的 wine 源码编这一个 DLL。

- `source` 里带 `wine-10.14.tar.xz`（`_winever`，和
  `deepin-wine10-stable` 的 wine 版本对齐），只 `--enable-archs=i386`，
  X11/音频/网络全部 `--without-*`，`make dlls/riched20/i386-windows/riched20.dll`
  实测整条链路 22 秒。
- 产物装到 `/usr/share/wecom-win-bin/riched20.dll`，启动器的
  `apply_wine_dlls()` 用 `cmp -s` 比对后拷进 prefix 的
  `Program Files (x86)/WXWork/`。DLL 搜索顺序里 exe 所在目录排第一，
  所以 `WXWork.exe` 会加载到它。
- `WINEDLLOVERRIDES='concrt140=n,b;riched20=n,b'`，留 builtin 兜底，
  免得别的子进程（比如 WeMeet 里的）找不到原生副本就起不来。

**必须踩的两个坑**：

- **winebuild 盖的 `Wine builtin DLL` 签名要去掉。** 留着它 wine 会把这份
  PE 当 builtin，去找配套的 `riched20.so`，找不到就 `LoadLibrary` 失败
  126，日志里只会看到 `load failed 126`，很容易以为是别的问题。构建里用
  `grep -abo` 定位签名再覆 32 字节 0。
- **测试宿主要有消息循环。** 用一个最小 32 位 `RichEdit20W` 宿主验证时，
  如果只 `WM_PASTE` 然后 `Sleep`，`ME_UpdateRepaint()` 只是
  `InvalidateRect`，没有消息循环就永远不重绘，看起来像“对象插进去了但没画出来”，
  白白多查一轮。要么 `UpdateWindow()`，要么老老实实 `PeekMessage` 泵消息。

**验证**：最小宿主里 `EM_CANPASTE` 返回 1；粘一张 240×160 的渐变图后用
`magick compare -metric AE` 比窗口截图和源图，`AE=0`（逐像素一致）。

### 14.1 只提供 `image/png` 的剪贴板也能粘（8 位 PNG）

排查时一度以为“剪贴板里只有 `image/png` 就粘不进去”，结论是**错怪了**：
deepin 的 `winex11.so` 自己链了 `libpng`（`strings` 能看到 `png_read_image`
等符号），会把 X11 的 `image/png` 直转成 `CF_DIB`。所以在 Windows 侧看到的
始终是 `CF_DIB`／`CF_DIBV5`／`CF_BITMAP`，只要 PNG 是 8 位就没问题。

实测矩阵（`/tmp/xtool/pngserve` 只对外提供 `image/png`，win32 侧用
`dibdump2.exe` 读）：

| 剪贴板内容 | `GetClipboardData(CF_DIB)` |
|------------|----------------------------|
| 8 位 RGB PNG | 240×160 / 24bpp，正常 |
| 8 位 RGBA PNG | 240×160 / 32bpp，正常 |
| 16 位 RGB PNG | NULL，转换失败 |
| 16 位 RGBA PNG | NULL，转换失败 |

同一份 8 位 PNG 在最小 RichEdit 宿主里：打过补丁的 `riched20`
`EM_CANPASTE=1`，粘完截图和源图 `AE=0`；wine 自带 `riched20`
`EM_CANPASTE=0`，粘完长度不变。文字粘贴回归：粘 `hello` 长度 5，正常。

**16 位 PNG 粘不进去是驱动侧限制**，锅不在 `riched20`：转换失败后 win32
剪贴板里连一个能取到原始 PNG 字节的格式都没有（注册的 `PNG` 命名格式也是空的），
包内没法补。截图工具、浏览器、GTK 剪贴板管理器存的都是 8 位 PNG，日常够用。

**顺带记一笔**：Xfce 的 `xfsettingsd` 带剪贴板管理器，会接管 `CLIPBOARD`，
接管后只剩 `image/png` 一个 target（`/tmp/xtool/whoowns` 看到 owner 是
`xfsettingsd`）。所以“TARGETS 里只有 image/png”在本机是常态，不代表数据有问题。

### 14.2 企业微信拒收标准静态图片对象，强行绕过去会退出

最小宿主里 `EM_CANPASTE=1`、粘完逐像素一致，但到企业微信里 `Ctrl+V` 还是
没反应。卡点不在取图，也不在 riched20 插入失败，而在企业微信自己实现的
OLE 回调。

- riched20 插静态 DIB 之前会调宿主的
  `IRichEditOleCallback::QueryInsertObject()`，传的 CLSID 是
  `{00000316-0000-0000-C000-000000000046}`（`CLSID_Picture_Dib`）。
- 企业微信对这个 CLSID 直接返回 `E_INVALIDARG`。反汇编 `WXWork.exe`
  的 `0x03ED85C0` 可以看到：它把 GUID 逐字节和一个动态 `std::vector<GUID>`
  比对，不在表里就返回 `E_INVALIDARG`；这张表从回调 receiver 的 vtable
  槽 14（偏移 `0x38`）取出。
- 用临时探针把这张表 dump 出来，一共 28 个 CLSID，全是企业微信自定义类型，
  既没有 `Picture_Dib`，也没有 `StaticMetafile`。列表只在
  `/tmp/wecom-olemix.log`，没进仓库。

试过强行绕过（`WINE_RICHED20_BYPASS_QIO=1` 时忽略 QIO 的返回值）：

- `insert_static_object()` 返回 0，输入框里确实短暂出现了缩略图。这说明
  “取图 -> 转位图 -> 插入”这一段本身是通的。
- 紧接着企业微信自己有序调用 `ExitProcess`。`+seh` 日志里只有
  `DBG_PRINTEXCEPTION_*` 调试打印，没有访问违例、没有 backtrace，
  所以不是 Wine 崩，是企业微信遍历输入框内容时碰到不认识的 OLE 对象，
  主动退出。

结论：**不能靠伪造 `Picture_Dib` 蒙过去**。企业微信后续按自己的对象模型
处理输入框内容，硬塞进去只会把应用带崩。另外单独替换过带
`CF_DIB -> TYMED_HGLOBAL|TYMED_ISTREAM` 修复的 `ole32`（wine 10.14
上游已有这段），粘贴仍然走 riched20 的 `paste_dib` 并被 QIO 拒绝，
说明 OLE 剪贴板的 TYMED 不是这条路径的关键。

包内目前只保留 `paste_dib`：对普通 RichEdit 宿主有效，在企业微信里表现为
“粘了没反应”，但不会崩。要继续修，只能走企业微信认可的对象类型，或者
非 OLE 路径（`CF_HDROP`、企业微信自己的粘贴处理），目前还没验证完。

**复现注意**：绕过版和探针版 DLL 都是临时实验产物，不在包里。用绕过版
测试会让企业微信退出，并可能触发重新扫码登录。

### 14.3 企业微信不是靠 RichEdit 的粘贴逻辑处理图片

对照实验（剪贴板里放一张截图）：

| 操作 | 结果 |
|------|------|
| 补丁版 riched20 + `Ctrl+V` | 无反应（走 riched20 `paste_dib`，被 QIO 拒） |
| Wine 自带 riched20 + `Ctrl+V` | 无反应 |
| 文件管理器复制图片文件 + `Ctrl+V` | 粘出来的是文件绝对路径（文本） |
| 图片文件直接拖进输入框 | 正常出缩略图，能发送 |

结论：企业微信自己的图片/文件处理链路是好的，`Ctrl+V` 根本没走到它那里。
Wine 自带 riched20 下 `Ctrl+V` 也只是 RicEdit 的默认粘贴（文件被当成文本），
说明缺的是“按键/粘贴消息转发给宿主”这一环。`editor.c` 顶部的兼容性清单里
`EN_MSGFILTER` 正好标着未实现（`- EN_MSGFILTER`），而 `txthost.c` 那套
TxTextServices 路径才有。企业微信很可能就是用 `EN_MSGFILTER` 拦 `VK_V`
自己做剪贴板分发的，所以它从来没被通知到。

下一步方向：在 riched20 里补 `EN_MSGFILTER`（键盘/鼠标消息先 `WM_NOTIFY`
给父窗口，宿主返回非 0 就不自己处理），再看 `Ctrl+V` 是否回到企业微信
自己的 bitmap 分支。右键菜单里的“粘贴”可以作为对照：如果菜单粘贴能出图，
就基本坐实是快捷键消息没转发。

### 14.4 输入框右键没有菜单

Wine 的 `ME_ShowContextMenu()` 原来有条硬门槛：宿主没实现
`ITextHost2`、或者没设 `IRichEditOleCallback`，就直接返回 FALSE，
消息落到 `DefWindowProc`，于是右键什么也没有。企业微信就是这种情况——
它既不提供 `ITextHost2`，`GetContextMenu()` 也只会失败或返回空菜单。

`editor.c` 里的处理：

- 拿不到宿主窗口时退到 `GetActiveWindow()` / `GetFocus()`；
- 宿主没有 OLE 回调、或 `GetContextMenu()` 失败时，由 riched20 自己弹一个
  Cut / Copy / Paste / Delete / Select All 菜单（`ME_PopupDefaultContextMenu`），
  选中项直接走 `editor_handle_message()`；
- 宿主返回 `S_OK` 但 `menu` 为 NULL 时不再兜底弹第二个。
  之前“一次右键弹出两个菜单、一大一小”，就是宿主自己弹了一个、riched20
  又补了一个。

> `-8` 曾把“宿主返回 `S_OK` 但菜单为 NULL”当成拒绝处理，改由 riched20
> 补弹内置菜单；实测企业微信照样会弹自己那份，两个菜单又一起出现。
> `-9` 回退了这段，只保留“宿主不给菜单就不补”的逻辑。

### 14.5 包里的 DLL 会在每次启动时盖掉 prefix 里的实验 DLL

`apply_wine_dlls()` 发现 `/usr/share/wecom-win-bin/riched20.dll` 和 prefix
里的副本不一致就用包里的覆盖。调试期手工拷进 prefix 的实验 DLL，**重启一次
就没了**，症状是“刚修好的右键菜单又消失了”。

调试时用 `WECOM_WIN_SKIP_DLL_SYNC=1 wecom-win` 跳过同步；定稿后要同时更新
包内那份（重打 `pkgrel`），别只改 prefix。

### 14.6 崩溃后的孤儿进程会把下一次启动卡死

企业微信崩掉之后，CEF（`WXWorkWeb.exe`）、`WeMailNode_x64.exe`、
`crashpad_handler.exe` 这些子进程会变成孤儿继续挂着，占着单实例通道。
新启动的 `WXWork.exe` 会停在
`Log/MobileFramework/<日期>.log` 的最后一行 `WeWork AppLaunch`，
既不建窗口也不退出，看起来像“启动失败”。

处理办法：把这几类进程连同 `wineserver` 一起清掉再启动。

```bash
ps -eo pid=,args= | grep -E 'WXWork\.exe|WXWorkWeb|WeMailNode|crashpad_handler'
# kill 掉上面这些 pid
WINEPREFIX="$HOME/.local/share/wineprefixes/wecom-win" \
    /opt/deepin-wine10-stable/bin/wineserver -k
```

### 14.7 撤销“插入图片”会 assert 掉整个企业微信

测试 OLE 投放时，往输入框里插进一张图再按撤销，企业微信直接闪退。
`+richedit` 日志里是：

```text
err:msvcrt:_wassert (L"run->reobj",L"../../dlls/riched20/richole.c",5717)
```

根因在 `undo.c`：`editor_insert_oleobj()` 是“先 `run_insert()` 建一个
`MERF_GRAPHICS` 的 run，再把 `ME_ReObject` 挂上去”，但撤销项里只存了
文本和 flags。撤销/重做重新 `run_insert()` 时 `reobj` 是空的，
布局阶段 `ME_GetOLEObjectSize()` / `draw_ole()` 的
`assert(run->reobj)` 就把进程 abort 了。

修法（`wine-riched20-wecom.patch`）：

- `undo.c` 回放插入时清掉 `MERF_GRAPHICS`，降级成空的占位 run；
- `richole.c` 的 `ME_GetOLEObjectSize()` / `draw_ole()` 和
  `writer.c` 的 `stream_out_graphics()` 对 `reobj == NULL` 做保护，
  算 0 尺寸 / 直接返回，不再 assert。

**注意**：`assert` 在 `WINEDEBUG=-all` 下不会打到日志，所以带着补丁版 DLL
闪退时看不到这行，别以为它不存在。

### 14.8 绕过 QIO 白名单：把剪贴板图片当“文件投放”塞进输入框

14.2 的死路是硬闯 `QueryInsertObject()`，对象插进去了，企业微信自己退出。
可行的做法是**根本不让它看到 OLE 图片对象**，改用企业微信本来就会处理的
文件投放（`IDropTarget::Drop`）。

落地过程（`dlls/riched20/editor.c`、`clipboard.c`）：

1. `paste_special()` 发现剪贴板里有 `CF_DIB` 时，先照旧尝试
   `paste_dib()`；企业微信返回 `E_INVALIDARG` 后不再强行插入。
2. 取到 `CF_DIB` 的全局内存，按 BMP 文件头重排后写到
   `%TEMP%\wecom-paste-<pid>-<n>.bmp`（超过一小时的旧文件会被清掉）。
3. 用这个路径构造一个最小的 `IDataObject`（`CF_HDROP` +
   `DROPFILES`，`clipboard.c` 里的 `ME_CreateFileDataObject()`）。
4. 从企业微信的顶层窗口上取回它自己 `RegisterDragDrop()` 时存下的 target：
   `GetPropW(hwnd, L"OleDropTargetInterface")`，然后依次调
   `DragEnter()` / `DragOver()` / `Drop()`，坐标用输入框客户区中心。

企业微信把这些文件当作用户拖进来的图片附件处理，缩略图正常出现，发送也正常。
`DragEnter` 返回 `S_OK`、`Drop` 返回 `S_OK`、`effect=1`（COPY）即成功。

**文件管理器复制图片**走的是另一条路径：剪贴板里只有 `text/uri-list`，
wine 会映射成 `CF_HDROP`。`paste_special()` 在 OLE 格式匹配之前先检查
`clipboard_first_file_path()`，扩展名是图片就直接走同一个投放函数，
这样 Thunar 里复制图片再 `Ctrl+V` 得到的是图片而不是文件路径文本。

### 14.9 粘完图片后光标和图片一样高

图片在 riched20 里是一个 `MERF_GRAPHICS` 的 run，布局时它的
`nAscent` 存的是**图片高度**（`run.c` 里按 `extent.cy` 写入），不是字体
上沿。`caret.c` 的 `ME_GetCursorCoordinates()` 原来是“光标在 run 偏移 0
时，用前一个 run 的 `nAscent + nDescent` 当光标高度”；光标刚好停在图片后面
时，前一个 run 就是图片，于是光标高度变成 95、114 这种图片尺寸，看起来
顶出了输入框。

修法（`caret.c`）：

- 往前找“尺寸参照 run”时跳过所有 `MERF_GRAPHICS`；
- 万一参照 run 本身还是图片，改用该 run 的 `style->tm.tmAscent/tmDescent`
  文字度量，不再用图片高度。

实测粘贴 300x200、300x900、900x80 三种图片后，光标高度都稳定在 15，
图片后继续输入文字也正常。

**实测**（企业微信 5.0.13.6005，5.0.13.6005-8）：

- `/tmp/xtool/pngserve` 只提供 `image/png`：`drag enter/drop` 均 `S_OK`，
  输入框出现缩略图；
- 剪贴板只有 `text/uri-list` 指向 PNG：同样出图，不再是路径文本；
- 粘贴后 `Ctrl+Z` 撤销，企业微信不再闪退（与 14.7 的修复叠加）；
- `Ctrl+A` + `Backspace` 清空输入框，进程保持运行。

**注意**：`OleDropTargetInterface` 是 wine 自己 `RegisterDragDrop()` 时挂在
窗口属性上的指针，不是公开 API；升级 wine 后如果名字变了，日志里会看到
`host has no registered drop target`，需要按新版本 wine 的 `dlls/ole32` 实现
同步改。
