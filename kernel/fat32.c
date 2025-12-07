/*
 * VibeOS FAT32 Filesystem Driver
 *
 * Read-only FAT32 implementation.
 * Supports long filenames (LFN) and standard 8.3 names.
 */

#include "fat32.h"
#include "virtio_blk.h"
#include "printf.h"
#include "string.h"
#include "memory.h"

// Boot sector (BIOS Parameter Block)
typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;    // 0 for FAT32
    uint16_t total_sectors_16;    // 0 for FAT32
    uint8_t  media_type;
    uint16_t fat_size_16;         // 0 for FAT32
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    // FAT32 extended
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot;
    uint8_t  reserved[12];
    uint8_t  drive_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
} fat32_boot_t;

// Filesystem state
static fat32_fs_t fs;
static int fs_initialized = 0;

// Sector buffer
static uint8_t sector_buf[512] __attribute__((aligned(16)));

// Cluster buffer (for reading directory entries)
static uint8_t *cluster_buf = NULL;
static uint32_t cluster_buf_size = 0;

// Read a sector from disk
static int read_sector(uint32_t sector, void *buf) {
    return virtio_blk_read(sector, 1, buf);
}

// Write a sector to disk
static int write_sector(uint32_t sector, const void *buf) {
    return virtio_blk_write(sector, 1, buf);
}

// Write multiple sectors
static int write_sectors(uint32_t sector, uint32_t count, const void *buf) {
    return virtio_blk_write(sector, count, buf);
}

// Read multiple sectors
static int read_sectors(uint32_t sector, uint32_t count, void *buf) {
    return virtio_blk_read(sector, count, buf);
}

// Get the first sector of a cluster
static uint32_t cluster_to_sector(uint32_t cluster) {
    return fs.data_start + (cluster - 2) * fs.sectors_per_cluster;
}

// Read the FAT entry for a cluster (returns next cluster or EOC marker)
static uint32_t fat_next_cluster(uint32_t cluster) {
    // Calculate which sector of the FAT contains this entry
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs.reserved_sectors + (fat_offset / fs.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs.bytes_per_sector;

    if (read_sector(fat_sector, sector_buf) < 0) {
        return FAT32_EOC;
    }

    uint32_t next = *(uint32_t *)(sector_buf + entry_offset);
    return next & 0x0FFFFFFF;  // FAT32 uses only 28 bits
}

// Write a FAT entry (updates both FAT copies)
static int fat_set_cluster(uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs.reserved_sectors + (fat_offset / fs.bytes_per_sector);
    uint32_t entry_offset = fat_offset % fs.bytes_per_sector;

    // Read current sector
    if (read_sector(fat_sector, sector_buf) < 0) {
        return -1;
    }

    // Modify entry (preserve high 4 bits)
    uint32_t *entry = (uint32_t *)(sector_buf + entry_offset);
    *entry = (*entry & 0xF0000000) | (value & 0x0FFFFFFF);

    // Write to FAT1
    if (write_sector(fat_sector, sector_buf) < 0) {
        return -1;
    }

    // Write to FAT2 (if exists)
    if (fs.num_fats > 1) {
        uint32_t fat2_sector = fat_sector + fs.fat_size;
        if (write_sector(fat2_sector, sector_buf) < 0) {
            return -1;
        }
    }

    return 0;
}

// Find a free cluster and mark it as end-of-chain
static uint32_t fat_alloc_cluster(void) {
    // Start searching from cluster 2 (first data cluster)
    for (uint32_t cluster = 2; cluster < fs.total_clusters + 2; cluster++) {
        uint32_t entry = fat_next_cluster(cluster);
        if (entry == FAT32_FREE) {
            // Mark as end of chain
            if (fat_set_cluster(cluster, FAT32_EOC) < 0) {
                return 0;
            }
            return cluster;
        }
    }
    return 0;  // No free clusters
}

// Free a cluster chain starting at given cluster
static int fat_free_chain(uint32_t cluster) {
    while (cluster >= 2 && cluster < FAT32_EOC) {
        uint32_t next = fat_next_cluster(cluster);
        if (fat_set_cluster(cluster, FAT32_FREE) < 0) {
            return -1;
        }
        cluster = next;
    }
    return 0;
}

// Write a cluster to disk
static int write_cluster(uint32_t cluster, const void *buf) {
    uint32_t sector = cluster_to_sector(cluster);
    return write_sectors(sector, fs.sectors_per_cluster, buf);
}

// Zero out a cluster
static int zero_cluster(uint32_t cluster) {
    memset(cluster_buf, 0, cluster_buf_size);
    return write_cluster(cluster, cluster_buf);
}

// Read a cluster into buffer
static int read_cluster(uint32_t cluster, void *buf) {
    uint32_t sector = cluster_to_sector(cluster);
    return read_sectors(sector, fs.sectors_per_cluster, buf);
}

// Convert 8.3 name to normal string
static void fat_name_to_str(const char *fat_name, char *out) {
    int i, j = 0;

    // Copy name part (first 8 chars, trim trailing spaces)
    for (i = 0; i < 8 && fat_name[i] != ' '; i++) {
        out[j++] = fat_name[i];
    }

    // Add dot and extension if present
    if (fat_name[8] != ' ') {
        out[j++] = '.';
        for (i = 8; i < 11 && fat_name[i] != ' '; i++) {
            out[j++] = fat_name[i];
        }
    }

    out[j] = '\0';

    // Convert to lowercase for nicer display
    for (i = 0; out[i]; i++) {
        if (out[i] >= 'A' && out[i] <= 'Z') {
            out[i] += 32;
        }
    }
}

// Compare filename (case-insensitive)
static int name_match(const char *name1, const char *name2) {
    while (*name1 && *name2) {
        char c1 = *name1++;
        char c2 = *name2++;
        // Convert to uppercase for comparison
        if (c1 >= 'a' && c1 <= 'z') c1 -= 32;
        if (c2 >= 'a' && c2 <= 'z') c2 -= 32;
        if (c1 != c2) return 0;
    }
    return *name1 == *name2;
}

int fat32_init(void) {
    printf("[FAT32] Initializing...\n");

    // Read boot sector
    printf("[FAT32] Reading boot sector...\n");
    int ret = read_sector(0, sector_buf);
    printf("[FAT32] read_sector returned %d\n", ret);
    if (ret < 0) {
        printf("[FAT32] Failed to read boot sector\n");
        return -1;
    }

    printf("[FAT32] Sector read OK, parsing boot sector...\n");

    fat32_boot_t *boot = (fat32_boot_t *)sector_buf;

    printf("[FAT32] boot ptr = %p, sector_buf = %p\n", boot, sector_buf);
    printf("[FAT32] First bytes: %02x %02x %02x\n",
           sector_buf[0], sector_buf[1], sector_buf[2]);

    printf("[FAT32] Reading byte 11...\n");
    uint8_t b11 = sector_buf[11];
    printf("[FAT32] byte 11 = %02x\n", b11);
    uint8_t b12 = sector_buf[12];
    printf("[FAT32] byte 12 = %02x\n", b12);

    // Read fields manually to avoid unaligned access issues on ARM
    uint16_t bytes_per_sector = b11 | (b12 << 8);
    printf("[FAT32] bytes_per_sector = %d\n", bytes_per_sector);

    uint8_t sectors_per_cluster = sector_buf[13];
    printf("[FAT32] sectors_per_cluster = %d\n", sectors_per_cluster);

    uint16_t reserved_sectors = sector_buf[14] | (sector_buf[15] << 8);
    printf("[FAT32] reserved_sectors = %d\n", reserved_sectors);

    printf("[FAT32] reading num_fats...\n");
    uint8_t num_fats = sector_buf[16];
    printf("[FAT32] num_fats = %d\n", num_fats);

    printf("[FAT32] reading root_entry_count...\n");
    printf("[FAT32] sector_buf[17]...\n");
    uint8_t b17 = sector_buf[17];
    printf("[FAT32] b17 = %02x\n", b17);
    printf("[FAT32] sector_buf[18]...\n");
    uint8_t b18 = sector_buf[18];
    printf("[FAT32] b18 = %02x\n", b18);
    uint16_t root_entry_count = b17 | (b18 << 8);
    printf("[FAT32] root_entry_count = %d\n", root_entry_count);

    printf("[FAT32] reading fat_size_16...\n");
    uint16_t fat_size_16 = sector_buf[22] | (sector_buf[23] << 8);
    printf("[FAT32] fat_size_16 = %d\n", fat_size_16);

    uint32_t fat_size_32 = sector_buf[36] | (sector_buf[37] << 8) |
                           (sector_buf[38] << 16) | (sector_buf[39] << 24);
    uint32_t root_cluster = sector_buf[44] | (sector_buf[45] << 8) |
                            (sector_buf[46] << 16) | (sector_buf[47] << 24);
    uint32_t total_sectors_32 = sector_buf[32] | (sector_buf[33] << 8) |
                                (sector_buf[34] << 16) | (sector_buf[35] << 24);
    printf("[FAT32] fat_size_32=%d root_cluster=%d total_sectors=%d\n",
           fat_size_32, root_cluster, total_sectors_32);

    // Verify this is FAT32
    if (bytes_per_sector != 512) {
        printf("[FAT32] Unsupported sector size: %d\n", bytes_per_sector);
        return -1;
    }

    if (fat_size_16 != 0 || root_entry_count != 0) {
        printf("[FAT32] Not a FAT32 filesystem\n");
        return -1;
    }

    // Fill in filesystem info
    fs.bytes_per_sector = bytes_per_sector;
    fs.sectors_per_cluster = sectors_per_cluster;
    fs.reserved_sectors = reserved_sectors;
    fs.num_fats = num_fats;
    fs.fat_size = fat_size_32;
    fs.root_cluster = root_cluster;

    // Calculate data region start
    fs.data_start = fs.reserved_sectors + (fs.num_fats * fs.fat_size);

    // Calculate total data clusters
    uint32_t data_sectors = total_sectors_32 - fs.data_start;
    fs.total_clusters = data_sectors / fs.sectors_per_cluster;

    printf("[FAT32] Sectors/cluster: %d\n", fs.sectors_per_cluster);
    printf("[FAT32] Reserved sectors: %d\n", fs.reserved_sectors);
    printf("[FAT32] FAT size: %d sectors\n", fs.fat_size);
    printf("[FAT32] Root cluster: %d\n", fs.root_cluster);
    printf("[FAT32] Data start: sector %d\n", fs.data_start);
    printf("[FAT32] Total clusters: %d\n", fs.total_clusters);

    // Allocate cluster buffer
    cluster_buf_size = fs.sectors_per_cluster * fs.bytes_per_sector;
    cluster_buf = malloc(cluster_buf_size);
    if (!cluster_buf) {
        printf("[FAT32] Failed to allocate cluster buffer\n");
        return -1;
    }

    fs_initialized = 1;
    printf("[FAT32] Filesystem ready!\n");
    return 0;
}

// Helper to read uint16 from byte array (little-endian)
static uint16_t read16(uint8_t *p) {
    return p[0] | (p[1] << 8);
}

// Helper to read uint32 from byte array (little-endian)
static uint32_t read32(uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

// Find a directory entry by path component in a directory cluster chain
// Returns the directory entry or NULL if not found
static fat32_dirent_t *find_entry_in_dir(uint32_t dir_cluster, const char *name,
                                          uint32_t *out_cluster, uint32_t *out_offset) {
    static fat32_dirent_t found_entry;
    char entry_name[256];
    char lfn_name[256];
    int has_lfn = 0;

    uint32_t cluster = dir_cluster;

    while (cluster < FAT32_EOC) {
        if (read_cluster(cluster, cluster_buf) < 0) {
            return NULL;
        }

        int entries_per_cluster = cluster_buf_size / 32;  // Each entry is 32 bytes

        for (int i = 0; i < entries_per_cluster; i++) {
            uint8_t *e = cluster_buf + (i * 32);

            // Entry layout:
            // 0-10: name (11 bytes)
            // 11: attr
            // 20: cluster_hi (2 bytes)
            // 26: cluster_lo (2 bytes)
            // 28: size (4 bytes)

            uint8_t first_byte = e[0];
            uint8_t attr = e[11];

            // End of directory
            if (first_byte == 0x00) {
                return NULL;
            }

            // Deleted entry
            if (first_byte == 0xE5) {
                has_lfn = 0;
                continue;
            }

            // Long filename entry (attr == 0x0F)
            if (attr == FAT_ATTR_LFN) {
                uint8_t order = e[0];
                int seq = order & 0x1F;
                int is_last = order & 0x40;

                if (is_last) {
                    has_lfn = 1;
                    memset(lfn_name, 0, sizeof(lfn_name));
                }

                // Extract characters from LFN entry (stored as UTF-16LE, we just take low byte)
                int base = (seq - 1) * 13;
                // name1: bytes 1-10 (5 UTF-16 chars)
                for (int j = 0; j < 5; j++) {
                    uint16_t c = e[1 + j*2] | (e[2 + j*2] << 8);
                    if (c == 0 || c == 0xFFFF) break;
                    if (base + j < 255) lfn_name[base + j] = (char)c;
                }
                // name2: bytes 14-25 (6 UTF-16 chars)
                for (int j = 0; j < 6; j++) {
                    uint16_t c = e[14 + j*2] | (e[15 + j*2] << 8);
                    if (c == 0 || c == 0xFFFF) break;
                    if (base + 5 + j < 255) lfn_name[base + 5 + j] = (char)c;
                }
                // name3: bytes 28-31 (2 UTF-16 chars)
                for (int j = 0; j < 2; j++) {
                    uint16_t c = e[28 + j*2] | (e[29 + j*2] << 8);
                    if (c == 0 || c == 0xFFFF) break;
                    if (base + 11 + j < 255) lfn_name[base + 11 + j] = (char)c;
                }
                continue;
            }

            // Regular entry - skip volume label
            if (attr & FAT_ATTR_VOLUME_ID) {
                has_lfn = 0;
                continue;
            }

            // Get the name to compare
            if (has_lfn) {
                // Use long filename
                int k;
                for (k = 0; lfn_name[k]; k++) {
                    entry_name[k] = lfn_name[k];
                }
                entry_name[k] = '\0';
            } else {
                // Use 8.3 name - copy and convert
                fat_name_to_str((char *)e, entry_name);
            }

            // Compare names
            if (name_match(entry_name, name)) {
                // Build found_entry from bytes
                memcpy(found_entry.name, e, 11);
                found_entry.attr = attr;
                found_entry.cluster_hi = read16(e + 20);
                found_entry.cluster_lo = read16(e + 26);
                found_entry.size = read32(e + 28);
                if (out_cluster) *out_cluster = cluster;
                if (out_offset) *out_offset = i;
                return &found_entry;
            }

            has_lfn = 0;
        }

        // Follow cluster chain
        cluster = fat_next_cluster(cluster);
    }

    return NULL;
}

// Resolve a path to a directory entry
// Returns the entry or NULL if not found
static fat32_dirent_t *resolve_path(const char *path, uint32_t *out_cluster) {
    if (!fs_initialized || !path) return NULL;

    // Start at root
    uint32_t current_cluster = fs.root_cluster;

    // Skip leading slash
    if (*path == '/') path++;
    if (*path == '\0') {
        // Root directory requested - return a fake entry
        static fat32_dirent_t root_entry;
        memset(&root_entry, 0, sizeof(root_entry));
        root_entry.attr = FAT_ATTR_DIRECTORY;
        root_entry.cluster_hi = (fs.root_cluster >> 16) & 0xFFFF;
        root_entry.cluster_lo = fs.root_cluster & 0xFFFF;
        if (out_cluster) *out_cluster = fs.root_cluster;
        return &root_entry;
    }

    // Parse path components
    char pathbuf[256];
    int len = strlen(path);
    if (len >= 256) return NULL;
    memcpy(pathbuf, path, len + 1);

    char *rest = pathbuf;
    char *component;
    fat32_dirent_t *entry = NULL;

    while ((component = strtok_r(rest, "/", &rest)) != NULL) {
        if (component[0] == '\0') continue;

        entry = find_entry_in_dir(current_cluster, component, NULL, NULL);
        if (!entry) {
            return NULL;  // Not found
        }

        // Get cluster of this entry
        current_cluster = ((uint32_t)entry->cluster_hi << 16) | entry->cluster_lo;

        // If there's more path to go, this must be a directory
        if (*rest && !(entry->attr & FAT_ATTR_DIRECTORY)) {
            return NULL;  // Trying to traverse through a file
        }
    }

    if (out_cluster) *out_cluster = current_cluster;
    return entry;
}

int fat32_read_file(const char *path, void *buf, size_t size) {
    if (!fs_initialized) return -1;

    uint32_t cluster;
    fat32_dirent_t *entry = resolve_path(path, &cluster);

    if (!entry) {
        return -1;  // File not found
    }

    if (entry->attr & FAT_ATTR_DIRECTORY) {
        return -1;  // Can't read a directory as a file
    }

    uint32_t file_size = entry->size;
    if (size > file_size) size = file_size;

    // Read cluster chain
    uint8_t *dst = (uint8_t *)buf;
    size_t bytes_read = 0;

    while (cluster < FAT32_EOC && bytes_read < size) {
        if (read_cluster(cluster, cluster_buf) < 0) {
            return -1;
        }

        size_t to_copy = cluster_buf_size;
        if (bytes_read + to_copy > size) {
            to_copy = size - bytes_read;
        }

        memcpy(dst + bytes_read, cluster_buf, to_copy);
        bytes_read += to_copy;

        cluster = fat_next_cluster(cluster);
    }

    return (int)bytes_read;
}

int fat32_file_size(const char *path) {
    if (!fs_initialized) return -1;

    fat32_dirent_t *entry = resolve_path(path, NULL);
    if (!entry) return -1;
    if (entry->attr & FAT_ATTR_DIRECTORY) return -1;

    return (int)entry->size;
}

int fat32_is_dir(const char *path) {
    if (!fs_initialized) return -1;

    fat32_dirent_t *entry = resolve_path(path, NULL);
    if (!entry) return -1;

    return (entry->attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
}

int fat32_list_dir(const char *path, fat32_dir_callback callback, void *user_data) {
    if (!fs_initialized || !callback) return -1;

    uint32_t dir_cluster;
    fat32_dirent_t *entry = resolve_path(path, &dir_cluster);

    if (!entry) return -1;
    if (!(entry->attr & FAT_ATTR_DIRECTORY)) return -1;

    char entry_name[256];
    char lfn_name[256];
    int has_lfn = 0;

    uint32_t cluster = dir_cluster;

    while (cluster < FAT32_EOC) {
        if (read_cluster(cluster, cluster_buf) < 0) {
            return -1;
        }

        int entries_per_cluster = cluster_buf_size / 32;

        for (int i = 0; i < entries_per_cluster; i++) {
            uint8_t *e = cluster_buf + (i * 32);
            uint8_t first_byte = e[0];
            uint8_t attr = e[11];

            // End of directory
            if (first_byte == 0x00) {
                return 0;
            }

            // Deleted entry
            if (first_byte == 0xE5) {
                has_lfn = 0;
                continue;
            }

            // Long filename entry
            if (attr == FAT_ATTR_LFN) {
                uint8_t order = e[0];
                int seq = order & 0x1F;
                int is_last = order & 0x40;

                if (is_last) {
                    has_lfn = 1;
                    memset(lfn_name, 0, sizeof(lfn_name));
                }

                int base = (seq - 1) * 13;
                for (int j = 0; j < 5; j++) {
                    uint16_t c = e[1 + j*2] | (e[2 + j*2] << 8);
                    if (c == 0 || c == 0xFFFF) break;
                    if (base + j < 255) lfn_name[base + j] = (char)c;
                }
                for (int j = 0; j < 6; j++) {
                    uint16_t c = e[14 + j*2] | (e[15 + j*2] << 8);
                    if (c == 0 || c == 0xFFFF) break;
                    if (base + 5 + j < 255) lfn_name[base + 5 + j] = (char)c;
                }
                for (int j = 0; j < 2; j++) {
                    uint16_t c = e[28 + j*2] | (e[29 + j*2] << 8);
                    if (c == 0 || c == 0xFFFF) break;
                    if (base + 11 + j < 255) lfn_name[base + 11 + j] = (char)c;
                }
                continue;
            }

            // Skip volume label and . and ..
            if (attr & FAT_ATTR_VOLUME_ID) {
                has_lfn = 0;
                continue;
            }
            if (first_byte == '.') {
                has_lfn = 0;
                continue;
            }

            // Get the name
            if (has_lfn) {
                int k;
                for (k = 0; lfn_name[k]; k++) {
                    entry_name[k] = lfn_name[k];
                }
                entry_name[k] = '\0';
            } else {
                fat_name_to_str((char *)e, entry_name);
            }

            int is_dir = (attr & FAT_ATTR_DIRECTORY) ? 1 : 0;
            uint32_t size = read32(e + 28);
            callback(entry_name, is_dir, size, user_data);

            has_lfn = 0;
        }

        cluster = fat_next_cluster(cluster);
    }

    return 0;
}

fat32_fs_t *fat32_get_fs_info(void) {
    return fs_initialized ? &fs : NULL;
}

// Convert a normal filename to 8.3 format (uppercase, padded with spaces)
static void str_to_fat_name(const char *name, char *fat_name) {
    memset(fat_name, ' ', 11);

    int i = 0, j = 0;

    // Find the dot for extension
    const char *dot = NULL;
    for (const char *p = name; *p; p++) {
        if (*p == '.') dot = p;
    }

    // Copy name part (up to 8 chars, before dot or end)
    const char *end = dot ? dot : name + strlen(name);
    for (i = 0; i < 8 && name + i < end; i++) {
        char c = name[i];
        if (c >= 'a' && c <= 'z') c -= 32;  // Uppercase
        fat_name[i] = c;
    }

    // Copy extension (up to 3 chars, after dot)
    if (dot) {
        dot++;  // Skip the dot
        for (j = 0; j < 3 && dot[j]; j++) {
            char c = dot[j];
            if (c >= 'a' && c <= 'z') c -= 32;  // Uppercase
            fat_name[8 + j] = c;
        }
    }
}

// Calculate checksum for 8.3 name (used by LFN entries)
static uint8_t fat_checksum(const char *short_name) {
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
    }
    return sum;
}

// Find the parent directory cluster and the filename component
static int parse_parent_path(const char *path, uint32_t *parent_cluster, char *filename) {
    if (!path || path[0] != '/') return -1;

    // Find last slash
    const char *last_slash = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    // Extract filename
    const char *fname = last_slash + 1;
    if (!fname[0]) return -1;  // Path ends with /

    int len = strlen(fname);
    if (len >= 256) return -1;
    memcpy(filename, fname, len + 1);

    // Get parent directory
    if (last_slash == path) {
        // Parent is root
        *parent_cluster = fs.root_cluster;
    } else {
        // Build parent path
        char parent_path[256];
        int parent_len = last_slash - path;
        if (parent_len >= 256) return -1;
        memcpy(parent_path, path, parent_len);
        parent_path[parent_len] = '\0';

        fat32_dirent_t *parent = resolve_path(parent_path, parent_cluster);
        if (!parent) return -1;
        if (!(parent->attr & FAT_ATTR_DIRECTORY)) return -1;
    }

    return 0;
}

// Write 16-bit value to byte array (little-endian)
static void write16(uint8_t *p, uint16_t val) {
    p[0] = val & 0xFF;
    p[1] = (val >> 8) & 0xFF;
}

// Write 32-bit value to byte array (little-endian)
static void write32(uint8_t *p, uint32_t val) {
    p[0] = val & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[2] = (val >> 16) & 0xFF;
    p[3] = (val >> 24) & 0xFF;
}

// Find a free directory entry slot in a directory cluster chain
// Returns cluster and offset of free entry, or allocates new cluster if needed
static int find_free_dir_entry(uint32_t dir_cluster, uint32_t *out_cluster, uint32_t *out_offset) {
    uint32_t cluster = dir_cluster;
    uint32_t prev_cluster = 0;

    while (cluster < FAT32_EOC) {
        if (read_cluster(cluster, cluster_buf) < 0) {
            return -1;
        }

        int entries_per_cluster = cluster_buf_size / 32;

        for (int i = 0; i < entries_per_cluster; i++) {
            uint8_t *e = cluster_buf + (i * 32);
            uint8_t first_byte = e[0];

            // Free entry (deleted or never used)
            if (first_byte == 0x00 || first_byte == 0xE5) {
                *out_cluster = cluster;
                *out_offset = i;
                return 0;
            }
        }

        prev_cluster = cluster;
        cluster = fat_next_cluster(cluster);
    }

    // Need to allocate a new cluster for the directory
    uint32_t new_cluster = fat_alloc_cluster();
    if (new_cluster == 0) return -1;

    // Link it to the chain
    if (fat_set_cluster(prev_cluster, new_cluster) < 0) {
        fat_set_cluster(new_cluster, FAT32_FREE);
        return -1;
    }

    // Zero the new cluster
    if (zero_cluster(new_cluster) < 0) {
        return -1;
    }

    *out_cluster = new_cluster;
    *out_offset = 0;
    return 0;
}

// Create a new directory entry in a directory
// Returns the first cluster of the new file/dir, or 0 on error
static uint32_t create_dir_entry(uint32_t parent_cluster, const char *name, uint8_t attr, uint32_t first_cluster) {
    uint32_t entry_cluster, entry_offset;

    if (find_free_dir_entry(parent_cluster, &entry_cluster, &entry_offset) < 0) {
        return 0;
    }

    // Read the cluster containing the entry
    if (read_cluster(entry_cluster, cluster_buf) < 0) {
        return 0;
    }

    // Build the directory entry at the correct offset
    uint8_t *e = cluster_buf + (entry_offset * 32);
    memset(e, 0, 32);

    // Convert name to 8.3 format
    str_to_fat_name(name, (char *)e);

    // Set attributes
    e[11] = attr;

    // Set cluster (if provided)
    write16(e + 20, (first_cluster >> 16) & 0xFFFF);  // cluster_hi
    write16(e + 26, first_cluster & 0xFFFF);          // cluster_lo

    // Size is 0 for new files/directories
    write32(e + 28, 0);

    // Write back the cluster
    if (write_cluster(entry_cluster, cluster_buf) < 0) {
        return 0;
    }

    return 1;  // Success
}

// Update an existing directory entry's size and cluster
static int update_dir_entry(uint32_t dir_cluster, const char *name, uint32_t first_cluster, uint32_t size) {
    uint32_t entry_cluster, entry_offset;
    fat32_dirent_t *entry = find_entry_in_dir(dir_cluster, name, &entry_cluster, &entry_offset);

    if (!entry) return -1;

    // Read the cluster containing the entry
    if (read_cluster(entry_cluster, cluster_buf) < 0) {
        return -1;
    }

    // Update the entry
    uint8_t *e = cluster_buf + (entry_offset * 32);
    write16(e + 20, (first_cluster >> 16) & 0xFFFF);  // cluster_hi
    write16(e + 26, first_cluster & 0xFFFF);          // cluster_lo
    write32(e + 28, size);

    // Write back
    return write_cluster(entry_cluster, cluster_buf);
}

int fat32_create_file(const char *path) {
    if (!fs_initialized) return -1;

    char filename[256];
    uint32_t parent_cluster;

    if (parse_parent_path(path, &parent_cluster, filename) < 0) {
        return -1;
    }

    // Check if already exists
    fat32_dirent_t *existing = find_entry_in_dir(parent_cluster, filename, NULL, NULL);
    if (existing) {
        return 0;  // Already exists, that's fine
    }

    // Create directory entry with no cluster (empty file)
    if (!create_dir_entry(parent_cluster, filename, FAT_ATTR_ARCHIVE, 0)) {
        return -1;
    }

    return 0;
}

int fat32_mkdir(const char *path) {
    if (!fs_initialized) return -1;

    char dirname[256];
    uint32_t parent_cluster;

    if (parse_parent_path(path, &parent_cluster, dirname) < 0) {
        return -1;
    }

    // Check if already exists
    fat32_dirent_t *existing = find_entry_in_dir(parent_cluster, dirname, NULL, NULL);
    if (existing) {
        return -1;  // Already exists
    }

    // Allocate cluster for directory contents
    uint32_t dir_cluster = fat_alloc_cluster();
    if (dir_cluster == 0) {
        return -1;
    }

    // Zero the cluster
    if (zero_cluster(dir_cluster) < 0) {
        fat_set_cluster(dir_cluster, FAT32_FREE);
        return -1;
    }

    // Create . and .. entries in the new directory
    if (read_cluster(dir_cluster, cluster_buf) < 0) {
        fat_set_cluster(dir_cluster, FAT32_FREE);
        return -1;
    }

    // "." entry
    uint8_t *e = cluster_buf;
    memset(e, ' ', 11);
    e[0] = '.';
    e[11] = FAT_ATTR_DIRECTORY;
    write16(e + 20, (dir_cluster >> 16) & 0xFFFF);
    write16(e + 26, dir_cluster & 0xFFFF);

    // ".." entry
    e = cluster_buf + 32;
    memset(e, ' ', 11);
    e[0] = '.';
    e[1] = '.';
    e[11] = FAT_ATTR_DIRECTORY;
    write16(e + 20, (parent_cluster >> 16) & 0xFFFF);
    write16(e + 26, parent_cluster & 0xFFFF);

    if (write_cluster(dir_cluster, cluster_buf) < 0) {
        fat_set_cluster(dir_cluster, FAT32_FREE);
        return -1;
    }

    // Create entry in parent directory
    if (!create_dir_entry(parent_cluster, dirname, FAT_ATTR_DIRECTORY, dir_cluster)) {
        fat_set_cluster(dir_cluster, FAT32_FREE);
        return -1;
    }

    return 0;
}

int fat32_write_file(const char *path, const void *buf, size_t size) {
    if (!fs_initialized) return -1;

    char filename[256];
    uint32_t parent_cluster;

    if (parse_parent_path(path, &parent_cluster, filename) < 0) {
        return -1;
    }

    // Find or create the file
    fat32_dirent_t *entry = find_entry_in_dir(parent_cluster, filename, NULL, NULL);
    uint32_t old_cluster = 0;

    if (entry) {
        // File exists - get its current cluster chain to free later
        old_cluster = ((uint32_t)entry->cluster_hi << 16) | entry->cluster_lo;
        if (entry->attr & FAT_ATTR_DIRECTORY) {
            return -1;  // Can't write to a directory
        }
    } else {
        // Create new file entry
        if (!create_dir_entry(parent_cluster, filename, FAT_ATTR_ARCHIVE, 0)) {
            return -1;
        }
    }

    // Calculate clusters needed
    uint32_t cluster_size = cluster_buf_size;
    uint32_t clusters_needed = (size + cluster_size - 1) / cluster_size;
    if (clusters_needed == 0 && size > 0) clusters_needed = 1;

    // Allocate cluster chain for new data
    uint32_t first_cluster = 0;
    uint32_t prev_cluster = 0;
    const uint8_t *src = (const uint8_t *)buf;
    size_t remaining = size;

    for (uint32_t i = 0; i < clusters_needed; i++) {
        uint32_t cluster = fat_alloc_cluster();
        if (cluster == 0) {
            // Out of space - free what we allocated
            if (first_cluster) fat_free_chain(first_cluster);
            return -1;
        }

        if (first_cluster == 0) {
            first_cluster = cluster;
        }

        if (prev_cluster) {
            // Link previous cluster to this one
            fat_set_cluster(prev_cluster, cluster);
        }

        // Write data to this cluster
        size_t to_write = remaining > cluster_size ? cluster_size : remaining;
        memset(cluster_buf, 0, cluster_size);
        memcpy(cluster_buf, src, to_write);

        if (write_cluster(cluster, cluster_buf) < 0) {
            fat_free_chain(first_cluster);
            return -1;
        }

        src += to_write;
        remaining -= to_write;
        prev_cluster = cluster;
    }

    // Update directory entry with new cluster and size
    if (update_dir_entry(parent_cluster, filename, first_cluster, size) < 0) {
        if (first_cluster) fat_free_chain(first_cluster);
        return -1;
    }

    // Free old cluster chain
    if (old_cluster >= 2 && old_cluster < FAT32_EOC) {
        fat_free_chain(old_cluster);
    }

    return (int)size;
}

int fat32_delete(const char *path) {
    if (!fs_initialized) return -1;

    char filename[256];
    uint32_t parent_cluster;

    if (parse_parent_path(path, &parent_cluster, filename) < 0) {
        return -1;
    }

    // Find the entry
    uint32_t entry_cluster, entry_offset;
    fat32_dirent_t *entry = find_entry_in_dir(parent_cluster, filename, &entry_cluster, &entry_offset);
    if (!entry) return -1;

    // Don't allow deleting directories with this function
    if (entry->attr & FAT_ATTR_DIRECTORY) {
        return -1;
    }

    // Free the cluster chain
    uint32_t first_cluster = ((uint32_t)entry->cluster_hi << 16) | entry->cluster_lo;
    if (first_cluster >= 2 && first_cluster < FAT32_EOC) {
        fat_free_chain(first_cluster);
    }

    // Mark directory entry as deleted
    if (read_cluster(entry_cluster, cluster_buf) < 0) {
        return -1;
    }

    cluster_buf[entry_offset * 32] = 0xE5;  // Deleted marker

    return write_cluster(entry_cluster, cluster_buf);
}
