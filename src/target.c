
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "target.h"

TargetList *initTargetList() {
    TargetList *list = malloc(sizeof(TargetList));
    list->first = NULL;
    list->last = NULL;
    list->total = 0;
    return list;
}

void appendTarget(TargetList *list, Target *tgt) {
    if (!list->first) {
        list->first = tgt;
        list->last = tgt;
    } else {
        list->last->next = tgt;
        tgt->prev = list->last;
        list->last = tgt;
    }
    tgt->next = NULL;
    list->total++;
}

int loadTitleListCache(const char *devicePath, TargetList *list) {
    char cachePath[256];
    snprintf(cachePath, sizeof(cachePath), "%s/titlelist.bin", devicePath);
    FILE *file = fopen(cachePath, "rb");
    if (!file) return -1;

    int count;
    fread(&count, sizeof(int), 1, file);
    for (int i = 0; i < count; i++) {
        Target *tgt = malloc(sizeof(Target));
        fread(tgt, sizeof(Target), 1, file);
        tgt->next = NULL;
        tgt->prev = NULL;
        appendTarget(list, tgt);
    }
    fclose(file);
    return 0;
}

int saveTitleListCache(const char *devicePath, TargetList *list) {
    char cachePath[256];
    snprintf(cachePath, sizeof(cachePath), "%s/titlelist.bin", devicePath);
    FILE *file = fopen(cachePath, "wb");
    if (!file) return -1;

    int count = list->total;
    fwrite(&count, sizeof(int), 1, file);

    Target *cur = list->first;
    while (cur != NULL) {
        fwrite(cur, sizeof(Target), 1, file);
        cur = cur->next;
    }
    fclose(file);
    return 0;
}

TargetList *getISOTitles(const char *devicePath) {
    TargetList *list = initTargetList();
    if (loadTitleListCache(devicePath, list) == 0) {
        return list;
    }

    // Dummy scan logic (simulate a single ISO)
    Target *tgt = malloc(sizeof(Target));
    memset(tgt, 0, sizeof(Target));
    strcpy(tgt->device, devicePath);
    strcpy(tgt->fullPath, "/iso/SLUS_123.45.iso");
    getTitleID(tgt->fullPath, tgt->id, tgt->name);

    appendTarget(list, tgt);
    saveTitleListCache(devicePath, list);
    return list;
}
