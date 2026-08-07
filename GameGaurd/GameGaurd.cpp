#include "mem.h"

#include "PluginLoader.h"

#include <cstring>
#include <initializer_list>

namespace
{
constexpr uintptr_t kRegisterNotiPacketHandler = 0x01189FC0;
constexpr uintptr_t kWriteMessage = 0x01503530;
constexpr uintptr_t kMessageWindowPointer = 0x03189138;
constexpr uintptr_t kGameSocketPointer = 0x0319A114;

using RegisterNotiPacketHandler = int(__thiscall*)(
    void*, uintptr_t, uintptr_t, uintptr_t);
using WriteMessage = int(__thiscall*)(
    void*, uintptr_t, uintptr_t, uintptr_t, int, int, int);
}

// ============================================================
// Static byte patches — addresses from live memory analysis
// ============================================================

static bool PatchBytes(uintptr_t address,
    std::initializer_list<uint8_t> expected,
    std::initializer_list<uint8_t> replacement)
{
    if (expected.size() != replacement.size())
        return false;

    std::vector<uint8_t> current(expected.size(), 0);
    mem::read(address, current.data(), current.size());
    const std::vector<uint8_t> oldBytes(expected);
    const std::vector<uint8_t> newBytes(replacement);
    if (current != oldBytes && current != newBytes)
        return false;
    if (current == oldBytes)
        mem::patch(address, newBytes);
    return true;
}

static void CompatibilityPatches()
{
    // 以下五处写入逐字节迁移自 ijl15_jp.dll!app.init（RVA 0x15F0）。
    // 旧实现只检查地址可读性；这里额外校验原始字节，并允许重复执行时保持幂等。

    // app.init 写入块 RVA 0x1607：将代码页参数从 936 改为 UTF-8 的 65001。
    PatchBytes(0x00423EB0,
        { 0x68, 0xA8, 0x03, 0x00, 0x00 },
        { 0x68, 0xE9, 0xFD, 0x00, 0x00 });

    // app.init 写入块 RVA 0x1672。
    PatchBytes(0x00F54606, { 0x74 }, { 0xEB });

    // app.init 写入块 RVA 0x16B6。
    PatchBytes(0x01189628,
        { 0x0F, 0xB7, 0x46, 0x01, 0x57, 0x83, 0xF8, 0x57, 0x7F, 0x15, 0x74, 0x1A },
        { 0x0F, 0xB7, 0x46, 0x07, 0x57, 0x83, 0xF8, 0x00, 0x7F, 0x1C, 0x74, 0x0A });

    // app.init 写入块 RVA 0x1774。
    PatchBytes(0x013753A8, { 0x75, 0x3F }, { 0xEB, 0x3F });

    // app.init 写入块 RVA 0x17C6。
    PatchBytes(0x01CBFC00,
        { 0xBF, 0x06, 0x00, 0x00, 0x00 },
        { 0xBF, 0x03, 0x00, 0x00, 0x00 });
}

// 迁移自 ijl15_jp.dll!app.writeByteArray（RVA 0x1440）。
extern "C" __declspec(dllexport) BOOL ClientPatchWriteByteArray(
    void* destination, const void* source, unsigned int size)
{
    if (!destination || (size != 0 && !source) ||
        IsBadReadPtr(source, size) || IsBadWritePtr(destination, size))
        return FALSE;

    DWORD oldProtection = 0;
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &oldProtection))
        return FALSE;

    if (size != 0)
        std::memcpy(destination, source, size);
    FlushInstructionCache(GetCurrentProcess(), destination, size);

    DWORD ignored = 0;
    const BOOL restored = VirtualProtect(
        destination, size, oldProtection, &ignored);
    return restored;
}

// 迁移自 ijl15_jp.dll!app.registerNOTIPacketHandler（RVA 0x1840）。
extern "C" __declspec(dllexport) int ClientPatchRegisterNOTIPacketHandler(
    void* context, uintptr_t argument1, uintptr_t argument2, uintptr_t argument3)
{
    const auto function = reinterpret_cast<RegisterNotiPacketHandler>(
        kRegisterNotiPacketHandler);
    return function(context, argument1, argument2, argument3);
}

// 迁移自 ijl15_jp.dll!app.registerNOTIPacketHandler2（RVA 0x1870）。
extern "C" __declspec(dllexport) int ClientPatchRegisterNOTIPacketHandler2(
    void* context, uintptr_t argument1, uintptr_t argument2, uintptr_t argument3)
{
    const auto function = reinterpret_cast<RegisterNotiPacketHandler>(
        kRegisterNotiPacketHandler);
    return function(context, argument1, argument2, argument3);
}

// 迁移自 ijl15_jp.dll!app.writeMessage（RVA 0x18A0）。
extern "C" __declspec(dllexport) int ClientPatchWriteMessage(
    void* context, uintptr_t argument1, uintptr_t argument2, uintptr_t argument3)
{
    const auto function = reinterpret_cast<WriteMessage>(kWriteMessage);
    return function(context, argument1, argument2, argument3, 0, 0, 0);
}

// 迁移自 ijl15_jp.dll!app.getMessageWindowPointer（RVA 0x18D0）。
extern "C" __declspec(dllexport) void* ClientPatchGetMessageWindowPointer()
{
    return *reinterpret_cast<void**>(kMessageWindowPointer);
}

// 迁移自 ijl15_jp.dll!app.getGameSocketPointer（RVA 0x18E0）。
// 原函数返回的是全局槽位地址，不读取槽位内容。
extern "C" __declspec(dllexport) void* ClientPatchGetGameSocketPointer()
{
    return reinterpret_cast<void*>(kGameSocketPointer);
}

static void StaticPatches()
{
    // --- Anti-cheat bypass ---

    // #3-4: parseCommandLineChina — skip TCLS mode init (JZ→JMP+NOP)
    mem::patch(0x009F80FD, { 0xE9, 0x8E, 0x06, 0x00, 0x00, 0x90 });

    // #7: NOTIFUNC_CHANNELINFO — skip encrypted key section (JZ→JMP)
    mem::patch(0x00D14B81, { 0xEB });

    // #10: QQSafeStorage::Init — bypass (JNZ→NOP)
    mem::patch(0x00F9EB0A, { 0x90, 0x90 });

    // #33: startup init — bypass validation check (JZ→NOP)
    mem::patch(0x02522BDF, { 0x90, 0x90 });

    // --- Version/signature compatibility ---

    // #1: SetCodePage GBK(936)→UTF-8(65001)

    // #21: bypass version signature 0x1B412 check (JZ→JMP)
    mem::patch(0x01520157, { 0xEB });

    // #22: skip type 0x6F event processing (JNZ→JMP)
    mem::patch(0x01C9D104, { 0xEB });

    // #23: skip game state verification (JZ→JMP)
    mem::patch(0x01CAD5FB, { 0xE9, 0xB9, 0x00 });

    // --- Code fix / runtime patch ---

    // #8: restore prologue destroyed by packer
    mem::patch(0x00EB5C80, { 0x55, 0x8B, 0xEC, 0x6A, 0xFF });

    // NOP the fixed-key XOR decrypt in NetworkProc (server sends plaintext)
    mem::patch(0x0118999B, { 0x90, 0x90, 0x90, 0x90, 0x90 });

    // n5 check entries → return 1 (pass)
    mem::patch(0x009F7CE0, { 0x31, 0xC0, 0x40, 0xC2, 0x04, 0x00 });
    mem::patch(0x0118A160, { 0x31, 0xC0, 0x40, 0xC2, 0x04, 0x00 });
    mem::patch(0x0118B1D0, { 0x31, 0xC0, 0x40, 0xC2, 0x04, 0x00 });

    // Patch encrypted string at 0x02CD5C9C to decrypted "az.dll"
    // Original is encrypted; overwrite with decrypted form so GetOrDecryptWideString
    // returns "az.dll" → GetModuleHandleW finds our az.dll → CreateObj works
    mem::patch(0x02CD5C9C, {
        0xA1, 0x2C, 0x00, 0x00,                                     // header (decrypted flag)
        0x61, 0x00, 0x7A, 0x00, 0x2E, 0x00, 0x64, 0x00,           // "az.dll" UTF-16LE
        0x6C, 0x00, 0x6C, 0x00, 0x00, 0x00                         // + null terminator
        });

    // Cipher::Encrypt → memcpy passthrough
    mem::patch(0x02011360, {
        0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x10, 0x56, 0xFF, 0x75, 0x0C, 0xFF, 0x75, 0x14, 0xFF, 0x15,
        0xAC, 0x23, 0x71, 0x04, 0x8B, 0x45, 0x18, 0x83, 0xC4, 0x0C, 0x89, 0x30, 0xB0, 0x01, 0x5E, 0x5D,
        0xC2, 0x14, 0x00 });

    // Cipher::Decrypt → memcpy passthrough
    mem::patch(0x02011420, {
        0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x75, 0x10, 0x56, 0xFF, 0x75, 0x0C, 0xFF, 0x75, 0x14, 0xFF, 0x15,
        0xAC, 0x23, 0x71, 0x04, 0x8B, 0x45, 0x18, 0x83, 0xC4, 0x0C, 0x89, 0x30, 0xB0, 0x01, 0x5E, 0x5D,
        0xC2, 0x14, 0x00 });

    CompatibilityPatches();
}

// nengine::ISocket::Write replacement — bypass VM at 0x04CFF9AB
//
// REVERSED from the live 86JP client (86JPDump.exe): Write does NOT touch the
// send ring or the socket. It only appends to the packet staging buffer.
// Send flow: MakePacket(0x2090460) stores staging+pos at this+0x2BCC34 and
// Writes the 13-byte header → wrappers Write body bytes (counting them in
// this+0x2BCC2C) → TCP_SEND(0x0208FDD0) pops the staged packet
// (sub_208FB60), fills in length/checksum, Cipher::Encrypts the body, and
// appends the result to the flush ring (sub_4CFAB78, ring = this+0xAF1F8)
// → flush thread (sub_219E680) send()s the ring → TCP_SEND resets the whole
// staging buffer (sub_208F5C0).
//
// Staging layout (this = CNGameSocket / crypto_net, dword_319A114):
//   data       = this + 0x15E218            (0xAF000 bytes, reset per packet)
//   write pos  = this + 0x15E218 + 0xAF000  (advanced by Write)
//   packet pos = this + 0x15E218 + 0xAF004  (read by MakePacket/TCP_SEND — do NOT touch)
//   crit sect  = this + 0x15E218 + 0xAF008
#define STAGE_OFFSET     0x15E218
#define STAGE_SIZE       0xAF000
#define STAGE_WPOS_OFF   (STAGE_OFFSET + 0xAF000)
#define STAGE_CS_OFF     (STAGE_OFFSET + 0xAF008)

static int __fastcall Hook_ISocketWrite(void* this_ptr, void* /*edx*/, char* buffer, int size)
{
    char* base = (char*)this_ptr;
    if (!*(base + 8)) return -2147483564;
    if (!buffer || size <= 0) return -1;

    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)(base + STAGE_CS_OFF);
    DWORD* pWpos = (DWORD*)(base + STAGE_WPOS_OFF);

    EnterCriticalSection(cs);
    DWORD wp = *pWpos;
    if ((unsigned)size >= STAGE_SIZE - wp)
    {
        LeaveCriticalSection(cs);
        return -2;
    }
    memcpy(base + STAGE_OFFSET + wp, buffer, size);
    *pWpos = wp + size;
    LeaveCriticalSection(cs);
    return 0;
}

// ============================================================
// Entry point
// ============================================================

void PatchS4A12()
{
    StaticPatches();

    // nengine::ISocket::Write — staging append (reversed from MakePacket/TCP_SEND)
    mem::jmphook(0x04CFF9AB, reinterpret_cast<uintptr_t>(Hook_ISocketWrite));
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);

        // ijl15_jp.dll 原先由 app.attach（RVA 0x1320）临时 Hook
        // winmm!timeGetTime，再由 app.entry（RVA 0x1510）恢复原指令并调用
        // app.init。GameGaurd 被加载时客户端代码已经解压，因此直接应用补丁，
        // 不再复制这段一次性 Hook，也不增加等待逻辑。
        PatchS4A12();

        HANDLE thread = CreateThread(nullptr, 0,
            [](LPVOID context) -> DWORD {
                plugin_loader::LoadConfiguredPlugins(static_cast<HMODULE>(context));
                return 0;
            }, hModule, 0, nullptr);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
