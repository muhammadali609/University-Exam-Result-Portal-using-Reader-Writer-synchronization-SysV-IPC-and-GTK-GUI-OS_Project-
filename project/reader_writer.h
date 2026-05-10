#ifndef READER_WRITER_H
#define READER_WRITER_H

#define MAX_TEXT 1024
#define MAX_FIELD 64

typedef enum {
    ROLE_STUDENT = 1,
    ROLE_ADMIN = 2
} Role;

typedef struct {
    int reader_count;
    int total_reads;
    int total_writes;
} SharedState;

typedef struct {
    int request_id;
    Role role;
    int action;
    char roll[MAX_FIELD];
    char password[MAX_FIELD];
    char username[MAX_FIELD];
    char admin_password[MAX_FIELD];
    char name[MAX_FIELD];
    int marks[5];
} Request;

typedef struct {
    int request_id;
    int success;
    char message[MAX_TEXT];
} Response;

int init_ipc(void);
void cleanup_ipc(void);
void run_student_view(Request *request, int request_id);
void run_admin_view(Request *request, int request_id);
void process_request(const Request *request, Response *response);

#endif
