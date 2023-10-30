#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void print_process_info(const char* pid) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%s/status", pid);

    FILE* status_file = fopen(path, "r");
    if (NULL == status_file) {
        return;
    }

    char buffer[256];
    char name[256];

    while (NULL != (fgets(buffer, sizeof(buffer), status_file))) {
        if (0 == strncmp(buffer, "Name:\t", strlen("Name:\t"))) {
            strcpy(name, buffer + strlen("Name:\t"));
            name[strcspn(name, "\n")] = '\0';
            break;
        }
    }

    fclose(status_file);
    printf("%5s %s\n", pid, name);
}

void get_process_info(const char* dir) {
    printf("  PID CMD\n");

    DIR* proc_dir = opendir(dir);
    if (NULL == proc_dir) {
        exit(1);
    }

    struct dirent* ent;
    while (NULL != (ent = readdir(proc_dir))) {
        if (4 == ent->d_type && 0 != atoi(ent->d_name)) {
            print_process_info(ent->d_name);
        }
    }

    closedir(proc_dir);
}

int main() {
    get_process_info("/proc");

    return 0;
}
