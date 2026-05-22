#include "vfs.h"
#include "ramfs.h"
#include "../mm/heap.h"
#include "../lib/string.h"
#include "../arch/x86_64/serial.h"
#include "../sched/sched.h"

#define MOUNT_MAX   16

typedef struct {
    char        path[VFS_PATH_MAX];
    vfs_node_t *root;
} mount_t;

static mount_t  mount_table[MOUNT_MAX];
static int      mount_count = 0;
static file_t  *global_fd_table[VFS_FD_MAX];  /* kernel-level file objects */

/* ---- Path utilities ---- */

/* Advance past leading slashes */
static const char *skip_slash(const char *p) {
    while (*p == '/') p++;
    return p;
}

/* Copy one path component (up to next '/' or '\0') */
static int path_component(const char **p, char *out, size_t outsz)
{
    const char *s = skip_slash(*p);
    if (!*s) return 0;
    size_t i = 0;
    while (*s && *s != '/' && i < outsz - 1) out[i++] = *s++;
    out[i] = '\0';
    *p = s;
    return (int)i;
}

/* ---- Mount table ---- */

void vfs_init(void)
{
    memset(mount_table, 0, sizeof(mount_table));
    memset(global_fd_table, 0, sizeof(global_fd_table));
    mount_count = 0;

    /* Create and mount ramfs at / */
    vfs_node_t *rootfs = ramfs_create_root();
    vfs_mount("/", rootfs);

    /* Create standard directories */
    vfs_mkdir("/dev", 0755);
    vfs_mkdir("/proc", 0555);
    vfs_mkdir("/tmp", 0777);

    serial_printf("[VFS] init OK — ramfs mounted at /\n");
}

int vfs_mount(const char *path, vfs_node_t *root)
{
    if (mount_count >= MOUNT_MAX) return -1;
    strncpy(mount_table[mount_count].path, path, VFS_PATH_MAX - 1);
    mount_table[mount_count].root = root;
    mount_count++;
    return 0;
}

/* Find the deepest mount covering path, return its root and remaining path */
static vfs_node_t *find_mount(const char *path, const char **rel)
{
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < mount_count; i++) {
        size_t mlen = strlen(mount_table[i].path);
        if (strncmp(path, mount_table[i].path, mlen) == 0) {
            if (mlen > best_len) { best = i; best_len = mlen; }
        }
    }
    if (best < 0) return NULL;
    *rel = path + best_len;
    return mount_table[best].root;
}

/* ---- Path resolution ---- */

vfs_node_t *vfs_lookup(const char *path)
{
    if (!path || path[0] != '/') return NULL;

    const char *rel;
    vfs_node_t *node = find_mount(path, &rel);
    if (!node) return NULL;

    char comp[VFS_NAME_MAX + 1];
    const char *p = rel;
    while (path_component(&p, comp, sizeof(comp))) {
        if (!node->ops || !node->ops->lookup) return NULL;
        node = node->ops->lookup(node, comp);
        if (!node) return NULL;
    }
    return node;
}

/* ---- File descriptor management ---- */

/* Allocate a global file_t slot and return fd index */
static int UNUSED global_fd_alloc(file_t **out)
{
    /* fd 0,1,2 reserved for stdin/stdout/stderr */
    for (int i = 3; i < VFS_FD_MAX; i++) {
        if (!global_fd_table[i]) {
            file_t *f = (file_t *)kzalloc(sizeof(file_t));
            if (!f) return -1;
            global_fd_table[i] = f;
            *out = f;
            return i;
        }
    }
    return -1;
}

int fd_alloc(file_t *f)
{
    /* Store in task's fd table */
    if (!current_task) return -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (!current_task->fds.files[i]) {
            current_task->fds.files[i] = f;
            return i;
        }
    }
    return -1;
}

void fd_free(int fd)
{
    if (!current_task || fd < 0 || fd >= MAX_FDS) return;
    current_task->fds.files[fd] = NULL;
}

file_t *fd_get(int fd)
{
    if (!current_task || fd < 0 || fd >= MAX_FDS) return NULL;
    return (file_t *)current_task->fds.files[fd];
}

/* ---- VFS operations ---- */

int vfs_open(const char *path, int flags, uint32_t mode)
{
    (void)mode;
    vfs_node_t *node = vfs_lookup(path);

    if (!node) {
        if (!(flags & O_CREAT)) return -2; /* ENOENT */
        /* Create file in parent dir */
        const char *last_slash = NULL;
        for (const char *p = path; *p; p++)
            if (*p == '/') last_slash = p;
        if (!last_slash) return -22; /* EINVAL */

        char dir_path[VFS_PATH_MAX];
        size_t dlen = (size_t)(last_slash - path);
        memcpy(dir_path, path, dlen);
        dir_path[dlen] = '\0';
        if (dlen == 0) strcpy(dir_path, "/");

        vfs_node_t *dir = vfs_lookup(dir_path);
        if (!dir || !dir->ops || !dir->ops->create) return -2;
        if (dir->ops->create(dir, last_slash + 1, mode) != 0) return -13; /* EACCES */
        node = vfs_lookup(path);
        if (!node) return -2;
    }

    if (node->ops && node->ops->open) {
        int r = node->ops->open(node, flags);
        if (r != 0) return r;
    }

    file_t *f = (file_t *)kzalloc(sizeof(file_t));
    if (!f) return -12; /* ENOMEM */
    f->node     = node;
    f->offset   = (flags & O_APPEND) ? node->size : 0;
    f->flags    = flags;
    f->refcount = 1;
    node->refcount++;

    int fd = fd_alloc(f);
    if (fd < 0) { kfree(f); return -24; /* EMFILE */ }
    return fd;
}

int vfs_close(int fd)
{
    file_t *f = fd_get(fd);
    if (!f) return -9; /* EBADF */
    fd_free(fd);
    f->refcount--;
    if (f->refcount == 0) {
        if (f->node && f->node->ops && f->node->ops->close)
            f->node->ops->close(f->node);
        if (f->node) f->node->refcount--;
        kfree(f);
    }
    return 0;
}

ssize_t vfs_read(int fd, void *buf, size_t len)
{
    file_t *f = fd_get(fd);
    if (!f) return -9;
    if (!f->node || !f->node->ops || !f->node->ops->read) return -22;
    ssize_t n = f->node->ops->read(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (uint64_t)n;
    return n;
}

ssize_t vfs_write(int fd, const void *buf, size_t len)
{
    file_t *f = fd_get(fd);
    if (!f) return -9;
    if (!f->node || !f->node->ops || !f->node->ops->write) return -22;
    ssize_t n = f->node->ops->write(f->node, buf, len, f->offset);
    if (n > 0) f->offset += (uint64_t)n;
    return n;
}

int64_t vfs_lseek(int fd, int64_t off, int whence)
{
    file_t *f = fd_get(fd);
    if (!f) return -9;
    int64_t new_off;
    switch (whence) {
    case SEEK_SET: new_off = off; break;
    case SEEK_CUR: new_off = (int64_t)f->offset + off; break;
    case SEEK_END: new_off = (int64_t)f->node->size + off; break;
    default: return -22;
    }
    if (new_off < 0) return -22;
    f->offset = (uint64_t)new_off;
    return new_off;
}

int vfs_stat(const char *path, vfs_stat_t *st)
{
    vfs_node_t *node = vfs_lookup(path);
    if (!node) return -2;
    if (!node->ops || !node->ops->stat) return -38;
    return node->ops->stat(node, st);
}

int vfs_mkdir(const char *path, uint32_t mode)
{
    /* Find parent dir */
    const char *last = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '/') last = p;
    if (!last) return -22;

    char parent[VFS_PATH_MAX];
    size_t plen = (size_t)(last - path);
    if (plen == 0) strcpy(parent, "/");
    else { memcpy(parent, path, plen); parent[plen] = '\0'; }

    vfs_node_t *dir = vfs_lookup(parent);
    if (!dir) return -2;
    if (!dir->ops || !dir->ops->mkdir) return -38;
    return dir->ops->mkdir(dir, last + 1, mode);
}
