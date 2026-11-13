# CEF 弹窗白屏调查记录

日期：2026-11-12（2026-11-13 定位根因并落包，见第 11 节）
环境：Arch Linux + Xfce(xfwm4 合成器) + deepin-wine10-stable 10.14
包：`wecom-win-bin 5.0.13.6005-4`（exe 线路，prefix `~/.local/share/wineprefixes/wecom-win`）

本文是调查过程中的工作记录，结论会随证据更新。已验证的事实和推测分开写，不要混着用。

## 1. 现象

1. 企业微信主窗口、聊天正常；点开 `Industry News`、`Request for use`、
   `Approvals` 这类弹窗后，内容区是纯白，什么都不显示。
2. 有时不是纯白，而是“闪白”：切换两个弹窗的焦点时内容区整块变白，点一下鼠标又能
   恢复出一部分内容。
3. `Workspace` 有内容，说明不是所有 webview 都坏，坏的是这些独立弹窗的渲染进程。
4. 历史上（早几天）同一套 exe 是能看到内容的，所以这是退化，不是从来没通过。

## 2. 当前结论（2026-11-13 已定位并验证）

**白屏 = CEF 的 renderer 在启动阶段被 V8 的 `CHECK` 自杀，宿主不停重建 renderer，
页面自然是一块没人画的白色。** 不是网络问题、不是网页内容问题，也不是合成器问题。

精确链路：

1. Wine 把 PE 镜像里的可写数据节映射成 `PAGE_WRITECOPY`（`0x08`），而不是
   Windows 上的 `PAGE_READWRITE`（`0x04`）。
2. V8 初始化结尾默认调用 `FlagList::FreezeFlags()`（`freeze_flags_after_init`
   默认为 true）。
3. `FreezeFlags()` 调 `OS::SetDataReadOnly(&v8_flags, ...)`，其实现是：

   ```cpp
   CHECK(VirtualProtect(address, size, PAGE_READONLY, &old_protection));
   CHECK_EQ(PAGE_READWRITE, old_protection);
   ```

4. Wine 返回的旧保护属性是 `0x08`，第二条 `CHECK_EQ` 不成立，触发
   `IMMEDIATE_CRASH()`（`int3` → `STATUS_BREAKPOINT`），renderer 起来就死。

崩溃现场对齐（CEF `107.0.0-HEAD.2694+g87e0f30+chromium-107.0.5304.122`，V8 `10.7.193.22`）：

```
crash RVA    0x16a59ac      （相对 libcef.dll 镜像基址）
文件偏移      0x16a4dac      （.text VMA 0x10001000、file off 0x400，
                              RVA 0x1000 对应文件 0x400）
该处字节      cc 0f 0b cc   int3 / ud2 / int3，就是 IMMEDIATE_CRASH 桩

116a55d1  push 0x1a770000
116a55d6  call [VirtualProtect]
116a55dc  test eax,eax
116a55de  je   0x116a59a9
116a55e4  cmp  [ebp-0xd4], 4      ; 期望 PAGE_READWRITE
116a55eb  jne  0x116a59ac         ; 实际 0x08，跳进崩溃桩
```

早先根据字符串 `Multiple threads are initializating V8 in the wrong order`
推测的“V8 重复/乱序初始化”不成立：那两条字符串是同函数里相邻的另一组 CHECK，
真正失败的是紧随其后的保护属性断言。

**修复**：给 CEF 传 V8 参数 `--js-flags=--no-freeze-flags-after-init`，跳过
`FreezeFlags()`。代价只是少掉 V8 对自身 flag 存储的防御性只读保护（防的是进程内
已能写内存的攻击者），不影响页面逻辑、沙箱和其它进程。选它而不是 patch
`libcef.dll`，是因为不动腾讯的二进制，升级版本也不会失效。
`wecom-win.sh` 已默认注入；用户自己传 `--js-flags` 时不覆盖。

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

### 4.5 崩溃地址与文件偏移对齐（已修正）

早先按 `.text` VMA `0x10001000` 直接当 RVA 换算，得到 `0x6a4dac`，那里是正常
函数体（`55 89 e5 ...`），于是被误判为“地址错位”。问题出在 `.text` 的 VMA 是
**含镜像基址 `0x10000000` 的 VA**，节区 RVA 实际是 `0x1000`。

正确换算：

```
文件偏移 = RVA - 0x1000 + 0x400 = 0x16a59ac - 0xc00 = 0x16a4dac
0x16a4dac 处：cc 0f 0b cc（int3 / ud2 / int3）
```

与反汇编里 `jne 0x116a59ac` 的目标完全一致，没有错位。

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

## 6. 已排除的假设

1. **双 libcef**：`/proc/<pid>/maps` 里 renderer 只映射了
   `5.0.13.6005/compatible_web/libcef.dll`，AppData 下那份旧 cef 没有被加载。
2. **V8 平台重复 `Initialize` / 乱序初始化**：字符串是相邻 CHECK 桩，不是失败点；
   同一进程里 5 个 renderer 稳定并存，没有初始化竞争表现。
3. **网络 / 页面内容 / 代理**：同一篇 `mp.weixin.qq.com` 文章在 11-12 17:34 的
   日志里加载成功过（有页面 JS 的 console 输出），说明 CEF 本身能满足这类页面。
4. **合成器 / GPU / DPI**：关合成器、`--disable-gpu`、`--disable-direct-composition`
   都不改变 renderer 崩溃；崩溃发生在 V8 初始化，跟绘制路径无关。

## 7. 下一步计划

1. ~~对齐 crash RVA~~：已完成，见 4.5。
2. ~~确认是否重复加载 libcef~~：已排除，见第 6 节。
3. ~~参数实验~~：`--js-flags=--no-freeze-flags-after-init` 命中，见第 11 节。
4. ~~修好后落包~~：`wecom-win.sh` 注入该参数，`pkgrel 3 -> 4`，PITFALLS 补条目。
5. 剩下要用户自己确认的：在真实入口点开 `Industry News`、`Request for use`、
   `Approvals` 三个窗口，确认与 CDP 复现的结果一致。

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

## 10. 2026-11-13 09:10 崩溃与实验补丁回滚

**做了什么**：08:55 把 `compatible_web/libcef.dll` 文件偏移 `0x16a49eb` 处的 `jne`
改成 6 字节 NOP（原文件 182256176 字节，sha256
`007385e85fce0e70788dbc362792d0738f6a7f5a045858924ab4789fa58a369f`）。

**结果**：09:10 企业微信弹出报错后整个进程退出。`/tmp/wecom-patch1.log` 里：

```
wine: Unhandled page fault on read access to 00000000 at address 794FFF06 (thread 0b58), starting debugger...
```

`0x794FFF06` 落在 `ucrtbase.dll + 0x6ff06`（32 位进程），紧接着是 CEF 关闭 tab 的
日志。同一时刻 Bugly 留下的 minidump 记录的是 `WXWork.exe + 0x5a5b667`、异常码
`0xc0000005`。两份证据都指向进程内的空指针访问，不是协议注册引起的。

**排除协议因素**：重启后单独执行一次
`wine start 'workbuddy://discover'`，主进程一直存活、日志里没有 page fault。
说明单纯走 `workbuddy://` 不会崩。

**处理**：

1. 打补丁的 dll 移到 `~/wecom-crash-20261113/libcef.dll.patched-0913`
   （sha256 `4ded8297...`，留作对照）。
2. 从 `libcef.dll.orig-5.0.13.6005` 还原，sha256 与备份一致。
3. `wineserver -k` 后重启，`WXWorkWeb.exe` 的 `/proc/<pid>/maps` 已指向还原后的
   dll，启动日志无 page fault。

**结论**：NOP 补丁没验证出白屏改善，却引入了崩溃风险，先回滚。以后要再试，
必须单独立项、每次只改一处，并预留观察时间；`.orig` 备份不要删。

崩溃现场（minidump + Bugly 文本 + 崩溃时的 stderr）另存于
`~/wecom-crash-20261113/`。

## 11. 2026-11-13 09:29 参数验证与落包

### 11.1 验证启动方式

```bash
setsid nohup /usr/bin/wecom-win \
  --js-flags=--no-freeze-flags-after-init \
  --remote-debugging-port=9222 \
  >/tmp/wecom-jsflags.log 2>&1 </dev/null &
```

主进程 PID 2280095，browser 进程 2280253。5 个 renderer 的命令行里都能看到
`--js-flags=--no-freeze-flags-after-init`（browser 会把 js-flags 传给子进程）。

### 11.2 稳定性观察

- 09:29 启动，到 09:35 观察结束，renderer PID 全程稳定，没有起落重建。
- Crashpad 目录（`.../wxworkweb/User Data/Crashpad/reports`）停在 09:25 的 31 个
  dump，启动后没有新增。对照组：无参数时 renderer 每几十秒崩一次。
- CEF 日志里 09:29 之后只有 `host_resolver` / GPU DPI 之类的常规噪声，没有
  `Crash reporting enabled for process: renderer` 的反复重建。

### 11.3 真实弹窗复现（不依赖手工点击）

用 CDP 在企业微信自己的页面里触发 `window.open`，走的就是它创建 CEF 弹窗的那条
路径：

```python
# 对 群公告 页面执行
Runtime.evaluate: window.open('about:blank','_blank','width=900,height=700')
# 再把新 target Page.navigate 到日志里失败过的 Industry News 文章
https://mp.weixin.qq.com/s?...&from=industrynews&color_scheme=light#rd
```

结果：

| 检查项 | 结果 |
|--------|------|
| 新 X 窗口 | `0x04800020`，560x640，`_NET_WM_WINDOW_TYPE_NORMAL`，标题=文章标题 |
| 文章标题 | `中产因买房返贫，现在严重问题出现了`（说明 DOM 真正解析了） |
| 正文 | `document.body.innerText.length = 255`，布局尺寸 546x592 |
| X11 截图 | 309 种颜色、88% 白底、2.42% 彩色像素（微信红 `#fa5151`） |
| 文字像素 | 2.71% 深色像素，148 行连续文字行（y=42..536） |
| CDP 截图 | 558x569、81KB，与 X11 截图一致，有内容 |
| renderer | 新增 3 个 renderer 进程全部存活，dump 无新增 |

对照组（无参数时期）同一条导航在日志里是
`OnResourceLoadComplete: ... from=industrynews ...,failed, error_code:-3`，
随后 renderer 反复重建、窗口纯白。

测试完成后用 `wmctrl -ic 0x04800020` 关掉，窗口和 CDP target 都回到测试前的
3 个页面，没有残留。

### 11.4 落包

- `wecom-win.sh` 的 `launch()` 默认追加
  `--js-flags=--no-freeze-flags-after-init`；检测到用户已传 `--js-flags=*`
  时不再追加，避免覆盖用户参数。
- `PKGBUILD` 的 `pkgrel` 从 3 提到 4，`pkgver` 不变。
- `PITFALLS.md` 增加“CEF 弹窗白屏”条目。

### 11.5 已知代价

`--no-freeze-flags-after-init` 让 V8 不在初始化后把 flag 内存改成只读。这只影响
V8 自身的防御性加固，前提是攻击者已经能在进程内任意写；对功能、渲染、沙箱边界
没有影响。相比 patch `libcef.dll`（09:10 那次直接换来 unhandled page fault），
这是可逆、跟版本无关的做法。
