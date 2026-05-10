#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "reader_writer.h"

typedef struct {
    pid_t pid;
    int response_fd;
    int request_id;
    Role role;
} WorkerHandle;

static ssize_t write_all(int fd, const void *buf, size_t count) {
    const char *ptr = (const char *)buf;
    size_t sent = 0;
    while (sent < count) {
        ssize_t written = write(fd, ptr + sent, count - sent);
        if (written <= 0) {
            return -1;
        }
        sent += (size_t)written;
    }
    return (ssize_t)sent;
}

static ssize_t read_all(int fd, void *buf, size_t count) {
    char *ptr = (char *)buf;
    size_t received = 0;
    while (received < count) {
        ssize_t n = read(fd, ptr + received, count - received);
        if (n <= 0) {
            return -1;
        }
        received += (size_t)n;
    }
    return (ssize_t)received;
}

static void print_banner(void) {
    printf("\n=============================================\n");
    printf("University Exam Result Portal (Process Model)\n");
    printf("=============================================\n");
    printf("Uses fork() + pipe() + read()/write() + shmget() + semop()\n");
}

int main(void) {
    int students;
    int admins;
    int total;
    int i;
    Request *requests;
    WorkerHandle *workers;

    if (init_ipc() != 0) {
        printf("Failed to initialize shared memory/semaphores.\n");
        return 1;
    }

    print_banner();
    printf("\nHow many student requests? ");
    scanf("%d", &students);
    printf("How many admin requests? ");
    scanf("%d", &admins);

    if (students < 0 || admins < 0) {
        printf("Counts must be non-negative.\n");
        cleanup_ipc();
        return 1;
    }

    total = students + admins;
    if (total == 0) {
        printf("No requests entered. Exiting.\n");
        cleanup_ipc();
        return 0;
    }

    requests = (Request *)calloc((size_t)total, sizeof(Request));
    workers = (WorkerHandle *)calloc((size_t)total, sizeof(WorkerHandle));
    if (!requests || !workers) {
        printf("Memory allocation failed.\n");
        free(requests);
        free(workers);
        cleanup_ipc();
        return 1;
    }

    for (i = 0; i < students; i++) {
        run_student_view(&requests[i], i + 1);
    }
    for (i = 0; i < admins; i++) {
        run_admin_view(&requests[students + i], students + i + 1);
    }

    printf("\nProcessing all requests using worker processes...\n");

    for (i = 0; i < total; i++) {
        int req_pipe[2];
        int resp_pipe[2];
        pid_t pid;

        if (pipe(req_pipe) != 0 || pipe(resp_pipe) != 0) {
            printf("Pipe creation failed for request %d.\n", i + 1);
            continue;
        }

        pid = fork();
        if (pid == 0) {
            Request req;
            Response resp;
            close(req_pipe[1]);
            close(resp_pipe[0]);

            if (read_all(req_pipe[0], &req, sizeof(req)) == sizeof(req)) {
                process_request(&req, &resp);
                write_all(resp_pipe[1], &resp, sizeof(resp));
            }

            close(req_pipe[0]);
            close(resp_pipe[1]);
            _exit(0);
        }

        close(req_pipe[0]);
        close(resp_pipe[1]);
        write_all(req_pipe[1], &requests[i], sizeof(Request));
        close(req_pipe[1]);

        workers[i].pid = pid;
        workers[i].response_fd = resp_pipe[0];
        workers[i].request_id = requests[i].request_id;
        workers[i].role = requests[i].role;
    }

    for (i = 0; i < total; i++) {
        Response resp;
        int status;
        const char *role_text = workers[i].role == ROLE_STUDENT ? "Student" : "Admin";

        if (read_all(workers[i].response_fd, &resp, sizeof(resp)) == sizeof(resp)) {
            printf("\n[%s Request %d] %s\n", role_text, resp.request_id, resp.message);
        } else {
            printf("\n[%s Request %d] Failed to read worker response.\n", role_text, workers[i].request_id);
        }

        close(workers[i].response_fd);
        waitpid(workers[i].pid, &status, 0);
    }

    free(requests);
    free(workers);
    cleanup_ipc();
    printf("\nAll requests completed.\n");
    return 0;
}

