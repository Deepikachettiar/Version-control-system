// tree.c — Tree object serialization and construction
//
// PROVIDED functions: get_file_mode, tree_parse, tree_serialize
// TODO functions:     tree_from_index
//
// Binary tree format (per entry, concatenated with no separators):
//   "<mode-as-ascii-octal> <name>\0<32-byte-binary-hash>"
//
// Example single entry (conceptual):
//   "100644 hello.txt\0" followed by 32 raw bytes of SHA-256

#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

// ─── Mode Constants ─────────────────────────────────────────────────────────

#define MODE_FILE      0100644
#define MODE_EXEC      0100755
#define MODE_DIR       0040000

// ─── PROVIDED ───────────────────────────────────────────────────────────────

// Determine the object mode for a filesystem path.
uint32_t get_file_mode(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return 0;

    if (S_ISDIR(st.st_mode))  return MODE_DIR;
    if (st.st_mode & S_IXUSR) return MODE_EXEC;
    return MODE_FILE;
}

// Parse binary tree data into a Tree struct safely.
// Returns 0 on success, -1 on parse error.
int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        // 1. Safely find the space character for the mode
        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1; // Malformed data

        // Parse mode into an isolated buffer
        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1; // Skip space

        // 2. Safely find the null terminator for the name
        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1; // Malformed data

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0'; // Ensure null-terminated

        ptr = null_byte + 1; // Skip null byte

        // 3. Read the 32-byte binary hash
        if (ptr + HASH_SIZE > end) return -1; 
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

// Helper for qsort to ensure consistent tree hashing
static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name, ((const TreeEntry *)b)->name);
}

// Serialize a Tree struct into binary format for storage.
// Caller must free(*data_out).
// Returns 0 on success, -1 on error.
int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    // Estimate max size: (6 bytes mode + 1 byte space + 256 bytes name + 1 byte null + 32 bytes hash) per entry
    size_t max_size = tree->count * 296; 
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    // Create a mutable copy to sort entries (Git requirement)
    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries, sorted_tree.count, sizeof(TreeEntry), compare_tree_entries);

    size_t offset = 0;
    for (int i = 0; i < sorted_tree.count; i++) {
        const TreeEntry *entry = &sorted_tree.entries[i];
        
        // Write mode and name (%o writes octal correctly for Git standards)
        int written = sprintf((char *)buffer + offset, "%o %s", entry->mode, entry->name);
        offset += written + 1; // +1 to step over the null terminator written by sprintf
        
        // Write binary hash
        memcpy(buffer + offset, entry->hash.hash, HASH_SIZE);
        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

// Recursive helper function to build and write a tree for a specific directory level.
// - entries: The list of index entries we are currently processing.
// - count: How many entries are in this list.
// - prefix_len: How deep we are in the path (e.g., if we are inside "src/", prefix_len is 4).
// - tree_id_out: Where we store the hash of the tree we just created.
static int write_tree_level(IndexEntry *entries, int count, int prefix_len, ObjectID *tree_id_out) {
    Tree current_tree;
    current_tree.count = 0;

    int i = 0;
    while (i < count) {
        // Look at the current file path, starting from our current directory depth
        const char *relative_path = entries[i].path + prefix_len;
        
        // Find the next slash to see if this is a file or a subdirectory
        const char *slash = strchr(relative_path, '/');
        
        TreeEntry *t_entry = &current_tree.entries[current_tree.count];
        
        if (slash == NULL) {
            // No slash means it's a file in the current directory.
            t_entry->mode = entries[i].mode;
            strcpy(t_entry->name, relative_path);
            t_entry->hash = entries[i].hash;
            current_tree.count++;
            i++; // Move to the next entry
        } else {
            // It's a directory. We need to group all files that are inside this directory.
            int dir_name_len = slash - relative_path;
            strncpy(t_entry->name, relative_path, dir_name_len);
            t_entry->name[dir_name_len] = '\0'; // Null-terminate the directory name
            t_entry->mode = MODE_DIR;
            
            // Find how many consecutive entries belong to this same directory
            int dir_entries_count = 0;
            for (int j = i; j < count; j++) {
                const char *check_path = entries[j].path + prefix_len;
                if (strncmp(check_path, relative_path, dir_name_len) == 0 && check_path[dir_name_len] == '/') {
                    dir_entries_count++;
                } else {
                    break; // Found a file that belongs to a different directory
                }
            }
            
            // RECURSION: Build the tree for this subdirectory
            ObjectID subdir_hash;
            if (write_tree_level(&entries[i], dir_entries_count, prefix_len + dir_name_len + 1, &subdir_hash) != 0) {
                return -1;
            }
            
            // Link the subdirectory's hash to our current tree
            t_entry->hash = subdir_hash;
            current_tree.count++;
            
            // Skip past all the files we just processed in the recursion
            i += dir_entries_count;
        }
    }

    // Now serialize and write the current tree to the Object Store (Vault)
    void *tree_data;
    size_t tree_size;
    if (tree_serialize(&current_tree, &tree_data, &tree_size) != 0) return -1;
    
    int result = object_write(OBJ_TREE, tree_data, tree_size, tree_id_out);
    free(tree_data);
    
    return result;
}

// Build a tree hierarchy from the current index and write all tree
// objects to the object store.
int tree_from_index(ObjectID *id_out) {
    // 1. Load the current staging area into memory
    Index idx;
    if (index_load(&idx) != 0) return -1;
    
    // 2. If the index is empty, we can't make a tree
    if (idx.count == 0) {
        printf("Error: Nothing to commit (index is empty).\n");
        return -1;
    }

    // 3. The entries in .pes/index are already sorted alphabetically by `index_save`.
    // Start the recursive tree building from depth 0 (the root directory).
    return write_tree_level(idx.entries, idx.count, 0, id_out);
}
