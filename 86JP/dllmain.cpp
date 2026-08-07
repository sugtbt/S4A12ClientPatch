#include <windows.h>
#include "86JP.h"

#define EXPAPI(Name) EXTERN_C __declspec(dllexport) void Name() {}

EXPAPI(A)

EXTERN_C __declspec(dllexport) BOOL ClientPatchPluginInit()
{
	// ijl15_jp.dll 原先仅通过导入 A 强制加载本模块，并在 DllMain 中启动初始化。
	// 现在由 GameGaurd 在代码解压后调用显式入口，避免插件自行承担加载时序。
	LoadConfig();
	PluginEntry();
	return TRUE;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		break;
	}
	return TRUE;
}
