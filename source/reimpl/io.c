/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"

#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

FILE * fopen_soloader(const char * filename, const char * mode) {
    if (strcmp(filename, "/proc/cpuinfo") == 0) {
        return fopen_soloader("app0:/cpuinfo", mode);
    } else if (strcmp(filename, "/proc/meminfo") == 0) {
        return fopen_soloader("app0:/meminfo", mode);
    }

#ifdef USE_SCELIBC_IO
    FILE* ret = sceLibcBridge_fopen(filename, mode);
#else
    FILE* ret = fopen(filename, mode);
#endif

    if (ret)
        l_debug("fopen(%s, %s): %p", filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", filename, mode, ret);

    return ret;
}

/*
 * Port-specific (Raging Thunder 2): PFile::Open() in the engine tries, for
 * every file it reads,
 *   1. open(FUSEAPP_SAVEPATH + name)                 -> saves/<name>
 *   2. open("/sdcard" + FUSEAPP_SAVEPATH + name)     -> always bogus here
 *   3. the APK zip ("Assets/" + name), then Data.vfs, then the zip again.
 * FUSEAPP_SAVEPATH is DATA_PATH "saves/" (main.c), and the game data is
 * installed loose in DATA_PATH "assets/" (Data.vfs, moregames/), so a
 * read-only open that misses saves/ is retried in assets/.
 *
 * Failed read-only opens are remembered in a small negative cache: the engine
 * probes the same missing paths over and over (every asset goes through steps
 * 1-2 before reaching Data.vfs), and each miss is a memory-card access. Any
 * open that can create/modify a file, and rename/remove/mkdir, clear it.
 */
#define GAME_SAVE_DIR   DATA_PATH "saves/"
#define GAME_ASSETS_DIR DATA_PATH "assets/"

#define NEG_CACHE_SIZE 512 // power of two
static uint32_t neg_cache[NEG_CACHE_SIZE]; // FNV-1a hashes, 0 = empty
static int neg_cache_count = 0;

static uint32_t path_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (uint8_t) *s++; h *= 16777619u; }
    return h ? h : 1;
}

static int neg_cache_has(const char *path) {
    uint32_t h = path_hash(path);
    for (uint32_t i = 0; i < NEG_CACHE_SIZE; i++) {
        uint32_t v = neg_cache[(h + i) & (NEG_CACHE_SIZE - 1)];
        if (v == 0) return 0;
        if (v == h) return 1;
    }
    return 0;
}

static void neg_cache_add(const char *path) {
    if (neg_cache_count >= NEG_CACHE_SIZE / 2) // keep probing short
        return;
    uint32_t h = path_hash(path);
    for (uint32_t i = 0; i < NEG_CACHE_SIZE; i++) {
        uint32_t *slot = &neg_cache[(h + i) & (NEG_CACHE_SIZE - 1)];
        if (*slot == h) return;
        if (*slot == 0) { *slot = h; neg_cache_count++; return; }
    }
}

void io_neg_cache_clear(void) {
    if (neg_cache_count) {
        memset(neg_cache, 0, sizeof(neg_cache));
        neg_cache_count = 0;
    }
}

static int open_cached(const char *path, int newlib_flags, mode_t mode) {
    int readonly = (newlib_flags & O_ACCMODE) == O_RDONLY &&
                   !(newlib_flags & (O_CREAT | O_TRUNC | O_APPEND));
    if (!readonly) {
        io_neg_cache_clear();
        return open(path, newlib_flags, mode);
    }
    if (neg_cache_has(path))
        return -1;
    int ret = open(path, newlib_flags, mode);
    if (ret < 0)
        neg_cache_add(path);
    return ret;
}

int open_soloader(const char * path, int oflag, ...) {
    if (strcmp(path, "/proc/cpuinfo") == 0) {
        return open_soloader("app0:/cpuinfo", oflag);
    } else if (strcmp(path, "/proc/meminfo") == 0) {
        return open_soloader("app0:/meminfo", oflag);
    }

    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE)) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open_cached(path, oflag, mode);

    if (ret < 0 && (oflag & O_ACCMODE) == O_RDONLY && !(oflag & O_CREAT) &&
        strncmp(path, GAME_SAVE_DIR, sizeof(GAME_SAVE_DIR) - 1) == 0) {
        char alt[PATH_MAX];
        snprintf(alt, sizeof(alt), GAME_ASSETS_DIR "%s", path + sizeof(GAME_SAVE_DIR) - 1);
        ret = open_cached(alt, oflag, mode);
        if (ret >= 0) {
            l_debug("open(%s): served from %s: %i", path, alt, ret);
            return ret;
        }
    }

    l_debug("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int remove_soloader(const char *path) {
    io_neg_cache_clear();
    return remove(path);
}

int rename_soloader(const char *old_path, const char *new_path) {
    io_neg_cache_clear();
    return rename(old_path, new_path);
}

int mkdir_soloader(const char *path, mode_t mode) {
    io_neg_cache_clear();
    return mkdir(path, mode);
}

int rmdir_soloader(const char *path) {
    io_neg_cache_clear();
    return rmdir(path);
}

int fstat_soloader(int fd, stat64_bionic * buf) {
    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char * path, stat64_bionic * buf) {
    struct stat st;
    int res = stat(path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("stat(%s): %i", path, res);
    return res;
}

int fclose_soloader(FILE * f) {
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd) {
    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

DIR* opendir_soloader(char* _pathname) {
    DIR* ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic * readdir_soloader(DIR * dir) {
    static struct dirent64_bionic dirent_tmp;

    struct dirent* ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result) {
    struct dirent dirent_tmp;
    struct dirent * pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0) {
        dirent64_bionic* entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR * dir) {
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...) {
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...) {
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd) {
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}
