#include "86JP.h"
#include "HookInterface.h"
#include "XLog.h"
#include "xini_file.h"

#include <intrin.h>
#include <mutex>

#pragma comment(lib, "user32.lib")

static uintptr_t dnf_base = 0;
static uintptr_t net_base = 0;
int featDebug = 0;
int featGameHost = 0;
std::string PublicIP = "127.0.0.1";

typedef ULONG(WINAPI* fnInetAddr)(PCSTR);
fnInetAddr o_InetAddr = nullptr;
ULONG WINAPI Proxy_InetAddr(PCSTR cpIp)
{

	if (strcmp(cpIp, "127.0.0.1") == 0) {
		cpIp = PublicIP.c_str();
	}
	else if (strcmp(cpIp, "127.0.0.1/") == 0) {
		cpIp = PublicIP.c_str() + '/';
	}
	printf("[Proxy_InetAddr] cpIp = %s\n", cpIp);
	o_InetAddr = reinterpret_cast<fnInetAddr>(Hook_GetTrampoline(net_base));
	return o_InetAddr(cpIp);
}

void __cdecl ProxyGameLog(int a1, wchar_t* source_path, wchar_t* function_name, int logType, wchar_t* Format, ...)
{
	wchar_t Buffer[512] = { 0 };
	wchar_t* dynamicBuffer = NULL;
	wchar_t* outputBuffer = Buffer;
	int bufferSize = _countof(Buffer);

	va_list ArgList;
	va_start(ArgList, Format);

	int result = _vswprintf_c_l(Buffer, bufferSize, Format, 0, ArgList);

	if (result < 0) {
		va_end(ArgList);
		va_start(ArgList, Format);

		int neededSize = _vscwprintf_l(Format, 0, ArgList) + 1;

		if (neededSize > 0) {
			dynamicBuffer = (wchar_t*)malloc(neededSize * sizeof(wchar_t));
			if (dynamicBuffer) {
				va_end(ArgList);
				va_start(ArgList, Format);
				_vswprintf_c_l(dynamicBuffer, neededSize, Format, 0, ArgList);
				outputBuffer = dynamicBuffer;
			}
		}
	}

	va_end(ArgList);

	if (outputBuffer) {
		AppendFileLogFormatLine(L"GameLog.log", L"[%s] [%d] [%s]", function_name, logType, outputBuffer);

		if (featDebug) {
			LogMessageW(L"[%s] [%d] [%s]", function_name, logType, outputBuffer);
		}
	}

	if (dynamicBuffer) {
		free(dynamicBuffer);
	}
}

int __fastcall Proxy_CipherEncrypt(void* This, void* NotUsed, int packet_type, char* input, int in_size, char* out_put, int* out_size)
{
	*(int*)(input - 13 + 3) = in_size + 13;

	*out_size = in_size;
	memcpy(out_put, input, in_size);
	return 1;
}

static uintptr_t g_Ptr_SendMessageW = 0;
LRESULT WINAPI Proxy_SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (Msg == 0x111 && wParam == 0x19F && lParam == 0)
		return 0;
	auto original = reinterpret_cast<decltype(&Proxy_SendMessageW)>(Hook_GetTrampoline(g_Ptr_SendMessageW));
	return original(hWnd, Msg, wParam, lParam);
}

unsigned int DelayHook(void*)
{
	do
	{
		Sleep(100);
	} while (nullptr == GetModuleHandleW(L"GameGaurd.dll"));

	Sleep(1000);
	Hook_Inline(reinterpret_cast<void*>(dnf_base + 0x01C11360), Proxy_CipherEncrypt);
	Hook_Inline(reinterpret_cast<void*>(dnf_base + 0x01CF9700), ProxyGameLog);

	if (featGameHost) {
		HMODULE hWs2 = GetModuleHandleW(L"ws2_32.dll");
		if (hWs2)
		{
			net_base = (uintptr_t)GetProcAddress(hWs2, "inet_addr");
			if (net_base)
			{
				Hook_Inline((LPVOID)net_base, Proxy_InetAddr);
			}
		}
		return 0;
	}
}

void PluginEntry()
{
	dnf_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"DNF.exe"));

	DeleteFileW(L"GameLog.log");

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DelayHook, NULL, 0, NULL);

	Hook_Inline(reinterpret_cast<void*>(dnf_base + 0x01CF9700), ProxyGameLog);
	Hook_Inline(reinterpret_cast<void*>(dnf_base + 0x01CF9800), ProxyGameLog);

	auto user32 = GetModuleHandleW(L"user32.dll");
	if (user32)
	{
		g_Ptr_SendMessageW = (uintptr_t)GetProcAddress(user32, "SendMessageW");
		Hook_Inline(reinterpret_cast<void*>(g_Ptr_SendMessageW), Proxy_SendMessageW);
	}
}

uintptr_t g_Ptr_GetStartupInfoW = 0;
VOID WINAPI Proxy_GetStartupInfoW(_Out_ LPSTARTUPINFOW lpStartupInfo)
{
	auto return_addr = (uintptr_t)_ReturnAddress();
	if (return_addr == dnf_base + 0x04AE71A5)
		PluginEntry();

	auto orifunc = reinterpret_cast<decltype(&Proxy_GetStartupInfoW)>(Hook_GetTrampoline(g_Ptr_GetStartupInfoW));
	orifunc(lpStartupInfo);
}

void LoadConfig() {
	std::string programDir = GetProgramDir();
	std::string config_file = programDir + '\\' + "86JP.ini";
	xini_file_t xini_file(config_file); // 初始化
	std::string programPath = programDir + "\\DNF.exe";

	featDebug = xini_file["系统配置"]["Debug"].try_value(0); // 0关闭 1开启
	featGameHost = xini_file["系统配置"]["PublicEnable"].try_value(0); // 0关闭 1开启
	PublicIP = (const char*)xini_file["系统配置"]["PublicIP"].try_value("127.0.0.1"); // PublicEnable 1时启用

	if (featDebug) {
		CreateLocalConsole();
	}
}

void JPEntry()
{
	dnf_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"DNF.exe"));

	auto kernel32 = GetModuleHandleW(L"kernel32.dll");
	if (kernel32)
	{
		g_Ptr_GetStartupInfoW = (uintptr_t)GetProcAddress(kernel32, "GetStartupInfoW");
		Hook_Inline(reinterpret_cast<void*>(g_Ptr_GetStartupInfoW), Proxy_GetStartupInfoW);
	}
}
