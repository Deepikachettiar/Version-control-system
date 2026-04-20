#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int index_add(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    void *buf = malloc(size);
    fread(buf, 1, size, f);
    fclose(f);
    
    // Phase 2 - Commit 2: Hash the file and save to the Object Store
    char hash[65];
    if (object_write(buf, size, hash) != 0) {
        printf("Error: Failed to write object to vault.\n");
        free(buf);
        return -1;
    }
    
    free(buf);
    return 0;
}
