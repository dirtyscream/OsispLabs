#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "data_array.h"

#define CYCLE_COUNT 500
#define MAX_PROCESSES 100

int statistics[4] = { 0 };
data_array_t data = { 0, 0 };
volatile sig_atomic_t continue_flag = 0;

struct sigaction allow_signal_action, disallow_signal_action;
pid_t all_processes[MAX_PROCESSES];
int process_count = 0;
int is_stdout_open = 1;

void sigusr1_handler(int signal) {
    (void)signal;
    is_stdout_open = 1;
}

void sigusr2_handler(int signal) {
    (void)signal;
    is_stdout_open = 0;
}

void setup_signal_handlers() {
    allow_signal_action.sa_handler = sigusr1_handler;
    disallow_signal_action.sa_handler = sigusr2_handler;
    allow_signal_action.sa_flags = SA_RESTART;
    disallow_signal_action.sa_flags = SA_RESTART;

    sigaction(SIGUSR1, &allow_signal_action, NULL);
    sigaction(SIGUSR2, &disallow_signal_action, NULL);
}

void print_statistics(pid_t parent_pid, pid_t child_pid) {
    if (is_stdout_open) {
        printf("PPID: %d, PID: %d, 00: %d, 01: %d, 10: %d, 11: %d\n",
               parent_pid, child_pid, statistics[0], statistics[1], statistics[2], statistics[3]);
    }
}

void child_process_function() {
    setup_signal_handlers();
    pid_t parent_pid = getppid();
    pid_t child_pid = getpid();
    int value = 0;
    for (int i = 0; i < CYCLE_COUNT; i++) {
        usleep(10000);
        data.first_value = value;
        data.second_value = value;
        const int index = data.first_value * 2 + data.second_value;
        statistics[index] += 1;
        value = 1 - value;
    }
    print_statistics(parent_pid, child_pid);
    exit(0);
}

void create_child_process() {
    pid_t pid = fork();

    if (pid == -1) {
        perror("Error when creating new process");
        exit(1);
    }
    if (pid == 0) {
        child_process_function();
    }
    if (pid > 0) {
        printf("Parent: Created new process with PID %d\n", pid);
    }

    all_processes[process_count++] = pid;
}

void kill_last_child_process() {
    if (process_count > 0) {
        pid_t pid = all_processes[--process_count];
        kill(pid, SIGKILL);
        printf("Parent: Killed process with PID %d, Remaining: %d\n", pid, process_count);
    } else {
        printf("Parent: No child processes to kill\n");
    }
}

void kill_all_child_processes() {
    while (process_count > 0) {
        kill_last_child_process();
    }
    printf("Parent: Killed all child processes\n");
    process_count = 0;
}

void show_all_child_processes() {
    printf("Parent PID: %d\n", getpid());
    for (int i = 0; i < process_count; i++) {
        printf("|---Child PID: %d\n", all_processes[i]);
    }
}

void allow_stdout_for_all_children(int is_allow) {
    for (int i = 0; i < process_count; i++) {
        int result = kill(all_processes[i], is_allow ? SIGUSR1 : SIGUSR2);
        if (result) {
            perror("Parent: Error sending signal");
        }
    }
    printf("Parent: %s stdout for all children\n", is_allow ? "Allowed" : "Disallowed");
}

int main() {
    printf("\nEnter symbol (+, -, l, k, s, g, q - exit): ");
    while (1) {
        char symbol[10];
        if (scanf("%9s", symbol) != 1) {
            continue;
        }

        if (strcmp(symbol, "+") == 0) {
            create_child_process();
        } else if (strcmp(symbol, "-") == 0) {
            kill_last_child_process();
        } else if (strcmp(symbol, "l") == 0) {
            show_all_child_processes();
        } else if (strcmp(symbol, "k") == 0) {
            kill_all_child_processes();
        } else if (strcmp(symbol, "s") == 0) {
            allow_stdout_for_all_children(0);
        } else if (strcmp(symbol, "g") == 0) {
            allow_stdout_for_all_children(1);
        } else if (strcmp(symbol, "q") == 0) {
            kill_all_child_processes();
            printf("Parent: Exiting\n");
            break;
        }
    }

    return 0;
}