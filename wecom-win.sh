#!/usr/bin/env bash
# 企业微信（Windows 版）启动器：首次运行建 prefix、静默安装、打字体补丁。
set -euo pipefail

readonly PKGNAME='@PKGNAME@'
readonly PKGVER='@PKGVER@'
readonly SHARE="/usr/share/${PKGNAME}"
readonly WINE_ROOT='/opt/deepin-wine10-stable'
readonly WINE_BIN="${WINE_ROOT}/bin/wine"
readonly SETUP_EXE="${SHARE}/WeCom_${PKGVER}.exe"
readonly FONTS_REG="${SHARE}/wecom-win-fonts.reg"
readonly PROTOCOLS_REG="${SHARE}/wecom-win-protocols.reg"
readonly WIN_APP_DIR='Program Files (x86)/WXWork'

# 手工调试期建过的 prefix 也认，避免装完包装个新的又触发一次登录
readonly -a LEGACY_PREFIX_NAMES=('wecom-win-5.0.13' 'wecom-win')

message() {
	printf '%s\n' "$*" >&2
}

usage() {
	cat <<'EOF'
用法: wecom-win [子命令 | 启动参数...]

不带子命令即启动企业微信，首次运行会自动建 prefix、静默安装并打好字体补丁。
其他参数原样传给 WXWork.exe，例如:
  wecom-win --remote-debugging-port=9222 --remote-allow-origins='*'

子命令:
  setup      补装 prefix 或安装包（prefix 已存在时只补缺失部分）
  fontfix    重新应用 Tahoma 中文字体与字体替换（界面出现方框时用）
  prefix     打印当前使用的 WINEPREFIX 路径
  help       显示本帮助

环境变量:
  WECOM_WIN_PREFIX   指定 prefix，默认 $XDG_DATA_HOME/wineprefixes/wecom-win
EOF
}

resolve_prefix() {
	if [[ -n ${WECOM_WIN_PREFIX:-} ]]; then
		printf '%s\n' "$WECOM_WIN_PREFIX"
		return
	fi

	local base="${XDG_DATA_HOME:-$HOME/.local/share}/wineprefixes"
	local name
	for name in "${LEGACY_PREFIX_NAMES[@]}"; do
		if [[ -d "${base}/${name}/drive_c" ]]; then
			printf '%s\n' "${base}/${name}"
			return
		fi
	done
	printf '%s\n' "${base}/wecom-win"
}

setup_wine_env() {
	export WINEPREFIX="$1"
	export WINEDLLPATH="${WINE_ROOT}/lib:${WINE_ROOT}/lib64"
	# 窗口的 WM_CLASS。刻意不用 com.qq.weixin.work.deepin：那是 deepin deb 那条
	# 线路的标识，两条线路的窗口要能分开看。wecom-x11-fix 1.0.0-2 起同时认这两个值。
	export WINE_WMCLASS='wecom-win'
	export LANG='zh_CN.UTF-8'
	export LANGUAGE='zh_CN'
	export WINEDEBUG='-all'
	# 融合会议 wwmapp.exe 会构造 MSVC 并发库的 _TaskCollection，Wine 自带的
	# concrt140.dll 只是个 stub，调用即 EXCEPTION_WINE_STUB 中止，表现为
	# “加入快速会议”转圈后进程闪退。优先加载程序目录里的原生 concrt140；
	# 别的进程找不到原生副本时仍回退 builtin，避免影响主程序启动。
	#
	# riched20 是我们自己编的补丁版（支持把剪贴板图片粘进输入框），
	# 由 apply_wine_dlls 放进应用目录；同样留 builtin 兜底。
	export WINEDLLOVERRIDES='concrt140=n,b;riched20=n,b'
}

app_installed() {
	# 版本目录也要在：只判断 WXWork.exe 的话，包升到新版本后启动器会继续跑旧程序
	[[ -f "${WINEPREFIX}/drive_c/${WIN_APP_DIR}/WXWork.exe" ]] &&
		[[ -d "${WINEPREFIX}/drive_c/${WIN_APP_DIR}/${PKGVER}" ]]
}

apply_fonts() {
	local win_fonts="${WINEPREFIX}/drive_c/windows/Fonts"
	install -Dm644 "${SHARE}/fonts/tahoma.ttf" "${win_fonts}/tahoma.ttf"
	install -Dm644 "${SHARE}/fonts/tahomabd.ttf" "${win_fonts}/tahomabd.ttf"
	"${WINE_BIN}" regedit /S "${FONTS_REG}"
	message "字体补丁已应用（Tahoma -> 文泉驿微米黑）"
}

apply_protocols() {
	# 企业微信里的 WorkBuddy 入口用 ShellExecute 打开 workbuddy://，
	# Wine 只内置 http/https/mailto 等少数协议，不注册就只会提示“没有下载”。
	# 指向 winebrowser 后由它转发给宿主 xdg-open，落到系统的 WorkBuddy。
	"${WINE_BIN}" regedit /S "${PROTOCOLS_REG}"
	message "workbuddy:// 协议已注册（转发到宿主 xdg-open）"
}

apply_wine_dlls() {
	local app_dir="${WINEPREFIX}/drive_c/${WIN_APP_DIR}"
	local dll="${SHARE}/riched20.dll"

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
		"${WINE_BIN}" wineboot -u >/dev/null 2>&1 || true
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
	*)
		# 其余参数原样透传，调试时要靠 --remote-debugging-port 之类
		ensure_prefix
		launch "$@"
		;;
	esac
}

main "$@"
