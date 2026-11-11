/* Forwards the real hid.dll exports so the game keeps working with the loader in place. */
#include <windows.h>

void *g_fn[47];
static const char *g_names[47] = {
    "HidD_FlushQueue", "HidD_FreePreparsedData", "HidD_GetAttributes",
    "HidD_GetConfiguration", "HidD_GetFeature", "HidD_GetHidGuid",
    "HidD_GetIndexedString", "HidD_GetInputReport", "HidD_GetManufacturerString",
    "HidD_GetMsGenreDescriptor", "HidD_GetNumInputBuffers", "HidD_GetPhysicalDescriptor",
    "HidD_GetPreparsedData", "HidD_GetProductString", "HidD_GetSerialNumberString",
    "HidD_Hello", "HidD_SetConfiguration", "HidD_SetFeature",
    "HidD_SetNumInputBuffers", "HidD_SetOutputReport", "HidP_GetButtonArray",
    "HidP_GetButtonCaps", "HidP_GetCaps", "HidP_GetData",
    "HidP_GetExtendedAttributes", "HidP_GetLinkCollectionNodes", "HidP_GetScaledUsageValue",
    "HidP_GetSpecificButtonCaps", "HidP_GetSpecificValueCaps", "HidP_GetUsageValue",
    "HidP_GetUsageValueArray", "HidP_GetUsages", "HidP_GetUsagesEx",
    "HidP_GetValueCaps", "HidP_GetVersionInternal", "HidP_InitializeReportForID",
    "HidP_MaxDataListLength", "HidP_MaxUsageListLength", "HidP_SetButtonArray",
    "HidP_SetData", "HidP_SetScaledUsageValue", "HidP_SetUsageValue",
    "HidP_SetUsageValueArray", "HidP_SetUsages", "HidP_TranslateUsagesToI8042ScanCodes",
    "HidP_UnsetUsages", "HidP_UsageListDifference",
};

void proxy_init(void) {
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    lstrcatW(path, L"\\hid.dll");
    HMODULE h = LoadLibraryW(path);
    for (int i = 0; i < 47; i++) g_fn[i] = h ? (void *)GetProcAddress(h, g_names[i]) : NULL;
}

#define FWD(name, idx) \
    __declspec(dllexport) __attribute__((naked)) void name(void) { __asm__("jmp *g_fn+" #idx "*8(%rip)"); }

FWD(HidD_FlushQueue, 0)
FWD(HidD_FreePreparsedData, 1)
FWD(HidD_GetAttributes, 2)
FWD(HidD_GetConfiguration, 3)
FWD(HidD_GetFeature, 4)
FWD(HidD_GetHidGuid, 5)
FWD(HidD_GetIndexedString, 6)
FWD(HidD_GetInputReport, 7)
FWD(HidD_GetManufacturerString, 8)
FWD(HidD_GetMsGenreDescriptor, 9)
FWD(HidD_GetNumInputBuffers, 10)
FWD(HidD_GetPhysicalDescriptor, 11)
FWD(HidD_GetPreparsedData, 12)
FWD(HidD_GetProductString, 13)
FWD(HidD_GetSerialNumberString, 14)
FWD(HidD_Hello, 15)
FWD(HidD_SetConfiguration, 16)
FWD(HidD_SetFeature, 17)
FWD(HidD_SetNumInputBuffers, 18)
FWD(HidD_SetOutputReport, 19)
FWD(HidP_GetButtonArray, 20)
FWD(HidP_GetButtonCaps, 21)
FWD(HidP_GetCaps, 22)
FWD(HidP_GetData, 23)
FWD(HidP_GetExtendedAttributes, 24)
FWD(HidP_GetLinkCollectionNodes, 25)
FWD(HidP_GetScaledUsageValue, 26)
FWD(HidP_GetSpecificButtonCaps, 27)
FWD(HidP_GetSpecificValueCaps, 28)
FWD(HidP_GetUsageValue, 29)
FWD(HidP_GetUsageValueArray, 30)
FWD(HidP_GetUsages, 31)
FWD(HidP_GetUsagesEx, 32)
FWD(HidP_GetValueCaps, 33)
FWD(HidP_GetVersionInternal, 34)
FWD(HidP_InitializeReportForID, 35)
FWD(HidP_MaxDataListLength, 36)
FWD(HidP_MaxUsageListLength, 37)
FWD(HidP_SetButtonArray, 38)
FWD(HidP_SetData, 39)
FWD(HidP_SetScaledUsageValue, 40)
FWD(HidP_SetUsageValue, 41)
FWD(HidP_SetUsageValueArray, 42)
FWD(HidP_SetUsages, 43)
FWD(HidP_TranslateUsagesToI8042ScanCodes, 44)
FWD(HidP_UnsetUsages, 45)
FWD(HidP_UsageListDifference, 46)
