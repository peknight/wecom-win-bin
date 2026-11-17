#!/usr/bin/env bash
# 企业微信（Windows 版）启动器，跑在发行版自带的 wine 11 上：
# 首次运行建 prefix、静默安装、打字体补丁。
set -euo pipefail

readonly PKGNAME='@PKGNAME@'
readonly PKGVER='@PKGVER@'
# 这个值决定默认 prefix 名（~/.local/share/wineprefixes/<APP_ID>）。
# WM_CLASS 不受它影响：上游 wine 固定取进程名 wxwork.exe，见 PITFALLS.md 第 2 条。
readonly APP_ID='@APP_ID@'
readonly BIN='@BIN@'
readonly SHARE="/usr/share/${PKGNAME}"
readonly LIBDIR="/usr/lib/${PKGNAME}"
# 补丁版 wine 的包装器：在只属于本次启动的挂载命名空间里换上 winex11，
# 系统 /usr/lib/wine 和 prefix 都不改写。见 wine-fix 仓库。
readonly WINE_BIN='/usr/bin/wine-fix-run'
readonly SETUP_EXE="${SHARE}/WeCom_${PKGVER}.exe"
readonly FONTS_REG="${SHARE}/wecom-win-fonts.reg"
readonly PROTOCOLS_REG="${SHARE}/wecom-win-protocols.reg"
readonly DPI_REG="${SHARE}/wecom-win-dpi.reg"
readonly WIN_APP_DIR='Program Files (x86)/WXWork'

message() {
	printf '%s\n' "$*" >&2
}

usage() {
	cat <<EOF
用法: ${BIN} [子命令 | 启动参数...]

不带子命令即启动企业微信，首次运行会自动建 prefix、静默安装并打好字体补丁。
其他参数原样传给 WXWork.exe，例如:
  ${BIN} --remote-debugging-port=9222 --remote-allow-origins='*'

子命令:
  setup      补装 prefix 或安装包（prefix 已存在时只补缺失部分）
  fontfix    重新应用中文字体替换（界面出现方框时用）
  dpifix     重新应用 DPI 对齐（页面变成独立小窗口 / 面板全白时用）
  prefix     打印当前使用的 WINEPREFIX 路径
  help       显示本帮助

环境变量:
  WECOM_WIN_PREFIX   指定 prefix，默认 \$XDG_DATA_HOME/wineprefixes/${APP_ID}
EOF
}

resolve_prefix() {
	if [[ -n ${WECOM_WIN_PREFIX:-} ]]; then
		printf '%s\n' "$WECOM_WIN_PREFIX"
		return
	fi
	printf '%s\n' "${XDG_DATA_HOME:-$HOME/.local/share}/wineprefixes/${APP_ID}"
}

# 界面语言跟随系统 locale。Wine 的 Windows 用户区域设置（进而
# GetUserDefaultUILanguage）取自进程 locale，优先级 LC_ALL > LC_CTYPE > LANG；
# 纯 C / C.UTF-8 会被 Wine 当成 en-US，而工具会话常带这类值，所以遇到就跳过，
# 继续看下一个变量。三个都没给出明确区域时按英文处理。
detect_locale() {
	local candidate

	for candidate in "${LC_ALL:-}" "${LC_CTYPE:-}" "${LANG:-}"; do
		case "$candidate" in
		'' | C | C.UTF-8 | POSIX)
			continue
			;;
		esac
		printf '%s\n' "$candidate"
		return 0
	done
	printf '%s\n' 'en_US.UTF-8'
}

setup_wine_env() {
	export WINEPREFIX="$1"
	# 发行版 wine 自带全套 PE 库，别让环境里残留的 WINEDLLPATH 指到别处
	unset WINEDLLPATH
	# 注意：上游 wine 不支持 WINE_WMCLASS（那是 deepin 给 winex11 加的补丁），
	# WM_CLASS 固定取进程名，也就是 wxwork.exe。桌面入口的 StartupWMClass 得跟着
	# 写 wxwork.exe；wecom-x11-fix 也是靠这个名字兜底认窗口的。
	# 界面语言跟随系统，三个变量写成同一个值，避免 LC_ALL 压过 LANG 造成
	# 从不同环境启动时语言来回变；具体取值见 detect_locale。
	local app_locale
	app_locale=$(detect_locale)
	export LANG="$app_locale"
	export LANGUAGE="${app_locale%%.*}"
	export LC_ALL="$app_locale"
	export WINEDEBUG='-all'
	# wine-fix 里只吃 winex11（输入法候选框跟随光标）：它下面三份补丁里，
	# explorer 是本包自己编的（PITFALLS.md 第 14 条），wine-fix 那份会盖掉它；
	# imm32 和 explorer 在 wine-fix 里只有 x86_64 版本，而本 prefix 跑的是 32
	# 位程序，挂了也不生效。两项显式关掉，wine-fix-status 的输出才不误导人。
	export WINE_FIX_DISABLE='explorer,imm32'
	# 融合会议 wwmapp.exe 会构造 MSVC 并发库的 _TaskCollection，wine 自带的
	# concrt140.dll 到 11.22 仍是 stub，调用即 EXCEPTION_WINE_STUB 中止，表现为
	# “加入快速会议”转圈后进程闪退。优先加载企业微信自带目录里的原生 concrt140；
	# 找不到时回退 builtin（主程序本身用不到）。
	#
	# riched20 是我们自己编的补丁版（支持把剪贴板图片粘进输入框），
	# 由 apply_wine_dlls 放进应用目录；同样留 builtin 兜底。
	#
	# explorer 也是自己编的补丁版（托盘图标闪烁时不再出现纯黑方块），
	# 由 apply_wine_dlls 放进 prefix 的 system32；同样留 builtin 兜底。
	# exe 的 override 键要带扩展名，写成 explorer=n 不生效（workbuddy-win-bin
	# 那边也是 explorer.exe=n）。
	export WINEDLLOVERRIDES='concrt140=n,b;riched20=n,b;explorer.exe=n,b'

	# 托盘图标黑底：wine 会给托盘窗口设 bounding shape，Xfce panel 合成这个
	# socket 时把 shape 圈住的区域整块填黑。这个小库在 wine 进程里拦掉那次
	# shape 设置（只在有合成器时），ShapeInput 不动，点击穿透照旧。
	local noshape="${LIBDIR}/no-shaped-systray.so"
	if [[ -f ${noshape} ]]; then
		export LD_PRELOAD="${noshape}${LD_PRELOAD:+:${LD_PRELOAD}}"
	fi
}

app_installed() {
	# 版本目录也要在：只判断 WXWork.exe 的话，包升到新版本后启动器会继续跑旧程序
	[[ -f "${WINEPREFIX}/drive_c/${WIN_APP_DIR}/WXWork.exe" ]] &&
		[[ -d "${WINEPREFIX}/drive_c/${WIN_APP_DIR}/${PKGVER}" ]]
}

apply_fonts() {
	# DirectWrite（CEF 页面）走 wine 11 自己的 Noto Sans CJK 回退链，再经替换表
	# 落到文泉驿；GDI（主界面、RichEdit 输入框）按家族名取字，而 wine 自带的
	# tahoma.ttf 没有汉字、GDI 的 Replacements 又不覆盖已存在的家族，所以
	# 必须往 prefix 里放一份带汉字的派生 Tahoma。
	local win_fonts="${WINEPREFIX}/drive_c/windows/Fonts"
	install -Dm644 "${SHARE}/fonts/tahoma.ttf" "${win_fonts}/tahoma.ttf"
	install -Dm644 "${SHARE}/fonts/tahomabd.ttf" "${win_fonts}/tahomabd.ttf"
	"${WINE_BIN}" regedit /S "${FONTS_REG}"
	message "中文字体已应用（派生 Tahoma + 替换表 -> 文泉驿微米黑）"
}

apply_protocols() {
	# 企业微信里的 WorkBuddy 入口用 ShellExecute 打开 workbuddy://，
	# wine 只内置 http/https/mailto 等少数协议，不注册就只会提示“没有下载”。
	# 5.0.13 起企业微信还会读这个 exe 的 FileVersion 判断版本，winebrowser
	# 没有版本资源会被判成“版本过低”，所以指向 workbuddy-win-bin 提供的
	# 带版本转发器，再由它交给 winebrowser 转到宿主 xdg-open。
	"${WINE_BIN}" regedit /S "${PROTOCOLS_REG}"
	message "workbuddy:// 协议已注册（版本化转发器 -> 宿主 xdg-open）"
}

apply_dpi_compat() {
	# FlutterPlugins.exe 和 WeMail.exe 都是 PER_MONITOR_AWARE，而企业微信
	# 主程序是 SYSTEM_AWARE。wine 11 的 set_parent 会拒绝跨 awareness 挂载
	# （server/window.c 的 is_dpi_awareness_compatible）：前者让 Industry News、
	# Bot 面板这些 Flutter 页面掉成桌面上的无边框独立窗口，后者让 Emails /
	# Document 面板的宿主窗口一直是空的，页面全白。
	# IFEO 在读取 exe manifest 之前生效，把这两个进程压成 SYSTEM_AWARE 即可。
	"${WINE_BIN}" regedit /S "${DPI_REG}"
	message "DPI 对齐已应用（Flutter / WeMail → SYSTEM_AWARE）"
}

apply_wine_dlls() {
	local app_dir="${WINEPREFIX}/drive_c/${WIN_APP_DIR}"
	local dll="${SHARE}/riched20.dll"
	local explorer="${LIBDIR}/explorer.exe"
	local system32="${WINEPREFIX}/drive_c/windows/system32"

	# 调试期要往 prefix 里塞实验 DLL 时跳过同步，免得一重启就被包里那份覆盖
	[[ ${WECOM_WIN_SKIP_DLL_SYNC:-0} == 1 ]] && return

	# 只在内容变了才覆盖，避免每次启动都往 prefix 里写文件
	if ! cmp -s "${dll}" "${app_dir}/riched20.dll"; then
		install -Dm644 "${dll}" "${app_dir}/riched20.dll"
		message "riched20 补丁已更新（剪贴板图片粘贴）"
	fi

	# wineboot 会在 prefix 的 system32 里放一份 builtin explorer.exe 的副本，
	# 换成打过补丁的那份。带 builtin 签名的 PE 没法按 native 加载，包里那份
	# 是重链去掉签名的，见 PKGBUILD 的 build_explorer。
	if [[ -f ${explorer} ]] && ! cmp -s "${explorer}" "${system32}/explorer.exe"; then
		# 先写临时文件再改名：正在运行的 wine 进程还映射着旧文件，原地截断
		# 会让它读到的页面变成空洞。
		install -Dm755 "${explorer}" "${system32}/explorer.exe.new"
		mv -f "${system32}/explorer.exe.new" "${system32}/explorer.exe"
		message "explorer 补丁已更新（托盘图标闪烁不再变黑方块）"
	fi
}

ensure_prefix() {
	if [[ ! -d "${WINEPREFIX}/drive_c" ]]; then
		message "初始化 wine prefix: ${WINEPREFIX}"
		mkdir -p "${WINEPREFIX}"
		# 新 prefix 会让 wineboot 弹「要不要装 wine-mono」的对话框。企业微信不碰
		# .NET，装了纯属占地，直接把 mscoree/mshtml 置空省掉这次交互。
		WINEDLLOVERRIDES='mscoree,mshtml=' "${WINE_BIN}" wineboot -u >/dev/null 2>&1 || true
	fi

	if ! app_installed; then
		message "静默安装企业微信 ${PKGVER}，约两分钟、程序本体约 1.9G"
		"${WINE_BIN}" "${SETUP_EXE}" /S || true
		if ! app_installed; then
			message "安装失败：${WINEPREFIX} 里找不到 ${WIN_APP_DIR}/${PKGVER}"
			exit 1
		fi
	fi

	apply_fonts
	apply_protocols
	apply_dpi_compat
	apply_wine_dlls
}

# Emails / Document 是从 Messages 上盖下去的独立子窗口，收起时 wine 的
# win32u 只擦被露出来的父窗口背景，不会把父窗口标脏重画，于是面板原来盖住
# 的那一整块（邮件工具栏、会话列表）都会留在屏幕上，直到别的东西触发
# WM_PAINT。这个守护进程监听面板的 SHOW/HIDE 事件，收起后等 WeCom 自己
# 停止补画，再按面板原位置把那一块标脏重画；新面板一出现就取消尚未执行
# 的重绘，避免把 Messages 盖到刚打开的面板上。跟着主窗口消失自动退出。
start_repaint_watch() {
	local exe="${SHARE}/wecom-repaint-watch.exe"

	[[ -f ${exe} ]] || return 0
	[[ ${WECOM_WIN_NO_REPAINT_WATCH:-0} == 1 ]] && return 0

	# 不带 no-shaped-systray 进这个进程，它不建托盘窗口
	if command -v setsid >/dev/null 2>&1; then
		setsid env -u LD_PRELOAD "${WINE_BIN}" "${exe}" \
			</dev/null >/dev/null 2>&1 &
	else
		env -u LD_PRELOAD "${WINE_BIN}" "${exe}" \
			</dev/null >/dev/null 2>&1 &
	fi
}

launch() {
	# Wine 把 PE 的可写数据节映射成 PAGE_WRITECOPY，而 V8 启动时的
	# FreezeFlags() 会 VirtualProtect 成只读并断言旧保护属性是 PAGE_READWRITE，
	# 断言失败即 IMMEDIATE_CRASH，renderer 起来就死，CEF 弹窗（Industry News、
	# Request for use、Approvals）因此白屏。关掉 flag 冻结可绕过，代价是少了
	# 一层 V8 自身的防御性只读保护。用户自己传了 --js-flags 就尊重用户的。
	local -a cef_args=(--js-flags=--no-freeze-flags-after-init)
	local arg
	for arg in "$@"; do
		if [[ ${arg} == --js-flags=* ]]; then
			cef_args=()
			break
		fi
	done

	start_repaint_watch

	cd "${WINEPREFIX}/drive_c/${WIN_APP_DIR}"
	exec "${WINE_BIN}" WXWork.exe "${cef_args[@]}" "$@"
}

main() {
	case "${1:-}" in
	help | -h | --help)
		usage
		exit 0
		;;
	esac

	setup_wine_env "$(resolve_prefix)"

	case "${1:-}" in
	prefix)
		printf '%s\n' "${WINEPREFIX}"
		;;
	setup)
		ensure_prefix
		;;
	fontfix)
		apply_fonts
		;;
	dpifix)
		apply_dpi_compat
		;;
	*)
		# 其余参数原样透传，调试时要靠 --remote-debugging-port 之类
		ensure_prefix
		launch "$@"
		;;
	esac
}

main "$@"
