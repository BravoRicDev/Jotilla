/* Peak requested working-vector size, not process RAM or allocator internals. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
static size_t largest_request;
static void *measured_realloc(void *pointer, size_t size) {
    if(size>largest_request) largest_request=size;
    return realloc(pointer,size);
}
#define realloc measured_realloc
#include "../src/tags.c"
#undef realloc

int wmain(void) {
    TagStore store={0};
    LARGE_INTEGER frequency,start,end;
    int mode,i,run;
    store.count=10000;
    store.items=(TagRecord *)calloc(store.count,sizeof(*store.items));
    if(!store.items) return 1;
    QueryPerformanceFrequency(&frequency);
    puts("notes,tags_per_note,distribution,mean_ms,distinct,largest_vector_bytes");
    for(mode=0;mode<4;++mode) {
        static const char *distribution[]={"shared","unique_sorted","unique_reverse","unique_shuffled"};
        size_t distinct=0;
        for(i=0;i<(int)store.count;++i) {
            wchar_t names[TAGS_CAP]; int j,used=0;
            for(j=0;j<TAGS_MAX;++j) {
                int tag=mode?i*TAGS_MAX+j:j;
                if(mode==2) tag=240000-1-tag;
                if(mode==3) tag=(int)((unsigned long long)tag*104729%240000);
                used+=swprintf_s(names+used,TAGS_CAP-(size_t)used,L"%stag%06d",j?L";":L"",tag);
            }
            free(store.items[i].tags); store.items[i].tags=_wcsdup(names);
            if(!store.items[i].tags) { tags_store_free(&store); return 1; }
        }
        largest_request=0; QueryPerformanceCounter(&start);
        for(run=0;run<5;++run) {
            TagCatalog catalog;
            if(!tags_catalog_build(&store,&catalog)) { tags_store_free(&store); return 1; }
            distinct=catalog.count; tags_catalog_free(&catalog);
        }
        QueryPerformanceCounter(&end);
        printf("%zu,%d,%s,%.3f,%zu,%zu\n",store.count,TAGS_MAX,distribution[mode],
            (end.QuadPart-start.QuadPart)*200.0/frequency.QuadPart,distinct,largest_request);
    }
    tags_store_free(&store); return 0;
}
