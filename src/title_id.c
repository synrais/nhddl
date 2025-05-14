#include "common.h"
#include "gui.h"
#include "target.h"
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

struct dirTOCEntry {
  short length;
  uint32_t fileLBA;         // 2
  uint32_t fileLBA_bigend;  // 6
  uint32_t fileSize;        // 10
  uint32_t fileSize_bigend; // 14
  uint8_t dateStamp[6];     // 18
  uint8_t reserved1;        // 24
  uint8_t fileProperties;   // 25
  uint8_t reserved2[6];     // 26
  uint8_t filenameLength;   // 32
  char filename[128];       // 33
} __attribute__((packed));

static unsigned char iso_buf[SECTOR_SIZE];

// Reads Primary Volume Descriptor from specified LBA and extracts root directory LBA
static int getPVD(int fd, uint32_t *lba, int *length);
// Retrieves SYSTEM.CNF TOC entry using specified root directory TOC
static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLength);

// Loads SYSTEM.CNF from ISO and extracts title ID
char *getTitleID(char *path) {
  // Fast path: derive ID from filename when using old OPL naming
  char *quick = extractIDFromPath(path);
  if (quick) return quick;

  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    printf("WARN: %s: Failed to open file: %d\n", path, fd);
    return NULL;
  }

  uint32_t rootLBA = 0;
  int rootLength = 0;
  if (getPVD(fd, &rootLBA, &rootLength) != 0) {
    printf("WARN: %s: Failed to parse ISO PVD\n", path);
    close(fd);
    return NULL;
  }

  struct dirTOCEntry *tocEntry = getTOCEntry(fd, rootLBA, rootLength);
  if (!tocEntry) {
    printf("WARN: %s: Failed to find SYSTEM.CNF\n", path);
    close(fd);
    return NULL;
  }

  if (lseek64(fd, (int64_t)tocEntry->fileLBA * SECTOR_SIZE, SEEK_SET) < 0) {
    printf("WARN: %s: Failed to seek to SYSTEM.CNF\n", path);
    close(fd);
    return NULL;
  }
  char *systemCNF = malloc(tocEntry->length);
  if (read(fd, systemCNF, tocEntry->length) != tocEntry->length) {
    printf("WARN: %s: Failed to read SYSTEM.CNF\n", path);
    free(systemCNF);
    close(fd);
    return NULL;
  }

  char *boot2Arg = strstr(systemCNF, "BOOT2");
  if (!boot2Arg) {
    printf("WARN: %s: BOOT2 not found in SYSTEM.CNF\n", path);
    free(systemCNF);
    close(fd);
    return NULL;
  }

  char *titleID = calloc(12, 1);
  char *selfFile = strstr(boot2Arg, "cdrom0:");
  char *argEnd = strstr(boot2Arg, ";");
  if (!selfFile || !argEnd) {
    printf("WARN: %s: File name not found in SYSTEM.CNF\n", path);
    free(titleID);
    titleID = NULL;
  } else {
    argEnd[1] = '1';
    argEnd[2] = '\0';
    memcpy(titleID, &selfFile[8], 11);
  }

  free(systemCNF);
  close(fd);
  return titleID;
}

static int getPVD(int fd, uint32_t *lba, int *length) {
  if (lseek64(fd, (int64_t)TOC_LBA * SECTOR_SIZE, SEEK_SET) < 0) {
    return -EIO;
  }
  if (read(fd, iso_buf, SECTOR_SIZE) == SECTOR_SIZE) {
    if (iso_buf[0] == 1 && !memcmp(&iso_buf[1], "CD001", 5)) {
      struct dirTOCEntry *tocEntryPointer = (struct dirTOCEntry *)&iso_buf[0x9c];
      *lba = tocEntryPointer->fileLBA;
      *length = tocEntryPointer->length;
      return 0;
    }
    return -EINVAL;
  }
  return -EIO;
}

static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLength) {
  while (tocLength > 0) {
    if (lseek64(fd, (int64_t)tocLBA * SECTOR_SIZE, SEEK_SET) < 0) {
      return NULL;
    }
    if (read(fd, iso_buf, SECTOR_SIZE) != SECTOR_SIZE) {
      return NULL;
    }

    int tocPos = 0;
    struct dirTOCEntry *entry;
    while (tocPos < SECTOR_SIZE) {
      entry = (struct dirTOCEntry *)&iso_buf[tocPos];
      if (entry->length == 0) break;
      if (entry->filenameLength && !strcmp(SYSTEM_CNF_NAME, entry->filename)) {
        return entry;
      }
      tocPos += (entry->length & 0xFFFF);
    }
    tocLength -= SECTOR_SIZE;
    tocLBA++;
  }
  return NULL;
}

// Cache support
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
    while (cur) {
        fwrite(cur, sizeof(Target), 1, file);
        cur = cur->next;
    }
    fclose(file);
    return 0;
}
