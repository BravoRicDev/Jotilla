/* Measure the actual Release executable in an isolated portable archive. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <wchar.h>
#include "../src/resource.h"
#define CAP 1024
static DWORD app_pid;
static int visible_notes;

static BOOL CALLBACK prepare_note(HWND window,LPARAM fill) {
    DWORD pid;
    wchar_t name[80];
    GetWindowThreadProcessId(window,&pid);
    if (pid!=app_pid) return TRUE;
    GetClassNameW(window,name,80);
    if (wcscmp(name,APP_NOTE)) return TRUE;
    ++visible_notes;
    if (fill) {
        HWND edit=GetDlgItem(window,10);
        SendMessageW(edit,EM_SETSEL,0,-1);
        SendMessageW(edit,EM_REPLACESEL,TRUE,(LPARAM)L"A small note.\r\nPlain text, saved locally.");
    }
    return TRUE;
}

static ULONGLONG cpu_ticks(HANDLE process) {
    FILETIME created,exited,kernel,user;
    ULARGE_INTEGER k,u;
    if (!GetProcessTimes(process,&created,&exited,&kernel,&user)) return 0;
    k.LowPart=kernel.dwLowDateTime; k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime; u.HighPart=user.dwHighDateTime;
    return k.QuadPart+u.QuadPart;
}

int wmain(int argc,wchar_t **argv) {
    wchar_t temporary[CAP],root[CAP],app[CAP],flag[CAP],command[CAP+4],pattern[CAP],file[CAP];
    STARTUPINFOW startup={0};
    PROCESS_INFORMATION process={0};
    PROCESS_MEMORY_COUNTERS_EX memory={0};
    WIN32_FIND_DATAW entry;
    HANDLE handle;
    HWND control=NULL;
    DWORD pid;
    DWORD_PTR reply;
    int count,i;
    ULONGLONG deadline,wall,start_cpu;
    if (argc!=2 || FindWindowW(APP_CONTROLLER,NULL)) return 1;
    if (!GetTempPathW(CAP,temporary)) return 1;
    swprintf_s(root,CAP,L"%sPicoSlip-profile-%lu-%llu",temporary,GetCurrentProcessId(),GetTickCount64());
    if (!CreateDirectoryW(root,NULL)) return 1;
    swprintf_s(app,CAP,L"%s\\PicoSlip.exe",root);
    swprintf_s(flag,CAP,L"%s\\portable.flag",root);
    if (!CopyFileW(argv[1],app,TRUE)) return 1;
    handle=CreateFileW(flag,GENERIC_WRITE,0,NULL,CREATE_NEW,0,NULL);
    if (handle==INVALID_HANDLE_VALUE) return 1;
    CloseHandle(handle);
    startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    swprintf_s(command,CAP+4,L"\"%s\"",app);
    if (!CreateProcessW(app,command,NULL,NULL,FALSE,0,NULL,root,&startup,&process)) return 1;
    app_pid=process.dwProcessId; deadline=GetTickCount64()+10000;
    do {
        control=FindWindowW(APP_CONTROLLER,NULL);
        if (control) { GetWindowThreadProcessId(control,&pid); if (pid!=app_pid) return 1; }
        visible_notes=0; EnumWindows(prepare_note,0); Sleep(20);
    } while ((!control || visible_notes!=1) && GetTickCount64()<deadline);
    if (!control || visible_notes!=1) return 1;
    puts("notes,private_mib,working_set_mib,idle_cpu_percent_one_core,sample_seconds");
    for (count=1;count<=10;count+=9) {
        if (count==10) for (i=0;i<9;++i)
            if (!SendMessageTimeoutW(control,WM_APP+2,0,0,SMTO_ABORTIFHUNG,5000,&reply)) return 1;
        visible_notes=0; EnumWindows(prepare_note,1);
        if (visible_notes!=count) return 1;
        Sleep(1200);
        start_cpu=cpu_ticks(process.hProcess); wall=GetTickCount64();
        Sleep(10000);
        memory.cb=sizeof(memory);
        if (!GetProcessMemoryInfo(process.hProcess,(PROCESS_MEMORY_COUNTERS *)&memory,sizeof(memory))) return 1;
        printf("%d,%.2f,%.2f,%.4f,%.2f\n",count,memory.PrivateUsage/1048576.0,memory.WorkingSetSize/1048576.0,
               (cpu_ticks(process.hProcess)-start_cpu)/100.0/(GetTickCount64()-wall), (GetTickCount64()-wall)/1000.0);
        fflush(stdout);
    }
    if (!SendMessageTimeoutW(control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,5000,&reply) || WaitForSingleObject(process.hProcess,10000)!=WAIT_OBJECT_0) return 1;
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    swprintf_s(pattern,CAP,L"%s\\notes\\*",root);
    handle=FindFirstFileW(pattern,&entry);
    if (handle!=INVALID_HANDLE_VALUE) {
        do { if (!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            swprintf_s(file,CAP,L"%s\\notes\\%s",root,entry.cFileName); if (!DeleteFileW(file)) return 1;
        } } while (FindNextFileW(handle,&entry));
        FindClose(handle);
    }
    swprintf_s(file,CAP,L"%s\\notes",root);
    RemoveDirectoryW(file); DeleteFileW(app); DeleteFileW(flag); RemoveDirectoryW(root);
    return 0;
}
