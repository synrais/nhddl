#ifndef TITLE_ID_C
#define TITLE_ID_C

#include "common.h"
#include "gui.h"
#include "target.h"       // for TargetList, Target, appendTarget
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SECTOR_SIZE 2048
#define TOC_LBA 16
#define SYSTEM_CNF_NAME "SYSTEM.CNF;1"

// Forward declarations
static char *extractIDFromPath(const char *path);
static int getPVD(int fd, uint32_t *lba, int *length);
static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLen);

struct dirTOCEntry {
    short length;
    uint32_t fileLBA;
    uint32_t fileLBA_bigend;
    uint32_t fileSize;
    uint32_t fileSize_bigend;
    uint8_t dateStamp[6];
    uint8_t reserved1;
    uint8_t fileProperties;
    uint8_t reserved2[6];
    uint8_t filenameLength;
    char filename[128];
} __attribute__((packed));

static unsigned char iso_buf[SECTOR_SIZE];

// Quick path ID extraction
static char *extractIDFromPath(const char *path) {
    const char *filename = strrchr(path, '/');
    if (!filename) filename = path; else filename++;
    if (strlen(filename) < 12) return NULL;
    if (filename[4] != '_' || filename[8] != '.' || filename[11] != '.') return NULL;
    char *id = malloc(12);
    if (!id) return NULL;
    memcpy(id, filename, 11);
    id[11] = '\0';
    return id;
}

// Gets title ID
char *getTitleID(char *path) {
    char *quick = extractIDFromPath(path);
    if (quick) return quick;

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("WARN: %s: open failed: %s\n", path, strerror(errno));
        return NULL;
    }

    uint32_t rootLBA;
    int rootLen;
    if (getPVD(fd, &rootLBA, &rootLen) != 0) {
        printf("WARN: %s: PVD parse failed\n", path);
        close(fd);
        return NULL;
    }

    struct dirTOCEntry *toc = getTOCEntry(fd, rootLBA, rootLen);
    if (!toc) {
        printf("WARN: %s: SYSTEM.CNF not found\n", path);
        close(fd);
        return NULL;
    }

    if (lseek64(fd, (int64_t)toc->fileLBA * SECTOR_SIZE, SEEK_SET) < 0) {
        printf("WARN: %s: lseek failed\n", path);
        close(fd);
        return NULL;
    }
    char *buf = malloc(toc->length);
    if (!buf || read(fd, buf, toc->length) != toc->length) {
        printf("WARN: %s: read failed\n", path);
        free(buf);
        close(fd);
        return NULL;
    }

    char *boot2 = strstr(buf, "BOOT2");
    if (!boot2) {
        free(buf);
        close(fd);
        return NULL;
    }
    char *file = strstr(boot2, "cdrom0:");
    char *end = strstr(boot2, ";");
    if (!file || !end) {
        free(buf);
        close(fd);
        return NULL;
    }
    end[1] = '1'; end[2] = '\0';
    char *id = malloc(12);
    if (id) {
        memcpy(id, file + 8, 11);
        id[11] = '\0';
    }

    free(buf);
    close(fd);
    return id;
}

// Reads Primary Volume Descriptor
static int getPVD(int fd, uint32_t *lba, int *length) {
    if (lseek64(fd, (int64_t)TOC_LBA * SECTOR_SIZE, SEEK_SET) < 0) return -1;
    if (read(fd, iso_buf, SECTOR_SIZE) != SECTOR_SIZE) return -1;
    if (iso_buf[0] == 1 && !memcmp(iso_buf + 1, "CD001", 5)) {
        struct dirTOCEntry *d = (void *)(iso_buf + 0x9c);
        *lba = d->fileLBA;
        *length = d->length;
        return 0;
    }
    return -1;
}

// Retrieves SYSTEM.CNF TOC entry
static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLen) {
    while (tocLen > 0) {
        if (lseek64(fd, (int64_t)tocLBA * SECTOR_SIZE, SEEK_SET) < 0) return NULL;
        if (read(fd, iso_buf, SECTOR_SIZE) != SECTOR_SIZE) return NULL;
        int pos = 0;
        while (pos < SECTOR_SIZE) {
            struct dirTOCEntry *e = (void *)(iso_buf + pos);
            if (e->length == 0) break;
            if (e->filenameLength && strcmp(e->filename, SYSTEM_CNF_NAME) == 0) return e;
            pos += (e->length & 0xFFFF);
        }
        tocLen -= SECTOR_SIZE;
        tocLBA++;
    }
    return NULL;
}

// Cache support
int loadTitleListCache(const char *devicePath, TargetList *list) {
    char path[256];
    snprintf(path, sizeof(path), "%s/titlelist.bin", devicePath);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int cnt;
    if (fread(&cnt, sizeof(cnt), 1, f) != 1) { fclose(f); return -1; }
    for (int i = 0; i < cnt; i++) {
        Target *t = malloc(sizeof(Target));
        if (fread(t, sizeof(Target), 1, f) == 1) {
            appendTarget(list, t);
        } else {
            free(t);
            break;
        }
    }
    fclose(f);
    return 0;
}

int saveTitleListCache(const char *devicePath, TargetList *list) {
    char path[256];
    snprintf(path, sizeof(path), "%s/titlelist.bin", devicePath);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int cnt = list->total;
    fwrite(&cnt, sizeof(cnt), 1, f);
    for (Target *t = list->first; t; t = t->next) {
        fwrite(t, sizeof(Target), 1, f);
    }
    fclose(f);
    return 0;
}

#endif // TITLE_ID_C
