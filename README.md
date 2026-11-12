# wecom-win-bin

企业微信 Windows 客户端，通过 deepin-wine10 运行。

之所以有这个包：腾讯官方的 Linux 版（`/opt/企业微信`，见 AUR 的 `wecom-bin`）
是另一条产品线，迭代落后于 Windows 版；而原有 deepin 打包的
`com.qq.weixin.work.deepin` 停留在 5.0.10。这里直接用官方 Windows 安装包，
把版本追到最新。

## 安装

```bash
makepkg -si
```

构建期从官方 exe 里抠图标，并把文泉驿微米黑派生成 Tahoma（见 `PITFALLS.md`），
所以要 `icoutils`、`python-fonttools` 和 `wqy-microhei`；下载的安装包约 660M，
最终包装出约 1.9G。

## 首次运行

直接启动 `wecom-win`（或在应用菜单里点“企业微信 (Windows)”）。首次会：

1. 在 `$XDG_DATA_HOME/wineprefixes/wecom-win` 建 wine prefix
2. 用 NSIS `/S` 静默安装，约两分钟
3. 打字体补丁，然后启动

装完直接是登录界面。登录态保存在 prefix 里，和系统包升级无关，`pacman -Syu`
之后不用重新登录。

如果之前手工调试过、prefix 在 `~/.local/share/wineprefixes/wecom-win-5.0.13`，
启动器会优先复用它，不会新建一个 prefix 让你再登一次。

prefix 会在几年里长到 3~4G（程序本体约 1.9G，其余是 CEF 缓存和日志）。包升级
不动 prefix；`pkgver` 变了启动器会就地跑一次新版本安装包。

## 子命令

| 命令 | 用途 |
|---|---|
| `wecom-win` | 启动；首次运行会先装 |
| `wecom-win setup` | 只补装缺失的 prefix / 程序 |
| `wecom-win fontfix` | 重打字体补丁（界面出现方框时用） |
| `wecom-win prefix` | 打印当前使用的 `WINEPREFIX` |

其余参数原样传给 `WXWork.exe`，调试内嵌页面时可以这样开远程调试端口：

```bash
wecom-win --remote-debugging-port=9222 --remote-allow-origins='*'
```

| 环境变量 | 说明 |
|---|---|
| `WECOM_WIN_PREFIX` | 指定 prefix 路径，覆盖默认位置 |

## 可选的窗口修复

建议同时装 `wecom-x11-fix`：它解决 deepin-wine 窗口按钮穿透点击、以及拖动/缩放
时那条白边残留的问题（需要 1.0.0-2 或更新，它会同时认 `wecom-win` 和 deepin deb
线路的 class）。修复程序靠窗口的 `WM_CLASS` 认人，启动器已经导出了
`WINE_WMCLASS=wecom-win`，不需要额外配置。
（`wecom-win.desktop` 里的 `StartupWMClass` 用的是同一个值，但那只是为了任务栏
和 dock 能把窗口归到这个条目上，跟修复无关。）

## 依赖关系

本包依赖 `com.qq.weixin.work.deepin`，但只用它提供的那套 deepin 补丁 dll
（`WINEPREDLL`，含打过补丁的 ntdll/winex11.drv/user32 等）。这是 Windows 版能
在这套 wine 上跑起来的前提。它的 wine 本体和程序文件都用不上。

注意：AUR 的 `wecom-bin`（腾讯 Linux 原生版）声明了
`conflicts=('com.qq.weixin.work.deepin')`，两者不能共存。想装本包就得先卸掉
`wecom-bin`。

## 已知问题

`Industry News` 之类的内嵌资讯窗口在 deepin-wine 下可能是空白的。Windows/macOS
上正常，属于 wine 侧渲染差异，本包没有解决。

官方 Windows 包里带一套 OpenGL 能力探测（`win32-test.exe` /
`win32_voip_test.exe`）和兆芯 GLX 兜底逻辑，本包没有照搬，因为没有证据表明
5.0.13 需要它。真遇到花屏类问题再回头补。

## 卸载

```bash
pacman -R wecom-win-bin
rm -r ~/.local/share/wineprefixes/wecom-win    # 连同聊天记录、登录态一起删
```

prefix 不在包里，卸载不会自动清。
