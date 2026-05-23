/*
 * ramfs — simple in-memory filesystem.
 * Each directory holds a linked list of child nodes.
 * Each file holds a heap-allocated byte buffer grown on write.
 * No hard limits except heap size.
 */

#include "ramfs.h"
#include "../mm/heap.h"
#include "../lib/string.h"
#include "../arch/x86_64/serial.h"

#define RAMFS_BLOCK     4096
#define RAMFS_MAX_CHILDREN 256

typedef struct ramfs_node {
    vfs_node_t          vnode;      /* must be first — aliased */
    uint8_t            *data;       /* file content buffer */
    size_t              data_cap;   /* allocated capacity */
    struct ramfs_node  *children[RAMFS_MAX_CHILDREN];
    uint32_t            nchildren;
    uint64_t            ino;
} ramfs_node_t;

static uint64_t next_ino = 1;

static vfs_ops_t ramfs_file_ops;
static vfs_ops_t ramfs_dir_ops;

static ramfs_node_t *ramfs_alloc(const char *name, uint32_t type)
{
    ramfs_node_t *n = (ramfs_node_t *)kzalloc(sizeof(ramfs_node_t));
    if (!n) return NULL;
    strncpy(n->vnode.name, name, VFS_NAME_MAX);
    n->vnode.flags    = type;
    n->vnode.inode    = next_ino++;
    n->ino            = n->vnode.inode;
    n->vnode.ops      = (type & VFS_TYPE_DIR) ? &ramfs_dir_ops : &ramfs_file_ops;
    n->vnode.private  = n;
    return n;
}

/* ---- File ops ---- */

static ssize_t ramfs_read(vfs_node_t *node, void *buf, size_t len, uint64_t off)
{
    ramfs_node_t *n = (ramfs_node_t *)node->private;
    if (off >= node->size) return 0;
    if (off + len > node->size) len = (size_t)(node->size - off);
    memcpy(buf, n->data + off, len);
    return (ssize_t)len;
}

static ssize_t ramfs_write(vfs_node_t *node, const void *buf, size_t len, uint64_t off)
{
    ramfs_node_t *n = (ramfs_node_t *)node->private;
    uint64_t end = off + len;

    /* Grow buffer if needed */
    if (end > n->data_cap) {
        size_t new_cap = ALIGN_UP(end, RAMFS_BLOCK);
        uint8_t *new_data = (uint8_t *)krealloc(n->data, new_cap);
        if (!new_data) return -12;
        /* Zero new region */
        if (new_cap > n->data_cap)
            memset(new_data + n->data_cap, 0, new_cap - n->data_cap);
        n->data     = new_data;
        n->data_cap = new_cap;
    }

    memcpy(n->data + off, buf, len);
    if (end > node->size) node->size = end;
    return (ssize_t)len;
}

static int ramfs_stat_file(vfs_node_t *node, vfs_stat_t *st)
{
    ramfs_node_t *n = (ramfs_node_t *)node->private;
    st->st_ino     = n->ino;
    st->st_mode    = node->flags | node->mode;
    st->st_nlink   = 1;
    st->st_size    = node->size;
    st->st_blksize = RAMFS_BLOCK;
    st->st_blocks  = ALIGN_UP(node->size, 512) / 512;
    return 0;
}

static int ramfs_truncate(vfs_node_t *node, uint64_t size)
{
    ramfs_node_t *n = (ramfs_node_t *)node->private;
    if (size < node->size && n->data)
        memset(n->data + size, 0, (size_t)(node->size - size));
    node->size = size;
    return 0;
}

/* ---- Dir ops ---- */

static vfs_node_t *ramfs_lookup(vfs_node_t *dir, const char *name)
{
    ramfs_node_t *d = (ramfs_node_t *)dir->private;
    for (uint32_t i = 0; i < d->nchildren; i++) {
        if (strcmp(d->children[i]->vnode.name, name) == 0)
            return &d->children[i]->vnode;
    }
    return NULL;
}

static int ramfs_readdir(vfs_node_t *dir, uint32_t idx, char *name_out)
{
    ramfs_node_t *d = (ramfs_node_t *)dir->private;
    if (idx >= d->nchildren) return -1;
    strncpy(name_out, d->children[idx]->vnode.name, VFS_NAME_MAX);
    return 0;
}

static int ramfs_mkdir(vfs_node_t *dir, const char *name, uint32_t mode)
{
    ramfs_node_t *d = (ramfs_node_t *)dir->private;
    if (d->nchildren >= RAMFS_MAX_CHILDREN) return -28; /* ENOSPC */
    ramfs_node_t *child = ramfs_alloc(name, VFS_TYPE_DIR);
    if (!child) return -12;
    child->vnode.mode   = mode;
    child->vnode.parent = dir;
    d->children[d->nchildren++] = child;
    return 0;
}

static int ramfs_create(vfs_node_t *dir, const char *name, uint32_t mode)
{
    ramfs_node_t *d = (ramfs_node_t *)dir->private;
    if (d->nchildren >= RAMFS_MAX_CHILDREN) return -28;
    ramfs_node_t *child = ramfs_alloc(name, VFS_TYPE_FILE);
    if (!child) return -12;
    child->vnode.mode   = mode;
    child->vnode.parent = dir;
    d->children[d->nchildren++] = child;
    return 0;
}

static int ramfs_unlink(vfs_node_t *dir, const char *name)
{
    ramfs_node_t *d = (ramfs_node_t *)dir->private;
    for (uint32_t i = 0; i < d->nchildren; i++) {
        if (strcmp(d->children[i]->vnode.name, name) == 0) {
            /* Shift remaining */
            for (uint32_t j = i; j < d->nchildren - 1; j++)
                d->children[j] = d->children[j+1];
            d->nchildren--;
            return 0;
        }
    }
    return -2; /* ENOENT */
}

static int ramfs_stat_dir(vfs_node_t *node, vfs_stat_t *st)
{
    ramfs_node_t *n = (ramfs_node_t *)node->private;
    st->st_ino   = n->ino;
    st->st_mode  = node->flags | node->mode;
    st->st_nlink = (uint32_t)(n->nchildren + 2);
    st->st_size  = 0;
    return 0;
}

static vfs_ops_t ramfs_file_ops = {
    .read    = ramfs_read,
    .write   = ramfs_write,
    .stat    = ramfs_stat_file,
    .truncate= ramfs_truncate,
};

static vfs_ops_t ramfs_dir_ops = {
    .lookup  = ramfs_lookup,
    .readdir = ramfs_readdir,
    .mkdir   = ramfs_mkdir,
    .create  = ramfs_create,
    .unlink  = ramfs_unlink,
    .stat    = ramfs_stat_dir,
};

vfs_node_t *ramfs_create_root(void)
{
    ramfs_node_t *root = ramfs_alloc("/", VFS_TYPE_DIR);
    if (!root) return NULL;
    root->vnode.mode = 0755;
    return &root->vnode;
}
