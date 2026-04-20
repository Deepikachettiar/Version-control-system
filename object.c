// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── TODO: Implement these ──────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // 1. Determine the type string
    const char *type_str;
    if (type == OBJ_BLOB) type_str = "blob";
    else if (type == OBJ_TREE) type_str = "tree";
    else if (type == OBJ_COMMIT) type_str = "commit";
    else return -1;

    // 2. Format the header: "<type> <size>"
    char header[64];
    int hdr_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    
    // 3. Build the full object (header + '\0' + data)
    size_t full_size = hdr_len + 1 + len;
    void *full_buffer = malloc(full_size);
    if (!full_buffer) return -1;
    
    memcpy(full_buffer, header, hdr_len);
    ((char*)full_buffer)[hdr_len] = '\0'; // The null byte separator
    memcpy((char*)full_buffer + hdr_len + 1, data, len);

    // 4. Compute SHA-256 hash of the FULL object
    compute_hash(full_buffer, full_size, id_out);

    // 5. Deduplication check
    if (object_exists(id_out)) {
        free(full_buffer);
        return 0; // Already saved!
    }

    // 6. Create shard directory (.pes/objects/XX/)
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id_out, hex);
    char dir_path[256];
    snprintf(dir_path, sizeof(dir_path), "%s/%.2s", OBJECTS_DIR, hex);
    mkdir(dir_path, 0755);

    // 7. Determine paths
    char final_path[512];
    object_path(id_out, final_path, sizeof(final_path));
    
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", final_path);

    // 8. Write to temp file atomically
    int fd = open(temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        free(full_buffer);
        return -1;
    }
    if (write(fd, full_buffer, full_size) != (ssize_t)full_size) {
        close(fd);
        free(full_buffer);
        return -1;
    }
    
    fsync(fd); // Ensure it reaches disk
    close(fd);

    // 9. Atomically rename temp file to final hash name
    if (rename(temp_path, final_path) != 0) {
        free(full_buffer);
        return -1;
    }

    // 10. fsync the directory to persist the rename (best practice)
    int dir_fd = open(dir_path, O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }

    free(full_buffer);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    // 1. Get file path
    char path[512];
    object_path(id, path, sizeof(path));

    // 2. Open and read the entire file
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    size_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *file_buf = malloc(file_size);
    if (!file_buf) {
        fclose(f);
        return -1;
    }
    
    if (fread(file_buf, 1, file_size, f) != file_size) {
        free(file_buf);
        fclose(f);
        return -1;
    }
    fclose(f);

    // 3. Verify integrity by re-hashing the read data
    ObjectID computed_hash;
    compute_hash(file_buf, file_size, &computed_hash);
    if (memcmp(computed_hash.hash, id->hash, HASH_SIZE) != 0) {
        free(file_buf);
        return -1; // Hash mismatch! File is corrupt.
    }

    // 4. Find the null byte separating header from data
    char *null_pos = memchr(file_buf, '\0', file_size);
    if (!null_pos) {
        free(file_buf);
        return -1; // Invalid format
    }

    // 5. Parse the header
    char type_str[16];
    size_t data_len;
    if (sscanf((char*)file_buf, "%15s %zu", type_str, &data_len) != 2) {
        free(file_buf);
        return -1;
    }

    if (strcmp(type_str, "blob") == 0) *type_out = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *type_out = OBJ_TREE;
    else if (strcmp(type_str, "commit") == 0) *type_out = OBJ_COMMIT;
    else {
        free(file_buf);
        return -1;
    }

    // 6. Extract the data payload
    char *data_start = null_pos + 1;
    *len_out = data_len;
    *data_out = malloc(data_len);
    
    if (!*data_out) {
        free(file_buf);
        return -1;
    }
    memcpy(*data_out, data_start, data_len);

    free(file_buf);
    return 0;
}
