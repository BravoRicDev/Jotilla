/* Per-user Windows installer and uninstaller. C17 and Windows APIs only. */
#define COBJMACROS
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <commctrl.h>
#include <stdio.h>
#include <wchar.h>
#include "resource.h"

#define PATH_CAP 1024
#ifndef UNINSTALL_KEY
#define UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Jotilla"
#define LEGACY_UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PicoSlip"
#define PREVIOUS_UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Jotila"
#define OLDER_UNINSTALL_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\IOTA"
#endif
#ifndef STARTUP_KEY
#define STARTUP_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#endif
static wchar_t install_dir[PATH_CAP], app_path[PATH_CAP], uninstall_path[PATH_CAP];
static wchar_t start_link[PATH_CAP], desktop_link[PATH_CAP], self[PATH_CAP];
static BOOL silent;
static int startup_option = -1; /* Unspecified updates retain the existing choice. */

static BOOL paths(void) {
    wchar_t root[PATH_CAP];
    DWORD length;
    if (FAILED(SHGetFolderPathW(NULL,CSIDL_LOCAL_APPDATA,NULL,SHGFP_TYPE_CURRENT,root)) ||
        swprintf_s(install_dir,PATH_CAP,L"%s\\Programs\\Jotilla",root)<0 ||
        swprintf_s(app_path,PATH_CAP,L"%s\\Jotilla.exe",install_dir)<0 ||
        swprintf_s(uninstall_path,PATH_CAP,L"%s\\Uninstall.exe",install_dir)<0) return FALSE;
    if (FAILED(SHGetFolderPathW(NULL,CSIDL_PROGRAMS,NULL,SHGFP_TYPE_CURRENT,root)) ||
        swprintf_s(start_link,PATH_CAP,L"%s\\Jotilla.lnk",root)<0) return FALSE;
    if (FAILED(SHGetFolderPathW(NULL,CSIDL_DESKTOPDIRECTORY,NULL,SHGFP_TYPE_CURRENT,root)) ||
        swprintf_s(desktop_link,PATH_CAP,L"%s\\Jotilla.lnk",root)<0) return FALSE;
    length=GetModuleFileNameW(NULL,self,PATH_CAP);
    return length>0 && length<PATH_CAP;
}

static int failure(const wchar_t *message) {
    if (!silent) MessageBoxW(NULL,message,L"Jotilla Setup",MB_OK|MB_ICONERROR);
    return 1;
}

static BOOL close_app(void) {
    HWND window=FindWindowW(APP_CONTROLLER,NULL);
    DWORD pid;
    DWORD_PTR result;
    HANDLE process;
    BOOL ok;
    if (!window) return TRUE;
    GetWindowThreadProcessId(window,&pid);
    process=OpenProcess(SYNCHRONIZE,FALSE,pid);
    if (!process) return FALSE;
    ok=SendMessageTimeoutW(window,WM_CLOSE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,8000,&result)!=0;
    if (ok) ok=WaitForSingleObject(process,5000)==WAIT_OBJECT_0;
    CloseHandle(process);
    return ok;
}

static BOOL shortcut(const wchar_t *path) {
    IShellLinkW *link=NULL;
    IPersistFile *file=NULL;
    HRESULT result=CoCreateInstance(&CLSID_ShellLink,NULL,CLSCTX_INPROC_SERVER,&IID_IShellLinkW,(void **)&link);
    if (SUCCEEDED(result)) result=IShellLinkW_SetPath(link,app_path);
    if (SUCCEEDED(result)) result=IShellLinkW_SetWorkingDirectory(link,install_dir);
    if (SUCCEEDED(result)) result=IShellLinkW_SetDescription(link,L"Jotilla - Tiny Sticky Notes for Windows");
    if (SUCCEEDED(result)) result=IShellLinkW_SetIconLocation(link,app_path,0);
    if (SUCCEEDED(result)) result=IShellLinkW_QueryInterface(link,&IID_IPersistFile,(void **)&file);
    if (SUCCEEDED(result)) result=IPersistFile_Save(file,path,TRUE);
    if (file) IPersistFile_Release(file);
    if (link) IShellLinkW_Release(link);
    return SUCCEEDED(result);
}

static BOOL registry_text(HKEY key,const wchar_t *name,const wchar_t *value) {
    return RegSetValueExW(key,name,0,REG_SZ,(const BYTE *)value,(DWORD)((wcslen(value)+1)*sizeof(wchar_t)))==ERROR_SUCCESS;
}

static BOOL get_startup(BOOL *enabled) {
    wchar_t command[261]={0};
    DWORD bytes=sizeof(command);
    LSTATUS status=RegGetValueW(HKEY_CURRENT_USER,STARTUP_KEY,L"Jotilla",RRF_RT_REG_SZ,NULL,command,&bytes);
    if (status==ERROR_FILE_NOT_FOUND) {
        bytes=sizeof(command);
        status=RegGetValueW(HKEY_CURRENT_USER,STARTUP_KEY,L"Jotila",RRF_RT_REG_SZ,NULL,command,&bytes);
    }
    *enabled=FALSE;
    if (status==ERROR_FILE_NOT_FOUND) return TRUE;
    if (status!=ERROR_SUCCESS) return FALSE;
    *enabled=command[0]!=0;
    return TRUE;
}

static BOOL set_startup(BOOL enabled) {
    HKEY key;
    LSTATUS status;
    BOOL ok;
    /* Run commands have a 260-character limit, including arguments. Quote the
       executable and keep logon distinct from an intentional second launch. */
    wchar_t command[261];
    if (!enabled) {
        status=RegOpenKeyExW(HKEY_CURRENT_USER,STARTUP_KEY,0,KEY_SET_VALUE,&key);
        if (status==ERROR_FILE_NOT_FOUND) return TRUE;
        if (status!=ERROR_SUCCESS) return FALSE;
        status=RegDeleteValueW(key,L"Jotilla");
        if (status==ERROR_SUCCESS || status==ERROR_FILE_NOT_FOUND) status=RegDeleteValueW(key,L"Jotila");
        RegCloseKey(key);
        return status==ERROR_SUCCESS || status==ERROR_FILE_NOT_FOUND;
    }
    if (wcslen(app_path)+12>260 || swprintf_s(command,261,L"\"%s\" --startup",app_path)<0) return FALSE;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,STARTUP_KEY,0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL)!=ERROR_SUCCESS) return FALSE;
    ok=registry_text(key,L"Jotilla",command);
    if (ok) {
        status=RegDeleteValueW(key,L"Jotila");
        ok=status==ERROR_SUCCESS || status==ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(key);
    return ok;
}

static int install_options(BOOL *startup) {
    const TASKDIALOG_BUTTON button={IDOK,L"Install"};
    TASKDIALOGCONFIG dialog={0};
    int choice=IDCANCEL;
    dialog.cbSize=sizeof(dialog);
    dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION | TDF_SIZE_TO_CONTENT;
    if (*startup) dialog.dwFlags|=TDF_VERIFICATION_FLAG_CHECKED;
    dialog.dwCommonButtons=TDCBF_CANCEL_BUTTON;
    dialog.pszWindowTitle=L"Jotilla Setup";
    dialog.pszMainInstruction=L"Install Jotilla for this Windows account";
    dialog.pszContent=L"Adds Start menu and desktop shortcuts. No administrator rights needed.\n\n"
                      L"Your notes and chosen archive location are preserved. Open notes return to their saved positions.";
    dialog.pszVerificationText=L"Start Jotilla when I sign in to Windows";
    dialog.cButtons=1; dialog.pButtons=&button; dialog.nDefaultButton=IDOK;
    return SUCCEEDED(TaskDialogIndirect(&dialog,&choice,NULL,startup)) ? choice : -1;
}

static BOOL register_uninstaller(void) {
    HKEY key;
    wchar_t command[PATH_CAP+64];
    DWORD one=1;
    BOOL ok;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,UNINSTALL_KEY,0,NULL,0,KEY_WRITE,NULL,&key,NULL)!=ERROR_SUCCESS) return FALSE;
    swprintf_s(command,PATH_CAP+64,L"\"%s\"",uninstall_path);
    ok=registry_text(key,L"DisplayName",L"Jotilla") && registry_text(key,L"DisplayVersion",APP_VERSION_W) &&
       registry_text(key,L"Publisher",L"Alessio Garau") && registry_text(key,L"DisplayIcon",app_path) &&
       registry_text(key,L"InstallLocation",install_dir) && registry_text(key,L"UninstallString",command) &&
       registry_text(key,L"URLInfoAbout",L"https://github.com/GarauGarau/Jotilla");
    swprintf_s(command,PATH_CAP+64,L"\"%s\" /S",uninstall_path);
    ok=ok && registry_text(key,L"QuietUninstallString",command) &&
       RegSetValueExW(key,L"NoModify",0,REG_DWORD,(BYTE *)&one,sizeof(one))==ERROR_SUCCESS &&
       RegSetValueExW(key,L"NoRepair",0,REG_DWORD,(BYTE *)&one,sizeof(one))==ERROR_SUCCESS;
    RegCloseKey(key); return ok;
}

static BOOL extract_resource(int id,const wchar_t *path) {
    HMODULE module=GetModuleHandleW(NULL);
    HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    HGLOBAL loaded;
    DWORD size,written;
    const void *bytes;
    wchar_t temporary[PATH_CAP];
    HANDLE file;
    BOOL ok;
    if (!resource) return FALSE;
    size=SizeofResource(module,resource); loaded=LoadResource(module,resource);
    bytes=loaded ? LockResource(loaded) : NULL;
    if (!bytes || !size || swprintf_s(temporary,PATH_CAP,L"%s.new",path)<0) return FALSE;
    file=CreateFileW(temporary,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if (file==INVALID_HANDLE_VALUE) return FALSE;
    ok=WriteFile(file,bytes,size,&written,NULL) && written==size && FlushFileBuffers(file);
    if (!CloseHandle(file)) ok=FALSE;
    if (ok) ok=MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temporary);
    return ok;
}

static BOOL remove_file(const wchar_t *path) {
    if (DeleteFileW(path)) return TRUE;
    return GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND;
}

static BOOL sibling_path(wchar_t path[PATH_CAP],const wchar_t *current,const wchar_t *name) {
    const wchar_t *slash=wcsrchr(current,L'\\');
    return slash && swprintf_s(path,PATH_CAP,L"%.*s\\%s",(int)(slash-current),current,name)>0;
}

static BOOL retire_install(const wchar_t *brand,const wchar_t *key) {
    const wchar_t *files[]={NULL,L"Uninstall.exe",L"LICENSE.txt"};
    wchar_t registered[PATH_CAP], folder[PATH_CAP], path[PATH_CAP], name[80];
    DWORD bytes=sizeof(registered);
    LSTATUS status;
    int i;
    status=RegGetValueW(HKEY_CURRENT_USER,key,L"InstallLocation",RRF_RT_REG_SZ,
                       NULL,registered,&bytes);
    if (status==ERROR_FILE_NOT_FOUND) return TRUE;
    /* Only retire the known per-user installation. Never recurse or follow a
       registry-provided path, and never touch notes or portable configuration. */
    if (!sibling_path(folder,install_dir,brand)) return FALSE;
    if (status!=ERROR_SUCCESS || _wcsicmp(registered,folder)) return TRUE;
    swprintf_s(name,80,L"%s.exe",brand); files[0]=name;
    for (i=0;i<3;++i) {
        if (swprintf_s(path,PATH_CAP,L"%s\\%s",folder,files[i])<0 || !remove_file(path)) return FALSE;
    }
    swprintf_s(name,80,L"%s.lnk",brand);
    if (!sibling_path(path,start_link,name) || !remove_file(path) ||
        !sibling_path(path,desktop_link,name) || !remove_file(path)) return FALSE;
    status=RegDeleteTreeW(HKEY_CURRENT_USER,key);
    if (status!=ERROR_SUCCESS && status!=ERROR_FILE_NOT_FOUND) return FALSE;
    RemoveDirectoryW(folder); /* A nonempty folder, including user data, remains. */
    return TRUE;
}

static BOOL retire_legacy_install(void) {
    return retire_install(L"Jotila",PREVIOUS_UNINSTALL_KEY) &&
           retire_install(L"IOTA",OLDER_UNINSTALL_KEY) && retire_install(L"PicoSlip",LEGACY_UNINSTALL_KEY);
}

static int install(void) {
    int directory_result, choice;
    BOOL links, startup=FALSE;
    HRESULT com;
    wchar_t license[PATH_CAP];
    if (startup_option<0) {
        if (!get_startup(&startup)) return failure(L"The current Windows startup setting could not be read.");
    } else startup=startup_option!=0;
    if (!silent) {
        choice=install_options(&startup);
        if (choice<0) return failure(L"The installation options could not be displayed.");
        if (choice!=IDOK) return 0;
    }
    if (!close_app()) return failure(L"Jotilla could not save and close. Close it and run Setup again.");
    directory_result=SHCreateDirectoryExW(NULL,install_dir,NULL);
    if (directory_result!=ERROR_SUCCESS && directory_result!=ERROR_ALREADY_EXISTS && directory_result!=ERROR_FILE_EXISTS)
        return failure(L"The installation folder could not be created.");
    swprintf_s(license,PATH_CAP,L"%s\\LICENSE.txt",install_dir);
    if (!extract_resource(IDR_APPLICATION,app_path) || !extract_resource(IDR_LICENSE,license) || !CopyFileW(self,uninstall_path,FALSE))
        return failure(L"The application could not be installed. Check folder permissions and try again.");
    com=CoInitializeEx(NULL,COINIT_APARTMENTTHREADED);
    links=SUCCEEDED(com) && shortcut(start_link) && shortcut(desktop_link);
    if (SUCCEEDED(com)) CoUninitialize();
    if (!links || !register_uninstaller()) return failure(L"Jotilla was copied, but a shortcut or uninstall entry could not be created. Run Setup again.");
    if (!retire_legacy_install()) return failure(L"Jotilla is installed, but the previous program files could not be retired. Close the previous Setup and run Jotilla Setup again. Your notes have been kept.");
    if (!set_startup(startup)) return failure(L"Jotilla is installed, but its Windows startup setting could not be saved. Run Setup again.");
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,NULL,NULL);
    if (!silent && MessageBoxW(NULL,L"Jotilla is installed. Open it now?",L"Jotilla Setup",MB_YESNO|MB_ICONINFORMATION)==IDYES)
        ShellExecuteW(NULL,L"open",app_path,NULL,install_dir,SW_SHOWNORMAL);
    return 0;
}

static int cleanup(DWORD parent_pid) {
    wchar_t license[PATH_CAP];
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,parent_pid);
    if (parent) {
        DWORD result=WaitForSingleObject(parent,15000); CloseHandle(parent);
        if (result!=WAIT_OBJECT_0) return failure(L"The uninstaller is still running. Please try again.");
    }
    /* Only these exact per-user program files are removed. Never recurse and
       never touch notes, archive.path or a portable note archive. */
    if (!set_startup(FALSE)) return failure(L"The Windows startup entry could not be removed. Run Uninstall again.");
    swprintf_s(license,PATH_CAP,L"%s\\LICENSE.txt",install_dir);
    if (!remove_file(app_path) || !remove_file(uninstall_path) || !remove_file(license)) return failure(L"Jotilla could not be removed. Close the application and try again.");
    remove_file(start_link); remove_file(desktop_link);
    RegDeleteTreeW(HKEY_CURRENT_USER,UNINSTALL_KEY);
    RemoveDirectoryW(install_dir);
    SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,NULL,NULL);
    if (!silent) MessageBoxW(NULL,L"Jotilla has been uninstalled. Your notes have been kept.",L"Jotilla",MB_OK|MB_ICONINFORMATION);
    return 0;
}

static int uninstall(void) {
    wchar_t temporary[PATH_CAP], helper[PATH_CAP], command[PATH_CAP+80];
    STARTUPINFOW startup={0};
    PROCESS_INFORMATION process={0};
    if (!silent && MessageBoxW(NULL,L"Uninstall Jotilla?\n\nYour saved notes will be kept.",L"Jotilla",MB_YESNO|MB_ICONQUESTION)!=IDYES) return 0;
    if (!close_app()) return failure(L"Jotilla could not save and close. Close it before uninstalling.");
    if (!GetTempPathW(PATH_CAP,temporary) ||
        swprintf_s(helper,PATH_CAP,L"%sJotilla-cleanup-%lu-%llu.exe",temporary,GetCurrentProcessId(),GetTickCount64())<0 ||
        !CopyFileW(self,helper,TRUE)) return failure(L"The temporary uninstaller could not be created.");
    swprintf_s(command,PATH_CAP+80,L"\"%s\" --cleanup %lu %s",helper,GetCurrentProcessId(),silent?L"/S":L"");
    startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    if (!CreateProcessW(helper,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&process)) {
        DeleteFileW(helper); return failure(L"The uninstaller could not start.");
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return 0;
}

int WINAPI wWinMain(_In_ HINSTANCE app,_In_opt_ HINSTANCE previous,_In_ PWSTR command,_In_ int show) {
    int argc,i,result;
    wchar_t **argv=CommandLineToArgvW(GetCommandLineW(),&argc), *name;
    (void)app; (void)previous; (void)command; (void)show;
    if (!argv) return failure(L"The command line could not be read.");
    if (!paths()) { LocalFree(argv); return failure(L"Windows user folders could not be located."); }
    for (i=1;i<argc;++i) {
        if (!_wcsicmp(argv[i],L"/S")) silent=TRUE;
        else if (!_wcsicmp(argv[i],L"/AUTOSTART")) startup_option=1;
        else if (!_wcsicmp(argv[i],L"/NOAUTOSTART")) startup_option=0;
    }
    name=wcsrchr(self,L'\\'); name=name ? name+1 : self;
    if (argc>=3 && !wcscmp(argv[1],L"--cleanup")) result=cleanup(wcstoul(argv[2],NULL,10));
    else if (!_wcsicmp(name,L"Uninstall.exe")) result=uninstall();
    else result=install();
    LocalFree(argv); return result;
}
