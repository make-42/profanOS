/*****************************************************************************\
|   === usg_dir.c : 2024 ===                                                  |
|                                                                             |
|    Part of the filesystem creation tool                          .pi0iq.    |
|                                                                 d"  . `'b   |
|    This file is part of profanOS and is released under          q. /|\  "   |
|    the terms of the GNU General Public License                   `// \\     |
|                                                                  //   \\    |
|   === elydre : https://github.com/elydre/profanOS ===         #######  \\   |
\*****************************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../butterfly.h"

int fu_is_dir(filesys_t *filesys, sid_t dir_sid) {
    char *name = fs_cnt_get_meta(filesys, dir_sid);
    if (name == NULL) return 0;
    if (name[0] == 'D') {
        free(name);
        return 1;
    }
    free(name);
    return 0;
}

/*
DIR STRUCTURE
    [size](4)
    [sid1](8) [name1 offset](4)
    [sid2](8) [name2 offset](4)
    ...
    [sidN](8) [nameN offset](4)
    ...
    [name1](N)
    [name2](N)
    ...
    [nameN](N)
*/

int fu_get_dir_content(filesys_t *filesys, sid_t dir_sid, sid_t **ids, char ***names) {
    // read the directory and get size
    uint32_t size = fs_cnt_get_size(filesys, dir_sid);
    if (size == UINT32_MAX) {
        printf("failed to get directory size\n");
        return -1;
    }

    if (!fu_is_dir(filesys, dir_sid)) {
        printf("not a directory\n");
        return -1;
    }

    // read the directory
    uint8_t *buf = malloc(size);
    if (fs_cnt_read(filesys, dir_sid, buf, 0, size)) {
        printf("failed to read directory\n");
        return -1;
    }

    // get the number of elements
    uint32_t count;
    memcpy(&count, buf, sizeof(uint32_t));

    if (count == 0 || ids == NULL || names == NULL) {
        free(buf);
        return count;
    }

    // get the elements
    *ids = malloc(sizeof(sid_t) * count);
    *names = malloc(sizeof(char *) * count);

    uint32_t name_offset;
    for (uint32_t i = 0; i < count; i++) {
        memcpy(&(*ids)[i], buf + sizeof(uint32_t) + i * (sizeof(sid_t) + sizeof(uint32_t)), sizeof(sid_t));
        memcpy(&name_offset, buf + sizeof(uint32_t) + i * (sizeof(sid_t) +
                sizeof(uint32_t)) + sizeof(sid_t), sizeof(uint32_t));
        char *tmp = (void *) buf + sizeof(uint32_t) + count * (sizeof(sid_t) + sizeof(uint32_t)) + name_offset;
        (*names)[i] = malloc(strlen(tmp) + 1);
        strcpy((*names)[i], tmp);
    }
    free(buf);
    return count;
}

int fu_add_element_to_dir(filesys_t *filesys, sid_t dir_sid, sid_t element_sid, char *name) {
    // read the directory and get size
    uint32_t size = fs_cnt_get_size(filesys, dir_sid);
    if (size == UINT32_MAX) {
        printf("failed to get directory size\n");
        return 1;
    }

    if (!fu_is_dir(filesys, dir_sid)) {
        printf("not a directory\n");
        return 1;
    }

    // extend the directory
    if (fs_cnt_set_size(filesys, dir_sid, size + sizeof(sid_t) + sizeof(uint32_t) + strlen(name) + 1)) {
        printf("failed to extend directory\n");
        return 1;
    }

    // read the directory
    uint8_t *buf = malloc(size + sizeof(sid_t) + sizeof(uint32_t) + strlen(name) + 1);
    if (fs_cnt_read(filesys, dir_sid, buf, 0, size)) {
        printf("failed to read directory\n");
        return 1;
    }

    uint32_t count = 0;
    // get the number of elements
    memcpy(&count, buf, sizeof(uint32_t));

    if (count > 0) {
        // move names
        for (uint32_t i = size - 1; i >= sizeof(uint32_t) + count * (sizeof(sid_t) + sizeof(uint32_t)); i--) {
            buf[i + sizeof(uint32_t) + sizeof(sid_t)] = buf[i];
        }
    }

    // insert the new element
    memcpy(buf + sizeof(uint32_t) + count * (sizeof(sid_t) + sizeof(uint32_t)), &element_sid, sizeof(sid_t));
    uint32_t name_offset = size - sizeof(uint32_t) - count * (sizeof(sid_t) + sizeof(uint32_t));
    memcpy(buf + sizeof(uint32_t) + count * (sizeof(sid_t) + sizeof(uint32_t)) + sizeof(sid_t),
            &name_offset, sizeof(uint32_t));

    // update the number of elements
    count++;
    memcpy(buf, &count, sizeof(uint32_t));

    strcpy((char *) buf + sizeof(uint32_t) + count * sizeof(sid_t) + count * sizeof(uint32_t) + name_offset, name);

    // write the directory
    if (fs_cnt_write(filesys, dir_sid, buf, 0, size + sizeof(sid_t) + sizeof(uint32_t) + strlen(name) + 1)) {
        printf("failed to write directory\n");
        return 1;
    }

    free(buf);

    return 0;
}

sid_t fu_dir_create(filesys_t *filesys, int device_id, char *path) {
    char *parent, *name;

    sid_t parent_sid;
    sid_t head_sid;

    sep_path(path, &parent, &name);
    if (parent[0]) {
        parent_sid = fu_path_to_sid(filesys, ROOT_SID, parent);
        if (IS_NULL_SID(parent_sid)) {
            printf("failed to find parent directory\n");
            free(parent);
            free(name);
            return NULL_SID;
        }
        if (!fu_is_dir(filesys, parent_sid)) {
            printf("parent is not a directory\n");
            free(parent);
            free(name);
            return NULL_SID;
        }
    } else {
        parent_sid.device = 2;
        parent_sid.sector = 0;
    }

    // generate the meta
    char *meta = malloc(META_MAXLEN);
    snprintf(meta, META_MAXLEN, "D-%s", name);

    head_sid = fs_cnt_init(filesys, (device_id > 0) ? (uint32_t) device_id : parent_sid.device, meta);
    free(meta);

    if (IS_NULL_SID(head_sid)) {
        printf("failed to create directory\n");
        free(parent);
        free(name);
        return NULL_SID;
    }

    // create a link in parent directory
    if (parent[0]) {
        if (fu_add_element_to_dir(filesys, parent_sid, head_sid, name)) {
            printf("failed to add directory to parent\n");
            free(parent);
            free(name);
            return NULL_SID;
        }
    }

    fs_cnt_set_size(filesys, head_sid, sizeof(uint32_t));
    fs_cnt_write(filesys, head_sid, "\0\0\0\0", 0, 4);

    // create '.' and '..'
    if (fu_add_element_to_dir(filesys, head_sid, parent_sid, "..")) {
        printf("failed to add '..' to directory\n");
        free(parent);
        free(name);
        return NULL_SID;
    }

    if (fu_add_element_to_dir(filesys, head_sid, head_sid, ".")) {
        printf("failed to add '.' to directory\n");
        free(parent);
        free(name);
        return NULL_SID;
    }

    free(parent);
    free(name);

    return head_sid;
}
