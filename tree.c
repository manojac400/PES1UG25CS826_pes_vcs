#include "pes.h"
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry*)a)->name, ((const TreeEntry*)b)->name);
}

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const char *ptr = data;
    const char *end = ptr + len;
    
    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *e = &tree_out->entries[tree_out->count];
        char mode_str[16], hash_hex[HASH_HEX_SIZE + 1];
        
        int i = 0;
        while (ptr < end && *ptr != ' ' && i < 15) mode_str[i++] = *ptr++;
        mode_str[i] = '\0';
        if (ptr < end && *ptr == ' ') ptr++;
        e->mode = strtol(mode_str, NULL, 8);
        
        i = 0;
        while (ptr < end && *ptr != ' ' && i < HASH_HEX_SIZE) hash_hex[i++] = *ptr++;
        hash_hex[i] = '\0';
        if (ptr < end && *ptr == ' ') ptr++;
        hex_to_hash(hash_hex, &e->hash);
        
        i = 0;
        while (ptr < end && *ptr != '\n' && i < 255) e->name[i++] = *ptr++;
        e->name[i] = '\0';
        if (ptr < end && *ptr == '\n') ptr++;
        
        tree_out->count++;
    }
    return 0;
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {
    Tree sorted = *tree;
    qsort(sorted.entries, sorted.count, sizeof(TreeEntry), compare_entries);
    
    size_t total = 0;
    for (int i = 0; i < sorted.count; i++) {
        total += strlen(sorted.entries[i].name) + HASH_HEX_SIZE + 20;
    }
    
    char *data = malloc(total);
    if (!data) return -1;
    
    size_t pos = 0;
    for (int i = 0; i < sorted.count; i++) {
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted.entries[i].hash, hex);
        pos += snprintf(data + pos, total - pos, "%06o %s %s\n",
                       sorted.entries[i].mode, hex, sorted.entries[i].name);
    }
    
    *data_out = data;
    *len_out = pos;
    return 0;
}

int tree_from_index(ObjectID *id_out) {
    Index index;
    if (index_load(&index) != 0) return -1;
    
    Tree tree;
    tree.count = 0;
    
    for (int i = 0; i < index.count && tree.count < MAX_TREE_ENTRIES; i++) {
        TreeEntry *e = &tree.entries[tree.count];
        e->mode = index.entries[i].mode;
        e->hash = index.entries[i].hash;
        const char *name = strrchr(index.entries[i].path, '/');
        strcpy(e->name, name ? name + 1 : index.entries[i].path);
        tree.count++;
    }
    
    void *data;
    size_t len;
    if (tree_serialize(&tree, &data, &len) != 0) return -1;
    
    int result = object_write(OBJ_TREE, data, len, id_out);
    free(data);
    return result;
}
