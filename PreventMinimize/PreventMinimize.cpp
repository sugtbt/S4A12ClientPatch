#include <windows.h>

#include <array>
#include <cstring>

namespace
{
constexpr uintptr_t kSendMessageCallRva = 0x0099E698;
constexpr std::array<unsigned char, 6> kOriginalBytes = {
    0xFF, 0x15, 0x60, 0x35, 0x7D, 0x02
};
constexpr std::array<unsigned char, 6> kPatchedBytes = {
    0x83, 0xC4, 0x10, 0x31, 0xC0, 0x90
};

bool ApplyPatch()
{
    const auto executable = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto target = reinterpret_cast<unsigned char*>(
        executable + kSendMessageCallRva);

    if (std::memcmp(target, kPatchedBytes.data(), kPatchedBytes.size()) == 0)
        return true;
    if (std::memcmp(target, kOriginalBytes.data(), kOriginalBytes.size()) != 0)
        return false;

    // 将返回值设为 0。
    DWORD oldProtection = 0;
    if (!VirtualProtect(target, kPatchedBytes.size(),
        PAGE_EXECUTE_READWRITE, &oldProtection))
        return false;

    std::memcpy(target, kPatchedBytes.data(), kPatchedBytes.size());
    FlushInstructionCache(GetCurrentProcess(), target, kPatchedBytes.size());

    DWORD ignored = 0;
    return VirtualProtect(target, kPatchedBytes.size(), oldProtection, &ignored) != FALSE;
}
}

extern "C" __declspec(dllexport) BOOL ClientPatchPluginInit()
{
    return ApplyPatch() ? TRUE : FALSE;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}
