/****** This file is part of profanOS **************************\
|   == ctr_size.c ==                                 .pi0iq.    |
|                                                   d"  . `'b   |
|   Part of the filesystem creation tool            q. /|\  u   |
|                                                    `// \\     |
|                                                    //   \\    |
|   [ github.com/elydre/profanOS - GPLv3 ]          //     \\   |
\***************************************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "../butterfly.h"

int fs_cnt_grow_size(filesys_t *filesys, sid_t loca_sid, uint32_t to_grow) {
    vdisk_t *vdisk;
    uint8_t *data;

    vdisk = fs_get_vdisk(filesys, loca_sid.device);

    if (vdisk == NULL) {
        printf("d%ds%d not found\n", loca_sid.device, loca_sid.sector);
        return -1;
    }

    sid_t next_sid;

    do {
        // check if sector is used
        if (!vdisk_is_sector_used(vdisk, loca_sid)) {
            printf("d%ds%d not used\n", loca_sid.device, loca_sid.sector);
            return -1;
        }

        // check if sector is cnt locator
        data = vdisk_load_sector(vdisk, loca_sid);

        if (data[0] != ST_CONT || data[1] != SF_LOCA) {
            printf("d%ds%d not cnt locator\n", loca_sid.device, loca_sid.sector);
            free(data);
            return -1;
        }

        memcpy(&next_sid, data + LAST_SID_OFFSET, sizeof(sid_t));
        vdisk_unload_sector(vdisk, loca_sid, data, NO_SAVE);

        if (IS_NULL_SID(next_sid)) {
            break;
        }

        loca_sid = next_sid;
    } while (1);

    uint32_t core_count = 0;
    sid_t new_loca_sid;
    sid_t core_sid;

    // check if loca is full
    for (uint32_t byte = sizeof(sid_t); byte < LAST_SID_OFFSET; byte += sizeof(sid_t)) {
        memcpy(&core_sid, data + byte, sizeof(sid_t));
        if (IS_NULL_SID(core_sid)) break;
        core_count += 1;
    }

    // fill loca
    while (core_count < LINKS_IN_LOCA && to_grow > 0) {
        core_sid.device = loca_sid.device;
        core_sid.sector = vdisk_get_unused_sector(vdisk);
        if (IS_NULL_SID(core_sid)) {
            printf("no free sectors\n");
            vdisk_unload_sector(vdisk, loca_sid, data, NO_SAVE);
            return -1;
        }
        if (fs_cnt_init_core_in_sector(vdisk, core_sid) == -1) {
            printf("failed to init core\n");
            vdisk_unload_sector(vdisk, loca_sid, data, NO_SAVE);
            return -1;
        }
        core_count += 1;
        memcpy(data + core_count * sizeof(sid_t), &core_sid, sizeof(sid_t));
        to_grow -= 1;
    }

    if (!to_grow) {
        vdisk_unload_sector(vdisk, loca_sid, data, SAVE);
        return 0;
    }

    while (to_grow > 0) {
        // create new locator

        new_loca_sid.device = loca_sid.device;
        new_loca_sid.sector = vdisk_get_unused_sector(vdisk);

        if (IS_NULL_SID(new_loca_sid)) {
            printf("no free sectors\n");
            vdisk_unload_sector(vdisk, loca_sid, data, NO_SAVE);
            return -1;
        }

        if (fs_cnt_init_loca_in_sector(vdisk, new_loca_sid) == -1) {
            printf("failed to init locator\n");
            vdisk_unload_sector(vdisk, loca_sid, data, NO_SAVE);
            return -1;
        }

        memcpy(data + LAST_SID_OFFSET, &new_loca_sid, sizeof(sid_t));
        vdisk_unload_sector(vdisk, loca_sid, data, SAVE);

        loca_sid = new_loca_sid;

        data = vdisk_load_sector(vdisk, loca_sid);

        // fill loca
        core_count = 0;
        while (core_count < LINKS_IN_LOCA && to_grow > 0) {
            core_sid.device = loca_sid.device;
            core_sid.sector = vdisk_get_unused_sector(vdisk);
            if (IS_NULL_SID(core_sid)) {
                printf("no free sectors\n");
                vdisk_unload_sector(vdisk, loca_sid, data, NO_SAVE);
                return -1;
            }
            if (fs_cnt_init_core_in_sector(vdisk, core_sid) == -1) {
                printf("failed to init core\n");
                vdisk_unload_sector(vdisk, loca_sid, data, NO_SAVE);
                return -1;
            }
            core_count += 1;
            memcpy(data + core_count * sizeof(sid_t), &core_sid, sizeof(sid_t));
            to_grow -= 1;
        }
    }

    vdisk_unload_sector(vdisk, loca_sid, data, SAVE);
    return 0;
}

int fs_cnt_set_size(filesys_t *filesys, sid_t head_sid, uint32_t size) {
    vdisk_t *vdisk;
    uint8_t *data;

    vdisk = fs_get_vdisk(filesys, head_sid.device);

    if (vdisk == NULL) {
        printf("d%ds%d not found\n", head_sid.device, head_sid.sector);
        return 1;
    }

    // check if sector is used
    if (!vdisk_is_sector_used(vdisk, head_sid)) {
        printf("d%ds%d not used\n", head_sid.device, head_sid.sector);
        return 1;
    }

    // check if sector is cnt header
    data = vdisk_load_sector(vdisk, head_sid);

    if (data[0] != ST_CONT || data[1] != SF_HEAD) {
        printf("d%ds%d not cnt header\n", head_sid.device, head_sid.sector);
        free(data);
        return 1;
    }

    uint32_t old_count = *((uint32_t *) (data + 2 + META_MAXLEN));
    uint32_t new_count = size / BYTE_IN_CORE;
    old_count = (old_count / BYTE_IN_CORE) + (old_count % BYTE_IN_CORE ? 1 : 0);
    if (size) new_count++;

    sid_t loca_sid = *((sid_t *) (data + LAST_SID_OFFSET));
    if (old_count < new_count) {
        // grow cnt
        if (fs_cnt_grow_size(filesys, loca_sid, new_count - old_count)) {
            printf("failed to grow cnt\n");
            vdisk_unload_sector(vdisk, head_sid, data, NO_SAVE);
            return 1;
        }
    } else if (old_count > new_count) {
        printf("shrink cnt not implemented\n");
    }

    *((uint32_t *) (data + 2 + META_MAXLEN)) = size;
    vdisk_unload_sector(vdisk, head_sid, data, SAVE);
    return 0;
}

uint32_t fs_cnt_get_size(filesys_t *filesys, sid_t head_sid) {
    vdisk_t *vdisk;
    uint8_t *data;

    vdisk = fs_get_vdisk(filesys, head_sid.device);

    if (vdisk == NULL) {
        printf("d%ds%d not found\n", head_sid.device, head_sid.sector);
        return UINT32_MAX;
    }

    // check if sector is used
    if (!vdisk_is_sector_used(vdisk, head_sid)) {
        printf("d%ds%d not used\n", head_sid.device, head_sid.sector);
        return UINT32_MAX;
    }

    // check if sector is cnt header
    data = vdisk_load_sector(vdisk, head_sid);

    if (data[0] != ST_CONT || data[1] != SF_HEAD) {
        printf("d%ds%d not cnt header\n", head_sid.device, head_sid.sector);
        vdisk_unload_sector(vdisk, head_sid, data, NO_SAVE);
        return UINT32_MAX;
    }

    uint32_t size = *((uint32_t *) (data + 2 + META_MAXLEN));
    vdisk_unload_sector(vdisk, head_sid, data, NO_SAVE);
    return size;
}
