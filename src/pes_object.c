#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>

// Phase 1 - Commit 1: Basic setup and hashing
int object_write(const void *buf, size_t len, char *hash_out) {
    // Generate the SHA-256 hash for the content
    sha256_hex(buf, len, hash_out);
    return 0; 
}
