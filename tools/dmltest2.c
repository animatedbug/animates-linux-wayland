// Mimic ONNX Runtime's DML adapter path: enumerate adapters via DXCore, create a
// D3D12 device + DirectML device on each, and report which GPU vkd3d-proton used.
typedef unsigned int UINT;
typedef unsigned long long U64;
typedef long HRESULT;
typedef void *HMODULE;
typedef struct { unsigned long d1; unsigned short d2, d3; unsigned char d4[8]; } GUID;
typedef struct { unsigned int lo; int hi; } LUID;

__declspec(dllimport) HMODULE __stdcall LoadLibraryA(const char *);
__declspec(dllimport) void *__stdcall GetProcAddress(HMODULE, const char *);
__declspec(dllimport) void __stdcall ExitProcess(UINT);
__declspec(dllimport) int printf(const char *, ...);

static const GUID IID_ID3D12Device = {0x189819f1, 0x1db6, 0x4b57, {0xbe, 0x54, 0x18, 0x21, 0x33, 0x9b, 0x85, 0xf7}};
static const GUID IID_IDMLDevice = {0x6dbd6437, 0x96fd, 0x423f, {0xa9, 0x8c, 0xae, 0x5e, 0x7c, 0x2a, 0x57, 0x3f}};
static const GUID IID_IDXCoreAdapterFactory = {0x78ee5945, 0xc36e, 0x4b13, {0xa6, 0x69, 0x00, 0x5d, 0xd1, 0x1c, 0x0f, 0x06}};
static const GUID IID_IDXCoreAdapterList = {0x526c7776, 0x40e9, 0x459b, {0xb7, 0x11, 0xf3, 0x2a, 0xd7, 0x6d, 0xfc, 0x28}};
static const GUID IID_IDXCoreAdapter = {0xf0db4c7f, 0xfe5a, 0x42a2, {0xbd, 0x62, 0xf2, 0xa6, 0xcf, 0x6f, 0xc8, 0x3e}};
static const GUID ATTR_D3D12_CORE_COMPUTE = {0x248e2800, 0xa793, 0x4724, {0xab, 0xaa, 0x23, 0xa6, 0xde, 0x1b, 0xe0, 0x90}};

typedef void **VT;
#define M(obj, idx, type) ((type)((*(VT *)(obj))[idx]))

typedef HRESULT(__stdcall *PFN_Factory)(const GUID *, void **);
typedef HRESULT(__stdcall *PFN_CreateList)(void *, UINT, const GUID *, const GUID *, void **);
typedef UINT(__stdcall *PFN_GetCount)(void *);
typedef HRESULT(__stdcall *PFN_GetAdapter)(void *, UINT, const GUID *, void **);
typedef HRESULT(__stdcall *PFN_GetProperty)(void *, UINT, U64, void *);
typedef HRESULT(__stdcall *PFN_Sort)(void *, UINT, const UINT *);
typedef HRESULT(__stdcall *PFN_D3D12CreateDevice)(void *, UINT, const GUID *, void **);
typedef HRESULT(__stdcall *PFN_DMLCreateDevice)(void *, UINT, const GUID *, void **);
typedef LUID *(__stdcall *PFN_GetAdapterLuid)(void *, LUID *);

#define DML_DLL "C:\\users\\steamuser\\AppData\\Local\\AnimateApp\\current\\Animates_Data\\Plugins\\x86_64\\DirectML.dll"

void __stdcall mainCRTStartup(void)
{
    PFN_Factory mkfactory = (PFN_Factory)GetProcAddress(LoadLibraryA("dxcore.dll"), "DXCoreCreateAdapterFactory");
    PFN_D3D12CreateDevice d3d12create = (PFN_D3D12CreateDevice)GetProcAddress(LoadLibraryA("d3d12.dll"), "D3D12CreateDevice");
    PFN_DMLCreateDevice dmlcreate = (PFN_DMLCreateDevice)GetProcAddress(LoadLibraryA(DML_DLL), "DMLCreateDevice");

    void *factory = 0, *list = 0;
    HRESULT hr = mkfactory(&IID_IDXCoreAdapterFactory, &factory);
    printf("factory hr=%08lx\n", (unsigned long)hr);
    hr = M(factory, 3, PFN_CreateList)(factory, 1, &ATTR_D3D12_CORE_COMPUTE, &IID_IDXCoreAdapterList, &list);
    UINT pref = 2; /* DXCoreAdapterPreference::HighPerformance, what ORT asks for on dGPU setups */
    HRESULT sorthr = M(list, 7, PFN_Sort)(list, 1, &pref);
    UINT n = M(list, 4, PFN_GetCount)(list);
    printf("adapter list hr=%08lx count=%u (sort hr=%08lx)\n", (unsigned long)hr, n, (unsigned long)sorthr);

    for (UINT i = 0; i < n; i++) {
        void *ad = 0;
        char desc[256] = {0};
        LUID luid = {0}, devluid = {0};
        M(list, 3, PFN_GetAdapter)(list, i, &IID_IDXCoreAdapter, &ad);
        M(ad, 6, PFN_GetProperty)(ad, 2 /* DriverDescription */, sizeof(desc), desc);
        M(ad, 6, PFN_GetProperty)(ad, 0 /* InstanceLuid */, sizeof(luid), &luid);
        void *dev = 0, *dml = 0;
        hr = d3d12create(ad, 0xb000, &IID_ID3D12Device, &dev);
        if (hr >= 0)
            M(dev, 43, PFN_GetAdapterLuid)(dev, &devluid);
        HRESULT dhr = dev ? dmlcreate(dev, 0, &IID_IDMLDevice, &dml) : -1;
        printf("[%u] %s luid=%x:%x -> D3D12 hr=%08lx devluid=%x:%x DML hr=%08lx\n", i, desc, luid.hi, luid.lo,
               (unsigned long)hr, devluid.hi, devluid.lo, (unsigned long)dhr);
    }
    ExitProcess(0);
}
