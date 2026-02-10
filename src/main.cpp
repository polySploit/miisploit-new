#include <Windows.h>
#include <CommCtrl.h>
#include <commdlg.h>
#include <atomic>
#include <string>
#include <vector>
#include <mutex>
#include <fstream>
#include <sstream>
#include <ctime>
#include <ShlObj.h>
#include "unity.hpp"
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
using U = UnityResolve;
using UT = U::UnityType;
using UC = UT::Component;
static std::atomic<bool> g_Running = true;
static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hTab = NULL;
static HWND g_hTreeView = NULL;
static HWND g_hListInfo = NULL; 
static HWND g_hBtnRefresh = NULL;
static HWND g_hBtnDelete = NULL;
static HWND g_hBtnClone = NULL;
static HWND g_hEditName = NULL;
static HWND g_hBtnSetName = NULL;
static HWND g_hEditCmd = NULL;
static HWND g_hEditCmdLog = NULL;
static HWND g_hBtnRunCmd = NULL;
static HWND g_hEditScript = NULL;
static HWND g_hBtnExecute = NULL;
static HWND g_hBtnClear = NULL;
static HWND g_hBtnLoadFile = NULL;
static HWND g_hStatusLabel = NULL;
static HWND g_hOutputLog = NULL;
static HWND g_hCreditsLabel1 = NULL;
static HWND g_hCreditsLabel2 = NULL;
static HWND g_hCreditsLabel3 = NULL;
static void ShowTabControls(int tab);
static std::mutex g_LogMutex;
static std::vector<std::string> g_LogLines;
#include <unordered_map>
static std::unordered_map<HTREEITEM, void*> g_TreeItemToInstance;
enum {
    IDC_TAB = 1001,
    IDC_TREEVIEW,
    IDC_BTN_REFRESH,
    IDC_BTN_DELETE,
    IDC_BTN_CLONE,
    IDC_EDIT_NAME,
    IDC_BTN_SET_NAME,
    IDC_LIST_PROPS = 1100, 
    IDC_EDIT_CMD,
    IDC_CMD_LOG,
    IDC_BTN_RUN_CMD,
    IDC_EDIT_SCRIPT,
    IDC_BTN_EXECUTE,
    IDC_BTN_CLEAR,
    IDC_BTN_LOAD_FILE,
    IDC_STATUS_LABEL,
    IDC_OUTPUT_LOG,
    ID_CTX_COPY_PATH = 2000,
    ID_CTX_DECOMPILE
};
void LogOutput(const std::string& msg) {
    if (g_hOutputLog) {
        int len = GetWindowTextLengthA(g_hOutputLog);
        SendMessageA(g_hOutputLog, EM_SETSEL, len, len);
        std::string line = (len > 0 ? "\r\n" : "") + msg;
        SendMessageA(g_hOutputLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
        SendMessageA(g_hOutputLog, EM_SCROLLCARET, 0, 0);
    }
}
void LogCmd(const std::string& msg) {
    if (g_hEditCmdLog) {
        int len = GetWindowTextLengthA(g_hEditCmdLog);
        SendMessageA(g_hEditCmdLog, EM_SETSEL, len, len);
        std::string line = (len > 0 ? "\r\n" : "") + msg;
        SendMessageA(g_hEditCmdLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
        SendMessageA(g_hEditCmdLog, EM_SCROLLCARET, 0, 0);
    }
}
void SetStatus(const std::string& status) {
    if (g_hStatusLabel) {
        SetWindowTextA(g_hStatusLabel, status.c_str());
    }
}
void RunLuaScript(const std::string& script)
{
    const auto pAssembly = U::Get("Assembly-CSharp.dll");
    if (!pAssembly) { LogOutput("[Error] Assembly-CSharp.dll not found"); return; }
    const auto pScriptService = pAssembly->Get("ScriptService");
    const auto pGame = pAssembly->Get("Game");
    const auto pScriptInstance = pAssembly->Get("ScriptInstance");
    const auto pBaseScript = pAssembly->Get("BaseScript");
    if (pScriptService && pGame && pScriptInstance && pBaseScript) {
        const auto pGetScriptServiceInstanceField = pScriptService->Get<U::Field>("<Instance>k__BackingField");
        const auto pGameInstanceField = pGame->Get<U::Field>("singleton");
        UC* gameInstance = nullptr;
        if (pGameInstanceField) {
            pGameInstanceField->GetStaticValue<UC*>(&gameInstance);
        } else {
            LogOutput("[Error] Game singleton field not found");
            return;
        }
        UC* scriptServiceInstance = nullptr;
        if (pGetScriptServiceInstanceField) {
            pGetScriptServiceInstanceField->GetStaticValue<UC*>(&scriptServiceInstance);
        } else {
            LogOutput("[Error] ScriptService instance field not found");
            return;
        }
        if (scriptServiceInstance && gameInstance) {
            const auto pGameObject = gameInstance->GetGameObject();
            if (pGameObject) {
                UC* scriptInstance = pGameObject->AddComponent<UC*>(pScriptInstance);
                if (scriptInstance) {
                    const auto Script = UT::String::New(script);
                    if (Script) {
                        auto setFieldRecursive = [&](const std::string& name, auto value) -> bool {
                            if (pScriptInstance->Get<U::Field>(name)) {
                                pScriptInstance->SetValue(scriptInstance, name, value);
                                return true;
                            }
                            if (pBaseScript && pBaseScript->Get<U::Field>(name)) {
                                pBaseScript->SetValue(scriptInstance, name, value);
                                return true;
                            }
                            return false;
                        };
                        setFieldRecursive("source", Script);
                        setFieldRecursive("running", false);
                        const auto pRunScriptMethod = pScriptService->Get<U::Method>("RunScript");
                        if (pRunScriptMethod) {
                            const auto pRunScript = pRunScriptMethod->Cast<void, UC*, UC*>();
                            if (pRunScript) pRunScript(scriptServiceInstance, scriptInstance);
                        }
                    }
                }
            }
        }
    } else {
    }
}
static bool IsValidPtr(const void* ptr) {
    if (!ptr) return false;
    return !IsBadReadPtr(ptr, sizeof(void*));
}
struct PolytoriaAPI {
    U::Method* instanceGetChildren = nullptr;
    U::Method* instanceGetName = nullptr;  
    U::Method* instanceSetName = nullptr;  
    U::Method* instanceDestroy = nullptr;  
    U::Method* instanceClone = nullptr;    
    U::Class*  instanceClass = nullptr;
    U::Class*  baseScriptClass = nullptr;
    bool initialized = false;
    bool Init() {
        if (initialized) return (instanceClass != nullptr);
        initialized = true;
        try {
            const auto pAssembly = U::Get("Assembly-CSharp.dll");
            if (!pAssembly) { LogOutput("[API] Assembly-CSharp.dll not found"); return false; }
            instanceClass = pAssembly->Get("Instance", "Polytoria.Datamodel");
            if (!instanceClass) instanceClass = pAssembly->Get("Instance");
            if (!instanceClass) {
                LogOutput("[API] Instance class not found");
                return false;
            }
            instanceGetChildren = instanceClass->Get<U::Method>("GetChildren");
            if (!instanceGetChildren) LogOutput("[API] Warning: GetChildren not found");
            instanceGetName = instanceClass->Get<U::Method>("get_Name");
            if (!instanceGetName) instanceGetName = instanceClass->Get<U::Method>("GetName");
            if (!instanceGetName) instanceGetName = instanceClass->Get<U::Method>("get_name");
            if (!instanceGetName) {
                auto* coreAssembly = U::Get("UnityEngine.CoreModule.dll");
                if (coreAssembly) {
                    auto* objClass = coreAssembly->Get("Object");
                    if (objClass) {
                        instanceGetName = objClass->Get<U::Method>("get_name");
                    }
                }
            }
            if (!instanceGetName) LogOutput("[API] Warning: get_Name not found");
            instanceSetName = instanceClass->Get<U::Method>("SetName");
            if (!instanceSetName) instanceSetName = instanceClass->Get<U::Method>("set_Name");
            if (!instanceSetName) instanceSetName = instanceClass->Get<U::Method>("set_name");
            if (!instanceSetName) {
                auto* coreAssembly = U::Get("UnityEngine.CoreModule.dll");
                if (coreAssembly) {
                    auto* objClass = coreAssembly->Get("Object");
                    if (objClass) {
                        instanceSetName = objClass->Get<U::Method>("set_name");
                    }
                }
            }
            if (!instanceSetName) LogOutput("[API] Warning: SetName not found");
            instanceDestroy = instanceClass->Get<U::Method>("Destroy", {"System.Single"});
            if (!instanceDestroy) instanceDestroy = instanceClass->Get<U::Method>("Destroy");
            if (!instanceDestroy) LogOutput("[API] Warning: Destroy not found");
            instanceClone = instanceClass->Get<U::Method>("Clone");
            if (!instanceClone) LogOutput("[API] Warning: Clone not found");
            baseScriptClass = pAssembly->Get("BaseScript", "Polytoria.Datamodel");
            if (!baseScriptClass) baseScriptClass = pAssembly->Get("BaseScript");
            LogOutput("[API] Polytoria API initialized successfully");
            return true;
        } catch (...) {
            LogOutput("[API] Error initializing Polytoria API");
            return false;
        }
    }
};
static PolytoriaAPI g_API;
static void* GetGameInstance() {
    try {
        const auto pAssembly = U::Get("Assembly-CSharp.dll");
        if (!pAssembly) return nullptr;
        const auto pGame = pAssembly->Get("Game");
        if (!pGame) return nullptr;
        const auto pField = pGame->Get<U::Field>("singleton");
        if (!pField) return nullptr;
        void* inst = nullptr;
        pField->GetStaticValue<void*>(&inst);
        if (!IsValidPtr(inst)) return nullptr;
        return inst;
    } catch (...) {
        return nullptr;
    }
}
static std::string SafeGetInstanceName(void* instance) {
    if (!IsValidPtr(instance)) return "???";
    try {
        if (g_API.instanceGetName) {
            auto fn = g_API.instanceGetName->Cast<UT::String*, void*>();
            if (fn) {
                auto* nameStr = fn(instance);
                if (IsValidPtr(nameStr) && nameStr->m_stringLength > 0) {
                    return nameStr->ToString();
                }
            }
        }
        return "???";
    } catch (...) {
        return "???";
    }
}
struct ChildrenResult {
    void** children = nullptr;
    int count = 0;
};
static ChildrenResult GetInstanceChildren(void* instance) {
    ChildrenResult result;
    if (!IsValidPtr(instance) || !g_API.instanceGetChildren) return result;
    try {
        auto fn = g_API.instanceGetChildren->Cast<void*, void*>();
        if (!fn) return result;
        void* arr = fn(instance);
        if (!IsValidPtr(arr)) return result;
        auto* typedArr = reinterpret_cast<UT::Array<void*>*>(arr);
        if (!IsValidPtr(typedArr)) return result;
        int count = (int)typedArr->max_length;
        if (count < 0 || count > 2000) return result; 
        result.count = count;
        result.children = (void**)&typedArr->vector;  
        return result;
    } catch (...) {
        return result;
    }
}
static bool DestroyInstance(void* instance) {
    if (!IsValidPtr(instance) || !g_API.instanceDestroy) return false;
    try {
        auto fn = g_API.instanceDestroy->Cast<void, void*, float>();
        if (fn) {
            fn(instance, 0.0f);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}
static bool CloneInstance(void* instance) {
    if (!IsValidPtr(instance) || !g_API.instanceClone) return false;
    try {
        auto fn = g_API.instanceClone->Cast<void*, void*>();
        if (fn) {
            fn(instance);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}
static bool SetInstanceName(void* instance, const char* name) {
    if (!IsValidPtr(instance) || !g_API.instanceSetName) return false;
    try {
        auto* newName = UT::String::New(std::string(name));
        if (!newName) return false;
        auto fn = g_API.instanceSetName->Cast<void, void*, UT::String*>();
        if (fn) {
            fn(instance, newName);
            return true;
        }
        return false;
    } catch (...) {
        return false;
    }
}
static int DetectScriptType(void* instance) {
    if (!IsValidPtr(instance)) return -1;
    try {
        const auto pAssembly = U::Get("Assembly-CSharp.dll");
        if (!pAssembly) return -1;
        const char* scriptTypes[] = { "LocalScript", "ScriptInstance", "ModuleScript" };
        for (int i = 0; i < 3; i++) {
            try {
                auto* pClass = pAssembly->Get(scriptTypes[i], "Polytoria.Datamodel");
                if (!pClass) pClass = pAssembly->Get(scriptTypes[i]);
                if (pClass) {
                    void* objClass = UnityResolve::Invoke<void*>("il2cpp_object_get_class", instance);
                    if (objClass == pClass->address) return i;
                    void* parentClass = objClass;
                    for (int depth = 0; depth < 5 && parentClass; depth++) {
                        if (parentClass == pClass->address) return i;
                        parentClass = UnityResolve::Invoke<void*>("il2cpp_class_get_parent", parentClass);
                    }
                }
            } catch (...) {
                continue;
            }
        }
        return -1;
    } catch (...) {
        return -1;
    }
}
static int ReadScriptSource(void* instance, char* outBuf, int outBufSize) {
    if (!IsValidPtr(instance)) return 0;
    try {
        const auto pAssembly = U::Get("Assembly-CSharp.dll");
        if (!pAssembly) return 0;
        U::Class* baseScript = pAssembly->Get("BaseScript", "Polytoria.Datamodel");
        if (!baseScript) baseScript = pAssembly->Get("BaseScript");
        const char* classNames[] = { "LocalScript", "ScriptInstance", "ModuleScript", "BaseScript" };
        const char* fieldNames[] = { "Source", "source", "Networksource" };
        for (const char* clsName : classNames) {
            U::Class* pClass = pAssembly->Get(clsName, "Polytoria.Datamodel");
            if (!pClass) pClass = pAssembly->Get(clsName);
            if (!pClass) continue;
            for (const char* fieldName : fieldNames) {
                try {
                    U::Field* pField = pClass->Get<U::Field>(fieldName);
                    if (pField && pField->offset > 0) {
                        auto* srcStr = *reinterpret_cast<UT::String**>(
                            reinterpret_cast<uintptr_t>(instance) + pField->offset);
                        if (IsValidPtr(srcStr) && srcStr->m_stringLength > 0) {
                            int len = WideCharToMultiByte(CP_UTF8, 0,
                                srcStr->m_firstChar, srcStr->m_stringLength,
                                outBuf, outBufSize - 1, NULL, NULL);
                            if (len > 0) {
                                outBuf[len] = '\0';
                                return len;
                            }
                        }
                    }
                } catch (...) {
                    continue;
                }
            }
        }
        return 0;
    } catch (...) {
        return 0;
    }
}
static void PopulateTreeRecursive(HWND hTree, HTREEITEM hParent, void* instance, int depth) {
    if (!IsValidPtr(instance) || depth > 10) return;
    std::string name = SafeGetInstanceName(instance);
    TVINSERTSTRUCTA tvis = {};
    tvis.hParent = hParent;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT;
    tvis.item.pszText = (LPSTR)name.c_str();
    HTREEITEM hItem = (HTREEITEM)SendMessageA(hTree, TVM_INSERTITEMA, 0, (LPARAM)&tvis);
    if (hItem) {
        g_TreeItemToInstance[hItem] = instance;
    }
    auto children = GetInstanceChildren(instance);
    for (int i = 0; i < children.count; i++) {
        try {
            void* child = children.children[i];
            if (IsValidPtr(child)) {
                PopulateTreeRecursive(hTree, hItem, child, depth + 1);
            }
        } catch (...) {
            continue;
        }
    }
}
static void RefreshExplorer() {
    SetStatus("Refreshing...");
    TreeView_DeleteAllItems(g_hTreeView);
    g_TreeItemToInstance.clear();
    if (!g_API.Init()) {
        LogOutput("[Explorer] Failed to initialize Polytoria API");
        SetStatus("Ready");
        return;
    }
    void* gameInstance = GetGameInstance();
    if (!gameInstance) {
        LogOutput("[Explorer] Game instance not found");
        SetStatus("Ready");
        return;
    }
    TVINSERTSTRUCTA tvis = {};
    tvis.hParent = TVI_ROOT;
    tvis.hInsertAfter = TVI_LAST;
    tvis.item.mask = TVIF_TEXT;
    char rootName[] = "Game";
    tvis.item.pszText = rootName;
    HTREEITEM hRoot = (HTREEITEM)SendMessageA(g_hTreeView, TVM_INSERTITEMA, 0, (LPARAM)&tvis);
    g_TreeItemToInstance[hRoot] = gameInstance;
    auto children = GetInstanceChildren(gameInstance);
    for (int i = 0; i < children.count; i++) {
        try {
            void* child = children.children[i];
            if (IsValidPtr(child)) {
                PopulateTreeRecursive(g_hTreeView, hRoot, child, 1);
            }
        } catch (...) {
            continue;
        }
    }
    TreeView_Expand(g_hTreeView, hRoot, TVE_EXPAND);
    LogOutput("[Explorer] Refreshed");
    LogOutput("You can decompile the scripts by double clicking btw so");
    SetStatus("Ready");
}
static void* GetSelectedInstance() {
    HTREEITEM hSel = TreeView_GetSelection(g_hTreeView);
    if (!hSel) return nullptr;
    auto it = g_TreeItemToInstance.find(hSel);
    if (it == g_TreeItemToInstance.end()) return nullptr;
    return it->second;
}
static void DeleteSelected() {
    auto* instance = GetSelectedInstance();
    if (!instance) { LogOutput("[Explorer] No item selected"); return; }
    if (DestroyInstance(instance)) {
        LogOutput("[Explorer] Object destroyed");
        RefreshExplorer();
    } else {
        LogOutput("[Explorer] Error destroying object");
    }
}
static void CloneSelected() {
    auto* instance = GetSelectedInstance();
    if (!instance) { LogOutput("[Explorer] No item selected"); return; }
    if (CloneInstance(instance)) {
        LogOutput("[Explorer] Object cloned");
        RefreshExplorer();
    } else {
        LogOutput("[Explorer] Error cloning object");
    }
}
static void SetSelectedName() {
    auto* instance = GetSelectedInstance();
    if (!instance) { LogOutput("[Explorer] No item selected"); return; }
    char buf[256] = {};
    GetWindowTextA(g_hEditName, buf, 255);
    if (strlen(buf) == 0) { LogOutput("[Explorer] Name is empty"); return; }
    if (SetInstanceName(instance, buf)) {
        LogOutput("[Explorer] Name set to: " + std::string(buf));
        RefreshExplorer();
    } else {
        LogOutput("[Explorer] Error setting name");
    }
}
static void RefreshInfo(void* instance) {
    SendMessageA(g_hListInfo, LB_RESETCONTENT, 0, 0);
    if (!IsValidPtr(instance)) return;
    char addrBuf[64];
    sprintf_s(addrBuf, "Address: 0x%p", instance);
    SendMessageA(g_hListInfo, LB_ADDSTRING, 0, (LPARAM)addrBuf);
    std::string name = SafeGetInstanceName(instance);
    if (!name.empty()) {
        std::string s = "Name: " + name;
        SendMessageA(g_hListInfo, LB_ADDSTRING, 0, (LPARAM)s.c_str());
    }
    std::string className = "Instance";
    static auto* get_class = (void*(*)(void*))GetProcAddress((HMODULE)GetModuleHandle("GameAssembly.dll"), "il2cpp_object_get_class");
    static auto* class_get_name = (const char*(*)(void*))GetProcAddress((HMODULE)GetModuleHandle("GameAssembly.dll"), "il2cpp_class_get_name");
    if (get_class && class_get_name) {
         void* klass = get_class(instance);
         if (klass) {
             const char* clsName = class_get_name(klass);
             if (clsName) className = clsName;
         }
    }
    std::string clsStr = "Class: " + className;
    SendMessageA(g_hListInfo, LB_ADDSTRING, 0, (LPARAM)clsStr.c_str());
}
static int g_DecompileChoice = 0; 
static LRESULT CALLBACK DecompileDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND) {
        int id = LOWORD(wParam);
        if (id >= 1 && id <= 3) g_DecompileChoice = id;
        else g_DecompileChoice = 0;
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_CLOSE) {
        g_DecompileChoice = 0;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
static int ShowDecompileChoice(HWND parent) {
    g_DecompileChoice = 0;
    const char* DLG_CLASS = "MiisploitDecompileDlg";
    WNDCLASSEXA wc = {};
    if (!GetClassInfoExA(g_hInstance, DLG_CLASS, &wc)) {
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DecompileDlgProc;
        wc.hInstance = g_hInstance;
        wc.lpszClassName = DLG_CLASS;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassExA(&wc);
    }
    RECT rc; GetWindowRect(parent, &rc);
    int w = 300, h = 180; 
    int x = rc.left + (rc.right - rc.left - w) / 2;
    int y = rc.top + (rc.bottom - rc.top - h) / 2;
    HWND hDlg = CreateWindowExA(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, DLG_CLASS, "Decompile Script",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 
        x, y, w, h, parent, NULL, g_hInstance, NULL);
    CreateWindowA("STATIC", "Choose an action:", WS_CHILD | WS_VISIBLE | SS_CENTER, 
        10, 10, 280, 20, hDlg, NULL, g_hInstance, NULL);
    CreateWindowA("BUTTON", "Copy to Clipboard", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        50, 40, 200, 30, hDlg, (HMENU)1, g_hInstance, NULL);
    CreateWindowA("BUTTON", "Load to Editor", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        50, 80, 200, 30, hDlg, (HMENU)2, g_hInstance, NULL);
    CreateWindowA("BUTTON", "Save to File", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        50, 120, 200, 30, hDlg, (HMENU)3, g_hInstance, NULL);
    EnableWindow(parent, FALSE); 
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsWindow(hDlg)) break; 
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    EnableWindow(parent, TRUE); 
    SetFocus(parent);
    return g_DecompileChoice;
}
static void SaveScriptToFile(const std::string& source, const std::string& type) {
    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(g_hInstance, path, MAX_PATH)) {
        char* pSlash = strrchr(path, '\\');
        if (pSlash) *pSlash = '\0';
        std::string dir = std::string(path) + "\\ScriptDecompiles";
        CreateDirectoryA(dir.c_str(), NULL);
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        char timestamp[64] = {};
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
        std::string filename = dir + "\\" + type + "_" + timestamp + ".lua";
        std::ofstream file(filename);
        if (file.is_open()) {
            file << source;
            file.close();
            LogOutput("[Decompile] Saved to: " + filename);
            MessageBoxA(g_hMainWnd, ("Saved to:\n" + filename).c_str(), "Saved", MB_OK | MB_ICONINFORMATION);
        } else {
            LogOutput("[Decompile] Failed to save file");
        }
    }
}
static void HandleScriptClick(HTREEITEM hItem) {
    auto it = g_TreeItemToInstance.find(hItem);
    if (it == g_TreeItemToInstance.end()) return;
    void* instance = it->second;
    if (!IsValidPtr(instance)) return;
    int scriptIdx = DetectScriptType(instance);
    if (scriptIdx < 0) return; 
    const char* scriptTypeNames[] = { "LocalScript", "ScriptInstance", "ModuleScript" };
    const char* className = scriptTypeNames[scriptIdx];
    int choice = ShowDecompileChoice(g_hMainWnd);
    if (choice == 0) return; 
    static char sourceBuf[1024 * 1024]; 
    int len = ReadScriptSource(instance, sourceBuf, sizeof(sourceBuf));
    if (len <= 0) {
        LogOutput("[Decompile] No source found");
        MessageBoxA(g_hMainWnd, "No source code found.", "Decompile Error", MB_OK | MB_ICONWARNING);
        return;
    }
    std::string sourceStr(sourceBuf, len);
    if (choice == 1) { 
        if (OpenClipboard(g_hMainWnd)) {
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
            if (hMem) {
                char* pMem = (char*)GlobalLock(hMem);
                memcpy(pMem, sourceBuf, len + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
            CloseClipboard();
            LogOutput("[Decompile] Source copied to clipboard");
            MessageBoxA(g_hMainWnd, "Copied to clipboard!", "Decompile", MB_OK | MB_ICONINFORMATION);
        }
    } else if (choice == 2) { 
        SetWindowTextA(g_hEditScript, sourceStr.c_str());
        TabCtrl_SetCurSel(g_hTab, 1);
        ShowTabControls(1);
        LogOutput("[Decompile] Loaded to editor");
    } else if (choice == 3) { 
        SaveScriptToFile(sourceStr, className);
    }
}
static void SaveInstance() {
    try {
        const auto pAssembly = U::Get("Assembly-CSharp.dll");
        if (!pAssembly) { LogCmd("[Error] Assembly-CSharp.dll not found"); return; }
        auto* pGameIOClass = pAssembly->Get("GameIO", "Polytoria.Controllers");
        if (!pGameIOClass) pGameIOClass = pAssembly->Get("GameIO");
        if (!pGameIOClass) { LogCmd("[Error] GameIO class not found"); return; }
        auto* saveMethod = pGameIOClass->Get<U::Method>("SaveToFile", {"System.String"});
        if (!saveMethod) saveMethod = pGameIOClass->Get<U::Method>("SaveToFile");
        if (!saveMethod) { LogCmd("[Error] SaveToFile method not found"); return; }
        char desktopPath[MAX_PATH] = {};
        if (FAILED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
            LogCmd("[Error] Failed to get Desktop path"); return;
        }
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        char timestamp[64] = {};
        strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &timeinfo);
        std::string filePath = std::string(desktopPath) + "\\MIISPLOIT_" + timestamp + ".poly";
        auto* pathStr = UT::String::New(filePath);
        if (!pathStr) { LogCmd("[Error] Failed to create path string"); return; }
        void* gameInstance = GetGameInstance();
        void* gameIOComponent = nullptr;
        bool success = false;
        if (gameInstance) {
            auto* gameComp = (UT::Component*)gameInstance;
            auto* go = gameComp->GetGameObject();
            if (go) {
                 gameIOComponent = go->GetComponent<void*>(pGameIOClass);
            }
        }
        if (gameIOComponent) {
            auto fn = saveMethod->Cast<void, void*, UT::String*>();
            if (fn) {
                LogCmd("Attempting instance save...");
                fn(gameIOComponent, pathStr);
                success = true;
            }
        }
        if (!success) {
            LogCmd("Instance attempt failed/skipped. Attempting static save...");
            auto fnStatic = saveMethod->Cast<void, UT::String*>();
            if (fnStatic) {
                fnStatic(pathStr);
                success = true;
            }
        }
        if (success) {
            LogCmd("Saved to: " + filePath);
            MessageBoxA(g_hMainWnd, ("Game saved to:\n" + filePath).c_str(), "Save Successful", MB_OK | MB_ICONINFORMATION);
        } else {
            LogCmd("[Error] Failed to execute SaveToFile (checked instance and static)");
        }
    } catch (...) {
        LogCmd("[Exception] Error saving game");
    }
}
static void ProcessCommand(const std::string& input) {
    if (input.empty()) return;
    if (input == "clear" || input == "cls") {
        SetWindowTextA(g_hEditCmdLog, "");
        return;
    }
    if (input == "help") {
        LogCmd("=== MiiSploit Commands ===");
        LogCmd("sh <health>     - Set local player health");
        LogCmd("saveinstance    - Save game to Desktop as .poly");
        LogCmd("clear           - Clear output log");
        LogCmd("help            - Show this help");
        return;
    }
    if (input.substr(0, 3) == "sh ") {
        std::string healthStr = input.substr(3);
        try {
            float health = std::stof(healthStr);
            std::string lua = "game[\"Players\"].LocalPlayer.Health = " + std::to_string(health);
            RunLuaScript(lua);
            LogCmd("Set health to " + std::to_string(health));
        } catch (...) {
            LogCmd("Invalid health value: " + healthStr);
        }
        return;
    }
    if (input == "saveinstance" || input.substr(0, 13) == "saveinstance(") {
        SaveInstance();
        return;
    }
    LogCmd("Unknown command: " + input + ", please type 'help' for all commands");
}
static int g_CurrentTab = 0; 
static void ShowTabControls(int tab) {
    g_CurrentTab = tab;
    BOOL showExplorer = (tab == 0) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hTreeView, showExplorer);
    ShowWindow(g_hBtnRefresh, showExplorer);
    ShowWindow(g_hBtnDelete, showExplorer);
    ShowWindow(g_hBtnClone, showExplorer);
    ShowWindow(g_hEditName, showExplorer);
    ShowWindow(g_hBtnSetName, showExplorer);
    ShowWindow(g_hListInfo, showExplorer); 
    BOOL showLua = (tab == 1) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hEditScript, showLua);
    BOOL showCmd = (tab == 2) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hEditCmdLog, showCmd);
    ShowWindow(g_hEditCmd, showCmd);
    ShowWindow(g_hBtnRunCmd, showCmd);
    ShowWindow(g_hOutputLog, (tab == 0 || tab == 1) ? SW_SHOW : SW_HIDE);
    BOOL showBottomBar = (tab == 0 || tab == 1);
    ShowWindow(g_hBtnExecute, showBottomBar ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnClear, showBottomBar ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hBtnLoadFile, showBottomBar ? SW_SHOW : SW_HIDE);
    ShowWindow(g_hStatusLabel, showBottomBar ? SW_SHOW : SW_HIDE);
    BOOL showCredits = (tab == 3) ? SW_SHOW : SW_HIDE;
    ShowWindow(g_hCreditsLabel1, showCredits);
    ShowWindow(g_hCreditsLabel2, showCredits);
    ShowWindow(g_hCreditsLabel3, showCredits);
}
static void LayoutControls(int width, int height) {
    if (!g_hTab) return;
    int margin = 5;
    int bottomBarHeight = 130; 
    int tabHeight = 25;
    int btnHeight = 25;
    int btnWidth = 70;
    MoveWindow(g_hTab, 0, 0, width, tabHeight + 5, TRUE);
    int contentTop = tabHeight + 10;
    int contentHeight = height - contentTop - bottomBarHeight;
    int explorerBtnY = contentTop;
    int bx = margin;
    MoveWindow(g_hBtnRefresh, bx, explorerBtnY, btnWidth, btnHeight, TRUE); bx += btnWidth + margin;
    MoveWindow(g_hBtnDelete, bx, explorerBtnY, btnWidth, btnHeight, TRUE); bx += btnWidth + margin;
    MoveWindow(g_hBtnClone, bx, explorerBtnY, btnWidth, btnHeight, TRUE); bx += btnWidth + margin;
    MoveWindow(g_hEditName, bx, explorerBtnY, 120, btnHeight, TRUE); bx += 120 + margin;
    MoveWindow(g_hBtnSetName, bx, explorerBtnY, 40, btnHeight, TRUE);
    int treeTop = explorerBtnY + btnHeight + margin;
    int contentH = contentHeight - btnHeight - margin;
    int treeWidth = (int)((width - margin * 3) * 0.7f);
    int infoWidth = (width - margin * 3) - treeWidth;
    MoveWindow(g_hTreeView, margin, treeTop, treeWidth, contentH, TRUE);
    MoveWindow(g_hListInfo, margin + treeWidth + margin, treeTop, infoWidth, contentH, TRUE);
    MoveWindow(g_hEditScript, margin, contentTop, width - margin * 2, contentHeight, TRUE);
    int cmdInputHeight = 25;
    int cmdLogHeight = height - contentTop - cmdInputHeight - margin * 2;
    MoveWindow(g_hEditCmdLog, margin, contentTop, width - margin * 2, cmdLogHeight, TRUE);
    int cmdInputTop = contentTop + cmdLogHeight + margin;
    int runBtnWidth = 60;
    MoveWindow(g_hEditCmd, margin, cmdInputTop, width - margin * 2 - runBtnWidth - margin, cmdInputHeight, TRUE);
    MoveWindow(g_hBtnRunCmd, width - margin - runBtnWidth, cmdInputTop, runBtnWidth, cmdInputHeight, TRUE);
    int bottomY = height - bottomBarHeight;
    bx = margin;
    MoveWindow(g_hBtnExecute, bx, bottomY, btnWidth, btnHeight, TRUE); bx += btnWidth + margin;
    MoveWindow(g_hBtnClear, bx, bottomY, btnWidth, btnHeight, TRUE); bx += btnWidth + margin;
    MoveWindow(g_hBtnLoadFile, bx, bottomY, btnWidth + 10, btnHeight, TRUE); bx += btnWidth + 10 + margin;
    MoveWindow(g_hStatusLabel, bx, bottomY, width - bx - margin, btnHeight, TRUE);
    int logTop = bottomY + btnHeight + margin;
    int logHeight = height - logTop - margin;
    MoveWindow(g_hOutputLog, margin, logTop, width - margin * 2, logHeight, TRUE);
    int creditsY = contentTop + 40;
    int labelH = 25;
    MoveWindow(g_hCreditsLabel1, margin, creditsY, width - margin * 2, labelH, TRUE);
    creditsY += labelH + margin;
    MoveWindow(g_hCreditsLabel2, margin, creditsY, width - margin * 2, labelH, TRUE);
    creditsY += labelH + margin;
    MoveWindow(g_hCreditsLabel3, margin, creditsY, width - margin * 2, labelH, TRUE);
}
static std::string GetInstancePath(HTREEITEM hItem) {
    std::vector<std::string> parts;
    HTREEITEM current = hItem;
    while (current) {
        auto it = g_TreeItemToInstance.find(current);
        if (it != g_TreeItemToInstance.end()) {
            void* inst = it->second;
            if (IsValidPtr(inst)) {
                std::string nameStr = SafeGetInstanceName(inst);
                if (!nameStr.empty()) {
                    parts.push_back(nameStr);
                } else {
                    parts.push_back(std::string("Instance"));
                }
            } else {
            }
        }
        current = TreeView_GetParent(g_hTreeView, current);
    }
    if (parts.empty()) return "";
    std::string path = "game";
    for (int i = (int)parts.size() - 2; i >= 0; i--) {
        path += "[\"" + parts[i] + "\"]";
    }
    return path;
}
static void ShowExplorerContextMenu(POINT pt, HTREEITEM hItem) {
    auto it = g_TreeItemToInstance.find(hItem);
    if (it == g_TreeItemToInstance.end()) return;
    void* instance = it->second;
    if (!IsValidPtr(instance)) return;
    HMENU hMenu = CreatePopupMenu();
    if (hMenu) {
        AppendMenuA(hMenu, MF_STRING, ID_CTX_COPY_PATH, "Copy Path");
        int scriptType = DetectScriptType(instance); 
        UINT flags = (scriptType >= 0) ? MF_STRING : (MF_STRING | MF_GRAYED);
        AppendMenuA(hMenu, flags, ID_CTX_DECOMPILE, "Decompile");
        TreeView_SelectItem(g_hTreeView, hItem);
        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hMainWnd, NULL);
        DestroyMenu(hMenu);
    }
}
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        LayoutControls(width, height);
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* nmhdr = (NMHDR*)lParam;
        if (nmhdr->hwndFrom == g_hTab && nmhdr->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(g_hTab);
            ShowTabControls(sel);
        }
        if (nmhdr->hwndFrom == g_hTreeView) {
            if (nmhdr->code == NM_DBLCLK) {
                HTREEITEM hSel = TreeView_GetSelection(g_hTreeView);
                if (hSel) {
                    HandleScriptClick(hSel);
                }
            } else if (nmhdr->code == NM_RCLICK) {
                POINT pt;
                GetCursorPos(&pt);
                POINT ptClient = pt;
                ScreenToClient(g_hTreeView, &ptClient);
                TVHITTESTINFO ht = {};
                ht.pt = ptClient;
                TreeView_HitTest(g_hTreeView, &ht);
                if (ht.hItem) {
                    ShowExplorerContextMenu(pt, ht.hItem);
                }
            } else if (nmhdr->code == TVN_SELCHANGED) {
                 HTREEITEM hSel = TreeView_GetSelection(g_hTreeView);
                 LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lParam;
                 if (pnmtv->itemNew.hItem) {
                      auto it = g_TreeItemToInstance.find(pnmtv->itemNew.hItem);
                      if (it != g_TreeItemToInstance.end()) {
                          RefreshInfo(it->second);
                          std::string path = GetInstancePath(pnmtv->itemNew.hItem);
                          if (!path.empty()) {
                              std::string p = "Path: " + path;
                              SendMessageA(g_hListInfo, LB_ADDSTRING, 0, (LPARAM)p.c_str());
                          }
                      } else {
                          SendMessageA(g_hListInfo, LB_RESETCONTENT, 0, 0);
                      }
                 }
            }
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_CTX_COPY_PATH) {
             HTREEITEM hItem = TreeView_GetSelection(g_hTreeView);
             if (hItem) {
                 std::string path = GetInstancePath(hItem);
                 if (!path.empty()) {
                     if (OpenClipboard(g_hMainWnd)) {
                         EmptyClipboard();
                         HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, path.length() + 1);
                         if (hMem) {
                             char* pMem = (char*)GlobalLock(hMem);
                             memcpy(pMem, path.c_str(), path.length() + 1);
                             GlobalUnlock(hMem);
                             SetClipboardData(CF_TEXT, hMem);
                         }
                         CloseClipboard();
                         LogOutput("[Explorer] Copied path: " + path);
                     }
                 }
             }
        } else if (id == ID_CTX_DECOMPILE) {
            HTREEITEM hItem = TreeView_GetSelection(g_hTreeView);
            if (hItem) {
                HandleScriptClick(hItem);
            }
        }
        switch (id) {
        case IDC_BTN_REFRESH:
            RefreshExplorer();
            break;
        case IDC_BTN_DELETE:
            DeleteSelected();
            break;
        case IDC_BTN_CLONE:
            CloneSelected();
            break;
        case IDC_BTN_SET_NAME:
            SetSelectedName();
            break;
        case IDC_BTN_EXECUTE: {
            if (HIWORD(wParam) != BN_CLICKED) break; 
            if (g_CurrentTab == 1) { 
                int len = GetWindowTextLengthA(g_hEditScript);
                if (len > 0) {
                    std::string script(len + 1, '\0');
                    GetWindowTextA(g_hEditScript, &script[0], len + 1);
                    script.resize(len);
                    SetStatus("Executing...");
                    RunLuaScript(script);
                    SetStatus("Ready");
                }
            }
            break;
        }
        case IDC_BTN_RUN_CMD: {
            if (HIWORD(wParam) != BN_CLICKED) break; 
            int len = GetWindowTextLengthA(g_hEditCmd);
            if (len > 0) {
                std::string cmd(len + 1, '\0');
                GetWindowTextA(g_hEditCmd, &cmd[0], len + 1);
                cmd.resize(len);
                SetStatus("Running command...");
                ProcessCommand(cmd);
                SetWindowTextA(g_hEditCmd, ""); 
                SetStatus("Ready");
                SetFocus(g_hEditCmd);
            }
            break;
        }
        case IDC_BTN_CLEAR:
            if (g_CurrentTab == 1) { 
                SetWindowTextA(g_hEditScript, "");
            } else { 
                SetWindowTextA(g_hOutputLog, "");
            }
            break;
        case IDC_BTN_LOAD_FILE: {
            char filename[MAX_PATH] = {};
            OPENFILENAMEA ofn = {};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Lua Files (*.lua)\0*.lua\0Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = filename;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                std::ifstream file(filename);
                if (file.is_open()) {
                    std::stringstream ss;
                    ss << file.rdbuf();
                    SetWindowTextA(g_hEditScript, ss.str().c_str());
                    TabCtrl_SetCurSel(g_hTab, 1);
                    ShowTabControls(1);
                    LogOutput("[File] Loaded: " + std::string(filename));
                } else {
                    LogOutput("[File] Failed to open: " + std::string(filename));
                }
            }
            break;
        }
        }
        return 0;
    }
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        g_hMainWnd = NULL;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}
static void CreateUI() {
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_TAB_CLASSES | ICC_TREEVIEW_CLASSES;
    InitCommonControlsEx(&icex);
    const char* CLASS_NAME = "MiisploitWndClass";
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExA(&wc);
    g_hMainWnd = CreateWindowExA(
        WS_EX_APPWINDOW | WS_EX_TOPMOST,
        CLASS_NAME, "Miisploit",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 550,
        NULL, NULL, g_hInstance, NULL);
    if (!g_hMainWnd) {
        LogOutput("[UI] Failed to create window");
        return;
    }
    g_hTab = CreateWindowA(WC_TABCONTROLA, "",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        0, 0, 500, 30,
        g_hMainWnd, (HMENU)IDC_TAB, g_hInstance, NULL);
    TCITEMA tie = {};
    tie.mask = TCIF_TEXT;
    tie.pszText = (LPSTR)"Explorer";
    SendMessageA(g_hTab, TCM_INSERTITEMA, 0, (LPARAM)&tie);
    tie.pszText = (LPSTR)"Lua Script";
    SendMessageA(g_hTab, TCM_INSERTITEMA, 1, (LPARAM)&tie);
    tie.pszText = (LPSTR)"CMD";
    SendMessageA(g_hTab, TCM_INSERTITEMA, 2, (LPARAM)&tie);
    tie.pszText = (LPSTR)"Credits";
    SendMessageA(g_hTab, TCM_INSERTITEMA, 3, (LPARAM)&tie);
    g_hBtnRefresh = CreateWindowA("BUTTON", "Refresh",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        5, 35, 70, 25,
        g_hMainWnd, (HMENU)IDC_BTN_REFRESH, g_hInstance, NULL);
    g_hBtnDelete = CreateWindowA("BUTTON", "Delete",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        80, 35, 70, 25,
        g_hMainWnd, (HMENU)IDC_BTN_DELETE, g_hInstance, NULL);
    g_hBtnClone = CreateWindowA("BUTTON", "Clone",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        155, 35, 70, 25,
        g_hMainWnd, (HMENU)IDC_BTN_CLONE, g_hInstance, NULL);
    g_hEditName = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        230, 35, 120, 25,
        g_hMainWnd, (HMENU)IDC_EDIT_NAME, g_hInstance, NULL);
    g_hBtnSetName = CreateWindowA("BUTTON", "Set",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        355, 35, 40, 25,
        g_hMainWnd, (HMENU)IDC_BTN_SET_NAME, g_hInstance, NULL);
    g_hTreeView = CreateWindowA(WC_TREEVIEWA, "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT,
        5, 65, 340, 280,
        g_hMainWnd, (HMENU)IDC_TREEVIEW, g_hInstance, NULL);
    g_hListInfo = CreateWindowA("LISTBOX", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL,
        350, 65, 130, 280,
        g_hMainWnd, (HMENU)1100, g_hInstance, NULL);
    g_hEditCmdLog = CreateWindowA("EDIT", "",
        WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        5, 35, 480, 200,
        g_hMainWnd, (HMENU)IDC_CMD_LOG, g_hInstance, NULL);
    g_hEditCmd = CreateWindowA("EDIT", "",
        WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
        5, 240, 400, 25,
        g_hMainWnd, (HMENU)IDC_EDIT_CMD, g_hInstance, NULL);
    g_hBtnRunCmd = CreateWindowA("BUTTON", "Run",
        WS_CHILD | BS_PUSHBUTTON,
        410, 240, 70, 25,
        g_hMainWnd, (HMENU)IDC_BTN_RUN_CMD, g_hInstance, NULL);
    g_hEditScript = CreateWindowA("EDIT", "",
        WS_CHILD | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        5, 35, 480, 310,
        g_hMainWnd, (HMENU)IDC_EDIT_SCRIPT, g_hInstance, NULL);
    g_hCreditsLabel1 = CreateWindowA("STATIC", "MiiSploit made by Miiself",
        WS_CHILD | SS_CENTER,
        5, 60, 480, 25,
        g_hMainWnd, NULL, g_hInstance, NULL);
    g_hCreditsLabel2 = CreateWindowA("STATIC", "Original Source by YuTech Labs",
        WS_CHILD | SS_CENTER,
        5, 90, 480, 25,
        g_hMainWnd, NULL, g_hInstance, NULL);
    g_hCreditsLabel3 = CreateWindowA("STATIC", "Ideas from Elytra",
        WS_CHILD | SS_CENTER,
        5, 120, 480, 25,
        g_hMainWnd, NULL, g_hInstance, NULL);
    g_hBtnExecute = CreateWindowA("BUTTON", "Execute",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        5, 420, 70, 25,
        g_hMainWnd, (HMENU)IDC_BTN_EXECUTE, g_hInstance, NULL);
    g_hBtnClear = CreateWindowA("BUTTON", "Clear",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        80, 420, 70, 25,
        g_hMainWnd, (HMENU)IDC_BTN_CLEAR, g_hInstance, NULL);
    g_hBtnLoadFile = CreateWindowA("BUTTON", "Load File",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        155, 420, 80, 25,
        g_hMainWnd, (HMENU)IDC_BTN_LOAD_FILE, g_hInstance, NULL);
    g_hStatusLabel = CreateWindowA("STATIC", "Ready",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        240, 420, 250, 25,
        g_hMainWnd, (HMENU)IDC_STATUS_LABEL, g_hInstance, NULL);
    g_hOutputLog = CreateWindowA("EDIT", "",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        5, 450, 480, 70,
        g_hMainWnd, (HMENU)IDC_OUTPUT_LOG, g_hInstance, NULL);
    ShowTabControls(0);
    RECT rc;
    GetClientRect(g_hMainWnd, &rc);
    LayoutControls(rc.right, rc.bottom);
}
DWORD WINAPI UIThread(LPVOID param) {
    HANDLE g = GetModuleHandleA("GameAssembly.dll");
    U::Init(g, U::Mode::Il2Cpp);
    U::ThreadAttach();
    CreateUI();
    LogOutput("Welcome to MiiSploit Rewritten v2.0");
    LogOutput("Made by Miiself!");
    MSG msg;
    while (g_Running) {
        if (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) break;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        } else {
            Sleep(1);
        }
    }
    if (g_hMainWnd) {
        DestroyWindow(g_hMainWnd);
        g_hMainWnd = NULL;
    }
    U::ThreadDetach();
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        CreateThread(NULL, 0, UIThread, NULL, 0, NULL);
        break;
    case DLL_PROCESS_DETACH:
        g_Running = false;
        if (g_hMainWnd) {
            PostMessageA(g_hMainWnd, WM_CLOSE, 0, 0);
        }
        Sleep(200);
        break;
    }
    return TRUE;
}