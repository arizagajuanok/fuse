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
    fat_file tmp_dir = fat_file_init_orphan_dir(".bb_orphan", table, cur_cluster);  // Creo archivos temporales para poder ver el contenido del cluster
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

    g_list_free(children_list); // Libero Memoria
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

static int bb_create_bb_directory_in_cluster(u32 cur_cluster, fat_table table) {
    errno = 0;
    fat_file orphan_dir = fat_file_init_orphan_dir(BB_DIRNAME, table, cur_cluster); // Creo archivos temporales para poder usar add_child
    fat_file log_file = fat_file_init(table, false, strdup(BB_LOG_FILE));
    if (log_file == NULL) {
        return -errno;
    }
    fat_file_dentry_add_child(orphan_dir, log_file);    // Escribo en el disco fs.log y bb
    if (errno != 0) {
        DEBUG("Error al escribir en el disco\n");
        return -errno;
    }
    fat_file_destroy(log_file);     // Libero momoria
    fat_file_destroy(orphan_dir);
    return -errno;
}

static u32 bb_create_orphan_directory(fat_table table) {
    u32 orphan_cur_cluster = fat_table_get_next_free_cluster(table);    // Obtengo un la direccion de un cluster libre
    int error = fat_table_set_next_cluster(table, orphan_cur_cluster, FAT_CLUSTER_BAD_SECTOR);  // Marco el cluster como BAD_SECTOR
    if (error == -1) {
        DEBUG("Error al escribir el cluster: %u\n", orphan_cur_cluster);
        return 0;
    }
    bb_create_bb_directory_in_cluster(orphan_cur_cluster, table);   // Escribo el archivo fs.log y el directorio bb en el disco
    return orphan_cur_cluster;
}

int bb_create_new_log_files(fat_volume vol) {
    errno = 0;
    u32 orphan_cur_cluster = bb_find_orphan_cluster(vol->table);    // Obtengo la posicion del cluster huerfano en la FAT Table
    
    if (orphan_cur_cluster == 0) {
        orphan_cur_cluster = bb_create_orphan_directory(vol->table);    // Creo un directorio huerfano y guardo la direccion del cluster
        if (orphan_cur_cluster == 0) {
            DEBUG("Error al crear el cluster huerfano\n");
        }
    }

    fat_tree_node root_dir_node = fat_tree_node_search(vol->file_tree, "/");    // Busco el nodo de la raiz del arbol de direcciones del volumen
    if (root_dir_node == NULL) {
        errno = ENOENT;
        return -errno;
    }

    fat_file bb_dir = fat_file_init_orphan_dir(BB_DIRNAME, vol->table, orphan_cur_cluster); // Creo el directorio con el cluster encontrado
    if (errno != 0) {
        return -errno;
    }
    vol->file_tree = fat_tree_insert(vol->file_tree, root_dir_node, bb_dir);    // Inserto el directorio en el arbol de directorios del volumen

    fat_tree_node bb_dir_node = fat_tree_node_search(vol->file_tree, BB_DIRNAME);   // Buscon el nodo del directorio huerfano
    if (bb_dir_node == NULL) {
        errno = ENOENT;
        return -errno;
    }

    fat_file log_file = (fat_file)(fat_file_read_children(bb_dir)->data);   // Busco el archivo de logs en los hijos del directorio huerfano
    vol->file_tree = fat_tree_insert(vol->file_tree, bb_dir_node, log_file);    // Inserto el archivo de logs en el arbol de directorios del volumen

    return -errno;
}