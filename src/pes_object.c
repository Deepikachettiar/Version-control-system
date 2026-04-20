#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>

int object_write(const void *buf, size_t len, char *hash_out) {
    sha256_hex(buf, len, hash_out);
    
    // Phase 1 - Commit 2: Create directory path
    char dir_path[PATH_MAX];
    snprintf(dir_path, sizeof(dir_path), ".pes/objects/%.2s", hash_out);
    mkdir(dir_path, 0755); // Create folder with read/write/execute permissions
    
    return 0; 
}
