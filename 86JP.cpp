#include "86JP.h"
#include "HookInterface.h"
#include "XLog.h"
#include "xini_file.h"

#include <intrin.h>
#include <mutex>

#pragma comment(lib, "user32.lib")

static uintptr_t dnf_base = 0;
static uintptr_t g_Ptr_inet_addr = 0;
int featDebug = 0;
int featGameHost = 0;
std::string PublicIP = "127.0.0.1";

typedef ULONG(WINAPI* fnInetAddr)(PCSTR);
fnInetAddr o_InetAddr = nullptr;
ULONG WINAPI Proxy_InetAddr(PCSTR cpIp)
{
	std::string targetIP;
	if (strcmp(cpIp, "127.0.0.1") == 0) {
		targetIP = PublicIP;
	}
	else if (strcmp(cpIp, "127.0.0.1/") == 0) {
		targetIP = PublicIP + "/";
	}
	else {
		targetIP = cpIp;
	}

	if (featDebug) {
		printf("[Proxy_InetAddr] %s -> %s\n", cpIp, targetIP.c_str());
	}
	return o_InetAddr ? o_InetAddr(targetIP.c_str()) : (ULONG)-1;
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

static uintptr_t g_Ptr_CharacterNameFilter = 0;
static uintptr_t g_Ptr_LegacyEditUpdate = 0;
static uintptr_t g_Ptr_CheckDuplicateNameResult = 0;
using CharacterNameFilterFn = void(__thiscall*)(void* self, void* insertion, const wchar_t* existingText, int insertionIndex);
using LegacyEditUpdateFn = void(__thiscall*)(void* self);
using CheckDuplicateNameResultFn = void(__cdecl*)(int command, int result, int errorCode);
static thread_local int g_CharacterNameEditDepth = 0;

struct ClientWideString
{
	unsigned int reserved;
	union
	{
		wchar_t inlineText[8];
		wchar_t* heapText;
	} storage;
	unsigned int length;
	unsigned int capacity;
};
static_assert(offsetof(ClientWideString, length) == 0x14, "Unexpected client wide-string length offset");
static_assert(offsetof(ClientWideString, capacity) == 0x18, "Unexpected client wide-string capacity offset");

static const wchar_t kDuplicateNameNotice[] =
	L"\u89D2\u8272\u540D\u5DF2\u5B58\u5728\uFF0C\u8BF7\u91CD\u65B0\u8F93\u5165\u3002";

void __cdecl Proxy_CheckDuplicateNameResult(int command, int result, int errorCode)
{
	auto original = reinterpret_cast<CheckDuplicateNameResultFn>(
		Hook_GetTrampoline(g_Ptr_CheckDuplicateNameResult));
	original(command, result, errorCode);

	// CMD 0x02B5 returns [00 00] when the name is already in use.
	if ((result & 0xFF) == 0 && (errorCode & 0xFF) == 0)
	{
		using ShowNoticeFn = void(__thiscall*)(void* manager, int noticeId, const wchar_t* text, int flags);
		auto showNotice = reinterpret_cast<ShowNoticeFn>(dnf_base + 0x0189CFB0);
		void* manager = *reinterpret_cast<void**>(dnf_base + 0x02C91F7C);
		if (manager != NULL)
			showNotice(manager, 0x1BB, kDuplicateNameNotice, 0);
	}
}

static bool IsCharacterCreationNameEdit(void* self)
{
	const auto fields = reinterpret_cast<const unsigned int*>(self);
	return fields[0] == dnf_base + 0x028F6CFC &&
		fields[0x360 / 4] == 12 &&
		fields[0x364 / 4] == 0x87;
}

void __fastcall Proxy_LegacyEditUpdate(void* self, void* /*edx*/)
{
	const bool isCharacterNameEdit = IsCharacterCreationNameEdit(self);
	if (isCharacterNameEdit)
		++g_CharacterNameEditDepth;

	auto original = reinterpret_cast<LegacyEditUpdateFn>(
		Hook_GetTrampoline(g_Ptr_LegacyEditUpdate));
	original(self);

	if (isCharacterNameEdit)
		--g_CharacterNameEditDepth;
}

static unsigned int GetCharacterNameWidth(const wchar_t* text, unsigned int length)
{
	unsigned int width = 0;
	for (unsigned int i = 0; text != NULL && i < length; ++i)
		width += text[i] <= 0x7F ? 1 : 2;
	return width;
}

void __fastcall Proxy_CharacterNameFilter(
	void* self,
	void* /*edx*/,
	void* insertion,
	const wchar_t* existingText,
	int insertionIndex)
{
	auto original = reinterpret_cast<CharacterNameFilterFn>(
		Hook_GetTrampoline(g_Ptr_CharacterNameFilter));
	const auto configuredLimit = reinterpret_cast<const unsigned int*>(
		reinterpret_cast<const unsigned char*>(self) + 4);
	if (g_CharacterNameEditDepth <= 0 || *configuredLimit != 12 || insertion == NULL)
	{
		original(self, insertion, existingText, insertionIndex);
		return;
	}

	auto pending = reinterpret_cast<ClientWideString*>(insertion);
	wchar_t* pendingText = pending->capacity >= 8
		? pending->storage.heapText
		: pending->storage.inlineText;
	const unsigned int existingLength = existingText == NULL
		? 0
		: static_cast<unsigned int>(wcslen(existingText));
	const unsigned int existingWidth = GetCharacterNameWidth(existingText, existingLength);
	const unsigned int remainingWidth = existingWidth >= *configuredLimit
		? 0
		: *configuredLimit - existingWidth;

	unsigned int prefixLength = 0;
	unsigned int prefixWidth = 0;
	while (prefixLength < pending->length)
	{
		const unsigned int nextWidth = pendingText[prefixLength] <= 0x7F ? 1 : 2;
		if (prefixWidth + nextWidth > remainingWidth)
			break;
		prefixWidth += nextWidth;
		++prefixLength;
	}

	if (prefixLength < pending->length)
	{
		pendingText[prefixLength] = L'\0';
		pending->length = prefixLength;
	}
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
			g_Ptr_inet_addr = (uintptr_t)GetProcAddress(hWs2, "inet_addr");
			if (g_Ptr_inet_addr && Hook_Inline((LPVOID)g_Ptr_inet_addr, Proxy_InetAddr))
			{
				o_InetAddr = reinterpret_cast<fnInetAddr>(Hook_GetTrampoline(g_Ptr_inet_addr));
			}
		}
	}

	return 0;
}

void PluginEntry()
{
	dnf_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"DNF.exe"));

	DeleteFileW(L"GameLog.log");

	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)DelayHook, NULL, 0, NULL);

	Hook_Inline(reinterpret_cast<void*>(dnf_base + 0x01CF9700), ProxyGameLog);
	g_Ptr_LegacyEditUpdate = dnf_base + 0x01D1D190;
	Hook_Inline(reinterpret_cast<void*>(g_Ptr_LegacyEditUpdate), Proxy_LegacyEditUpdate);
	g_Ptr_CharacterNameFilter = dnf_base + 0x01DB3800;
	Hook_Inline(reinterpret_cast<void*>(g_Ptr_CharacterNameFilter), Proxy_CharacterNameFilter);
	g_Ptr_CheckDuplicateNameResult = dnf_base + 0x008C5E30;
	Hook_Inline(reinterpret_cast<void*>(g_Ptr_CheckDuplicateNameResult), Proxy_CheckDuplicateNameResult);
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
	std::string config_file = programDir + "\\86JP.ini";
	xini_file_t xini_file(config_file);

	featDebug = xini_file["SystemConfig"]["Debug"].try_value(0); // 0 Disable  1 Enable
	featGameHost = xini_file["SystemConfig"]["PublicEnable"].try_value(0); // 0 Disable  1 Enable
	PublicIP = xini_file["SystemConfig"]["PublicIP"].try_value("127.0.0.1"); // PublicEnable=1 Enable

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
