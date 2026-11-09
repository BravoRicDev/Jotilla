/* Launch the packaged application against a disposable portable archive. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../src/resource.h"
#include "../src/palette_picker.h"
#define CAP 1024
static wchar_t root[CAP], executable[CAP];
static HANDLE running;
static DWORD running_pid;
static HWND control;

static void require(BOOL ok, const char *message) {
    if (!ok) {
        DWORD_PTR result;
        fprintf(stderr,"FAIL: %s (Windows error %lu)\n",message,GetLastError());
        if (control) SendMessageTimeoutW(control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result);
        if (running && WaitForSingleObject(running,3000)!=WAIT_OBJECT_0) TerminateProcess(running,1);
        exit(1);
    }
}

static void path(wchar_t value[CAP], const wchar_t *name) {
    require(swprintf_s(value,CAP,L"%s\\%s",root,name)>0,"bounded private launch path");
}

static void fixture(const wchar_t *name, const char *text) {
    wchar_t value[CAP];
    DWORD written, length=(DWORD)strlen(text);
    HANDLE file;
    path(value,name);
    file=CreateFileW(value,GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
    require(file!=INVALID_HANDLE_VALUE,"create portable startup fixture");
    require(WriteFile(file,text,length,&written,NULL) && written==length && CloseHandle(file),"write startup fixture");
}

static HANDLE launch(BOOL startup, DWORD *pid) {
    STARTUPINFOW info={sizeof(info)};
    PROCESS_INFORMATION process={0};
    wchar_t command[CAP+32];
    swprintf_s(command,CAP+32,L"\"%s\"%s",executable,startup ? L" --startup" : L"");
    info.dwFlags=STARTF_USESHOWWINDOW; info.wShowWindow=SW_SHOWNOACTIVATE;
    require(CreateProcessW(executable,command,NULL,NULL,FALSE,0,NULL,root,&info,&process),"launch private portable application");
    CloseHandle(process.hThread); *pid=process.dwProcessId;
    return process.hProcess;
}

static int note_windows(HWND windows[8]) {
    HWND window=NULL;
    DWORD pid;
    int count=0;
    while ((window=FindWindowExW(NULL,window,APP_NOTE,NULL))!=NULL) {
        GetWindowThreadProcessId(window,&pid);
        if (pid==running_pid) { if (count<8) windows[count]=window; ++count; }
    }
    return count;
}

static void start(BOOL startup) {
    HWND window=NULL;
    DWORD pid;
    running=launch(startup,&running_pid);
    require(WaitForInputIdle(running,8000)==0,"application reaches its event loop");
    while ((window=FindWindowExW(NULL,window,APP_CONTROLLER,NULL))!=NULL) {
        GetWindowThreadProcessId(window,&pid);
        if (pid==running_pid) { control=window; break; }
    }
    require(control!=NULL,"private process owns its controller");
}

static void stop(void) {
    DWORD_PTR result;
    require(SendMessageTimeoutW(control,WM_CLOSE,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,5000,&result)!=0 &&
            WaitForSingleObject(running,5000)==WAIT_OBJECT_0,"application saves and exits normally");
    CloseHandle(running); running=NULL; control=NULL;
}

static void second_launch(BOOL startup) {
    DWORD pid, result;
    HANDLE process=launch(startup,&pid);
    require(WaitForSingleObject(process,5000)==WAIT_OBJECT_0 && GetExitCodeProcess(process,&result) && !result,
            "second launch reuses the running instance");
    CloseHandle(process);
    require(SendMessageTimeoutW(control,WM_NULL,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,NULL)!=0,"drain instance handoff");
    Sleep(50); /* NEW_NOTE_MESSAGE is posted; allow the receiving event loop to dispatch it. */
}

static BOOL CALLBACK palette_on_monitor(HMONITOR monitor,HDC dc,LPRECT rect,LPARAM data) {
    MONITORINFO info={sizeof(info)};
    HWND note=(HWND)data, edit=GetDlgItem(note,10), popup;
    DWORD pid;
    DWORD_PTR result;
    (void)dc; (void)rect;
    require(edit && GetMonitorInfoW(monitor,&info),"locate real-process editor and monitor");
    require(SetWindowPos(note,NULL,info.rcWork.left+32,info.rcWork.top+32,0,0,
                        SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE),"move private note to each monitor");
    require(SendMessageTimeoutW(edit,WM_RBUTTONDOWN,MK_RBUTTON,MAKELPARAM(40,60),
                               SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result) &&
            SendMessageTimeoutW(edit,WM_RBUTTONUP,0,MAKELPARAM(40,60),
                               SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result),"release executable handles right-click");
    popup=FindWindowW(PALETTE_PICKER_CLASS,NULL);
    pid=0; if(popup) GetWindowThreadProcessId(popup,&pid);
    require(popup && pid==running_pid && GetDpiForWindow(popup)==GetDpiForWindow(note) &&
            MonitorFromWindow(popup,MONITOR_DEFAULTTONEAREST)==monitor,"release palette uses target monitor DPI");
    require(SendMessageTimeoutW(popup,WM_KEYDOWN,VK_RETURN,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result) &&
            !IsWindow(popup) && WaitForSingleObject(running,0)==WAIT_TIMEOUT,"release survives palette selection");
    return TRUE;
}

int main(void) {
    wchar_t temporary[CAP], value[CAP], pattern[CAP];
    HWND windows[8];
    HANDLE mutex, scan;
    WIN32_FIND_DATAW entry;
    RECT before, after;
    int count;
    mutex=OpenMutexW(SYNCHRONIZE,FALSE,L"Local\\PicoSlip.SingleInstance");
    if (mutex) { CloseHandle(mutex); puts("SKIP: real-process startup checks leave the running user application untouched."); return 0; }
    require(GetTempPathW(CAP,temporary)>0,"locate temporary directory");
    swprintf_s(root,CAP,L"%sJotila startup è %lu-%llu",temporary,GetCurrentProcessId(),GetTickCount64());
    require(CreateDirectoryW(root,NULL),"create private portable root");
    path(executable,L"Jotila.exe");
    require(CopyFileW(L"dist\\Jotila.exe",executable,TRUE),"copy release executable into portable fixture");
    fixture(L"portable.flag","");
    start(TRUE);
    require(note_windows(windows)==0,"automatic startup of an empty archive creates no blank note");
    second_launch(TRUE);
    require(note_windows(windows)==0,"duplicate automatic startup creates no note");
    second_launch(FALSE);
    require(note_windows(windows)==1,"an intentional second launch still creates a note");
    require(EnumDisplayMonitors(NULL,NULL,palette_on_monitor,(LPARAM)windows[0]),"check release palette across monitors");
    stop();
    path(value,L"notes\\notes.ini"); require(DeleteFileW(value),"replace private empty-session preferences");
    fixture(L"notes\\note-session.txt","Restored after startup");
    fixture(L"notes\\note-archived.txt","Keep archived");
    fixture(L"notes\\notes.ini","[note-session]\nx=170\ny=180\nwidth=300\nheight=240\nopen=1\ncollapsed=1\n[note-archived]\nopen=0\n");
    start(TRUE);
    count=note_windows(windows);
    require(count==1 && IsWindowVisible(windows[0]) && GetForegroundWindow()!=windows[0],
            "startup shows only saved open notes without taking focus");
    require(GetWindowRect(windows[0],&before),"read restored note position");
    second_launch(TRUE);
    require(note_windows(windows)==1,"duplicate startup does not add to a restored session");
    stop(); start(TRUE);
    require(note_windows(windows)==1 && GetWindowRect(windows[0],&after) && EqualRect(&before,&after),
            "real process restart retains note geometry and folded state");
    stop();
    path(pattern,L"notes\\*");
    scan=FindFirstFileW(pattern,&entry);
    require(scan!=INVALID_HANDLE_VALUE,"enumerate private archive for cleanup");
    do {
        if (!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) {
            swprintf_s(value,CAP,L"%s\\notes\\%s",root,entry.cFileName);
            require(DeleteFileW(value),"remove private startup note");
        }
    } while (FindNextFileW(scan,&entry));
    FindClose(scan);
    path(value,L"notes"); require(RemoveDirectoryW(value),"remove private startup archive");
    path(value,L"portable.flag"); require(DeleteFileW(value),"remove private portable marker");
    require(DeleteFileW(executable) && RemoveDirectoryW(root),"remove private launch fixtures");
    puts("PASS: real-process startup, right-click on each monitor, no blank or duplicate notes, focus preservation and session restart.");
    return 0;
}
