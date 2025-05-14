
// title_id.c - corrected to compile (removed appendTarget/extractIDFromPath)

#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

#define SECTOR_SIZE 2048
#define TOC_LBA 16
#define SYSTEM_CNF_NAME "SYSTEM.CNF;1"

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

// Forward declarations
static int getPVD(int fd, uint32_t *lba, int *length);
static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLength);

char *getTitleID(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("WARN: %s: Failed to open file: %s\n", path, strerror(errno));
        return NULL;
    }

    uint32_t rootLBA = 0;
    int rootLen = 0;
    if (getPVD(fd, &rootLBA, &rootLen) != 0) {
        printf("WARN: %s: Failed to parse ISO PVD\n", path);
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
        printf("WARN: %s: Seek failed\n", path);
        close(fd);
        return NULL;
    }
    char *buf = malloc(toc->length + 1);
    if (read(fd, buf, toc->length) != toc->length) {
        printf("WARN: %s: Read failed\n", path);
        free(buf);
        close(fd);
        return NULL;
    }
    buf[toc->length] = '\0';

    char *p = strstr(buf, "cdrom0:");
    if (!p) {
        free(buf);
        close(fd);
        return NULL;
    }
    // extract 11-char ID after cdrom0:
    char *id = malloc(12);
    memcpy(id, p + 8, 11);
    id[11] = '\0';

    free(buf);
    close(fd);
    return id;
}

// Read Primary Volume Descriptor
static int getPVD(int fd, uint32_t *lba, int *length) {
    if (lseek64(fd, (int64_t)TOC_LBA * SECTOR_SIZE, SEEK_SET) < 0)
        return -EIO;
    if (read(fd, iso_buf, SECTOR_SIZE) != SECTOR_SIZE)
        return -EIO;
    if (iso_buf[0] != 1 || memcmp(&iso_buf[1], "CD001", 5) != 0)
        return -EINVAL;
    struct dirTOCEntry *root = (struct dirTOCEntry *)&iso_buf[0x9c];
    *lba = root->fileLBA;
    *length = root->length;
    return 0;
}

// Get SYSTEM.CNF entry
static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLength) {
    while (tocLength > 0) {
        if (lseek64(fd, (int64_t)tocLBA * SECTOR_SIZE, SEEK_SET) < 0)
            return NULL;
        if (read(fd, iso_buf, SECTOR_SIZE) != SECTOR_SIZE)
            return NULL;
        int pos = 0;
        while (pos < SECTOR_SIZE) {
            struct dirTOCEntry *e = (struct dirTOCEntry *)&iso_buf[pos];
            if (e->length == 0) break;
            if (e->filenameLength && !strcmp(e->filename, SYSTEM_CNF_NAME))
                return e;
            pos += (e->length & 0xFFFF);
        }
        tocLength -= SECTOR_SIZE;
        tocLBA++;
    }
    return NULL;
}

// Stubbed cache functions (no-op, to allow compilation)
int loadTitleListCache(void *list, const char *cachePath) {
    // Cache loading disabled
    return -1;
}
int saveTitleListCache(void *list, const char *cachePath) {
    // Cache saving disabled
    return -1;
}
