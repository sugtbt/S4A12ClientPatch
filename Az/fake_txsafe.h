#pragma once

// ============================================================
// TerSafe interface — CreateObj returns vtable pointers
// Exact ret N values matched from 86JP az.dll dump
// ============================================================

static void __cdecl TS_Ret0_cdecl_log()
{
}

static void __stdcall TS_vtA0(int a1, int a2, int a3)
{
}

static void __stdcall TS_vtA2(int a1, int a2)
{
}

static void __stdcall TS_vtA8(int a1, int a2, int a3, int a4, int a5, int a6, int* out)
{
    *out = 0;
}

static int __stdcall TS_vtB0(int a1)
{
    return 0;
}

static void __stdcall TS_vtB11(int a1, int a2)
{
}

static int __stdcall TS_vtB16(int a1)
{
    return 1;
}

static void __stdcall TS_vtB18(int a1, int a2, int a3)
{
}

static void __cdecl TS_Ret0_cdecl() {}
static void __stdcall TS_Ret_0C(int, int, int) {}
static void __stdcall TS_Ret_08(int, int) {}
static int __stdcall TS_Ret0_04(int) { return 0; }
static int __stdcall TS_Ret1_04(int) { return 1; }

// Vtable A: 10 entries (index 9 = NULL, matches 86JP)
static void* g_vtableA[] = {
    (void*)TS_vtA0,         // [0] ret 0Ch — logged
    (void*)TS_Ret0_cdecl,   // [1] ret 0
    (void*)TS_vtA2,         // [2] ret 08h — logged
    (void*)TS_Ret0_cdecl,   // [3] ret 0
    (void*)TS_Ret0_cdecl,   // [4] ret 0
    (void*)TS_Ret0_cdecl,   // [5] ret 0
    (void*)TS_Ret0_cdecl,   // [6] ret 0
    (void*)TS_Ret0_cdecl,   // [7] ret 0
    (void*)TS_vtA8,         // [8] write 0 to arg7, ret 1Ch — logged
    nullptr,                    // [9] NULL
};

// vtB[17] data block (12 zero bytes + two NULL pointers)
static unsigned int g_vtB17_data[6] = { 0 };

// Vtable B: 20 entries (matches 86JP)
static void* g_vtableB[] = {
    (void*)TS_vtB0,         // [0]  return 0, ret 4 — logged
    (void*)TS_Ret0_cdecl,   // [1]  ret 0
    (void*)TS_Ret0_cdecl,   // [2]  ret 0
    (void*)TS_Ret0_cdecl,   // [3]  ret 0
    (void*)TS_Ret0_cdecl,   // [4]  ret 0
    (void*)TS_Ret0_cdecl,   // [5]  ret 0
    (void*)TS_Ret0_cdecl,   // [6]  ret 0
    (void*)TS_Ret0_cdecl,   // [7]  ret 0
    (void*)TS_Ret0_cdecl,   // [8]  ret 0
    (void*)TS_Ret0_cdecl,   // [9]  ret 0
    (void*)TS_Ret0_cdecl,   // [10] ret 0
    (void*)TS_vtB11,        // [11] ret 08h — logged
    (void*)TS_Ret0_cdecl,   // [12] ret 0
    (void*)TS_Ret0_cdecl,   // [13] ret 0
    (void*)TS_Ret0_cdecl,   // [14] ret 0
    (void*)TS_Ret0_cdecl,   // [15] ret 0
    (void*)TS_vtB16,        // [16] return 1, ret 4 — logged
    (void*)g_vtB17_data,    // [17] data pointer (not a function)
    (void*)TS_vtB18,        // [18] ret 0Ch — logged
    (void*)TS_Ret0_cdecl,   // [19] ret 0
};

// Object layout: CreateObj(3) returns &obj[0], CreateObj(7) returns &obj[4]
// +00: vtable A ptr    ← type 3
// +04: 0
// +08: 0 (86JP fills at runtime but zero works)
// +0C: 0
// +10: vtable B ptr    ← type 7
// +14: 0
// +18: 0
// +1C: 0
// +20: 0
static void* g_tersafe_obj[9] = { 0 };
static bool g_tersafe_init = false;


void** CreateObj_Impl(char type)
{
    if (!g_tersafe_init)
    {
        g_tersafe_obj[0] = (void*)g_vtableA;  // +00
        g_tersafe_obj[4] = (void*)g_vtableB;  // +10
        g_tersafe_init = true;
    }
    if (type == 3) return (void**)&g_tersafe_obj[0];
    if (type == 7) return (void**)&g_tersafe_obj[4];
    return nullptr;
}