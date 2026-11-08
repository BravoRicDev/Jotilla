/* Exercise upgrade cleanup against private paths and registry keys only. */
#include <wchar.h>
#include <stdlib.h>
static wchar_t test_key[160], test_legacy_key[160], test_previous_key[160];
#define UNINSTALL_KEY test_key
#define LEGACY_UNINSTALL_KEY test_legacy_key
#define PREVIOUS_UNINSTALL_KEY test_previous_key
#include "../src/setup.c"
static wchar_t legacy_dir[PATH_CAP], legacy_start_link[PATH_CAP], legacy_desktop_link[PATH_CAP];

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

static void register_legacy(const wchar_t *path) {
    HKEY key;
    require(RegCreateKeyExW(HKEY_CURRENT_USER,test_legacy_key,0,NULL,0,KEY_WRITE,NULL,&key,NULL)==ERROR_SUCCESS,
            "create private legacy registration");
    require(registry_text(key,L"InstallLocation",path),"record private legacy installation");
    RegCloseKey(key);
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
    require(CreateDirectoryW(base,NULL),"create private setup directory");
    child(install_dir,base,L"Jotila"); child(app_path,install_dir,L"Jotila.exe");
    child(uninstall_path,install_dir,L"Uninstall.exe");
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
