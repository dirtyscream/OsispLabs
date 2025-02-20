#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#define MAX_ENTRIES 1024

typedef struct ObjectMap {
    int flag;
    char* entry;
} ObjectMap;

int compare_entries(const void *a, const void *b) {
    return strcoll(((ObjectMap *)a)->entry, ((ObjectMap *)b)->entry);
}

void scan_directory(const char *dir_path, int show_links, int show_dirs, int show_files, ObjectMap entries[], int *entry_count) {
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
        if ((show_links && flag == 0) || (show_dirs && flag == 2) || (show_files && flag == 1) || (!show_links && !show_dirs && !show_files)) {
            if (*entry_count < MAX_ENTRIES) {
                entries[*entry_count].entry = strdup(full_path);
                entries[*entry_count].flag = flag;
                (*entry_count)++;
            }
        }
        if (flag == 2) {
            scan_directory(full_path, show_links, show_dirs, show_files, entries, entry_count);
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    int show_links = 0, show_dirs = 0, show_files = 0, sort_output = 0;
    ObjectMap entries[MAX_ENTRIES];
    int entry_count = 0;
    int opt;
    while ((opt = getopt(argc, argv, "ldfs")) != -1) {
        switch (opt) {
            case 'l': show_links = 1; break;
            case 'd': show_dirs = 1; break;
            case 'f': show_files = 1; break;
            case 's': sort_output = 1; break;
            default: fprintf(stderr, "Usage: %s [-l] [-d] [-f] [-s] [directory]\n", argv[0]); exit(EXIT_FAILURE);
        }
    }
    const char *start_dir;
    if (optind < argc) {
        start_dir = argv[optind];
    } else {
        start_dir = ".";
    }
    scan_directory(start_dir, show_links, show_dirs, show_files, entries, &entry_count);
    if (sort_output) {
        qsort(entries, entry_count, sizeof(ObjectMap), compare_entries);
    }
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].flag == 0) {
            printf("Symlink: ");
        } else if (entries[i].flag == 1) {
            printf("File: ");
        } else if (entries[i].flag == 2) {
            printf("Directory: ");
        }
        printf("%s\n", entries[i].entry);
        free(entries[i].entry);
    }
    return 0;
}
