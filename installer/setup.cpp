#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>

// resource ids
#define RID_EXE      100
#define RID_MANIFEST 101
#define RID_ICON     1

static std::wstring g_installDir;
static std::wstring g_exePath;

static void GetTargets(){
    // Install to D:\GlassDock by default (user rule: keep software off C:).
    // Falls back to C: only if the D: drive does not exist or is unwritable.
    wchar_t local[MAX_PATH] = {0};
    DWORD attr = GetFileAttributesW(L"D:\\");
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)){
        g_installDir = L"D:\\GlassDock";
    } else {
        SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local);
        g_installDir = std::wstring(local) + L"\\Programs\\GlassDock";
    }
    g_exePath = g_installDir + L"\\GlassDock.exe";
}

static bool ExtractResource(int id, const wchar_t* destPath){
    HRSRC hr = FindResourceW(NULL, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!hr) return false;
    HGLOBAL hg = LoadResource(NULL, hr);
    if (!hg) return false;
    void* data = LockResource(hg);
    DWORD size = SizeofResource(NULL, hr);
    if (!data || !size) return false;

    HANDLE hf = CreateFileW(destPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(hf, data, size, &written, NULL);
    CloseHandle(hf);
    return ok && written == size;
}

static bool CreateShortcut(const wchar_t* lnkPath){
    CoInitialize(NULL);
    IShellLinkW* psl = NULL;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                   IID_IShellLinkW, (void**)&psl))){
        psl->SetPath(g_exePath.c_str());
        psl->SetWorkingDirectory(g_installDir.c_str());
        psl->SetIconLocation(g_exePath.c_str(), 0);
        psl->SetDescription(L"GlassDock 桌面文件夹小组件");
        IPersistFile* ppf = NULL;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf))){
            if (SUCCEEDED(ppf->Save(lnkPath, TRUE))) ok = true;
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    return ok;
}

static void WriteUninstallEntry(){
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GlassDock",
        0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS) return;

    DWORD one = 1, zero = 0;
    std::wstring un = L"\"" + g_exePath + L"\" --uninstall";
    RegSetValueExW(k, L"DisplayName", 0, REG_SZ, (const BYTE*)L"GlassDock 桌面文件夹小组件",
                   (DWORD)((wcslen(L"GlassDock 桌面文件夹小组件") + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"DisplayVersion", 0, REG_SZ, (const BYTE*)L"1.0.0", 12);
    RegSetValueExW(k, L"Publisher", 0, REG_SZ, (const BYTE*)L"GlassDock", 20);
    RegSetValueExW(k, L"InstallLocation", 0, REG_SZ, (const BYTE*)g_installDir.c_str(),
                   (DWORD)((g_installDir.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"UninstallString", 0, REG_SZ, (const BYTE*)un.c_str(),
                   (DWORD)((un.size() + 1) * sizeof(wchar_t)));
    RegSetValueExW(k, L"NoModify", 0, REG_DWORD, (const BYTE*)&one, 4);
    RegSetValueExW(k, L"NoRepair", 0, REG_DWORD, (const BYTE*)&one, 4);
    RegSetValueExW(k, L"EstimatedSize", 0, REG_DWORD, (const BYTE*)&one, 4);
    RegSetValueExW(k, L"SystemComponent", 0, REG_DWORD, (const BYTE*)&zero, 4);
    RegCloseKey(k);
}

static void LaunchApp(){
    ShellExecuteW(NULL, L"open", g_exePath.c_str(), NULL, g_installDir.c_str(), SW_SHOWNORMAL);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int){
    SetProcessDPIAware();
    GetTargets();

    // create install directory
    if (SHCreateDirectoryExW(NULL, g_installDir.c_str(), NULL) != ERROR_SUCCESS &&
        GetFileAttributesW(g_installDir.c_str()) == INVALID_FILE_ATTRIBUTES){
        MessageBoxW(NULL, L"无法创建安装目录。", L"GlassDock 安装程序", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!ExtractResource(RID_EXE, g_exePath.c_str()) ||
        !ExtractResource(RID_MANIFEST, (g_installDir + L"\\GlassDock.exe.manifest").c_str())){
        MessageBoxW(NULL, L"安装文件写入失败。", L"GlassDock 安装程序", MB_OK | MB_ICONERROR);
        return 1;
    }

    // shortcuts
    wchar_t desktop[MAX_PATH] = {0}, programs[MAX_PATH] = {0};
    SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop);
    SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, programs);
    if (desktop[0]){
        std::wstring p = std::wstring(desktop) + L"\\GlassDock.lnk";
        CreateShortcut(p.c_str());
    }
    if (programs[0]){
        std::wstring p = std::wstring(programs) + L"\\GlassDock.lnk";
        CreateShortcut(p.c_str());
    }

    WriteUninstallEntry();

    int ret = MessageBoxW(NULL,
        L"GlassDock 已安装完成。\n\n是否立即启动？",
        L"GlassDock 安装程序", MB_YESNO | MB_ICONINFORMATION);
    if (ret == IDYES) LaunchApp();

    return 0;
}
