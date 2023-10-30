#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
  pid_t pid;
  char* name;
  int status;
} sub_process;

sub_process** processes;
int process_num = 0;

sub_process** unknown_processes;
int unknown_processes_num = 0;

int arr_size_proc = 128;
int arr_size_unknown = 128;

void size_increase(sub_process*** arr, int* size){
  *size = *size * 2;
  *arr = realloc(*arr, (*size) * sizeof(sub_process*));
  if (!arr) {
    fprintf(stderr, "Failed to allocate memory\n");
    exit(EXIT_FAILURE);
  }
}

void ssp_init() {
  processes = malloc(arr_size_proc * sizeof(sub_process*));
  unknown_processes = malloc(arr_size_unknown * sizeof(sub_process*));
  if (!processes || !unknown_processes) {
    fprintf(stderr, "Failed to allocate memory\n");
    exit(EXIT_FAILURE);
  }
  prctl(PR_SET_CHILD_SUBREAPER, 1);
}

int ssp_create(char *const *argv, int fd0, int fd1, int fd2) {
  pid_t pid = fork();
  if (-1 == pid) {
    fprintf(stderr, "Fail to create a process\n");
    return -1;
  }
  if (0 == pid) {
    dup2(fd0, 0);
    dup2(fd1, 1);
    dup2(fd2, 2);

    DIR* dir = opendir("/proc/self/fd");
    struct dirent* ent;
    if (dir) {
      while ((ent = readdir(dir))) {
        if (ent->d_type == DT_LNK) {
          int fd = atoi(ent->d_name);
          if (fd != 0 && fd != 1 && fd != 2) {
            close(fd);
          }
        }
      }
      closedir(dir);
    }
    if (execvp(argv[0], argv) == -1) {
      exit(errno);
    }
  }
  else {
    sub_process* proc = malloc(sizeof(sub_process));
    if (!proc) {
      fprintf(stderr, "Failed to allocate memory\n");
      exit(EXIT_FAILURE);
    }

    proc->pid = pid;
    proc->name = strdup(argv[0]);
    proc->status = -1;
    processes[process_num] = proc;
    ++ process_num;

    if (process_num >= arr_size_proc) {
      size_increase(&processes, &arr_size_proc);
    }
    return process_num - 1;
  }
  return -1;
}

int ssp_get_status(int ssp_id) {
  if (ssp_id >= process_num || ssp_id < 0) return -1;

  int status = processes[ssp_id]->status;
  if (status != -1) return status;

  int pid = processes[ssp_id]->pid;

  if (waitpid(pid, &status, WNOHANG) > 0) {
    if (WIFEXITED(status)) {
      processes[ssp_id]->status = WEXITSTATUS(status);
    } 
    else if (WIFSIGNALED(status)) {
      processes[ssp_id]->status = WTERMSIG(processes[ssp_id]->status) + 128;
    }
  }
  return processes[ssp_id]->status;
}

void ssp_send_signal(int ssp_id, int signum) {
  if (ssp_id >= process_num || process_num < 0) return;
  if (ssp_get_status(ssp_id) == -1) {
    kill(processes[ssp_id]->pid, signum);
  }
}

void ssp_wait() {
  for (int i = 0; i < process_num; ++ i) {
    int status = processes[i]->status;
    if (status != -1) continue;
    if (waitpid(processes[i]->pid, &processes[i]->status, 0) > 0) {
      if (WIFEXITED(processes[i]->status)) {
        processes[i]->status = WEXITSTATUS(processes[i]->status);
      } 
      else if (WIFSIGNALED(processes[i]->status)) {
        processes[i]->status = WTERMSIG(processes[i]->status) + 128;
      } 
    }
  }
}

void record_unknown_processes() {
  pid_t pid;
  int status;
  while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
    bool found = false;
    for (int i = 0; i < process_num; ++ i) {
      if (processes[i]->pid == pid) {
        found = true;
        break;
      }
    }
    if (found) continue;
    sub_process* unknown_proc = malloc(sizeof(sub_process));
    if (!unknown_proc) { 
      fprintf(stderr, "Failed to allocate memory\n");
      exit(EXIT_FAILURE);
    }

    unknown_proc->pid = pid;
    unknown_proc->name = strdup("<unknown>");
    if (WIFEXITED(status)) {
      unknown_proc->status = WEXITSTATUS(status);
    } 
    else if (WIFSIGNALED(unknown_proc->status)) {
      unknown_proc->status = WTERMSIG(status) + 128;
    } 
    unknown_processes[unknown_processes_num] = unknown_proc;
    ++ unknown_processes_num;

    if (unknown_processes_num >= arr_size_unknown) {
      size_increase(&unknown_processes, &arr_size_unknown);
    }
  }
}

void ssp_print() {
  int space = 3;
  for (int i = 0; i < process_num; ++ i) {
    int current = strlen(processes[i]->name);
    space = current > space ? current : space;
  }
  if (process_num == 0) {
    space = 9;
  }

  printf("%7s %-*s %s\n", "PID", space, "CMD", "STATUS");

  for (int i = 0; i < process_num; ++ i) {
    int status = ssp_get_status(i);
    printf("%7d %-*s %d\n", processes[i]->pid, space, processes[i]->name, status);
  }
  record_unknown_processes();
  for (int i = 0; i < unknown_processes_num; ++ i) {
    printf("%7d %-9s %d\n", unknown_processes[i]->pid, 
           unknown_processes[i]->name, unknown_processes[i]->status);
  }
}
