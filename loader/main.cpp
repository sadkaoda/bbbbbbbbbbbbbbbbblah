#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dcomp.h>
#include <wininet.h>
#include <winreg.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>
#include <atomic>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstddef>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "wininet.lib")

namespace L
{
    constexpr float Density = 0.85f;
    constexpr float RefH = 1080.0f;
    constexpr float PanelW = 300.0f;
    constexpr float PanelRh = 92.0f;
    constexpr float Margin = 10.0f;
    constexpr float HeaderH = 34.0f;
    constexpr float PadX = 11.0f;
    constexpr float PadY = 11.0f;
    constexpr float RowH = 36.0f;
    constexpr float ComboW = 190.0f;
    constexpr float BtnW = 78.0f;
    constexpr float FontPx = 18.0f;
    constexpr int MaxPopupItems = 6;
}

static const ImU32 COL_HEADER = IM_COL32(0x35, 0x57, 0x80, 0xFF);
static const ImU32 COL_PANEL = IM_COL32(0x15, 0x15, 0x15, 0xFF);
static const ImU32 COL_WHITE = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF);
static const ImU32 COL_PANEL_LINE = IM_COL32(0x3A, 0x3A, 0x3A, 0xFF);
static const ImU32 COL_HEADER_SEP = IM_COL32(0, 0, 0, 0x50);

static const ImVec4 C_PANEL = ImVec4(0x15 / 255.f, 0x15 / 255.f, 0x15 / 255.f, 1.0f);
static const ImVec4 C_TEXT = ImVec4(0xDD / 255.f, 0xDD / 255.f, 0xDD / 255.f, 1.0f);
static const ImVec4 C_BTN = ImVec4(0x2F / 255.f, 0x5A / 255.f, 0x86 / 255.f, 1.0f);
static const ImVec4 C_BTN_HOVER = ImVec4(0x3B / 255.f, 0x6A / 255.f, 0x9B / 255.f, 1.0f);
static const ImVec4 C_BTN_ACTIVE = ImVec4(0x27 / 255.f, 0x4B / 255.f, 0x71 / 255.f, 1.0f);
static const ImVec4 C_FRAME = ImVec4(0x24 / 255.f, 0x40 / 255.f, 0x5E / 255.f, 1.0f);
static const ImVec4 C_FRAME_HOV = ImVec4(0x2A / 255.f, 0x4B / 255.f, 0x6C / 255.f, 1.0f);
static const ImVec4 C_FRAME_ACT = ImVec4(0x2F / 255.f, 0x53 / 255.f, 0x78 / 255.f, 1.0f);
static const ImVec4 C_BORDER = ImVec4(0x5A / 255.f, 0x7A / 255.f, 0x9A / 255.f, 1.0f);
static const ImVec4 C_SEL = ImVec4(0x2F / 255.f, 0x53 / 255.f, 0x78 / 255.f, 0.85f);

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain1* g_swap = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static IDCompositionDevice* g_dcomp = nullptr;
static IDCompositionTarget* g_dcompTarget = nullptr;
static IDCompositionVisual* g_dcompVisual = nullptr;

static HWND g_hwnd = nullptr;
static float g_scale = 1.0f;
static float g_wantH = 0.0f;
static float g_appH = 0.0f;
static std::vector<std::string> g_dlls;
static int g_sel = 0;
static std::atomic<bool> g_loading{ false };
static bool g_quitRequested = false;
static std::wstring g_status = L"Ready";

static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf, n);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

static bool Exists(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES &&
        !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static bool IsDll64Bit(const std::wstring& dllPath)
{
    HANDLE hFile = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(hFile);
    if (!hMap)
        return false;

    void* pView = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(hMap);
    if (!pView)
        return false;

    PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(pView);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        UnmapViewOfFile(pView);
        return false;
    }

    PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<BYTE*>(pView) + dos->e_lfanew);
    bool is64Bit = nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64;

    UnmapViewOfFile(pView);
    return is64Bit;
}

static bool IsProcess64Bit(DWORD pid)
{
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProcess)
        return false;

    BOOL isWow64 = FALSE;
    BOOL result = IsWow64Process(hProcess, &isWow64);
    CloseHandle(hProcess);

    if (!result)
        return false;

    return !isWow64;
}

static std::wstring FindCsgoExePath()
{
    wchar_t steamPath[MAX_PATH] = {};
    DWORD size = MAX_PATH;

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(hKey, L"SteamPath", nullptr, nullptr, reinterpret_cast<LPBYTE>(steamPath), &size) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);

            std::wstring base = std::wstring(steamPath) + L"\\steamapps\\common";

            std::vector<std::wstring> candidates = {
                base + L"\\csgo legacy\\csgo.exe",
                base + L"\\Counter-Strike Global Offensive\\csgo.exe",
                base + L"\\CSGO\\csgo.exe"
            };

            for (const auto& path : candidates)
            {
                if (Exists(path))
                    return path;
            }
        }
        else
        {
            RegCloseKey(hKey);
        }
    }

    return L"";
}

static std::wstring FullPath(const std::wstring& path)
{
    wchar_t buffer[MAX_PATH * 4] = {};
    DWORD n = GetFullPathNameW(
        path.c_str(), _countof(buffer), buffer, nullptr);

    if (n == 0 || n >= _countof(buffer))
        return path;

    return std::wstring(buffer, n);
}

static bool WaitForProcess(
    const wchar_t* exeName, DWORD timeoutMs, DWORD& pidOut)
{
    const DWORD start = GetTickCount();
    bool found = false;

    for (;;)
    {
        HANDLE snapshot =
            CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);

        if (snapshot != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W entry = {};
            entry.dwSize = sizeof(entry);

            if (Process32FirstW(snapshot, &entry))
            {
                do
                {
                    if (CompareStringOrdinal(
                        entry.szExeFile, -1,
                        exeName, -1, TRUE) == CSTR_EQUAL)
                    {
                        pidOut = entry.th32ProcessID;
                        found = true;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }

            CloseHandle(snapshot);
        }

        if (found || (GetTickCount() - start) >= timeoutMs)
            break;

        Sleep(250);
    }

    return found;
}

static bool InjectDllIntoProcess(
    DWORD pid, const std::wstring& dllPath, DWORD& errorOut)
{
    errorOut = ERROR_SUCCESS;

    HANDLE process = OpenProcess(
        PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE |
        PROCESS_VM_READ,
        FALSE, pid);

    if (!process)
    {
        errorOut = GetLastError();
        return false;
    }

    const size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);

    void* remote = VirtualAllocEx(
        process, nullptr, bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

    if (!remote)
    {
        errorOut = GetLastError();
        CloseHandle(process);
        return false;
    }

    SIZE_T written = 0;

    if (!WriteProcessMemory(
        process, remote, dllPath.c_str(), bytes, &written) ||
        written != bytes)
    {
        errorOut = GetLastError();
        if (written != bytes && errorOut == ERROR_SUCCESS)
            errorOut = ERROR_PARTIAL_COPY;
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    FARPROC loadLibrary =
        kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;

    if (!loadLibrary)
    {
        errorOut = ERROR_PROC_NOT_FOUND;
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    DWORD threadId = 0;

    HANDLE thread = CreateRemoteThread(
        process, nullptr, 0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibrary),
        remote, 0, &threadId);

    if (!thread)
    {
        errorOut = GetLastError();
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        CloseHandle(process);
        return false;
    }

    const DWORD wait = WaitForSingleObject(thread, 30000);

    DWORD remoteModule = 0;

    if (wait == WAIT_OBJECT_0)
    {
        if (!GetExitCodeThread(thread, &remoteModule))
        {
            errorOut = GetLastError();
        }
    }
    else if (wait == WAIT_TIMEOUT)
    {
        errorOut = WAIT_TIMEOUT;
    }
    else
    {
        errorOut = GetLastError();
    }

    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);

    if (wait != WAIT_OBJECT_0 ||
        errorOut != ERROR_SUCCESS ||
        remoteModule == 0)
    {
        if (errorOut == ERROR_SUCCESS)
            errorOut = ERROR_MOD_NOT_FOUND;
        return false;
    }

    return true;
}

static std::string DllFileNameForName(const std::string& name)
{
    if (name == "airflow")     return "airflow.dll";
    if (name == "ev0lve")      return "ev0lve.dll";
    if (name == "fatality")    return "fatality.dll";
    if (name == "gamesense")   return "gamesense.dll";
    if (name == "havoc")       return "havoc.dll";
    if (name == "intellect")   return "intellect.dll";
    if (name == "nemesis")     return "nemesis.dll";
    if (name == "otcV3")       return "otcv3.dll";
    if (name == "pandora")     return "pandora.dll";
    if (name == "primordial")  return "primordial.dll";
    return "";
}

static bool DownloadFileFromGitHub(
    const std::string& fileName,
    const std::wstring& outputPath,
    std::wstring& errorMsg)
{
    std::string url = "https://raw.githubusercontent.com/sadkaoda/blahblahblah/main/" + fileName;

    HINTERNET hInternet = InternetOpenW(
        L"Loader",
        INTERNET_OPEN_TYPE_PRECONFIG,
        nullptr,
        nullptr,
        0);

    if (!hInternet)
    {
        DWORD err = GetLastError();
        wchar_t buf[256];
        swprintf_s(buf, L"InternetOpen failed: %lu", err);
        errorMsg = buf;
        return false;
    }

    HINTERNET hUrl = InternetOpenUrlA(
        hInternet,
        url.c_str(),
        nullptr,
        0,
        INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE,
        0);

    if (!hUrl)
    {
        DWORD err = GetLastError();
        InternetCloseHandle(hInternet);
        wchar_t buf[256];
        swprintf_s(buf, L"InternetOpenUrl failed: %lu", err);
        errorMsg = buf;
        return false;
    }

    HANDLE hFile = CreateFileW(
        outputPath.c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInternet);
        wchar_t buf[256];
        swprintf_s(buf, L"CreateFile failed: %lu", err);
        errorMsg = buf;
        return false;
    }

    const DWORD bufferSize = 8192;
    std::vector<BYTE> buffer(bufferSize);
    DWORD bytesRead = 0;
    DWORD bytesWritten = 0;
    bool success = true;

    while (InternetReadFile(hUrl, buffer.data(), bufferSize, &bytesRead) && bytesRead > 0)
    {
        if (!WriteFile(hFile, buffer.data(), bytesRead, &bytesWritten, nullptr) ||
            bytesWritten != bytesRead)
        {
            success = false;
            break;
        }
    }

    CloseHandle(hFile);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);

    if (!success)
    {
        DeleteFileW(outputPath.c_str());
        errorMsg = L"Download write failed";
        return false;
    }

    return true;
}

static std::wstring MakeTempDllPath()
{
    wchar_t tempDir[MAX_PATH] = {};
    if (GetTempPathW(_countof(tempDir), tempDir) == 0)
        return L"";

    wchar_t tempFile[MAX_PATH] = {};
    UINT tick = static_cast<UINT>(GetTickCount());
    static volatile LONG s_ctr = 0;
    DWORD rnd = static_cast<DWORD>(GetCurrentProcessId() ^ GetCurrentThreadId() ^ static_cast<DWORD>(InterlockedIncrement(&s_ctr)));
    LARGE_INTEGER pc = {};
    QueryPerformanceCounter(&pc);
    rnd ^= static_cast<DWORD>(pc.LowPart) ^ static_cast<DWORD>(pc.HighPart * 2654435761u);

    swprintf_s(tempFile, L"%sloader_%08X_%08X.dll", tempDir, tick, rnd);

    return std::wstring(tempFile);
}

static bool DownloadCheatToTemp(const std::string& name, std::wstring& outPath, std::wstring& errorMsg)
{
    outPath.clear();
    errorMsg.clear();

    std::string fileName = DllFileNameForName(name);
    if (fileName.empty())
    {
        errorMsg = L"Unknown cheat name";
        return false;
    }

    std::wstring tmp = MakeTempDllPath();
    if (tmp.empty())
    {
        errorMsg = L"Failed to create temp path";
        return false;
    }

    if (!DownloadFileFromGitHub(fileName, tmp, errorMsg))
    {
        DeleteFileW(tmp.c_str());
        return false;
    }

    outPath = tmp;
    return true;
}

static void EnumerateDlls()
{
    g_dlls.clear();
    g_sel = 0;

    const char* cheatNames[] = {
        "airflow",
        "ev0lve",
        "fatality",
        "gamesense",
        "havoc",
        "intellect",
        "nemesis",
        "otcV3",
        "pandora",
        "primordial"
    };

    for (const char* name : cheatNames)
    {
        g_dlls.push_back(name);
    }

    std::sort(g_dlls.begin(), g_dlls.end());
    g_status = g_dlls.empty() ? L"No DLLs found" : L"Ready";
}

static void LoadSelected()
{
    if (g_loading.exchange(true))
        return;

    if (g_dlls.empty() || g_sel < 0 ||
        static_cast<size_t>(g_sel) >= g_dlls.size())
    {
        g_status = L"Select a valid cheat";
        g_loading = false;
        return;
    }

    const std::string& selName = g_dlls[static_cast<size_t>(g_sel)];

    std::wstring csgoDllTemp;
    std::wstring downloadError;
    if (!DownloadCheatToTemp(selName, csgoDllTemp, downloadError))
    {
        g_status = downloadError.empty() ? L"Download failed" : downloadError;
        g_loading = false;
        return;
    }

    std::wstring gameExe = FindCsgoExePath();
    if (gameExe.empty())
    {
        g_status = L"csgo.exe not found";
        DeleteFileW(csgoDllTemp.c_str());
        g_loading = false;
        return;
    }

    std::filesystem::path gamePath(gameExe);
    std::wstring gameDir = gamePath.parent_path().wstring();
    std::wstring gameDllPath = gameDir + L"\\" + std::filesystem::path(csgoDllTemp).filename().wstring();

    if (!CopyFileW(csgoDllTemp.c_str(), gameDllPath.c_str(), FALSE))
    {
        g_status = L"Copy failed";
        DeleteFileW(csgoDllTemp.c_str());
        g_loading = false;
        return;
    }

    DeleteFileW(csgoDllTemp.c_str());
    csgoDllTemp = gameDllPath;

    if (reinterpret_cast<INT_PTR>(ShellExecuteW(
        nullptr,
        L"open",
        L"steam://run/4465480",
        nullptr,
        nullptr,
        SW_SHOWNORMAL)) <= 32)
    {
        g_status = L"Steam launch failed";
        DeleteFileW(csgoDllTemp.c_str());
        g_loading = false;
        return;
    }

    g_status = L"Launching game";

    DWORD pid = 0;
    if (!WaitForProcess(L"csgo.exe", 120000, pid))
    {
        g_status = L"Game not detected";
        DeleteFileW(csgoDllTemp.c_str());
        g_loading = false;
        return;
    }

    g_status = L"Checking architecture";

    bool process64 = IsProcess64Bit(pid);
    bool dll64 = IsDll64Bit(csgoDllTemp);

    if (process64 != dll64)
    {
        g_status = L"Architecture mismatch";
        DeleteFileW(csgoDllTemp.c_str());
        g_loading = false;
        return;
    }

    g_status = L"Testing DLL";

    HMODULE hTest = LoadLibraryExW(csgoDllTemp.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hTest)
    {
        g_status = L"DLL load error (missing dependencies)";
        DeleteFileW(csgoDllTemp.c_str());
        g_loading = false;
        return;
    }
    FreeLibrary(hTest);

    g_status = L"Injecting";

    DWORD error = ERROR_SUCCESS;
    bool injected =
        InjectDllIntoProcess(pid, FullPath(csgoDllTemp), error);

    if (injected)
    {
        g_status = L"Injected";
    }
    else
    {
        wchar_t buf[64];
        swprintf_s(buf, L"Error: %lu", error);
        g_status = buf;
        DeleteFileW(csgoDllTemp.c_str());
    }

    g_loading = false;
}

static bool InitD3D()
{
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL got = {};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, _countof(levels), D3D11_SDK_VERSION,
        &g_device, &got, &g_context);

    if (FAILED(hr))
        return false;

    UINT w = static_cast<UINT>(
        std::lround((L::PanelW + 2 * L::Margin) * g_scale));
    UINT h = static_cast<UINT>(
        std::lround((L::PanelRh + 2 * L::Margin) * g_scale));

    IDXGIDevice* dxgiDev = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory2* factory = nullptr;

    hr = g_device->QueryInterface(
        __uuidof(IDXGIDevice), (void**)&dxgiDev);
    if (FAILED(hr))
        return false;

    hr = dxgiDev->GetAdapter(&adapter);
    dxgiDev->Release();
    if (FAILED(hr))
        return false;

    hr = adapter->GetParent(
        __uuidof(IDXGIFactory2), (void**)&factory);
    adapter->Release();
    if (FAILED(hr))
        return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = w;
    sd.Height = h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = factory->CreateSwapChainForComposition(
        g_device, &sd, nullptr, &g_swap);
    factory->Release();

    if (FAILED(hr))
        return false;

    IDXGIDevice* dxgiDev2 = nullptr;
    hr = g_device->QueryInterface(
        __uuidof(IDXGIDevice), (void**)&dxgiDev2);
    if (FAILED(hr))
        return false;

    hr = DCompositionCreateDevice(
        dxgiDev2, __uuidof(IDCompositionDevice), (void**)&g_dcomp);
    dxgiDev2->Release();

    if (FAILED(hr))
        return false;

    hr = g_dcomp->CreateTargetForHwnd(
        g_hwnd, TRUE, &g_dcompTarget);
    if (FAILED(hr))
        return false;

    hr = g_dcomp->CreateVisual(&g_dcompVisual);
    if (FAILED(hr))
        return false;

    g_dcompVisual->SetContent(g_swap);
    g_dcompTarget->SetRoot(g_dcompVisual);

    hr = g_dcomp->Commit();
    if (FAILED(hr))
        return false;

    ID3D11Texture2D* back = nullptr;
    hr = g_swap->GetBuffer(
        0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (FAILED(hr))
        return false;

    hr = g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();

    return SUCCEEDED(hr);
}

static bool ResizePanelTo(float h)
{
    UINT w = static_cast<UINT>(
        std::lround((L::PanelW + 2 * L::Margin) * g_scale));
    UINT hh = static_cast<UINT>(
        std::lround(h + 2 * L::Margin * g_scale));

    if (g_rtv)
    {
        g_rtv->Release();
        g_rtv = nullptr;
    }

    HRESULT hr = g_swap->ResizeBuffers(
        0, w, hh, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        return false;

    ID3D11Texture2D* back = nullptr;
    hr = g_swap->GetBuffer(
        0, __uuidof(ID3D11Texture2D), (void**)&back);
    if (FAILED(hr))
        return false;

    hr = g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();

    if (FAILED(hr))
        return false;

    SetWindowPos(
        g_hwnd, nullptr, 0, 0, w, hh,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    g_appH = h;
    return true;
}

static void CleanupD3D()
{
    if (g_context) g_context->ClearState();
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
    if (g_dcompVisual) { g_dcompVisual->Release(); g_dcompVisual = nullptr; }
    if (g_dcompTarget) { g_dcompTarget->Release(); g_dcompTarget = nullptr; }
    if (g_dcomp) { g_dcomp->Release(); g_dcomp = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

static void SetupStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 0.0f;
    s.FrameRounding = 0.0f;
    s.PopupRounding = 0.0f;
    s.ScrollbarRounding = 0.0f;
    s.GrabRounding = 0.0f;
    s.TabRounding = 0.0f;
    s.WindowBorderSize = 0.0f;
    s.FrameBorderSize = 1.0f;
    s.WindowPadding = ImVec2(0, 0);
    s.FramePadding = ImVec2(
        8.0f * g_scale,
        (L::RowH - L::FontPx) * 0.5f * g_scale);
    s.ItemSpacing = ImVec2(8.0f * g_scale, 6.0f * g_scale);
    s.ScrollbarSize = 10.0f * g_scale;
    s.GrabMinSize = 10.0f * g_scale;
}

static void LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    const char* fonts[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf"
    };

    for (const char* f : fonts)
    {
        if (GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES &&
            io.Fonts->AddFontFromFileTTF(f, L::FontPx * g_scale))
            return;
    }

    ImFontConfig cfg = {};
    cfg.SizePixels = L::FontPx * g_scale;
    io.Fonts->AddFontDefault(&cfg);
}

static void RenderUI()
{
    float s = g_scale;
    float margin = L::Margin * s;
    float panelW = L::PanelW * s;
    float panelH = g_appH;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(ImVec2(margin, margin));
    ImGui::SetNextWindowSize(ImVec2(panelW, panelH));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, C_PANEL);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);

    ImGui::Begin("##loaderpanel", nullptr, flags);

    ImVec2 wp = ImGui::GetWindowPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float fs = ImGui::GetFontSize();

    dl->AddRectFilled(
        wp, ImVec2(wp.x + panelW, wp.y + panelH), COL_PANEL);
    dl->AddRectFilled(
        wp, ImVec2(wp.x + panelW, wp.y + L::HeaderH * s), COL_HEADER);
    dl->AddLine(
        ImVec2(wp.x, wp.y + L::HeaderH * s),
        ImVec2(wp.x + panelW, wp.y + L::HeaderH * s),
        COL_HEADER_SEP, 1.0f);
    dl->AddRect(
        wp, ImVec2(wp.x + panelW, wp.y + panelH),
        COL_PANEL_LINE, 0, 0, 1.0f);

    float cy = wp.y + L::HeaderH * s * 0.5f;

    dl->AddText(
        ImVec2(wp.x + L::PadX * s,
            wp.y + (L::HeaderH * s - fs) * 0.5f),
        COL_WHITE, "@hound_23");

    ImVec2 xc(wp.x + panelW - 20.0f * s, cy);
    float xr = 4.5f * s;

    dl->AddLine(
        ImVec2(xc.x - xr, xc.y - xr),
        ImVec2(xc.x + xr, xc.y + xr), COL_WHITE, 1.6f * s);
    dl->AddLine(
        ImVec2(xc.x - xr, xc.y + xr),
        ImVec2(xc.x + xr, xc.y - xr), COL_WHITE, 1.6f * s);

    float rowY = wp.y + (L::HeaderH + L::PadY) * s;

    const char* preview =
        g_dlls.empty() ? "No DLL folders found" :
        g_dlls[static_cast<size_t>(g_sel)].c_str();

    ImGui::SetCursorScreenPos(
        ImVec2(wp.x + L::PadX * s, rowY));

    ImGui::PushStyleColor(ImGuiCol_Text, C_TEXT);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, C_FRAME);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, C_FRAME_HOV);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, C_FRAME_ACT);
    ImGui::PushStyleColor(ImGuiCol_Border, C_BORDER);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, C_PANEL);
    ImGui::PushStyleColor(ImGuiCol_Header, C_SEL);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, C_FRAME_HOV);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * s, 0));

    ImGui::SetNextItemWidth(L::ComboW * s);

    if (ImGui::BeginCombo("##dll", preview, 0))
    {
        for (size_t i = 0; i < g_dlls.size(); ++i)
        {
            bool selected = static_cast<int>(i) == g_sel;

            if (ImGui::Selectable(g_dlls[i].c_str(), selected))
                g_sel = static_cast<int>(i);

            if (selected)
                ImGui::SetItemDefaultFocus();
        }

        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(5);
    ImGui::PopStyleColor(8);

    ImGui::SetCursorScreenPos(ImVec2(
        wp.x + panelW - L::PadX * s - L::BtnW * s,
        rowY));

    ImGui::PushStyleColor(ImGuiCol_Text, C_TEXT);
    ImGui::PushStyleColor(ImGuiCol_Button, C_BTN);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, C_BTN_HOVER);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, C_BTN_ACTIVE);
    ImGui::PushStyleColor(ImGuiCol_Border, C_BORDER);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

    ImGui::BeginDisabled(g_loading.load() || g_dlls.empty());

    if (ImGui::Button(
        g_loading.load() ? "Loading..." : "Load",
        ImVec2(L::BtnW * s, L::RowH * s)))
        LoadSelected();

    ImGui::EndDisabled();

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);

    bool popupOpen = ImGui::IsPopupOpen("##dll");
    float wantH = L::PanelRh * s;

    if (popupOpen && !g_dlls.empty())
    {
        int n = std::min(
            static_cast<int>(g_dlls.size()), L::MaxPopupItems);
        wantH += n * ImGui::GetFrameHeightWithSpacing() + 2.0f * s;
    }

    g_wantH = wantH;

    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 m = io.MousePos;

    bool inHeader =
        m.x >= margin && m.x <= margin + panelW &&
        m.y >= margin && m.y <= margin + L::HeaderH * s;

    bool overX =
        std::fabs(m.x - xc.x) <= 11.0f * s &&
        std::fabs(m.y - xc.y) <= 11.0f * s;

    static bool dragging = false;
    static POINT dragCursor = {};
    static POINT dragWindow = {};

    if (io.MouseClicked[0])
    {
        if (overX)
            g_quitRequested = true;
        else if (inHeader)
        {
            dragging = true;
            GetCursorPos(&dragCursor);

            RECT r = {};
            GetWindowRect(g_hwnd, &r);
            dragWindow.x = r.left;
            dragWindow.y = r.top;
        }
    }

    if (dragging)
    {
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
        {
            POINT p = {};
            GetCursorPos(&p);

            SetWindowPos(
                g_hwnd, nullptr,
                dragWindow.x + p.x - dragCursor.x,
                dragWindow.y + p.y - dragCursor.y,
                0, 0,
                SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        else
        {
            dragging = false;
        }
    }

    if (g_quitRequested)
        DestroyWindow(g_hwnd);
}

static void RenderFrame()
{
    if (g_quitRequested)
        return;

    if (std::fabs(g_wantH - g_appH) > 0.5f)
        ResizePanelTo(g_wantH);

    const float clear[4] = { 0, 0, 0, 0 };

    g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_context->ClearRenderTargetView(g_rtv, clear);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    RenderUI();

    if (g_quitRequested)
        return;

    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    g_swap->Present(1, 0);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND, UINT, WPARAM, LPARAM);

static LRESULT CALLBACK WndProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(
        hwnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(
    HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    SetProcessDPIAware();

    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);

    if (dpi <= 0)
        dpi = 96;

    int screenH = GetSystemMetrics(SM_CYSCREEN);

    if (screenH <= 0)
        screenH = static_cast<int>(L::RefH);

    g_scale = std::max(
        static_cast<float>(dpi) / 96.0f,
        static_cast<float>(screenH) / L::RefH) * L::Density;

    g_wantH = g_appH = L::PanelRh * g_scale;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"LoaderPanelWnd";

    if (!RegisterClassExW(&wc))
        return 1;

    int winW = static_cast<int>(
        std::lround((L::PanelW + 2 * L::Margin) * g_scale));

    int winH = static_cast<int>(
        std::lround((L::PanelRh + 2 * L::Margin) * g_scale));

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hwnd = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"Loader",
        WS_POPUP,
        std::max(0, (screenW - winW) / 2),
        std::max(0, (screenH - winH) / 2),
        winW,
        winH,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!g_hwnd)
    {
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    if (!InitD3D())
    {
        CleanupD3D();
        DestroyWindow(g_hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    SetupStyle();
    LoadFonts();

    if (!ImGui_ImplWin32_Init(g_hwnd))
    {
        ImGui::DestroyContext();
        CleanupD3D();
        DestroyWindow(g_hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    if (!ImGui_ImplDX11_Init(g_device, g_context))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        CleanupD3D();
        DestroyWindow(g_hwnd);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    EnumerateDlls();

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    bool running = true;
    MSG msg = {};

    while (running)
    {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);

            if (msg.message == WM_QUIT)
            {
                running = false;
                break;
            }
        }

        if (!running)
            break;

        RenderFrame();
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupD3D();
    UnregisterClassW(wc.lpszClassName, hInstance);

    return static_cast<int>(msg.wParam);
}