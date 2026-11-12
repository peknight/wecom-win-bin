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
