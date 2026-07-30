#include "mem.h"

// ============================================================
// Static byte patches — addresses from live memory analysis
// ============================================================

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
    mem::patch(0x00423EB1, { 0xE9, 0xFD });

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

static void* __cdecl Hook_BeforeLoadAndValidatePvfFile(int arg)
{
    const static wchar_t headPtr[] = L"HeaD";
    for (int i = 0; i < 4; i++)
        *(uint8_t*)(0x04440C10 + i) ^= 0x55;
    return (void*)headPtr;
}

// ============================================================
// Entry point
// ============================================================

void PatchS4A12()
{
    StaticPatches();

    // nengine::ISocket::Write — staging append (reversed from MakePacket/TCP_SEND)
    mem::jmphook(0x04CFF9AB, reinterpret_cast<uintptr_t>(Hook_ISocketWrite));

    // PVF key toggle
    mem::callhook(0x02492FB5, reinterpret_cast<uintptr_t>(Hook_BeforeLoadAndValidatePvfFile));
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
        PatchS4A12();
    }
    return TRUE;
}
