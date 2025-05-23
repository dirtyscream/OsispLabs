#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#include <errno.h>
#include "array_entries.h"

void add_entry(ArrayEntries *array, char* full_path, int flag) {
    array->size++;
    array->entries = (Map*)realloc(array->entries, array->size * sizeof(Map));
    if (!array->entries) {
        perror("realloc");
        exit(EXIT_FAILURE);
    }
    array->entries[array->size - 1].entry = strdup(full_path);
    array->entries[array->size - 1].flag = flag;
}

int compare_for_sorting(const void *a, const void *b) {
    return strcoll(((Map *)a)->entry, ((Map *)b)->entry);
}

void dirwalk(const char *dir_path, int flag_links, int flag_dirs, int flag_files, ArrayEntries *array_entries, int *entry_count) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        perror("opendir");
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        struct stat file_stat;
        if (lstat(full_path, &file_stat) == -1) {
            perror("lstat");
            continue;
        }
        int flag;
        if (S_ISLNK(file_stat.st_mode)) {
            flag = 0;
        } else if (S_ISREG(file_stat.st_mode)) {
            flag = 1;
        } else {
            flag = 2;
        }

        if ((flag_links && flag == 0) || (flag_dirs && flag == 2) || (flag_files && flag == 1) || (!flag_links && !flag_dirs && !flag_files)) {
            add_entry(array_entries, full_path, flag);
        }

        if (flag == 2) {
            dirwalk(full_path, flag_links, flag_dirs, flag_files, array_entries, entry_count);
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    setlocale(LC_COLLATE, "");
    int flag_links = 0, flag_dirs = 0, flag_files = 0, sort_output = 0;
    ArrayEntries array_entries = {0, NULL};
    int entry_count = 0;
    int opt;

    while ((opt = getopt(argc, argv, "ldfs")) != -1) {
        switch (opt) {
            case 'l': 
                flag_links = 1; 
                break;
            case 'd': 
                flag_dirs = 1; 
                break;
            case 'f': 
                flag_files = 1; 
                break;
            case 's': 
                sort_output = 1; 
                break;
            default: 
                fprintf(stderr, "Usage: %s [-l] [-d] [-f] [-s] [directory]\n", argv[0]); 
                exit(EXIT_FAILURE);
        }
    }  
    const char *start_dir = (optind < argc) ? argv[optind] : ".";
    dirwalk(start_dir, flag_links, flag_dirs, flag_files, &array_entries, &entry_count);
    if (sort_output) {
        qsort(array_entries.entries, array_entries.size, sizeof(Map), compare_for_sorting);
    }
    for (int i = 0; i < array_entries.size; i++) {
        printf("%s: %s\n", (array_entries.entries[i].flag == 0) ? "Symlink" :
                           (array_entries.entries[i].flag == 1) ? "File" : "Directory",
                           array_entries.entries[i].entry);
        free(array_entries.entries[i].entry);
    }
    free(array_entries.entries);
    return 0;
}
