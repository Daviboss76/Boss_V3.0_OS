#ifndef FS_H
#define FS_H
#include <stdint.h>
#include <stdbool.h>

enum { FS_OK = 0, FS_EIO = -1, FS_ENOENT = -2, FS_EEXIST = -3, FS_ENOTDIR = -4,
       FS_EISDIR = -5, FS_EFULL = -6, FS_ENAME = -7, FS_ENOFS = -8, FS_ENOTEMPTY = -9 };

int  fs_mount(void);
const char *fs_strerror(int e);
const char *fs_cwd(void);
int  fs_chdir(const char *path);
int  fs_list(const char *path, void (*cb)(const char *nome, bool dir, uint32_t tam));
int  fs_stat(const char *path, bool *dir, uint32_t *tam);
int  fs_read(const char *path, void *buf, uint32_t max, uint32_t *lido);
int  fs_write(const char *path, const void *buf, uint32_t n);   /* cria ou sobrescreve */
int  fs_mkdir(const char *path);
int  fs_remove(const char *path);
int  fs_info(uint32_t *total_kb, uint32_t *livre_kb);
#endif
