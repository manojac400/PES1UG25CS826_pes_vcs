#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/sha.h>

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        if (sscanf(hex + i * 2, "%02hhx", &id_out->hash[i]) != 1) {
            return -1;
        }
    }
    return 0;
}

static void compute_sha256(const void *data, size_t len, ObjectID *hash_out) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data, len);
    SHA256_Final(hash_out->hash, &ctx);
}

void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, ".pes/objects/%c%c/%s", hex[0], hex[1], hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

int object_write(ObjectType type, const void *data, size_t size, ObjectID *hash_out) {
    const char *type_str;
    switch (type) {
        case OBJ_BLOB: type_str = "blob"; break;
        case OBJ_TREE: type_str = "tree"; break;
        case OBJ_COMMIT: type_str = "commit"; break;
        default: return -1;
    }
    
    char header[256];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, size);
    header[header_len] = '\0';
    header_len++;
    
    size_t total_len = header_len + size;
    unsigned char *full = malloc(total_len);
    if (!full) return -1;
    
    memcpy(full, header, header_len);
    memcpy(full + header_len, data, size);
    
    compute_sha256(full, total_len, hash_out);
    
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(hash_out, hex);
    
    char obj_path[1024];
    snprintf(obj_path, sizeof(obj_path), ".pes/objects/%c%c/%s", hex[0], hex[1], hex + 2);
    
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "mkdir -p .pes/objects/%c%c", hex[0], hex[1]);
    system(cmd);
    
    FILE *f = fopen(obj_path, "wb");
    if (!f) {
        free(full);
        return -1;
    }
    fwrite(full, 1, total_len, f);
    fclose(f);
    free(full);
    return 0;
}

int object_read(const ObjectID *hash, ObjectType *out_type, void **out_data, size_t *out_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(hash, hex);
    
    char obj_path[1024];
    snprintf(obj_path, sizeof(obj_path), ".pes/objects/%c%c/%s", hex[0], hex[1], hex + 2);
    
    FILE *f = fopen(obj_path, "rb");
    if (!f) return -1;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    unsigned char *content = malloc(size + 1);
    if (!content) {
        fclose(f);
        return -1;
    }
    fread(content, 1, size, f);
    fclose(f);
    
    size_t header_end = 0;
    while (header_end < (size_t)size && content[header_end] != '\0') {
        header_end++;
    }
    
    if (header_end >= (size_t)size) {
        free(content);
        return -1;
    }
    
    char header[256];
    memcpy(header, content, header_end);
    header[header_end] = '\0';
    
    char type_str[32];
    size_t data_size;
    sscanf(header, "%s %zu", type_str, &data_size);
    
    if (strcmp(type_str, "blob") == 0) *out_type = OBJ_BLOB;
    else if (strcmp(type_str, "tree") == 0) *out_type = OBJ_TREE;
    else *out_type = OBJ_COMMIT;
    
    *out_data = malloc(data_size);
    if (!*out_data) {
        free(content);
        return -1;
    }
    memcpy(*out_data, content + header_end + 1, data_size);
    *out_size = data_size;
    
    free(content);
    return 0;
}
