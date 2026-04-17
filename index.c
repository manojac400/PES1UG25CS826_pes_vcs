#include "pes.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int index_load(Index *index) {
    if (!index) return -1;
    memset(index, 0, sizeof(Index));
    
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;
    
    char line[4096];
    while (fgets(line, sizeof(line), f) && index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *e = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];
        unsigned long long mtime;
        unsigned int size;
        sscanf(line, "%o %s %llu %u %s", &e->mode, hash_hex, &mtime, &size, e->path);
        hex_to_hash(hash_hex, &e->hash);
        e->mtime_sec = mtime;
        e->size = size;
        index->count++;
    }
    fclose(f);
    return 0;
}

int index_save(const Index *index) {
    char temp_path[1024];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", INDEX_FILE);
    
    FILE *f = fopen(temp_path, "w");
    if (!f) return -1;
    
    for (int i = 0; i < index->count; i++) {
        const IndexEntry *e = &index->entries[i];
        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&e->hash, hash_hex);
        fprintf(f, "%06o %s %llu %u %s\n", e->mode, hash_hex, 
                (unsigned long long)e->mtime_sec, e->size, e->path);
    }
    fclose(f);
    
    rename(temp_path, INDEX_FILE);
    return 0;
}

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            return &index->entries[i];
        }
    }
    return NULL;
}

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        printf("Error: Cannot stat file %s\n", path);
        return -1;
    }
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("Error: Cannot open file %s\n", path);
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(size);
    if (!data) { fclose(f); return -1; }
    size_t bytes_read = fread(data, 1, size, f);
    fclose(f);
    
    if (bytes_read != (size_t)size) {
        free(data);
        return -1;
    }
    
    ObjectID hash;
    if (object_write(OBJ_BLOB, data, size, &hash) != 0) {
        free(data);
        return -1;
    }
    free(data);
    
    IndexEntry *e = index_find(index, path);
    if (!e && index->count < MAX_INDEX_ENTRIES) {
        e = &index->entries[index->count];
        strcpy(e->path, path);
        index->count++;
    }
    
    if (e) {
        e->mode = st.st_mode & 0777;
        e->hash = hash;
        e->mtime_sec = st.st_mtime;
        e->size = st.st_size;
        printf("Added: %s (hash: ", path);
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&hash, hex);
        printf("%.12s...)\n", hex);
    }
    
    return 0;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            for (int j = i; j < index->count - 1; j++) {
                index->entries[j] = index->entries[j + 1];
            }
            index->count--;
            return 0;
        }
    }
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    if (index->count == 0) {
        printf("  (nothing to show)\n");
    } else {
        for (int i = 0; i < index->count; i++) {
            printf("  staged:     %s\n", index->entries[i].path);
        }
    }
    return 0;
}
