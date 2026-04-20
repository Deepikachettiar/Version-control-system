// index.c — Staging area implementation
//
// Text format of .pes/index (one entry per line, sorted by path):
//
//   <mode-octal> <64-char-hex-hash> <mtime-seconds> <size> <path>
//
// Example:
//   100644 a1b2c3d4e5f6...  1699900000 42 README.md
//   100644 f7e8d9c0b1a2...  1699900100 128 src/main.c
//
// This is intentionally a simple text format. No magic numbers, no
// binary parsing. The focus is on the staging area CONCEPT (tracking
// what will go into the next commit) and ATOMIC WRITES (temp+rename).
//
// PROVIDED functions: index_find, index_remove, index_status
// TODO functions:     index_load, index_save, index_add

#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

// Find an index entry by path (linear scan).
IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

// Remove a file from the index.
// Returns 0 on success, -1 if path not in index.
int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// Print the status of the working directory.
//
// Identifies files that are staged, unstaged (modified/deleted in working dir),
// and untracked (present in working dir but not in index).
// Returns 0.
int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged_count = 0;
    // Note: A true Git implementation deeply diffs against the HEAD tree here. 
    // For this lab, displaying indexed files represents the staging intent.
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }
    if (staged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    int unstaged_count = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            unstaged_count++;
        } else {
            // Fast diff: check metadata instead of re-hashing file content
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec || st.st_size != (off_t)index->entries[i].size) {
                printf("  modified:   %s\n", index->entries[i].path);
                unstaged_count++;
            }
        }
    }
    if (unstaged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    int untracked_count = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            // Skip hidden directories, parent directories, and build artifacts
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            if (strcmp(ent->d_name, "pes") == 0) continue; // compiled executable
            if (strstr(ent->d_name, ".o") != NULL) continue; // object files

            // Check if file is tracked in the index
            int is_tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    is_tracked = 1; 
                    break;
                }
            }
            
            if (!is_tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode)) { // Only list regular files for simplicity
                    printf("  untracked:  %s\n", ent->d_name);
                    untracked_count++;
                }
            }
        }
        closedir(dir);
    }
    if (untracked_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}

// ─── TODO: Implement these ───────────────────────────────────────────────────

// Helper function for qsort to sort index entries alphabetically by path
static int compare_entries(const void *a, const void *b) {
    const IndexEntry *entry_a = (const IndexEntry *)a;
    const IndexEntry *entry_b = (const IndexEntry *)b;
    return strcmp(entry_a->path, entry_b->path);
}

// Load the index from .pes/index.
int index_load(Index *index) {
    index->count = 0; // Initialize empty index
    
    FILE *f = fopen(".pes/index", "r");
    if (!f) return 0; // If file doesn't exist yet, return success (empty staging area)

    unsigned int mode;
    char hex[65];
    unsigned long long mtime;
    unsigned long long size;
    char path[256];

    // Read the formatted text file line by line
    while (fscanf(f, "%o %64s %llu %llu %255s", &mode, hex, &mtime, &size, path) == 5) {
        IndexEntry *entry = &index->entries[index->count];
        
        entry->mode = mode;
        hex_to_hash(hex, &entry->hash); // Convert string to ObjectID struct
        entry->mtime_sec = mtime;
        entry->size = size;
        strcpy(entry->path, path);
        
        index->count++;
    }

    fclose(f);
    return 0;
}

// Save the index to .pes/index atomically.
int index_save(const Index *index) {
    // 1. Sort the entries by path before saving (casting away const for the lab's qsort)
    qsort((void *)index->entries, index->count, sizeof(IndexEntry), compare_entries);

    // 2. Open a temporary file
    FILE *f = fopen(".pes/index.tmp", "w");
    if (!f) return -1;

    // 3. Write each entry in the exact format required
    for (int i = 0; i < index->count; i++) {
        char hex[65];
        hash_to_hex(&index->entries[i].hash, hex); // Convert ObjectID struct to string
        
        fprintf(f, "%o %s %llu %llu %s\n", 
                index->entries[i].mode, 
                hex, 
                (unsigned long long)index->entries[i].mtime_sec, 
                (unsigned long long)index->entries[i].size, 
                index->entries[i].path);
    }

    // 4. Force OS to write to physical disk before renaming (Atomicity)
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    // 5. Atomically replace the old index with the new one
    if (rename(".pes/index.tmp", ".pes/index") != 0) {
        return -1;
    }
    
    return 0;
}

// Stage a file for the next commit.
int index_add(Index *index, const char *path) {
    // 1. Get file metadata (size, time, mode)
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    // 2. Read the file into memory
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    
    void *buf = malloc(st.st_size);
    if (st.st_size > 0 && buf) {
        fread(buf, 1, st.st_size, f);
    }
    fclose(f);

    // 3. Hash and store it in the Vault (Phase 1 function)
    ObjectID hash;
    if (object_write(OBJ_BLOB, buf, st.st_size, &hash) != 0) {
        free(buf);
        return -1;
    }
    free(buf);

    // 4. Check if file is already in the index
    IndexEntry *entry = index_find(index, path);
    if (!entry) {
        // If not, create a new entry
        entry = &index->entries[index->count];
        index->count++;
    }

    // 5. Update the entry data
    entry->mode = st.st_mode;
    entry->hash = hash;
    entry->mtime_sec = st.st_mtime;
    entry->size = st.st_size;
    strcpy(entry->path, path);

    // 6. Save the index
    return index_save(index);
}
