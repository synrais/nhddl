// Implements titleScanFunc for file-based devices (MMCE, BDM)
#include "common.h"
#include "devices.h"
#include "gui.h"
#include "options.h"
#include "title_id.h"
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

typedef struct {
  char titleID[12];
  char *fullPath;
} CacheEntry;

typedef struct TitleIDCache {
  int total;           // Total number of elements in cache
  int lastMatchedIdx;  // Used to skip ahead to the last matched entry when getting title ID from cache
  CacheEntry *entries; // Pointer to cache entry array
} TitleIDCache;

int _findISO(DIR *directory, TargetList *result, struct DeviceMapEntry *device);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering _findISO%s", "");
void processTitleID(TargetList *result, struct DeviceMapEntry *device);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering processTitleID%s", "");

int storeTitleIDCache(TargetList *list, struct DeviceMapEntry *device);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering storeTitleIDCache%s", "");
int loadTitleIDCache(TitleIDCache *cache, struct DeviceMapEntry *device);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering loadTitleIDCache%s", "");
char *getCachedTitleID(char *fullPath, TitleIDCache *cache);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering getCachedTitleID%s", "");
void freeTitleCache(TitleIDCache *cache);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering freeTitleCache%s", "");

// Directories to skip when browsing for ISOs
const char *ignoredDirs[] = {
    "nhddl", "neutrino", "APPS", "ART", "CFG", "CHT", "LNG", "THM", "VMC", "XEBPLUS", "MemoryCards", "bbnl",
};
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting freeTitleCache%s", "");

// Used by _findISO to limit recursion depth
#define MAX_SCAN_DEPTH 6
static int curRecursionLevel = 1;

// Scans given storage device and appends valid launch candidates to TargetList
// Returns 0 if successful, non-zero if no targets were found or an error occurs
int findISO(TargetList *result, struct DeviceMapEntry *device) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering findISO%s", "");
  DIR *directory;

  if (device->mode == MODE_NONE || device->mountpoint == NULL) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Device mode NONE or mountpoint NULL in findISO%s", "");
    return -ENODEV;

  curRecursionLevel = 1; // Reset recursion level
  directory = opendir(device->mountpoint);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Opening directory for mountpoint: %s", device->mountpoint);
  // Check if the directory can be opened
  if (directory == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to open directory for mountpoint: %s", device->mountpoint);
    uiSplashLogString(LEVEL_ERROR, "ERROR: Can't open %s\n", device->mountpoint);
    return -ENOENT;
  }

  chdir(device->mountpoint);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Changing directory to mountpoint: %s", device->mountpoint);
  if (_findISO(directory, result, device)) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] _findISO returned error for device: %s", device->mountpoint);
    closedir(directory);
    return -ENOENT;
  }
  closedir(directory);

  if (result->total == 0) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] No ISOs found after scanning %s", device->mountpoint);
    return -ENOENT;
  }

  // Get title IDs for each found title
  processTitleID(result, device);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Processing Title IDs for device: %s", device->mountpoint);

  if (result->first == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] result->first is NULL after processTitleID%s", "");
    return -ENOENT;
  }

  // Set indexes for each title
  int idx = 0;
  Target *curTitle = result->first;
  while (curTitle != NULL) {
    curTitle->idx = idx;
    idx++;
    curTitle = curTitle->next;
  }

  return 0;
}
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting findISO%s", "");

// Searches rootpath and adds discovered ISOs to TargetList
int _findISO(DIR *directory, TargetList *result, struct DeviceMapEntry *device) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering _findISO%s", "");
  if (directory == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Directory is NULL in _findISO%s", "");
    return -ENOENT;

  // Read directory entries
  struct dirent *entry;
  char *fileext;
  char titlePath[PATH_MAX + 1];
  if (!getcwd(titlePath, PATH_MAX + 1)) { // Initialize titlePath with current working directory
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to getcwd in _findISO%s", "");
    uiSplashLogString(LEVEL_ERROR, "Failed to get cwd\n");
    return -ENOENT;
  }
  int cwdLen = strlen(titlePath);     // Get the length of base path string
  if (titlePath[cwdLen - 1] != '/') { // Add path separator if cwd doesn't have one
    strcat(titlePath, "/");
    cwdLen++;
  }

  curRecursionLevel++;
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Recursion level: %d", curRecursionLevel);
  if (curRecursionLevel == MAX_SCAN_DEPTH) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Max recursion level reached at %s", titlePath);
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Skipping directory due to max recursion: %s", entry->d_name);
    printf("Max recursion limit reached, all directories in %s will be ignored\n", titlePath);

  while ((entry = readdir(directory)) != NULL) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Reading directory entries at %s", titlePath);
    // Reset titlePath by ending string on base path
    titlePath[cwdLen] = '\0';

    // Ignore .files and directories
    if (entry->d_name[0] == '.') {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Skipping dotfile or dotdir: %s", entry->d_name);
      continue;

    // Check if the entry is a directory using d_type
    switch (entry->d_type) {
    case DT_DIR:
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Found directory: %s", entry->d_name);
      // Ignore directories if max scan depth is reached
      if (curRecursionLevel == MAX_SCAN_DEPTH) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Max recursion level reached at %s", titlePath);
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Skipping directory due to max recursion: %s", entry->d_name);
        continue;

      // Ignore special and invalid directories (non-ASCII paths seem to return '?' and cause crashes when used with opendir)
      if ((entry->d_name[0] == '$') || (entry->d_name[0] == '?'))
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Skipping special/invalid directory: %s", entry->d_name);
        continue;

      for (int i = 0; i < sizeof(ignoredDirs) / sizeof(char *); i++) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Checking ignoredDirs for: %s", entry->d_name);
        if (!strcmp(ignoredDirs[i], entry->d_name))
          goto next;
      }

      // Generate full path, open dir and change cwd
      strcat(titlePath, entry->d_name);
      DIR *d = opendir(titlePath);
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Recursively opening dir: %s", titlePath);
      if (d == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to open subdirectory: %s", entry->d_name);
        printf("Failed to open %s for scanning\n", entry->d_name);
        continue;
      }
      chdir(titlePath);
      // Process inner directory recursively
      _findISO(d, result, device);
      closedir(d);

    next:
      break;
    default:
      // Make sure file has .iso extension
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Checking file for .iso extension: %s", entry->d_name);
      fileext = strrchr(entry->d_name, '.');
      if ((fileext != NULL) && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
        // Generate full path
        strcat(titlePath, entry->d_name);

        // Initialize target
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Found ISO: %s", titlePath);
        Target *title = calloc(sizeof(Target), 1);
        title->prev = NULL;
        title->next = NULL;
        title->fullPath = strdup(titlePath);
        title->device = device;

        // Get file name without the extension
        int nameLength = (int)(fileext - entry->d_name);
        title->name = calloc(sizeof(char), nameLength + 1);
        strncpy(title->name, entry->d_name, nameLength);
        // —— START OF SERIAL-AT-START DETECTION ——
        {
            char tmp1[5], tmp2[4], tmp3[3];
            if (sscanf(title->name, "%4[A-Z]_%3[0-9].%2[0-9]", tmp1, tmp2, tmp3) == 3) {
                // Build the 11-char serial
                char serial[12];
                memcpy(serial, title->name, 11);
                serial[11] = ' ';
                title->id = strdup(serial);
                // Strip serial + separator from the stored name
                char *rest = title->name + 11;
                if (*rest == '.' || *rest == ' ' || *rest == '_' || *rest == '-') {
                    rest++;
                char *clean = strdup(rest);
                free(title->name);
                title->name = clean;
            }
        }
        // —— END OF SERIAL-AT-START DETECTION ——


        // Increment title counter and update target list
        result->total++;
        if (result->first == NULL) {
          // If this is the first entry, update both pointers
          result->first = title;
          result->last = title;
        } else {
          insertIntoTargetList(result, title);
        }
      }
    }
  }
  curRecursionLevel--;

  return 0;
}
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting _findISO%s", "");

// Fills in title ID for every entry in the list
void processTitleID(TargetList *result, struct DeviceMapEntry *device) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering processTitleID%s", "");
  if (result->total == 0) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] No entries in TargetList, skipping processTitleID%s", "");
    return;

  // Load title cache
  TitleIDCache *cache = malloc(sizeof(TitleIDCache));
  int isCacheUpdateNeeded = 0;
  if (loadTitleIDCache(cache, device)) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] TitleID cache missing or invalid, rebuilding...%s", "");
    // Cache file missing or invalid: force update
    isCacheUpdateNeeded = 1;
    uiSplashLogString(LEVEL_INFO_NODELAY, "Building cache.bin...\n");
    free(cache);
    cache = NULL;
  } else if (cache->total != result->total) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Cache entry count does not match, update needed.%s", "");
    // Set flag if number of entries is different
    isCacheUpdateNeeded = 1;
  }

  // For every entry in target list, try to get title ID from cache
  // If cache doesn't have title ID for the path,
  // get it from ISO
  int cacheMisses = 0;
  char *titleID = NULL;
  Target *curTarget = result->first;
  while (curTarget != NULL) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Processing targets for title IDs...%s", "");
    // Ignore targets not belonging to the current device
    if (curTarget->device != device) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Target device mismatch, skipping.%s", "");
      curTarget = curTarget->next;
      continue;
    }

    // Try to get title ID from cache
    if (cache != NULL) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Attempting to fetch TitleID from cache.%s", "");
      titleID = getCachedTitleID(curTarget->fullPath, cache);
    }

    if (titleID != NULL) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Cache HIT for %s", curTarget->fullPath);
      curTarget->id = strdup(titleID);
    } else { // Get title ID from ISO
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Cache MISS for %s, scanning ISO", curTarget->fullPath);
      cacheMisses++;
      printf("Cache miss for %s\n", curTarget->fullPath);
      curTarget->id = getTitleID(curTarget->fullPath);
      if (curTarget->id == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to scan ISO for TitleID: %s", curTarget->fullPath);
        uiSplashLogString(LEVEL_WARN, "Failed to scan\n%s\n", curTarget->fullPath);
        curTarget = freeTarget(result, curTarget);
        result->total -= 1;
        continue;
      }
    }

    curTarget = curTarget->next;
  }
  freeTitleCache(cache);

  if ((cacheMisses > 0) || (isCacheUpdateNeeded)) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Writing updated cache to disk...%s", "");
    uiSplashLogString(LEVEL_INFO_NODELAY, "Updating title ID cache...\n");
    if (storeTitleIDCache(result, device)) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Failed to save updated cache.%s", "");
      uiSplashLogString(LEVEL_WARN, "Failed to save title ID cache\n");
    }
  }
}
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting processTitleID%s", "");

//
// Title cache
//

#define CACHE_MAGIC "NIDC"
#define CACHE_VERSION 2

const char titleIDCacheFile[] = "/cache.bin";

// Structs used to read and write cache file contents
typedef struct {
  char titleID[12];
  size_t pathLength; // Includes null-terminator
} CacheEntryHeader;

typedef struct {
  char magic[4];   // Must be always equal to CACHE_MAGIC
  uint8_t version; // Cache version
  int total;       // Total number of elements in cache file
} CacheMetadata;

// Saves TargetList into title ID cache on given storage device
int storeTitleIDCache(TargetList *list, struct DeviceMapEntry *device) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering storeTitleIDCache%s", "");
  if (list->total == 0) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] TargetList is empty, nothing to cache.%s", "");
    return 0;
  }

  // Get total number of valid cache entries
  int total = 0;
  Target *curTitle = list->first;
  while (curTitle != NULL) {
    if (strlen(curTitle->id) == 11) {
      total++;
    }
    curTitle = curTitle->next;
  }
  if (total == 0) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] No valid cache entries found.%s", "");
    printf("WARN: No valid cache entries found\n");
    return 0;
  }

  // Make sure path exists
  if (device->mode == MODE_NONE || device->mountpoint == NULL) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Device not ready or mountpoint NULL.%s", "");
    return -ENODEV;

  // Prepare paths and header
  char cachePath[PATH_MAX];
  char dirPath[PATH_MAX];
  CacheEntryHeader header;
  CacheMetadata meta = {.magic = CACHE_MAGIC, .version = CACHE_VERSION, .total = total};

  buildConfigFilePath(dirPath, device->mountpoint, NULL);
  buildConfigFilePath(cachePath, device->mountpoint, titleIDCacheFile);

  // Get path to config directory and make sure it exists
  struct stat st;
  if (stat(dirPath, &st) == -1) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Creating config directory: %s", dirPath);
    printf("Creating config directory: %s\n", dirPath);
    if (mkdir(dirPath, 0777)) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to create config directory.%s", "");
      printf("ERROR: Failed to create directory\n");
      return -EIO;
    }
  }

  // Open cache file for writing
  FILE *file = fopen(cachePath, "wb");
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Opening cache file for writing: %s", cachePath);
  if (file == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to open cache file for writing: %s", cachePath);
    printf("ERROR: Failed to open cache file for writing\n");
    return -EIO;
  }

  int result;
  // Write cache file header
  result = fwrite(&meta, sizeof(CacheMetadata), 1, file);
  if (!result) {
    printf("ERROR: Failed to write metadata: %d\n", errno);
    fclose(file);
    remove(cachePath);
    return result;
  }

  // Write each entry
  curTitle = list->first;
  int mountpointLen = -1;
  while (curTitle != NULL) {
    // Ignore empty entries or entries not belonging to the current device
    if ((strlen(curTitle->id) < 11) || (curTitle->device != device)) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Skipping invalid/foreign entry during cache write.%s", "");
      curTitle = curTitle->next;
      continue;
    }

    // Compare paths without the mountpoint
    mountpointLen = getRelativePathIdx(curTitle->fullPath);
    if (mountpointLen == -1) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Failed to get relative path for cache entry.%s", "");
      printf("WARN: Failed to get device mountpoint for %s\n", curTitle->name);
      curTitle = curTitle->next;
      continue;
    }

    // Write entry header
    memcpy(header.titleID, curTitle->id, sizeof(header.titleID));
    header.titleID[11] = '\0';
    header.pathLength = strlen(curTitle->fullPath) - mountpointLen + 1;
    result = fwrite(&header, sizeof(CacheEntryHeader), 1, file);
    if (!result) {
      printf("ERROR: %s: Failed to write header: %d\n", curTitle->name, errno);
      fclose(file);
      remove(cachePath);
      return result;
    }
    // Write full ISO path without the mountpoint
    result = fwrite(curTitle->fullPath + mountpointLen, header.pathLength, 1, file);
    if (!result) {
      printf("ERROR: %s: Failed to write full path: %d\n", curTitle->name, errno);
      fclose(file);
      remove(cachePath);
      return result;
    }
    curTitle = curTitle->next;
  }
  fclose(file);

  return 0;
}
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting storeTitleIDCache%s", "");

// Loads title ID cache from storage into cache
int loadTitleIDCache(TitleIDCache *cache, struct DeviceMapEntry *device) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering loadTitleIDCache%s", "");
  // Make sure path exists
  if (device->mode == MODE_NONE || device->mountpoint == NULL) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Device mode NONE or mountpoint NULL in loadTitleIDCache%s", "");
    return -ENODEV;

  cache->total = 0;
  cache->lastMatchedIdx = 0;

  // Open cache file for reading
  char cachePath[PATH_MAX];

  FILE *file;
  // Load the first found cache file
  buildConfigFilePath(cachePath, device->mountpoint, titleIDCacheFile);

  file = fopen(cachePath, "rb");
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Opening cache file for reading: %s", cachePath);
  if (file == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to open cache file for reading: %s", cachePath);
    return -ENOENT;

  int result;

  // Read cache file header
  CacheMetadata meta;
  result = fread(&meta, sizeof(CacheMetadata), 1, file);
  if (!result) {
    printf("ERROR: Failed to read cache metadata\n");
    fclose(file);
    return result;
  }

  // Make sure header is valid
  if (!strcmp(meta.magic, CACHE_MAGIC)) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Cache magic mismatch, refusing to load cache.%s", "");
    printf("ERROR: Cache magic doesn't match, refusing to load\n");
    fclose(file);
    return -EINVAL;
  }
  if (meta.version != CACHE_VERSION) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Cache version mismatch.%s", "");
    printf("ERROR: Unsupported cache version %d\n", meta.version);
    fclose(file);
    return -EINVAL;
  }

  // Allocate memory for cache entries based on total entry count from header metadata
  int readIndex = 0;
  cache->entries = malloc((sizeof(CacheEntry) * meta.total));
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Allocating cache entry memory for %d entries.", meta.total);
  if (cache->entries == NULL) {
    uiSplashLogString(LEVEL_ERROR, "[DEBUG] Failed to allocate memory for cache entries.%s", "");
    printf("ERROR: Can't allocate enough memory\n");
    fclose(file);
    return -ENOMEM;
  }

  // Read each entry into cache
  CacheEntryHeader header;
  char pathBuf[PATH_MAX + 1];
  while (!feof(file)) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Reading cache entries...%s", "");
    // Read cache entry header
    pathBuf[0] = '\0';
    result = fread(&header, sizeof(CacheEntryHeader), 1, file);
    if (result != 1) {
      if (!feof(file))
        printf("WARN: Read less than expected, title ID cache might be incomplete\n");
      break;
    }
    // Read ISO path
    result = fread(&pathBuf, header.pathLength, 1, file);
    if (result != 1) {
      printf("WARN: Read less than expected, title ID cache might be incomplete\n");
      break;
    }
    pathBuf[header.pathLength] = '\0';

    // Store entry in cache
    CacheEntry entry;
    memcpy(entry.titleID, header.titleID, sizeof(entry.titleID));
    header.titleID[11] = '\0';
    entry.fullPath = strdup(pathBuf);
    cache->entries[readIndex] = entry;
    readIndex++;
  }
  fclose(file);

  // Free unused memory
  if (readIndex != meta.total) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Adjusted cache size, read entries: %d", readIndex);
    cache->entries = realloc(cache->entries, sizeof(CacheEntry) * readIndex);

  cache->total = readIndex;
  return 0;
}
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting loadTitleIDCache%s", "");

// Returns a pointer to title ID or NULL if fullPath is not found in the cache
char *getCachedTitleID(char *fullPath, TitleIDCache *cache) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering getCachedTitleID%s", "");
  // This code takes advantage of all entries in the title list being sorted alphabetically.
  // By starting from the index of the last matched entry, we can skip comparing fullPath with entries
  // that have already been matched to a title ID, improving lookup speeds for very large lists.
  int mountpointLen = getRelativePathIdx(fullPath);
  if (mountpointLen == -1) {
    uiSplashLogString(LEVEL_WARN, "[DEBUG] Failed to get relative mountpoint for: %s", fullPath);
    printf("WARN: Failed to get device mountpoint for %s\n", fullPath);
    return NULL;
  }

  for (int i = cache->lastMatchedIdx; i < cache->total; i++) {
    if (!strcmp(cache->entries[i].fullPath, fullPath + mountpointLen)) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Cache HIT for %s", fullPath);
      cache->lastMatchedIdx = i;
      return cache->entries[i].titleID;
    }
  }
  return NULL;
}
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting getCachedTitleID%s", "");

// Frees memory used by title ID cache
// All pointers to cache entries (including title IDs) will be invalid
void freeTitleCache(TitleIDCache *cache) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Entering freeTitleCache%s", "");
  if (cache == NULL) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] No cache to free in freeTitleCache.%s", "");
    return;

  for (int i = 0; i < cache->total; i++) {
    free(cache->entries[i].fullPath);
  }

  free(cache->entries);
  free(cache);
}
    uiSplashLogString(LEVEL_INFO_NODELAY, "[DEBUG] Exiting freeTitleCache%s", "");
