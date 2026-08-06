#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t kIniName[] = L"ijl15.ini";
constexpr char kDefaultIni[] =
    "; ClientPatch IJL 代理加载器\r\n"
    "; 路径默认相对于 ijl15.dll 所在目录，也可以使用绝对路径。\r\n"
    "[Loader]\r\n"
    "Enabled=1\r\n"
    "Debug=0\r\n"
    "PluginCount=1\r\n"
    "WaitTimeoutMs=15000\r\n"
    "\r\n"
    "[Plugins]\r\n"
    "Plugin0=86JP.dll\r\n"
    "Plugin0Stage=Early\r\n";

struct PluginSpec
{
    std::wstring path;
    std::wstring stage;
};

std::wstring g_moduleDir;
bool g_debug = false;

void Log(const wchar_t* format, ...)
{
    wchar_t message[1024] = {};
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, args);
    va_end(args);

    FILE* file = nullptr;
    const std::wstring logPath = g_moduleDir + L"\\ijl15.log";
    _wfopen_s(&file, logPath.c_str(), L"a, ccs=UTF-8");
    if (file)
    {
        fwprintf(file, L"%s\n", message);
        fclose(file);
    }
    if (g_debug)
        OutputDebugStringW(message);
}

std::wstring ModuleDirectory(HMODULE module)
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(module, path, _countof(path));
    if (!length || length >= _countof(path))
        return L".";

    std::wstring result(path, length);
    const size_t slash = result.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : result.substr(0, slash);
}

bool IsAbsolutePath(const std::wstring& path)
{
    return (path.size() >= 2 && path[1] == L':') ||
        (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') ||
        (!path.empty() && (path[0] == L'\\' || path[0] == L'/'));
}

std::wstring PluginPath(const std::wstring& configuredPath)
{
    if (IsAbsolutePath(configuredPath))
        return configuredPath;
    return g_moduleDir + L"\\" + configuredPath;
}

std::wstring FileName(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool EnsureDefaultConfig(const std::wstring& path)
{
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return true;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    const DWORD bytes = static_cast<DWORD>(strlen(kDefaultIni));
    const bool ok = WriteFile(file, kDefaultIni, bytes, &written, nullptr) && written == bytes;
    CloseHandle(file);
    return ok;
}

std::wstring ReadString(const std::wstring& ini, const wchar_t* section,
    const std::wstring& key, const wchar_t* fallback = L"")
{
    wchar_t value[1024] = {};
    GetPrivateProfileStringW(section, key.c_str(), fallback, value,
        _countof(value), ini.c_str());
    return value;
}

bool IsAfterGameGuard(const std::wstring& stage)
{
    return _wcsicmp(stage.c_str(), L"AfterGameGaurd") == 0 ||
        _wcsicmp(stage.c_str(), L"AfterGameGuard") == 0;
}

bool WaitForGameGuard(DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    while (!GetModuleHandleW(L"GameGaurd.dll"))
    {
        if (GetTickCount() - start >= timeoutMs)
        {
            Log(L"[加载器] 在 %lu 毫秒内未检测到 GameGaurd.dll", timeoutMs);
            return false;
        }
        Sleep(50);
    }
    return true;
}

void LoadPlugin(const PluginSpec& plugin)
{
    const std::wstring path = PluginPath(plugin.path);
    const std::wstring fileName = FileName(path);
    if (GetModuleHandleW(fileName.c_str()))
    {
        Log(L"[加载器] 复用已加载模块 %s（%s）", fileName.c_str(), plugin.stage.c_str());
        return;
    }

    HMODULE module = LoadLibraryW(path.c_str());
    if (!module)
    {
        Log(L"[加载器] 加载失败 %s（错误码 %lu）", path.c_str(), GetLastError());
        return;
    }
    Log(L"[加载器] 已加载 %s（%s）", path.c_str(), plugin.stage.c_str());
}

void LoadConfiguredStage(HMODULE module, bool afterGameGuardStage)
{
    g_moduleDir = ModuleDirectory(module);
    const std::wstring iniPath = g_moduleDir + L"\\" + kIniName;
    EnsureDefaultConfig(iniPath);

    g_debug = GetPrivateProfileIntW(L"Loader", L"Debug", 0, iniPath.c_str()) != 0;
    if (GetPrivateProfileIntW(L"Loader", L"Enabled", 1, iniPath.c_str()) == 0)
    {
        Log(L"[加载器] 已由配置文件禁用：%s", iniPath.c_str());
        return;
    }

    const int count = GetPrivateProfileIntW(L"Loader", L"PluginCount", 0, iniPath.c_str());
    const DWORD timeout = static_cast<DWORD>(GetPrivateProfileIntW(
        L"Loader", L"WaitTimeoutMs", 15000, iniPath.c_str()));

    std::vector<PluginSpec> selected;
    for (int i = 0; i < count && i < 64; ++i)
    {
        wchar_t key[32] = {};
        _snwprintf_s(key, _countof(key), _TRUNCATE, L"Plugin%d", i);
        const std::wstring path = ReadString(iniPath, L"Plugins", key);
        if (path.empty())
            continue;

        _snwprintf_s(key, _countof(key), _TRUNCATE, L"Plugin%dStage", i);
        PluginSpec plugin{ path, ReadString(iniPath, L"Plugins", key, L"Early") };
        if (IsAfterGameGuard(plugin.stage) == afterGameGuardStage)
            selected.push_back(plugin);
    }

    if (!selected.empty() && (!afterGameGuardStage || WaitForGameGuard(timeout)))
        for (const PluginSpec& plugin : selected)
            LoadPlugin(plugin);
}
}

// 主程序当前按名称导入 ijlFree。保留已部署代理使用的完整兼容导出表，
// 对客户端不需要的 IJL 操作返回无副作用的结果。
extern "C" __declspec(dllexport) void ClearRSInfo() {}
extern "C" __declspec(dllexport) void** CreateObj(char) { return nullptr; }
extern "C" __declspec(dllexport) void PutRSInfo() {}
extern "C" __declspec(dllexport) void Test() {}
extern "C" __declspec(dllexport) const char* ijlErrorStr(int) { return "OK"; }
extern "C" __declspec(dllexport) void ijlFree(void*) {}
extern "C" __declspec(dllexport) int ijlGetLibVersion() { return 0; }
extern "C" __declspec(dllexport) int ijlInit(void*) { return 0; }
extern "C" __declspec(dllexport) int ijlRead(void*, int) { return 0; }
extern "C" __declspec(dllexport) int ijlWrite(void*, int) { return 0; }

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        // 86JP 需要安装启动阶段的 Hook，历史上由 IJL 导入触发加载。
        // Early 插件保持同步加载以维持 Hook 时序，只有依赖其他模块的阶段使用工作线程。
        LoadConfiguredStage(module, false);
        HANDLE thread = CreateThread(nullptr, 0,
            [](LPVOID context) -> DWORD {
                LoadConfiguredStage(static_cast<HMODULE>(context), true);
                return 0;
            },
            module, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
