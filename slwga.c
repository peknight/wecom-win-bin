/*
 * 极简 SLWGA.dll 存根。
 *
 * 融合会议（wwmapp.exe）的 wemeet_platform.dll 从 SLWGA.dll 导入
 * SLIsGenuineLocal，用返回值判断当前 Windows 是不是正版（软件许可校验）。
 * wine 至今没有实现 slwga，deepin-wine 当年自带一份假 DLL，所以 deb 线路
 * 能进会议，换成发行版 wine 后整条 WeMeet 模块链以 c0000135 连锁失败：
 * wemeet_platform -> wemeet_module_api -> qt_util -> ... -> wemeet.dll，
 * 表现就是"加入快速会议"转完圈没反应。
 *
 * 这里只做一件事：声明系统是正版（SL_GENUINE_STATE = 0）并返回 S_OK。
 * 企业微信不靠它做功能限制，它只是启动时的一道环境检查。
 */
#include <windows.h>

typedef UINT SL_GENUINE_STATE;

#define SL_GENUINE 0

HRESULT WINAPI SLIsGenuineLocal(const GUID *pGuid, SL_GENUINE_STATE *pState, void *pOptions)
{
	(void)pGuid;
	(void)pOptions;

	if (pState) {
		*pState = SL_GENUINE;
	}
	return S_OK;
}
