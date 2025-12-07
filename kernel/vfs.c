/*
 * VibeOS Virtual File System
 *
 * Simple in-memory filesystem with hierarchical directories
 */

#include "vfs.h"
#include "string.h"
#include "memory.h"
#include "printf.h"

// Inode pool
static vfs_node_t inodes[VFS_MAX_INODES];
static int inode_count = 0;

// Root directory and current working directory
static vfs_node_t *root = NULL;
static vfs_node_t *cwd = NULL;

// Allocate a new inode
static vfs_node_t *alloc_inode(void) {
    if (inode_count >= VFS_MAX_INODES) {
        return NULL;
    }
    // No need to memset - we cleared all inodes at init
    vfs_node_t *node = &inodes[inode_count++];
    return node;
}

// Create a directory node
static vfs_node_t *create_dir(const char *name, vfs_node_t *parent) {
    vfs_node_t *dir = alloc_inode();
    if (!dir) return NULL;

    // Copy name manually
    int i;
    for (i = 0; name[i] && i < VFS_MAX_NAME - 1; i++) {
        dir->name[i] = name[i];
    }
    dir->name[i] = '\0';

    dir->type = VFS_DIRECTORY;
    dir->parent = parent;
    dir->child_count = 0;

    if (parent) {
        if (parent->child_count >= VFS_MAX_CHILDREN) {
            return NULL;
        }
        parent->children[parent->child_count++] = dir;
    }

    return dir;
}

// Create a file node
static vfs_node_t *create_file(const char *name, vfs_node_t *parent) {
    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }

    vfs_node_t *file = alloc_inode();
    if (!file) return NULL;

    // Copy name manually (strncpy has issues)
    int i;
    for (i = 0; name[i] && i < VFS_MAX_NAME - 1; i++) {
        file->name[i] = name[i];
    }
    file->name[i] = '\0';

    file->type = VFS_FILE;
    file->parent = parent;
    file->data = NULL;
    file->size = 0;
    file->capacity = 0;

    if (parent->child_count >= VFS_MAX_CHILDREN) {
        return NULL;
    }
    parent->children[parent->child_count++] = file;

    return file;
}

// Find child by name in a directory
static vfs_node_t *find_child(vfs_node_t *dir, const char *name) {
    if (!dir || dir->type != VFS_DIRECTORY) {
        return NULL;
    }

    for (int i = 0; i < dir->child_count; i++) {
        if (strcmp(dir->children[i]->name, name) == 0) {
            return dir->children[i];
        }
    }
    return NULL;
}

// Initialize the filesystem
void vfs_init(void) {
    printf("[VFS] Initializing filesystem...\n");

    inode_count = 0;
    // Don't memset - static arrays are already zero

    // Create root directory
    root = alloc_inode();
    root->name[0] = '/';
    root->name[1] = '\0';
    root->type = VFS_DIRECTORY;
    root->parent = root;  // Root's parent is itself
    root->child_count = 0;

    // Create default directories
    create_dir("bin", root);
    create_dir("tmp", root);
    vfs_node_t *home = create_dir("home", root);
    vfs_node_t *user = create_dir("user", home);

    // Set cwd to /home/user
    cwd = user;

    printf("[VFS] Filesystem ready!\n");
}

// Resolve a path to a node
// Handles absolute paths (starting with /) and relative paths
// Handles . and ..
vfs_node_t *vfs_lookup(const char *path) {
    if (!path || !path[0]) {
        return cwd;
    }

    vfs_node_t *current;
    char pathbuf[VFS_MAX_PATH];
    char *token;
    char *rest;

    // Start from root or cwd
    if (path[0] == '/') {
        current = root;
        path++;  // Skip leading /
    } else {
        current = cwd;
    }

    // Empty path after / means root
    if (!path[0]) {
        return current;
    }

    // Copy path for tokenization
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    // Tokenize by /
    rest = pathbuf;
    while ((token = strtok_r(rest, "/", &rest)) != NULL) {
        if (token[0] == '\0') {
            continue;  // Skip empty tokens (double slashes)
        }

        if (strcmp(token, ".") == 0) {
            // Current directory, do nothing
            continue;
        }

        if (strcmp(token, "..") == 0) {
            // Parent directory
            if (current->parent) {
                current = current->parent;
            }
            continue;
        }

        // Look for child
        vfs_node_t *child = find_child(current, token);
        if (!child) {
            return NULL;  // Path not found
        }
        current = child;
    }

    return current;
}

vfs_node_t *vfs_get_root(void) {
    return root;
}

vfs_node_t *vfs_get_cwd(void) {
    return cwd;
}

int vfs_set_cwd(const char *path) {
    vfs_node_t *node = vfs_lookup(path);
    if (!node) {
        return -1;  // Path not found
    }
    if (node->type != VFS_DIRECTORY) {
        return -2;  // Not a directory
    }
    cwd = node;
    return 0;
}

int vfs_get_cwd_path(char *buf, size_t size) {
    if (!buf || size == 0) return -1;

    // Build path by walking up to root
    char *parts[32];
    int depth = 0;

    vfs_node_t *node = cwd;
    while (node != root && depth < 32) {
        parts[depth++] = node->name;
        node = node->parent;
    }

    // Build path string
    buf[0] = '\0';
    size_t pos = 0;

    if (depth == 0) {
        // We're at root
        if (pos < size - 1) buf[pos++] = '/';
        buf[pos] = '\0';
        return 0;
    }

    // Write parts in reverse order
    for (int i = depth - 1; i >= 0; i--) {
        if (pos < size - 1) buf[pos++] = '/';
        size_t len = strlen(parts[i]);
        if (pos + len < size) {
            strcpy(buf + pos, parts[i]);
            pos += len;
        }
    }
    buf[pos] = '\0';

    return 0;
}

// Create a directory at the given path
vfs_node_t *vfs_mkdir(const char *path) {
    if (!path || !path[0]) return NULL;

    // Find the parent directory and the new directory name
    char pathbuf[VFS_MAX_PATH];
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    // Find last /
    char *last_slash = NULL;
    for (char *p = pathbuf; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent;
    char *dirname;

    if (last_slash == NULL) {
        // No slash, create in cwd
        parent = cwd;
        dirname = pathbuf;
    } else if (last_slash == pathbuf) {
        // Starts with /, create in root
        parent = root;
        dirname = last_slash + 1;
    } else {
        // Split path
        *last_slash = '\0';
        parent = vfs_lookup(pathbuf);
        dirname = last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }

    // Check if already exists
    if (find_child(parent, dirname)) {
        return NULL;  // Already exists
    }

    return create_dir(dirname, parent);
}

// Read directory entries
int vfs_readdir(vfs_node_t *dir, int index, char *name, size_t name_size, uint8_t *type) {
    if (!dir || dir->type != VFS_DIRECTORY) {
        return -1;
    }

    if (index < 0 || index >= dir->child_count) {
        return -1;  // No more entries
    }

    vfs_node_t *child = dir->children[index];
    strncpy(name, child->name, name_size - 1);
    name[name_size - 1] = '\0';
    if (type) *type = child->type;

    return 0;
}

// Create a file at the given path
vfs_node_t *vfs_create(const char *path) {
    if (!path || !path[0]) return NULL;

    char pathbuf[VFS_MAX_PATH];
    strncpy(pathbuf, path, VFS_MAX_PATH - 1);
    pathbuf[VFS_MAX_PATH - 1] = '\0';

    // Find last /
    char *last_slash = NULL;
    for (char *p = pathbuf; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    vfs_node_t *parent;
    char *filename;

    if (last_slash == NULL) {
        parent = cwd;
        filename = pathbuf;
    } else if (last_slash == pathbuf) {
        parent = root;
        filename = last_slash + 1;
    } else {
        *last_slash = '\0';
        parent = vfs_lookup(pathbuf);
        filename = last_slash + 1;
    }

    if (!parent || parent->type != VFS_DIRECTORY) {
        return NULL;
    }

    // Check if already exists
    vfs_node_t *existing = find_child(parent, filename);
    if (existing) {
        return existing;  // Return existing file
    }

    return create_file(filename, parent);
}

// Read from a file
int vfs_read(vfs_node_t *file, char *buf, size_t size, size_t offset) {
    if (!file || file->type != VFS_FILE || !buf) {
        return -1;
    }

    if (offset >= file->size) {
        return 0;  // EOF
    }

    size_t to_read = file->size - offset;
    if (to_read > size) to_read = size;

    memcpy(buf, file->data + offset, to_read);
    return (int)to_read;
}

// Write to a file (overwrites)
int vfs_write(vfs_node_t *file, const char *buf, size_t size) {
    if (!file || file->type != VFS_FILE) {
        return -1;
    }

    // Allocate/reallocate buffer if needed
    if (size > file->capacity) {
        size_t new_cap = size + 64;  // Some extra space
        char *new_data = malloc(new_cap);
        if (!new_data) return -1;

        if (file->data) {
            free(file->data);
        }
        file->data = new_data;
        file->capacity = new_cap;
    }

    memcpy(file->data, buf, size);
    file->size = size;
    return (int)size;
}

// Append to a file
int vfs_append(vfs_node_t *file, const char *buf, size_t size) {
    if (!file || file->type != VFS_FILE) {
        return -1;
    }

    size_t new_size = file->size + size;

    // Reallocate if needed
    if (new_size > file->capacity) {
        size_t new_cap = new_size + 64;
        char *new_data = malloc(new_cap);
        if (!new_data) return -1;

        if (file->data) {
            memcpy(new_data, file->data, file->size);
            free(file->data);
        }
        file->data = new_data;
        file->capacity = new_cap;
    }

    memcpy(file->data + file->size, buf, size);
    file->size = new_size;
    return (int)size;
}

int vfs_is_dir(vfs_node_t *node) {
    return node && node->type == VFS_DIRECTORY;
}

int vfs_is_file(vfs_node_t *node) {
    return node && node->type == VFS_FILE;
}
