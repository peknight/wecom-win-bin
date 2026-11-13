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

构建期从官方 exe 里抠图标、把文泉驿微米黑派生成 Tahoma（见 `PITFALLS.md`），
再从对应版本的 wine 源码里编一份打过补丁的 32 位 `riched20.dll`（只编这一个
DLL，二十来秒），所以要 `icoutils`、`python-fonttools`、`wqy-microhei` 和
`mingw-w64-gcc`；下载的安装包约 660M，最终包装出约 1.9G。

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

只依赖 `deepin-wine10-stable`。早期版本借过 `com.qq.weixin.work.deepin` 里那套
deepin 补丁 dll（`WINEPREDLL`），实测默认 prefix 不需要，所以 1.0.0-2 起已经不
依赖旧包，装本包不必留着 deepin deb 版企业微信。

注意：AUR 的 `wecom-bin`（腾讯 Linux 原生版）声明了
`conflicts=('com.qq.weixin.work.deepin')`，两者不能共存。想装本包就得先卸掉
`wecom-bin`。

## 已经处理掉的坑

启动器里现在带了几处运行期补丁，细节都在 `PITFALLS.md`：

- 输入框汉字方框：把文泉驿微米黑派生成 Tahoma（`fontfix` 可重打）
- `Industry News` / `Request for use` 等 CEF 弹窗白屏：启动时传
  `--js-flags=--no-freeze-flags-after-init`
- 输入框右键没有菜单：补丁版 `riched20.dll` 在宿主不提供
  `ITextHost2` / `IRichEditOleCallback` 时自己弹默认菜单（14.4）
- 撤销刚插入的图片会闪退：补丁版 `riched20.dll` 不再对空 `reobj` 断言（14.7）
- 剪贴板图片粘不进输入框：企业微信拒收标准图片 OLE 对象，补丁版
  `riched20.dll` 改为把图片临时存成 BMP 后走宿主的文件投放链路（14.8）。
  截图工具复制、文件管理器复制图片文件两种方式都支持。

## 已知问题

- 16 位色深的 PNG 剪贴板粘不进去：企业微信依赖的 wine 驱动转 `CF_DIB` 失败，
  包内没法补，日常截图工具存的都是 8 位 PNG，详见 `PITFALLS.md` 14.1。

官方 Windows 包里带一套 OpenGL 能力探测（`win32-test.exe` /
`win32_voip_test.exe`）和兆芯 GLX 兜底逻辑，本包没有照搬，因为没有证据表明
5.0.13 需要它。真遇到花屏类问题再回头补。

## 卸载

```bash
pacman -R wecom-win-bin
rm -r ~/.local/share/wineprefixes/wecom-win    # 连同聊天记录、登录态一起删
```

prefix 不在包里，卸载不会自动清。
