/* Exercise upgrade cleanup against private paths and registry keys only. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <commctrl.h>
#include <wchar.h>
#include <stdlib.h>
static HRESULT inspect_options(const TASKDIALOGCONFIG *dialog,int *button,int *radio,BOOL *checked);
static wchar_t test_key[160], test_legacy_key[160], test_previous_key[160], test_startup_key[160];
#define UNINSTALL_KEY test_key
#define LEGACY_UNINSTALL_KEY test_legacy_key
#define PREVIOUS_UNINSTALL_KEY test_previous_key
#define STARTUP_KEY test_startup_key
#define TaskDialogIndirect inspect_options
#include "../src/setup.c"
#undef TaskDialogIndirect
static wchar_t legacy_dir[PATH_CAP], legacy_start_link[PATH_CAP], legacy_desktop_link[PATH_CAP];
static BOOL expected_startup;
static int options_choice;

static void require(BOOL ok, const char *message) {
    if (!ok) { fprintf(stderr,"FAIL: %s (Windows error %lu)\n",message,GetLastError()); exit(1); }
}

static void child(wchar_t path[PATH_CAP],const wchar_t *parent,const wchar_t *name) {
    require(swprintf_s(path,PATH_CAP,L"%s\\%s",parent,name)>0,"bounded private path");
}

static void fixture(const wchar_t *path) {
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    DWORD written;
    require(file!=INVALID_HANDLE_VALUE,"create private setup fixture");
    require(WriteFile(file,"keep",4,&written,NULL) && written==4 && CloseHandle(file),"write setup fixture");
}

static BOOL present(const wchar_t *path) { return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES; }

static HRESULT CALLBACK choose_options(HWND window,UINT message,WPARAM wparam,LPARAM lparam,LONG_PTR data) {
    (void)wparam; (void)lparam; (void)data;
    if (message==TDN_CREATED) {
        PostMessageW(window,TDM_CLICK_VERIFICATION,!expected_startup,FALSE);
        PostMessageW(window,TDM_CLICK_BUTTON,(WPARAM)options_choice,0);
    }
    return S_OK;
}

static HRESULT inspect_options(const TASKDIALOGCONFIG *dialog,int *button,int *radio,BOOL *checked) {
    TASKDIALOGCONFIG test=*dialog;
    require(((dialog->dwFlags&TDF_VERIFICATION_FLAG_CHECKED)!=0)==expected_startup &&
            dialog->pszVerificationText && checked,"installer offers the retained startup choice as a native checkbox");
    test.pfCallback=choose_options;
    return TaskDialogIndirect(&test,button,radio,checked);
}

static void register_legacy(const wchar_t *path) {
    HKEY key;
    require(RegCreateKeyExW(HKEY_CURRENT_USER,test_legacy_key,0,NULL,0,KEY_WRITE,NULL,&key,NULL)==ERROR_SUCCESS,
            "create private legacy registration");
    require(registry_text(key,L"InstallLocation",path),"record private legacy installation");
    RegCloseKey(key);
}

static void startup_checks(void) {
    HKEY key;
    BOOL enabled=TRUE;
    DWORD bytes, invalid=1;
    wchar_t value[PATH_CAP], expected[PATH_CAP], original[PATH_CAP];
    require(get_startup(&enabled) && !enabled && set_startup(FALSE),"fresh install leaves startup off without creating a Run key");
    require(RegOpenKeyExW(HKEY_CURRENT_USER,test_startup_key,0,KEY_READ,&key)==ERROR_FILE_NOT_FOUND,
            "disabling a missing startup entry creates no registry key");
    require(set_startup(TRUE) && get_startup(&enabled) && enabled,"enable startup for the current account");
    bytes=sizeof(value);
    swprintf_s(expected,PATH_CAP,L"\"%s\" --startup",app_path);
    require(RegGetValueW(HKEY_CURRENT_USER,test_startup_key,L"Jotila",RRF_RT_REG_SZ,NULL,value,&bytes)==ERROR_SUCCESS &&
            !wcscmp(value,expected),"startup quotes the executable and requests background session restore");
    require(set_startup(enabled) && get_startup(&enabled) && enabled,"an unspecified upgrade retains enabled startup");
    require(RegOpenKeyExW(HKEY_CURRENT_USER,test_startup_key,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS &&
            registry_text(key,L"Other application",L"untouched"),"create unrelated startup sentinel");
    wcscpy_s(original,PATH_CAP,app_path);
    wmemset(app_path,L'x',260); app_path[260]=0;
    require(!set_startup(TRUE),"oversized Run commands are rejected before changing registration");
    wcscpy_s(app_path,PATH_CAP,original);
    bytes=sizeof(value);
    require(RegGetValueW(HKEY_CURRENT_USER,test_startup_key,L"Jotila",RRF_RT_REG_SZ,NULL,value,&bytes)==ERROR_SUCCESS &&
            !wcscmp(value,expected),"rejected startup change retains the previous command");
    require(set_startup(FALSE) && get_startup(&enabled) && !enabled && set_startup(FALSE),
            "opt-out and uninstall remove startup idempotently");
    require(set_startup(enabled) && get_startup(&enabled) && !enabled,"an unspecified upgrade retains disabled startup");
    bytes=sizeof(value);
    require(RegGetValueW(HKEY_CURRENT_USER,test_startup_key,L"Other application",RRF_RT_REG_SZ,NULL,value,&bytes)==ERROR_SUCCESS &&
            !wcscmp(value,L"untouched"),"uninstall preserves other applications' startup values");
    require(RegSetValueExW(key,L"Jotila",0,REG_DWORD,(BYTE *)&invalid,sizeof(invalid))==ERROR_SUCCESS && !get_startup(&enabled),
            "an unreadable startup choice is reported instead of silently reset");
    RegCloseKey(key);
    require(set_startup(FALSE),"clean malformed private startup fixture");
    expected_startup=FALSE; enabled=FALSE; options_choice=IDOK;
    require(install_options(&enabled)==IDOK && enabled,"checking the native installer checkbox opts into startup");
    expected_startup=TRUE; options_choice=IDOK;
    require(install_options(&enabled)==IDOK && !enabled,"unchecking the native installer checkbox opts out");
    expected_startup=FALSE; options_choice=IDCANCEL;
    require(!present(install_dir) && install()==0 && !present(install_dir) && get_startup(&enabled) && !enabled,
            "cancelling Setup after toggling its checkbox changes neither installation nor startup");
    puts("PASS: startup opt-in/out, upgrade retention, command quoting/limits and isolated uninstall cleanup.");
}

int main(void) {
    wchar_t temporary[PATH_CAP],base[PATH_CAP],registry_root[128],value[PATH_CAP];
    wchar_t executable[PATH_CAP],uninstaller[PATH_CAP],license[PATH_CAP],archive[PATH_CAP],note[PATH_CAP];
    HANDLE lock;
    DWORD bytes=sizeof(value);
    GetTempPathW(PATH_CAP,temporary);
    swprintf_s(base,PATH_CAP,L"%sJotila-setup-test-%lu-%llu",temporary,GetCurrentProcessId(),GetTickCount64());
    swprintf_s(registry_root,128,L"Software\\Jotila-setup-test-%lu-%llu",GetCurrentProcessId(),GetTickCount64());
    swprintf_s(test_key,160,L"%s\\Jotila",registry_root);
    swprintf_s(test_legacy_key,160,L"%s\\Legacy",registry_root);
    swprintf_s(test_previous_key,160,L"%s\\Previous",registry_root);
    swprintf_s(test_startup_key,160,L"%s\\Run",registry_root);
    require(CreateDirectoryW(base,NULL),"create private setup directory");
    child(install_dir,base,L"Jotila"); child(app_path,install_dir,L"Jotila.exe");
    child(uninstall_path,install_dir,L"Uninstall.exe");
    startup_checks();
    require(register_uninstaller(),"register Jotila under a private test key");
    require(RegGetValueW(HKEY_CURRENT_USER,test_key,L"DisplayName",RRF_RT_REG_SZ,NULL,value,&bytes)==ERROR_SUCCESS &&
            !wcscmp(value,L"Jotila"),"new uninstall registration uses the Jotila name");
    bytes=sizeof(value);
    require(RegGetValueW(HKEY_CURRENT_USER,test_key,L"DisplayVersion",RRF_RT_REG_SZ,NULL,value,&bytes)==ERROR_SUCCESS &&
            !wcscmp(value,APP_VERSION_W),"new uninstall registration uses the release version");
    child(start_link,base,L"Jotila.lnk"); child(desktop_link,base,L"Desktop\\Jotila.lnk");
    child(value,base,L"Desktop"); require(CreateDirectoryW(value,NULL),"create private desktop");
    child(legacy_dir,base,L"PicoSlip"); require(CreateDirectoryW(legacy_dir,NULL),"create private legacy install");
    child(executable,legacy_dir,L"PicoSlip.exe"); child(uninstaller,legacy_dir,L"Uninstall.exe");
    child(license,legacy_dir,L"LICENSE.txt"); child(archive,legacy_dir,L"notes");
    child(note,archive,L"note-keep.txt");
    child(legacy_start_link,base,L"PicoSlip.lnk"); child(legacy_desktop_link,base,L"Desktop\\PicoSlip.lnk");
    require(CreateDirectoryW(archive,NULL),"create archive sentinel");
    fixture(note); fixture(executable); fixture(uninstaller); fixture(license);
    fixture(legacy_start_link); fixture(legacy_desktop_link);
    register_legacy(base);
    require(retire_legacy_install() && present(executable) && present(legacy_start_link),
            "unexpected registered paths are never followed or removed");
    register_legacy(legacy_dir);
    lock=CreateFileW(executable,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
    require(lock!=INVALID_HANDLE_VALUE,"lock old executable for recovery test");
    require(!retire_legacy_install() && present(executable) && present(uninstaller) && present(legacy_start_link),
            "a locked old executable leaves registration and program files for retry");
    CloseHandle(lock);
    require(retire_legacy_install() && !present(executable) && !present(uninstaller) && !present(license) &&
            !present(legacy_start_link) && !present(legacy_desktop_link),"retire exact legacy files and shortcuts");
    require(present(legacy_dir) && present(archive) && present(note),"upgrade never recurses into an archive");
    bytes=sizeof(value);
    require(RegGetValueW(HKEY_CURRENT_USER,test_legacy_key,L"InstallLocation",RRF_RT_REG_SZ,NULL,value,&bytes)==ERROR_FILE_NOT_FOUND &&
            retire_legacy_install(),"legacy registration is removed and cleanup is idempotent");
    require(DeleteFileW(note) && RemoveDirectoryW(archive),"remove only the test archive sentinel");
    fixture(executable); fixture(uninstaller); fixture(license); register_legacy(legacy_dir);
    require(retire_legacy_install() && !present(legacy_dir),"empty legacy installation directory is removed");
    child(legacy_dir,base,L"IOTA"); require(CreateDirectoryW(legacy_dir,NULL),"create private IOTA installation");
    child(executable,legacy_dir,L"IOTA.exe"); fixture(executable);
    wcscpy_s(test_legacy_key,160,test_previous_key); register_legacy(legacy_dir);
    require(retire_legacy_install() && !present(legacy_dir),"retire the immediately preceding IOTA release");
    child(value,base,L"Desktop");
    require(RegDeleteTreeW(HKEY_CURRENT_USER,registry_root)==ERROR_SUCCESS && RemoveDirectoryW(value) && RemoveDirectoryW(base),"remove private setup fixtures");
    puts("PASS: Jotila registration, upgrade path validation, locked-file recovery and exact-file retirement preserve archives.");
    return 0;
}
