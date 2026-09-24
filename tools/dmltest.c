// Feasibility test: can DirectML (the copy shipped with Animates) run on
// vkd3d-proton's D3D12 under Wine? Built with clang --target=x86_64-pc-windows-msvc.
typedef unsigned int UINT;
typedef long HRESULT;
typedef void *HMODULE;
typedef struct { unsigned long d1; unsigned short d2, d3; unsigned char d4[8]; } GUID;

__declspec(dllimport) HMODULE __stdcall LoadLibraryA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(HMODULE, const char *);
__declspec(dllimport) void __stdcall ExitProcess(UINT);
__declspec(dllimport) int printf(const char *, ...);

static const GUID IID_ID3D12Device = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
static const GUID IID_IDMLDevice = {0x6dbd6437, 0x96fd, 0x423f, {0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f}};

typedef HRESULT(__stdcall *PFN_D3D12CreateDevice)(void *adapter, UINT fl, const GUID *riid, void **dev);
typedef HRESULT(__stdcall *PFN_DMLCreateDevice)(void *d3d12, UINT flags, const GUID *riid, void **dml);
typedef HRESULT(__stdcall *PFN_CheckFeatureSupport)(void *self, UINT feature, UINT qsize, const void *q, UINT dsize, void *d);

#define DML_DLL "C:\\users\\steamuser\\AppData\\Local\\AnimateApp\\current\\Animates_Data\\Plugins\\x86_64\\DirectML.dll"

void __stdcall mainCRTStartup(void)
{
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    printf("d3d12.dll: %p\n", d3d12);
    PFN_D3D12CreateDevice create = (PFN_D3D12CreateDevice)GetProcAddress(d3d12, "D3D12CreateDevice");
    void *dev = 0;
    HRESULT hr = create ? create(0, 0xb000 /* FL 11_0 */, &IID_ID3D12Device, &dev) : -1;
    printf("D3D12CreateDevice: hr=%08lx dev=%p\n", (unsigned long)hr, dev);
    if (hr < 0) ExitProcess(1);

    HMODULE dml = LoadLibraryA(DML_DLL);
    printf("DirectML.dll: %p\n", dml);
    PFN_DMLCreateDevice dcreate = (PFN_DMLCreateDevice)GetProcAddress(dml, "DMLCreateDevice");
    void *dmldev = 0;
    hr = dcreate ? dcreate(dev, 0, &IID_IDMLDevice, &dmldev) : -1;
    printf("DMLCreateDevice: hr=%08lx dml=%p\n", (unsigned long)hr, dmldev);
    if (hr < 0) ExitProcess(2);

    // IDMLDevice::CheckFeatureSupport is vtable slot 7 (IUnknown 3 + IDMLObject 4).
    UINT levels[] = {0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x5100, 0x5200, 0x6000, 0x6100, 0x6200, 0x6300, 0x6400};
    struct { UINT count; const UINT *levels; } q = {sizeof(levels) / sizeof(levels[0]), levels};
    UINT max = 0;
    PFN_CheckFeatureSupport check = (*(PFN_CheckFeatureSupport **)dmldev)[7];
    hr = check(dmldev, 1 /* DML_FEATURE_FEATURE_LEVELS */, sizeof(q), &q, sizeof(max), &max);
    printf("DML max feature level: hr=%08lx level=%x\n", (unsigned long)hr, max);
    ExitProcess(0);
}
