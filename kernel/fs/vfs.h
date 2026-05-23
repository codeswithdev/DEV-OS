#pragma once
#include "../include/types.h"

#define VFS_NAME_MAX    255
#define VFS_PATH_MAX    4096
#define VFS_FD_MAX      1024

/* File type bits */
#define VFS_TYPE_FILE   0x8000
#define VFS_TYPE_DIR    0x4000
#define VFS_TYPE_PIPE   0x1000
#define VFS_TYPE_CHR    0x2000

/* Open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0040
#define O_TRUNC     0x0200
#define O_APPEND    0x0400
#define O_DIRECTORY 0x10000

/* Seek */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

struct vfs_node;
struct vfs_ops;

typedef struct vfs_stat {
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint64_t st_size;
    uint64_t st_blksize;
    uint64_t st_blocks;
} vfs_stat_t;

typedef struct vfs_ops {
    int      (*open)  (struct vfs_node *node, int flags);
    void     (*close) (struct vfs_node *node);
    ssize_t  (*read)  (struct vfs_node *node, void *buf, size_t len, uint64_t off);
    ssize_t  (*write) (struct vfs_node *node, const void *buf, size_t len, uint64_t off);
    int      (*stat)  (struct vfs_node *node, vfs_stat_t *st);
    struct vfs_node *(*lookup)(struct vfs_node *dir, const char *name);
    int      (*readdir)(struct vfs_node *dir, uint32_t idx, char *name_out);
    int      (*mkdir) (struct vfs_node *dir, const char *name, uint32_t mode);
    int      (*create)(struct vfs_node *dir, const char *name, uint32_t mode);
    int      (*unlink)(struct vfs_node *dir, const char *name);
    int      (*truncate)(struct vfs_node *node, uint64_t size);
} vfs_ops_t;

typedef struct vfs_node {
    char        name[VFS_NAME_MAX + 1];
    uint32_t    flags;      /* VFS_TYPE_* */
    uint32_t    mode;
    uint64_t    inode;
    uint64_t    size;
    uint32_t    refcount;
    vfs_ops_t  *ops;
    void       *private;    /* filesystem-specific data */
    struct vfs_node *parent;
} vfs_node_t;

typedef struct {
    vfs_node_t *node;
    uint64_t    offset;
    int         flags;
    uint32_t    refcount;
} file_t;

/* VFS public API */
void        vfs_init(void);
int         vfs_mount(const char *path, vfs_node_t *root);
vfs_node_t *vfs_lookup(const char *path);
int         vfs_open(const char *path, int flags, uint32_t mode);
int         vfs_close(int fd);
ssize_t     vfs_read(int fd, void *buf, size_t len);
ssize_t     vfs_write(int fd, const void *buf, size_t len);
int64_t     vfs_lseek(int fd, int64_t off, int whence);
int         vfs_stat(const char *path, vfs_stat_t *st);
int         vfs_mkdir(const char *path, uint32_t mode);

/* Per-process fd allocation (uses current_task->fds) */
int  fd_alloc(file_t *f);
void fd_free(int fd);
file_t *fd_get(int fd);
