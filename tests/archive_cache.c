/* A date-only notification must not reread metadata or evict note content. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include "../src/metadata.h"
static unsigned metadata_reads;
static BOOL counted_read(const wchar_t *path,wchar_t **text,BOOL *unicode) {
    ++metadata_reads; return metadata_read(path,text,unicode);
}
#define metadata_read counted_read
#include "../src/archive.c"
#undef metadata_read

static void require(BOOL condition,const char *message) {
    if(!condition) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
static unsigned members(Archive *archive,unsigned date) {
    int day=date_ordinal(date);
    return (unsigned)(archive->day_offsets[day+1]-archive->day_offsets[day]);
}
int wmain(void) {
    Archive archive={0};
    ArchiveEntry entries[3]={0};
    SearchHit hits[2]={{0,MAXDWORD,0},{2,MAXDWORD,0}};
    SearchResult results={0};
    const wchar_t *preview;
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_STANDARD_CLASSES};
    int i;
    InitCommonControlsEx(&controls);
    archive_window=CreateWindowExW(0,L"STATIC",L"",0,0,0,0,0,HWND_MESSAGE,NULL,GetModuleHandleW(NULL),NULL);
    require(archive_window!=NULL,"create isolated archive owner");
    archive.window=archive_window; SetWindowLongPtrW(archive_window,GWLP_USERDATA,(LONG_PTR)&archive);
    archive.calendar=calendar_create(archive_window,11); require(archive.calendar!=NULL,"create calendar index target");
    calendar_set_year(archive.calendar,2028);
    archive.entries=entries; archive.count=archive.visible=3; archive.calendar_view=TRUE;
    for(i=0;i<3;++i) {
        swprintf_s(entries[i].id,80,L"note-2028-01-0%d_00-00-00-000-000",3-i);
        entries[i].date=20280103u-i; entries[i].scheduled=20280229;
        entries[i].preview=_wcsdup(L"Cached body"); entries[i].tags=_wcsdup(L"work"); entries[i].loaded=TRUE;
        require(entries[i].preview && entries[i].tags,"allocate content cache");
    }
    calendar_update(&archive); require(members(&archive,20280229)==3,"initial day membership");
    preview=entries[0].preview;
    archive_date_changed(entries[0].id,20281231);
    require(members(&archive,20280229)==2 && members(&archive,20281231)==1,"move changes both day buckets");
    require(entries[0].preview==preview && entries[0].loaded && !wcscmp(entries[0].tags,L"work"),"date changes preserve cached body and tags");
    archive_date_changed(entries[0].id,20300101);
    require(!members(&archive,20281231),"moving to another year removes old membership");
    archive_date_changed(entries[0].id,0); require(!entries[0].scheduled,"remove date clears cached association");
    results.hits=hits; results.count=2; archive.results=&results; archive.visible=2;
    archive_date_changed(entries[0].id,20280229);
    require(members(&archive,20280229)==2 && archive.results==&results,"filtered results retain exactly their membership");
    require(!metadata_reads,"known date-only updates perform zero metadata reads");
    archive.calendar_view=FALSE; archive_date_changed(entries[0].id,20280101); archive.calendar_view=TRUE;
    calendar_update(&archive); require(members(&archive,20280101)==1,"date changes while hidden survive view changes");
    archive_date_changed(L"note-2030-01-01_00-00-00-000-000",20300101);
    require(archive.calendar_dirty && archive.refresh_pending,"new notes queue a full snapshot refresh");
    for(i=0;i<3;++i) { free(entries[i].preview); free(entries[i].tags); }
    free(archive.day_entries); DestroyWindow(archive_window); archive_window=NULL;
    puts("PASS: date-only updates read metadata 0 times, preserve previews/results, and update both day buckets, filters and hidden views.");
    return 0;
}
