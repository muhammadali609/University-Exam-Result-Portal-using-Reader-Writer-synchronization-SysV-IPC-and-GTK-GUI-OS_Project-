#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "reader_writer.h"

#define SEM_RESOURCE 0
#define SEM_READCOUNT 1
#define SEM_QUEUE 2
#define MARKS_COUNT 5

static int shm_id = -1;
static int sem_id = -1;
static SharedState *shared = NULL;

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static int sem_op(int sem_num, int value) {
    struct sembuf op = {0};
    op.sem_num = sem_num;
    op.sem_op = value;
    op.sem_flg = 0;
    return semop(sem_id, &op, 1);
}

static int lock_sem(int sem_num) {
    return sem_op(sem_num, -1);
}

static int unlock_sem(int sem_num) {
    return sem_op(sem_num, 1);
}

static int load_file(const char *path, char *buffer, size_t size) {
    int fd = open(path, O_RDONLY);
    ssize_t nread;
    size_t used = 0;

    if (fd < 0) {
        return -1;
    }

    while ((nread = read(fd, buffer + used, size - used - 1)) > 0) {
        used += (size_t)nread;
        if (used >= size - 1) {
            break;
        }
    }
    close(fd);

    if (nread < 0) {
        return -1;
    }
    buffer[used] = '\0';
    return (int)used;
}

static int save_file(const char *path, const char *data) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    size_t len = strlen(data);
    ssize_t total = 0;

    if (fd < 0) {
        return -1;
    }

    while (total < (ssize_t)len) {
        ssize_t written = write(fd, data + total, len - (size_t)total);
        if (written <= 0) {
            close(fd);
            return -1;
        }
        total += written;
    }

    close(fd);
    return 0;
}

static void append_log(const char *role, int request_id, const char *text) {
    char line[256];
    char ts[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    int fd;
    int n;

    if (!tm_info) {
        return;
    }

    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);
    n = snprintf(line, sizeof(line), "[%s] %s #%d: %s\n", ts, role, request_id, text);
    if (n <= 0) {
        return;
    }

    fd = open("operations.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        write(fd, line, (size_t)n);
        close(fd);
    }
}

static int check_line_credentials(const char *path, const char *id, const char *password) {
    char buf[8192];
    char *line;

    if (load_file(path, buf, sizeof(buf)) < 0) {
        return 0;
    }

    line = strtok(buf, "\n");
    while (line) {
        char id_in_file[MAX_FIELD];
        char pass_in_file[MAX_FIELD];
        if (sscanf(line, "%63s %63s", id_in_file, pass_in_file) == 2) {
            if (strcmp(id_in_file, id) == 0 && strcmp(pass_in_file, password) == 0) {
                return 1;
            }
        }
        line = strtok(NULL, "\n");
    }
    return 0;
}

static void enter_reader(void) {
    lock_sem(SEM_QUEUE);
    lock_sem(SEM_READCOUNT);
    shared->reader_count++;
    if (shared->reader_count == 1) {
        lock_sem(SEM_RESOURCE);
    }
    unlock_sem(SEM_READCOUNT);
    unlock_sem(SEM_QUEUE);
}

static void exit_reader(void) {
    lock_sem(SEM_READCOUNT);
    shared->reader_count--;
    if (shared->reader_count == 0) {
        unlock_sem(SEM_RESOURCE);
    }
    unlock_sem(SEM_READCOUNT);
}

static int build_student_result(const char *roll, char *out, size_t out_size) {
    char buf[16384];
    char *line;

    if (load_file("results.txt", buf, sizeof(buf)) < 0) {
        snprintf(out, out_size, "Could not open results file.");
        return 0;
    }

    line = strtok(buf, "\n");
    while (line) {
        char froll[MAX_FIELD];
        char name[MAX_FIELD];
        int m1, m2, m3, m4, m5;

        if (sscanf(line, "%63s %63s %d %d %d %d %d", froll, name, &m1, &m2, &m3, &m4, &m5) == 7) {
            if (strcmp(froll, roll) == 0) {
                float percentage = (m1 + m2 + m3 + m4 + m5) / 5.0f;
                snprintf(out, out_size,
                         "Record Found\nRoll: %s\nName: %s\nOS: %d Prob: %d TOA: %d CA: %d Psych: %d\nPercentage: %.2f%%",
                         froll, name, m1, m2, m3, m4, m5, percentage);
                return 1;
            }
        }
        line = strtok(NULL, "\n");
    }

    snprintf(out, out_size, "Record not found.");
    return 0;
}

static int write_results_record(const Request *request, char *message, size_t message_size) {
    char input[16384];
    char updated[16384];
    char line_copy[256];
    char *line;
    int found = 0;
    size_t used = 0;

    if (load_file("results.txt", input, sizeof(input)) < 0) {
        input[0] = '\0';
    }

    updated[0] = '\0';
    line = strtok(input, "\n");
    while (line) {
        char existing_roll[MAX_FIELD];
        strncpy(line_copy, line, sizeof(line_copy) - 1);
        line_copy[sizeof(line_copy) - 1] = '\0';
        if (sscanf(line_copy, "%63s", existing_roll) == 1 && strcmp(existing_roll, request->roll) == 0) {
            found = 1;
            if (request->action == 2) {
                used += (size_t)snprintf(updated + used, sizeof(updated) - used, "%s %s %d %d %d %d %d\n",
                                         request->roll, request->name, request->marks[0], request->marks[1],
                                         request->marks[2], request->marks[3], request->marks[4]);
            } else {
                used += (size_t)snprintf(updated + used, sizeof(updated) - used, "%s\n", line);
            }
        } else {
            used += (size_t)snprintf(updated + used, sizeof(updated) - used, "%s\n", line);
        }
        line = strtok(NULL, "\n");
    }

    if (request->action == 1) {
        if (found) {
            snprintf(message, message_size, "Add failed: roll already exists.");
            return 0;
        }
        snprintf(updated + used, sizeof(updated) - used, "%s %s %d %d %d %d %d\n",
                 request->roll, request->name, request->marks[0], request->marks[1],
                 request->marks[2], request->marks[3], request->marks[4]);
    } else if (request->action == 2) {
        if (!found) {
            snprintf(message, message_size, "Update failed: roll not found.");
            return 0;
        }
    } else {
        snprintf(message, message_size, "Invalid admin action.");
        return 0;
    }

    if (save_file("results.tmp", updated) < 0 || rename("results.tmp", "results.txt") != 0) {
        snprintf(message, message_size, "Failed to save results.");
        return 0;
    }
    return 1;
}

static int write_password_record(const Request *request, char *message, size_t message_size) {
    char input[8192];
    char updated[8192];
    char *line;
    int found = 0;
    size_t used = 0;

    if (load_file("passwords.txt", input, sizeof(input)) < 0) {
        input[0] = '\0';
    }

    updated[0] = '\0';
    line = strtok(input, "\n");
    while (line) {
        char roll[MAX_FIELD];
        char password[MAX_FIELD];
        if (sscanf(line, "%63s %63s", roll, password) == 2) {
            if (strcmp(roll, request->roll) == 0) {
                found = 1;
                used += (size_t)snprintf(updated + used, sizeof(updated) - used, "%s %s\n", request->roll, request->password);
            } else {
                used += (size_t)snprintf(updated + used, sizeof(updated) - used, "%s %s\n", roll, password);
            }
        }
        line = strtok(NULL, "\n");
    }

    if (!found && request->action == 1) {
        snprintf(updated + used, sizeof(updated) - used, "%s %s\n", request->roll, request->password);
    }

    if (save_file("passwords.tmp", updated) < 0 || rename("passwords.tmp", "passwords.txt") != 0) {
        snprintf(message, message_size, "Failed to save passwords.");
        return 0;
    }
    return 1;
}

int init_ipc(void) {
    key_t key = ftok("results.txt", 66);
    unsigned short values[3] = {1, 1, 1};
    union semun arg;

    if (key == -1) {
        return -1;
    }

    shm_id = shmget(key, sizeof(SharedState), IPC_CREAT | 0666);
    if (shm_id < 0) {
        return -1;
    }

    shared = (SharedState *)shmat(shm_id, NULL, 0);
    if (shared == (void *)-1) {
        shared = NULL;
        return -1;
    }

    shared->reader_count = 0;
    shared->total_reads = 0;
    shared->total_writes = 0;

    sem_id = semget(key, 3, IPC_CREAT | 0666);
    if (sem_id < 0) {
        return -1;
    }

    arg.array = values;
    if (semctl(sem_id, 0, SETALL, arg) < 0) {
        return -1;
    }

    return 0;
}

void cleanup_ipc(void) {
    if (shared) {
        shmdt(shared);
        shared = NULL;
    }
    if (shm_id >= 0) {
        shmctl(shm_id, IPC_RMID, NULL);
    }
    if (sem_id >= 0) {
        semctl(sem_id, 0, IPC_RMID);
    }
}

void run_student_view(Request *request, int request_id) {
    memset(request, 0, sizeof(*request));
    request->request_id = request_id;
    request->role = ROLE_STUDENT;

    printf("\n=== Student View ===\n");
    printf("Roll Number: ");
    scanf("%63s", request->roll);
    printf("Password: ");
    scanf("%63s", request->password);
}

void run_admin_view(Request *request, int request_id) {
    memset(request, 0, sizeof(*request));
    request->request_id = request_id;
    request->role = ROLE_ADMIN;

    printf("\n=== Admin View ===\n");
    printf("Username: ");
    scanf("%63s", request->username);
    printf("Password: ");
    scanf("%63s", request->admin_password);
    printf("Choose action (1=Add, 2=Update): ");
    scanf("%d", &request->action);
    printf("Student Roll Number: ");
    scanf("%63s", request->roll);
    printf("Student Name (use _ instead of spaces): ");
    scanf("%63s", request->name);
    printf("Student Password: ");
    scanf("%63s", request->password);
    printf("Enter marks (OS Prob TOA CA Psych): ");
    scanf("%d %d %d %d %d", &request->marks[0], &request->marks[1], &request->marks[2],
          &request->marks[3], &request->marks[4]);
}

void process_request(const Request *request, Response *response) {
    response->request_id = request->request_id;
    response->success = 0;
    response->message[0] = '\0';

    if (request->role == ROLE_STUDENT) {
        enter_reader();
        if (!check_line_credentials("passwords.txt", request->roll, request->password)) {
            snprintf(response->message, sizeof(response->message), "Invalid student credentials.");
            append_log("Student", request->request_id, "Invalid login");
        } else {
            response->success = build_student_result(request->roll, response->message, sizeof(response->message));
            if (response->success) {
                shared->total_reads++;
                append_log("Student", request->request_id, "Read result success");
            } else {
                append_log("Student", request->request_id, "Read result failed");
            }
        }
        exit_reader();
        return;
    }

    if (request->role == ROLE_ADMIN) {
        if (!check_line_credentials("admins.txt", request->username, request->admin_password)) {
            snprintf(response->message, sizeof(response->message), "Invalid admin credentials.");
            append_log("Admin", request->request_id, "Invalid login");
            return;
        }

        lock_sem(SEM_QUEUE);
        lock_sem(SEM_RESOURCE);
        unlock_sem(SEM_QUEUE);

        if (write_results_record(request, response->message, sizeof(response->message)) &&
            write_password_record(request, response->message, sizeof(response->message))) {
            response->success = 1;
            shared->total_writes++;
            snprintf(response->message, sizeof(response->message), "Record saved successfully.");
            append_log("Admin", request->request_id, "Write success");
        } else {
            append_log("Admin", request->request_id, "Write failed");
        }

        unlock_sem(SEM_RESOURCE);
        return;
    }

    snprintf(response->message, sizeof(response->message), "Invalid role request.");
}

