# wecom-win-bin

企业微信 Windows 客户端（腾讯官方 exe），跑在发行版自带的 wine 11 上。

之所以有这个包：腾讯官方的 Linux 版（`/opt/企业微信`，见 AUR 的 `wecom-bin`）
是另一条产品线，迭代落后于 Windows 版；而原有 deepin 打包的
`com.qq.weixin.work.deepin` 停在 5.0.10、还绑着 deepin-wine10。这里直接用官方
Windows 安装包，把版本追到最新，运行库用发行版 wine，不再依赖 deepin 的闭源
wine 分支。

## 历史：这个包以前是 deepin-wine10 版

`5.0.13.6005-9` 及以前，本包依赖 `deepin-wine10-stable`。`-10` 起换成发行版
wine 11，包名、prefix、可执行文件都是 `wecom-win-bin` / `wecom-win`，所以从
git 上看就是一次运行库升级：

```bash
git show 1edcdd6:PKGBUILD        # 最后一版 deepin-wine10 的实现
git show 1edcdd6:PITFALLS.md     # 那条线路的完整踩坑记录
```

中间还有一段过渡：wine 11 先在独立的 `wecom-win11-bin` 包名（prefix
`wecom-win11`）上验证，确认可用后并回本仓库。本包 `conflicts`/`replaces` 了
`wecom-win11-bin`，`pacman -Su` 会直接把过渡包换掉；prefix 需要手动改名，
见 `PITFALLS.md` 第 7 条。

## 安装

```bash
makepkg -si
```

构建期从官方 exe 里抠图标，再从对应版本的 wine 源码里编一份打过补丁的 32 位
`riched20.dll`（只编这一个 DLL，半分钟），所以要 `icoutils`、`mingw-w64-gcc`、
`flex`、`bison`；下载的安装包约 660M，最终包装出约 1.9G。

## 首次运行

直接启动 `wecom-win`（或在应用菜单里点“企业微信”）。首次会：

1. 在 `$XDG_DATA_HOME/wineprefixes/wecom-win` 建 wine prefix
2. 用 NSIS `/S` 静默安装，约两分钟
3. 打字体替换、注册 `workbuddy://`、对齐 Flutter 的 DPI、铺补丁 riched20
4. 启动

装完直接是登录界面。登录态保存在 prefix 里，和系统包升级无关，`pacman -Syu`
之后不用重新登录。

prefix 会在几年里长到 3~4G（程序本体约 1.9G，其余是 CEF 缓存和日志）。包升级
不动 prefix；`pkgver` 变了启动器会就地跑一次新版本安装包。

## 子命令

| 命令 | 用途 |
|---|---|
| `wecom-win` | 启动；首次运行会先装 |
| `wecom-win setup` | 只补装缺失的 prefix / 程序 |
| `wecom-win fontfix` | 重打字体替换表（界面出现方框时用） |
| `wecom-win dpifix` | 重对齐 Flutter DPI（页面变成独立小窗口时用） |
| `wecom-win prefix` | 打印当前使用的 `WINEPREFIX` |

其余参数原样传给 `WXWork.exe`，调试内嵌页面时可以这样开远程调试端口：

```bash
wecom-win --remote-debugging-port=9222 --remote-allow-origins='*'
```

| 环境变量 | 说明 |
|---|---|
| `WECOM_WIN_PREFIX` | 指定 prefix 路径，覆盖默认位置 |
| `WECOM_WIN_SKIP_DLL_SYNC` | 设为 `1` 时跳过包内 `riched20.dll` 同步，调试实验 DLL 用 |

## 可选的窗口修复

建议同时装 `wecom-x11-fix`：它解决窗口按钮穿透点击、拖动/缩放时的白边残留，
以及弹窗盖住主窗口时 `- 口 X` / `...` 仍然透出来的问题。修复程序靠窗口的
`WM_CLASS` 和进程名认人，`wxwork.exe` 在它的白名单里，不需要额外配置。

## 依赖关系

只依赖发行版 `wine`（>= 11.21）和字体、图标这类基础件。不再依赖
`deepin-wine10-stable`、`spark-dwine-helper` 或 `com.qq.weixin.work.deepin`。

## 已经处理掉的坑

启动器里带了几处运行期补丁，细节在 `PITFALLS.md` 和 `PITFALLS-common.md`：

- 输入框汉字方框：写字体替换表，字形来自系统文泉驿微米黑（`PITFALLS.md` 第 9 条）
- `Industry News` / `Request for use` 等 CEF 弹窗白屏：启动时传
  `--js-flags=--no-freeze-flags-after-init`（`PITFALLS-common.md` 第 12 条）
- Flutter 页面（Industry News / Bot 面板）掉出主窗口：IFEO 覆盖 DPI 感知
  （`PITFALLS.md` 第 8 条）
- 点 WorkBuddy 提示“没有下载”：注册 `workbuddy://` 协议
  （`PITFALLS-common.md` 第 11 条）
- 加入快速会议转圈后闪退：优先原生 `concrt140`（`PITFALLS-common.md` 第 13 条）
- 输入框右键没有菜单、撤销插入图片闪退、剪贴板图片粘不进输入框、粘完光标
  顶出输入框：补丁版 `riched20.dll`（`PITFALLS-common.md` 第 14 条）
- 托盘图标黑底加边框：`LD_PRELOAD` 拦掉 wine 给托盘窗口设的 bounding shape
  （`PITFALLS.md` 第 11 条）

## 已知问题

- 剪贴板里只提供 `image/png` 时粘不进输入框（截图工具直粘）。这层转换原来由
  deepin 的 `winex11.so` 做，上游 wine 不做，包内还没补，详见 `PITFALLS.md`
  第 6 条。文件管理器里复制图片（`text/uri-list`）和拖拽都正常。
- `WM_CLASS` 只能是 `wxwork.exe`：上游 wine 不支持 `WINE_WMCLASS`，除非自己维护
  一份打过补丁的 wine。桌面入口的 `StartupWMClass` 已按实际值写。

官方 Windows 包里带一套 OpenGL 能力探测（`win32-test.exe` /
`win32_voip_test.exe`）和兆芯 GLX 兜底逻辑，本包没有照搬，因为没有证据表明
5.0.13 需要它。真遇到花屏类问题再回头补。

## 卸载

```bash
pacman -R wecom-win-bin
rm -r ~/.local/share/wineprefixes/wecom-win    # 连同聊天记录、登录态一起删
```

prefix 不在包里，卸载不会自动清。
