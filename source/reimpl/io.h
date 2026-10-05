/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

/**
 * @file  io.h
 * @brief Wrappers and implementations for some of the IO functions.
 */

#ifndef SOLOADER_IO_H
#define SOLOADER_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <sys/dirent.h>
#include <sys/syslimits.h>
#include <sys/fcntl.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#ifndef DT_DIR
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#endif

/*
 * bionic's 32-bit ARM struct stat (== struct stat64), naturally aligned, 0x68
 * bytes. Fixed-width types only: newlib's nlink_t/uid_t/gid_t are 16-bit and
 * its timespec has a 64-bit time_t, and the old packed copy of this struct put
 * st_size at 0x26. PFile::Size() reads it at 0x30 (ldr r0, [sp, #0x30] after
 * fstat), so every save file looked empty: CStreamReader hit EndOfStream before
 * the header and LoadProfile failed with "corrupt or wrong version".
 */
typedef struct stat64_bionic {
    uint64_t st_dev;            // 0x00
    uint8_t  __pad0[4];         // 0x08
    uint32_t __st_ino;          // 0x0c
    uint32_t st_mode;           // 0x10
    uint32_t st_nlink;          // 0x14
    uint32_t st_uid;            // 0x18
    uint32_t st_gid;            // 0x1c
    uint64_t st_rdev;           // 0x20
    uint8_t  __pad3[4];         // 0x28
    int64_t  st_size;           // 0x30
    uint32_t st_blksize;        // 0x38
    uint64_t st_blocks;         // 0x40
    struct { int32_t tv_sec; int32_t tv_nsec; } st_atim, st_mtim, st_ctim; // 0x48
    uint64_t st_ino;            // 0x60
} stat64_bionic;

#ifndef __cplusplus
_Static_assert(__builtin_offsetof(stat64_bionic, st_size) == 0x30, "bionic stat layout");
_Static_assert(sizeof(stat64_bionic) == 0x68, "bionic stat layout");
#endif

typedef struct __attribute__((__packed__)) dirent64_bionic {
    int16_t d_ino; // 2 bytes // offset 0x0
    int64_t d_off; // 8 bytes // offset 0x2
    uint64_t d_reclen; // 8 bytes // 0xA
    unsigned char d_type; // 1 byte // offset 0x12
    char d_name[256]; // 256 bytes // offset 0x13
} dirent64_bionic;

/** Forget cached "file does not exist" results (see open_soloader). */
void io_neg_cache_clear(void);

int remove_soloader(const char *path);
int rename_soloader(const char *old_path, const char *new_path);
int mkdir_soloader(const char *path, mode_t mode);
int rmdir_soloader(const char *path);

int open_soloader(const char * path, int oflag, ...);

FILE * fopen_soloader(const char * filename, const char * mode);

DIR *opendir_soloader(char *name);

int stat_soloader(const char * path, stat64_bionic * buf);

int fstat_soloader(int fd, stat64_bionic * buf);

struct dirent64_bionic * readdir_soloader(DIR *dir);

int readdir_r_soloader(DIR * dirp, dirent64_bionic * entry,
                       dirent64_bionic ** result);

int close_soloader(int fd);

int fclose_soloader(FILE *f);

int closedir_soloader(DIR *dir);

int fcntl_soloader(int fd, int cmd, ...);

int ioctl_soloader(int fd, int request, ... /* arg */);

int fsync_soloader(int fd);

#ifdef __cplusplus
};
#endif

#endif // SOLOADER_IO_H
