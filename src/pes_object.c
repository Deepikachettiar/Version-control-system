#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>

int object_write(const void *buf, size_t len, char *hash_out) {
    sha256_hex(buf, len, hash_out);
    
    char dir_path[PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), ".pes/objects/%.2s", hash_out);
    mkdir(dir_path, 0755);
    
    // Phase 1 - Commit 3: Write data atomically using a temp file
    char file_path[PATH_MAX];
    snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, hash_out + 2);
    
    char temp_path[PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", file_path);
    
    FILE *f = fopen(temp_path, "wb");
    if (!f) return -1;
    fwrite(buf, 1, len, f);
    fclose(f);
    
    // Rename temp file to final hash name
    return rename(temp_path, file_path);
}

// Phase 1 - Commit 4: Setup object read function
void *object_read(const char *hash, size_t *size_out) {
    char file_path[PATH_MAX];
    // Reconstruct the file path from the hash
    snprintf(file_path, sizeof(file_path), ".pes/objects/%.2s/%s", hash, hash + 2);
    
    FILE *f = fopen(file_path, "rb");
    if (!f) return NULL; // File not found
    
    fclose(f);
    return NULL; 
}
