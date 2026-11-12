# CEF 弹窗白屏调查记录

日期：2026-11-12
环境：Arch Linux + Xfce(xfwm4 合成器) + deepin-wine10-stable 10.14
包：`wecom-win-bin 5.0.13.6005-2`（exe 线路，prefix `~/.local/share/wineprefixes/wecom-win`）

本文是调查过程中的工作记录，结论会随证据更新。已验证的事实和推测分开写，不要混着用。

## 1. 现象

1. 企业微信主窗口、聊天正常；点开 `Industry News`、`Request for use`、
   `Approvals` 这类弹窗后，内容区是纯白，什么都不显示。
2. 有时不是纯白，而是“闪白”：切换两个弹窗的焦点时内容区整块变白，点一下鼠标又能
   恢复出一部分内容。
3. `Workspace` 有内容，说明不是所有 webview 都坏，坏的是这些独立弹窗的渲染进程。
4. 历史上（早几天）同一套 exe 是能看到内容的，所以这是退化，不是从来没通过。

## 2. 当前结论

**白屏的原因是 CEF 的 renderer 进程启动即崩溃，不是网络问题、不是网页内容问题、
也不是单纯的重绘/合成器问题。**

崩溃点固定为 Chromium 自己的 `CHECK` 失败（`STATUS_BREAKPOINT`），崩在 libcef 内部
V8 初始化顺序检查上。renderer 一死，页面就是一块没人画的空白；宿主程序会不断重建
renderer，于是表现为“闪白”“点一下出来一点”“有时完全白”。

证据链见第 4 节。尚未确认的是：**为什么在 Wine 下 V8 会被乱序/重复初始化**，
这是下一步要查的点。

## 3. 时间线与操作记录

| 时间 | 操作 | 结果 |
|------|------|------|
| 早些时候 | 用 `--remote-debugging-port` 抓 DevTools、截图对比 | 确认白屏区域是 webview 内容区，主窗口正常 |
| 早些时候 | 试 `--disable-direct-composition` | 无效，renderer 仍崩 |
| 早些时候 | 完全去掉该参数 | 无效 |
| 早些时候 | 试 `--no-sandbox` | 无效 |
| 早些时候 | 试 `--disable-gpu`、去掉调试端口、关掉系统合成器 | 均无效 |
| 早些时候 | 关闭合成器做对照实验 | 副作用：企业微信窗口出现黑色毛边（见 5.1） |
| 18:06 左右 | 恢复 `xfwm4 --replace --compositor=on` | 合成器恢复，毛边消失 |
| 18:42–18:43 | 正常模式运行，Crashpad 持续产出 dump | 累计 90 个 dump，约 2.7G，全部指向 libcef 同一偏移 |
| 18:47:03 | 试 `--single-process`（把 renderer 并进主进程） | 失败：`WXWorkWeb.exe` 变成每 4 秒崩一次并重启（18:47:12/20/24/31/35 五次 `Crash reporting enabled for process: browser`） |
| 18:47:37 起 | 崩溃触发 `minidump.exe` 刷 caton dump | 用户侧表现就是“企业微信起不来/一闪就退” |
| 18:48:4x | 杀掉 single-process 测试实例，`wineserver -k` | 全部 wine 进程清空 |
| 18:48:52 | 用 `/usr/bin/wecom-win` 正常启动 | 窗口恢复（`0x0560000a`），主界面、聊天可用 |

## 4. 证据

### 4.1 Crashpad dump

目录（路径带空格）：

```
~/.local/share/wineprefixes/wecom-win/drive_c/users/pek/AppData/Local/wxworkweb/User Data/Crashpad/reports/
```

90 个 `.dmp`，每个约 32M，合计 2.7G。这些是崩溃调查的产物，不是用户数据，
确认修好之后可以清掉（先确认不再需要回溯）。

单个 dump 读出来的关键字段：

```
code=0x80000003   (STATUS_BREAKPOINT)
addr=0x697759ac
libcef.dll base=0x680d0000  size=0xafed000
=> 偏移 0x16a59ac
```

`0x80000003` 不是访问违例，是 `int3` 断点，也就是 Chromium 的
`IMMEDIATE_CRASH()`，即某个 `CHECK()` 条件不成立后主动自杀。

有出问题的 dump 标注为 `ptype=renderer`（Crashpad 注解），即崩的是渲染进程，
不是 browser 进程。

### 4.2 调用链（EBP 回溯）

```
libcef.dll+0x668170
libcef.dll+0x663183
libcef.dll+0x662f32
libcef.dll+0x661f7f
...
WXWorkWeb.exe+0x1fc478
WXWorkWeb.exe+0x181aa3
```

### 4.3 libcef 里的相关字符串

```
$ strings -t x libcef.dll | grep -i 'wrong order'
9ae9d94 Multiple threads are initializating V8 in the wrong order: expected %d got %d!
```

同一段代码里还有 `Wrong initialization order: got %d expected %d!`。
两条都是 V8 初始化顺序检查（注意原字符串把 initializing 拼成了 `initializating`，
这是上游原文，搜资料时可以直接拿这个拼写当特征）。

### 4.4 CEF 日志

```
~/.local/share/wineprefixes/wecom-win/drive_c/users/pek/AppData/Roaming/Tencent/WXWork/Log/CEF/
```

白屏期间能看到 renderer 反复启动：`Crash reporting enabled for process: renderer`
每隔几秒出现一次，说明 renderer 起来就死、宿主一直重建。

### 4.5 未完全对齐的一点

按 PE 节区表（`.text` VMA `0x10001000`、文件偏移 `0x400`）换算，RVA `0x16a59ac`
落在文件偏移 `0x6a4dac`，该处是正常函数体，不是 `int3`；附近的 `int3` 填充在
`0x6a4d9d`–`0x6a4d9f`，紧跟一个函数开头 `0x6a4da0`。

也就是说 minidump 记录的异常地址和文件里的断点字节之间存在十几字节的错位，
可能是 Wine/minidump 记录方式导致。**结论（V8 CHECK 自杀）有字符串和调用链两条
独立证据支撑，但具体到哪一条 `CHECK` 还没落到源码行**，下一步要精确对齐。

## 5. 排除项与副作用

### 5.1 黑色毛边（已解决，是测试副作用）

排查时为了排除合成器影响，临时关掉了 xfwm4 合成器，重启 xfwm4 时合成器没被接管，
企业微信窗口 ring/shadow 的透明区域被画成黑色，于是窗口四周出现黑边毛边。

恢复：

```bash
setsid xfwm4 --replace --compositor=on >/tmp/xfwm4-compositor.log 2>&1 </dev/null &
```

注意 `_NET_WM_CM_S0` 是 selection，不是 root 属性，`xprop -root _NET_WM_CM_S0`
查不到不代表没有合成器；要看 selection owner（用 `xfwm4` 是否在跑、或写个小程序
`XGetSelectionOwner`）。

恢复后截图检查窗口边缘无黑色像素，毛边消失。xfwm4 配置文件未被改写
（mtime 仍是 18:06），桌面主题/面板配置没有动过。

### 5.2 试过无效的启动参数

| 参数 | 结果 |
|------|------|
| `--disable-direct-composition` | renderer 仍崩 |
| 去掉该参数（默认） | 仍崩 |
| `--no-sandbox` | 仍崩 |
| `--disable-gpu` | 仍崩 |
| 去掉 `--remote-debugging-port` | 仍崩 |
| 关闭系统合成器 | 仍崩（且引入黑毛边） |
| `--single-process` | 更糟：整进程崩溃循环 |

### 5.3 `--single-process` 不可用

CEF 日志里出现 `Cannot use V8 Proxy resolver in single process mode.`，随后
每 4 秒一次 browser 级崩溃重启。这条路不成立，别再试。

## 6. 尚未验证的假设

1. **同一进程加载了两个不同版本的 libcef**：Program Files 下 `5.0.13.6005/compatible_web/libcef.dll`，
   AppData 下还有 `cef/5.0.82.5512`。如果两份都被加载，V8 全局状态被两套代码初始化，
   正好能解释“初始化顺序错”的 CHECK。目前进程命令行用的是 Program Files 那份，
   需要查进程模块列表确认是否只加载了一份。
2. **V8 平台被重复 `Initialize`**：Wine 下线程/TLS 行为差异（比如 `ThreadLocalStorage`
   或 `FlsAlloc` 实现）导致 V8 认为初始化乱序。
3. **宿主程序在 wine 下并发创建多个 webview**，各自触发 V8 初始化，但共享同一份
   libcef 静态状态。

## 7. 下一步计划

1. 精确对齐 crash RVA 到源码/符号：拿 `ptype=renderer` 的 dump，确认实际 `int3`
   地址（对比异常地址 ±32 字节），再用 V8 源码里那句拼错的字符串定位 `CHECK`。
2. 确认是否重复加载 libcef：看 `WXWorkWeb.exe` 进程的模块列表（wine 下可用
   `/proc/<pid>/maps` 过滤 `libcef`）。
3. 参数实验矩阵（一次只改一个）：
   `--renderer-process-limit=1`、`--disable-site-isolation-trials`、
   `--disable-features=...`、`--js-flags=--no-...`（V8 侧限制）。
4. 如果确认是双 libcef，考虑在启动器里把 AppData 那份旧 cef 目录改名/隔离（可回滚，
   先备份），再验证弹窗。
5. 修好后：改 `wecom-win.sh`（或 desktop/环境变量），`pkgrel 2 -> 3`，
   在 `PITFALLS.md` 补一条“CEF 弹窗白屏”，重新构建安装，确认三个弹窗都能出内容，
   最后提交并推 `origin` / `upstream`。

## 8. 回滚与安全边界

- 本轮调查**没有修改** `PKGBUILD`、`wecom-win.sh`、`wecom-x11-fix` 的任何文件，
  仓库工作区只有本文档新增。
- prefix 里的登录态、聊天数据未动；只产生了 CEF 缓存、日志和 Crashpad dump。
- 桌面配置（xfwm4 配置、xfce4-panel、plank 主题）未修改；合成器是运行时恢复的。
- 任何后续实验都按“先备份、可还原”的方式做：改文件前 `cp -a` 到 `/tmp` 或
  `.bak`，验证失败立刻还原。

## 9. 调查用临时脚本（在 /tmp，重启会丢）

| 脚本 | 用途 |
|------|------|
| `/tmp/mdexc.py` | 读 minidump 异常码/地址/线程 |
| `/tmp/mdmod.py` | 列模块基址，算崩溃模块偏移 |
| `/tmp/walk.py` | 按 EBP 链回溯调用栈 |
| `/tmp/stackscan.py` | 扫描崩溃线程栈里的返回地址 |
| `/tmp/strscan.py` | 把 dump 里的地址映射回 libcef 字符串 |
| `/tmp/cmowner.c` / `/tmp/cmowner` | 查 X Composite selection owner（判断合成器） |
| `/tmp/renderer-watch.sh` | 盯 renderer 进程起落，记录时间戳 |
| `/tmp/flash_probe.py` | 定时截窗口，统计白屏/闪白 |

如果这些脚本还要继续用，下一步应当把它们挪进仓库的 `tools/` 下再提交，避免重启丢失。
