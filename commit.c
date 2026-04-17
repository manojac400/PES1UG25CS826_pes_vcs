#include "pes.h"
#include "commit.h"
#include "tree.h"
#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int commit_create(const char *message, ObjectID *commit_id_out) {
    // Build tree from index
    ObjectID tree_hash;
    if (tree_from_index(&tree_hash) != 0) return -1;
    
    char tree_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&tree_hash, tree_hex);
    
    // Get parent commit
    ObjectID parent_hash;
    int has_parent = 0;
    if (head_read(&parent_hash) == 0) {
        has_parent = 1;
    }
    
    time_t now = time(NULL);
    const char *author = pes_author();
    
    char content[16384];
    int len = snprintf(content, sizeof(content),
                      "tree %s\n"
                      "parent %s\n"
                      "author %s %ld\n"
                      "committer %s %ld\n"
                      "\n"
                      "%s\n",
                      tree_hex,
                      has_parent ? (char[]){0} : "",  // Will fix
                      author, now,
                      author, now,
                      message);
    
    // Fix parent line
    if (has_parent) {
        char parent_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&parent_hash, parent_hex);
        len = snprintf(content, sizeof(content),
                      "tree %s\n"
                      "parent %s\n"
                      "author %s %ld\n"
                      "committer %s %ld\n"
                      "\n"
                      "%s\n",
                      tree_hex, parent_hex,
                      author, now,
                      author, now,
                      message);
    }
    
    if (object_write(OBJ_COMMIT, content, len, commit_id_out) != 0) return -1;
    
    // Update HEAD
    if (head_update(commit_id_out) != 0) return -1;
    
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(commit_id_out, hex);
    printf("Created commit: %s\n", hex);
    return 0;
}

int commit_parse(const void *data, size_t len, Commit *commit_out) {
    memset(commit_out, 0, sizeof(Commit));
    const char *ptr = data;
    
    // Parse tree
    if (strncmp(ptr, "tree ", 5) == 0) {
        ptr += 5;
        char hex[HASH_HEX_SIZE + 1];
        int i = 0;
        while (i < HASH_HEX_SIZE && *ptr && *ptr != '\n') hex[i++] = *ptr++;
        hex[i] = '\0';
        hex_to_hash(hex, &commit_out->tree);
        if (*ptr == '\n') ptr++;
    }
    
    // Parse parent
    if (strncmp(ptr, "parent ", 7) == 0) {
        ptr += 7;
        char hex[HASH_HEX_SIZE + 1];
        int i = 0;
        while (i < HASH_HEX_SIZE && *ptr && *ptr != '\n') hex[i++] = *ptr++;
        hex[i] = '\0';
        hex_to_hash(hex, &commit_out->parent);
        commit_out->has_parent = 1;
        if (*ptr == '\n') ptr++;
    }
    
    // Parse author
    if (strncmp(ptr, "author ", 7) == 0) {
        ptr += 7;
        int i = 0;
        while (*ptr && *ptr != '\n' && i < 255) commit_out->author[i++] = *ptr++;
        commit_out->author[i] = '\0';
        if (*ptr == '\n') ptr++;
    }
    
    // Parse committer (skip)
    while (*ptr && *ptr != '\n') ptr++;
    if (*ptr == '\n') ptr++;
    if (*ptr == '\n') ptr++;
    
    // Parse message
    size_t msg_len = len - (ptr - (const char*)data);
    if (msg_len > sizeof(commit_out->message) - 1) msg_len = sizeof(commit_out->message) - 1;
    memcpy(commit_out->message, ptr, msg_len);
    commit_out->message[msg_len] = '\0';
    
    return 0;
}

int commit_serialize(const Commit *commit, void **data_out, size_t *len_out) {
    char tree_hex[HASH_HEX_SIZE + 1];
    hash_to_hex(&commit->tree, tree_hex);
    
    char parent_hex[HASH_HEX_SIZE + 1] = "";
    if (commit->has_parent) {
        hash_to_hex(&commit->parent, parent_hex);
    }
    
    char content[16384];
    int len = snprintf(content, sizeof(content),
                      "tree %s\n"
                      "parent %s\n"
                      "author %s %llu\n"
                      "committer %s %llu\n"
                      "\n"
                      "%s\n",
                      tree_hex,
                      commit->has_parent ? parent_hex : "",
                      commit->author, commit->timestamp,
                      commit->author, commit->timestamp,
                      commit->message);
    
    *data_out = strdup(content);
    *len_out = len;
    return 0;
}

int commit_walk(commit_walk_fn callback, void *ctx) {
    ObjectID id;
    if (head_read(&id) != 0) return -1;
    
    while (1) {
        Commit commit;
        void *data;
        size_t size;
        ObjectType type;
        
        if (object_read(&id, &type, &data, &size) != 0 || type != OBJ_COMMIT) break;
        commit_parse(data, size, &commit);
        free(data);
        
        callback(&id, &commit, ctx);
        
        if (!commit.has_parent) break;
        id = commit.parent;
    }
    return 0;
}

int head_read(ObjectID *id_out) {
    FILE *f = fopen(HEAD_FILE, "r");
    if (!f) return -1;
    
    char line[1024];
    if (!fgets(line, sizeof(line), f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    
    line[strcspn(line, "\n")] = 0;
    
    if (strncmp(line, "ref: ", 5) == 0) {
        char ref_path[1024];
        snprintf(ref_path, sizeof(ref_path), ".pes/%s", line + 5);
        f = fopen(ref_path, "r");
        if (!f) return -1;
        if (!fgets(line, sizeof(line), f)) {
            fclose(f);
            return -1;
        }
        fclose(f);
        line[strcspn(line, "\n")] = 0;
    }
    
    return hex_to_hash(line, id_out);
}

int head_update(const ObjectID *new_commit) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(new_commit, hex);
    
    char temp_path[1024];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", HEAD_FILE);
    
    FILE *f = fopen(temp_path, "w");
    if (!f) return -1;
    fprintf(f, "%s\n", hex);
    fclose(f);
    
    rename(temp_path, HEAD_FILE);
    return 0;
}
