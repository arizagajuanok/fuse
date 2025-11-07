#include "big_brother.h"
#include "fat_volume.h"
#include "fat_table.h"
#include "fat_util.h"
#include <stdio.h>
#include <string.h>

int bb_is_log_file_dentry(fat_dir_entry dir_entry) {
    return strncmp(LOG_FILE_BASENAME, (char *)(dir_entry->base_name), 2) == 0 &&
           strncmp(LOG_FILE_EXTENSION, (char *)(dir_entry->extension), 3) == 0;
}

int bb_is_log_filepath(char *filepath) {
    return strncmp(BB_LOG_FILE, filepath, 8) == 0;
}

int bb_is_log_dirpath(char *filepath) {
    return strncmp(BB_DIRNAME, filepath, 15) == 0;
}

static bool bb_verify_orphan_directory(fat_table table, u32 cur_cluster) {
    bool res = false;
    fat_file tmp_dir = fat_file_init_orphan_dir(".bb_orphan", table, cur_cluster);
    GList *children_list = fat_file_read_children(tmp_dir);
    if (children_list == NULL) {
        DEBUG("Error al leer el archivo: %s\n", tmp_dir->name);
        return false;
    }

    for (GList *l = children_list; l != NULL; l = l->next) {
        if (bb_is_log_file_dentry(((fat_file)l->data)->dentry)) {   // Busco a fs.log dentro del cluster
            res = true;
            break;
        }
    }

    g_list_free_full(children_list, fat_file_destroy);
    fat_file_destroy(tmp_dir);

    return res;
}

static u32 bb_find_orphan_cluster(fat_table table) {
    u32 cluster, orphan_cur_cluster = 0;

    for (u32 cur_cluster = 2; cur_cluster < 10000; cur_cluster++) {
        cluster = fat_table_get_next_cluster(table, cur_cluster);
        if (fat_table_cluster_is_bad_sector(cluster)) {
            if (bb_verify_orphan_directory(table, cur_cluster)) {   // Verifica si el cluster que esta en el sector dañado contiene a fs.log
                orphan_cur_cluster = cur_cluster;
                break;
            }
        }
    }

    return orphan_cur_cluster;
}

int bb_create_new_log_files(fat_volume vol) {
    errno = 0;
    u32 orphan_cur_cluster = bb_find_orphan_cluster(vol->table);    // Obtengo la posicion del cluster huerfano en la FAT Table
    return -errno;
}