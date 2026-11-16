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
readonly WINE_BIN='/usr/bin/wine'
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

setup_wine_env() {
	export WINEPREFIX="$1"
	# 发行版 wine 自带全套 PE 库，别让环境里残留的 WINEDLLPATH 指到别处
	unset WINEDLLPATH
	# 注意：上游 wine 不支持 WINE_WMCLASS（那是 deepin 给 winex11 加的补丁），
	# WM_CLASS 固定取进程名，也就是 wxwork.exe。桌面入口的 StartupWMClass 得跟着
	# 写 wxwork.exe；wecom-x11-fix 也是靠这个名字兜底认窗口的。
	export LANG='zh_CN.UTF-8'
	export LANGUAGE='zh_CN'
	export WINEDEBUG='-all'
	# 融合会议 wwmapp.exe 会构造 MSVC 并发库的 _TaskCollection，wine 自带的
	# concrt140.dll 到 11.22 仍是 stub，调用即 EXCEPTION_WINE_STUB 中止，表现为
	# “加入快速会议”转圈后进程闪退。优先加载企业微信自带目录里的原生 concrt140；
	# 找不到时回退 builtin（主程序本身用不到）。
	#
	# riched20 是我们自己编的补丁版（支持把剪贴板图片粘进输入框），
	# 由 apply_wine_dlls 放进应用目录；同样留 builtin 兜底。
	export WINEDLLOVERRIDES='concrt140=n,b;riched20=n,b'

	# 托盘图标黑底：wine 会给托盘窗口设 bounding shape，Xfce panel 合成这个
	# socket 时把 shape 圈住的区域整块填黑。这个小库在 wine 进程里拦掉那次
	# shape 设置（只在有合成器时），ShapeInput 不动，点击穿透照旧。
	local noshape="/usr/lib/${PKGNAME}/no-shaped-systray.so"
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
	# 指向 winebrowser 后由它转发给宿主 xdg-open，落到系统的 WorkBuddy。
	"${WINE_BIN}" regedit /S "${PROTOCOLS_REG}"
	message "workbuddy:// 协议已注册（转发到宿主 xdg-open）"
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

	# 调试期要往 prefix 里塞实验 DLL 时跳过同步，免得一重启就被包里那份覆盖
	[[ ${WECOM_WIN_SKIP_DLL_SYNC:-0} == 1 ]] && return

	# 只在内容变了才覆盖，避免每次启动都往 prefix 里写文件
	if ! cmp -s "${dll}" "${app_dir}/riched20.dll"; then
		install -Dm644 "${dll}" "${app_dir}/riched20.dll"
		message "riched20 补丁已更新（剪贴板图片粘贴）"
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
