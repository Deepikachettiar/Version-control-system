#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Phase 2 - Commit 1: Read the file the user wants to add
int index_add(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Error: Could not open file %s\n", filename);
        return -1;
    }
    
    // Find out how big the file is
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Read the file into memory
    void *buf = malloc(size);
    if (buf) {
        fread(buf, 1, size, f);
    }
    fclose(f);
    
    free(buf); // Clean up for now
    return 0;
}
