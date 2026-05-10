#include <gtk/gtk.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "reader_writer.h"

static GtkWidget *student_roll_entry;
static GtkWidget *student_password_entry;
static GtkWidget *student_output_label;

static GtkWidget *admin_user_entry;
static GtkWidget *admin_password_entry;
static GtkWidget *admin_action_entry;
static GtkWidget *admin_roll_entry;
static GtkWidget *admin_name_entry;
static GtkWidget *admin_student_password_entry;
static GtkWidget *admin_marks_entries[5];
static GtkWidget *admin_output_label;

static int request_counter = 1;

static ssize_t write_all(int fd, const void *buf, size_t count) {
    const char *ptr = (const char *)buf;
    size_t sent = 0;
    while (sent < count) {
        ssize_t n = write(fd, ptr + sent, count - sent);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

static ssize_t read_all(int fd, void *buf, size_t count) {
    char *ptr = (char *)buf;
    size_t got = 0;
    while (got < count) {
        ssize_t n = read(fd, ptr + got, count - got);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return (ssize_t)got;
}

static int send_request_to_worker(const Request *request, Response *response) {
    int req_pipe[2];
    int resp_pipe[2];
    pid_t pid;
    int status;

    if (pipe(req_pipe) != 0 || pipe(resp_pipe) != 0) {
        return 0;
    }

    pid = fork();
    if (pid < 0) {
        return 0;
    }

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
    write_all(req_pipe[1], request, sizeof(*request));
    close(req_pipe[1]);

    if (read_all(resp_pipe[0], response, sizeof(*response)) != sizeof(*response)) {
        close(resp_pipe[0]);
        waitpid(pid, &status, 0);
        return 0;
    }

    close(resp_pipe[0]);
    waitpid(pid, &status, 0);
    return 1;
}

static void on_student_submit(GtkButton *button, gpointer user_data) {
    Request req;
    Response resp;
    const char *roll;
    const char *password;

    (void)button;
    (void)user_data;

    memset(&req, 0, sizeof(req));
    req.request_id = request_counter++;
    req.role = ROLE_STUDENT;
    req.action = 0;

    roll = gtk_entry_get_text(GTK_ENTRY(student_roll_entry));
    password = gtk_entry_get_text(GTK_ENTRY(student_password_entry));
    g_strlcpy(req.roll, roll, sizeof(req.roll));
    g_strlcpy(req.password, password, sizeof(req.password));

    if (req.roll[0] == '\0' || req.password[0] == '\0') {
        gtk_label_set_text(GTK_LABEL(student_output_label), "Please enter roll and password.");
        return;
    }

    if (!send_request_to_worker(&req, &resp)) {
        gtk_label_set_text(GTK_LABEL(student_output_label), "Failed to process student request.");
        return;
    }

    gtk_label_set_text(GTK_LABEL(student_output_label), resp.message);
}

static int parse_mark(GtkWidget *entry) {
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
    return atoi(text);
}

static void on_admin_submit(GtkButton *button, gpointer user_data) {
    Request req;
    Response resp;
    const char *username;
    const char *admin_password;
    const char *action_text;
    const char *roll;
    const char *name;
    const char *student_password;
    int i;

    (void)button;
    (void)user_data;

    memset(&req, 0, sizeof(req));
    req.request_id = request_counter++;
    req.role = ROLE_ADMIN;

    username = gtk_entry_get_text(GTK_ENTRY(admin_user_entry));
    admin_password = gtk_entry_get_text(GTK_ENTRY(admin_password_entry));
    action_text = gtk_entry_get_text(GTK_ENTRY(admin_action_entry));
    roll = gtk_entry_get_text(GTK_ENTRY(admin_roll_entry));
    name = gtk_entry_get_text(GTK_ENTRY(admin_name_entry));
    student_password = gtk_entry_get_text(GTK_ENTRY(admin_student_password_entry));

    g_strlcpy(req.username, username, sizeof(req.username));
    g_strlcpy(req.admin_password, admin_password, sizeof(req.admin_password));
    g_strlcpy(req.roll, roll, sizeof(req.roll));
    g_strlcpy(req.name, name, sizeof(req.name));
    g_strlcpy(req.password, student_password, sizeof(req.password));
    req.action = atoi(action_text);

    for (i = 0; i < 5; i++) {
        req.marks[i] = parse_mark(admin_marks_entries[i]);
    }

    if (req.username[0] == '\0' || req.admin_password[0] == '\0' ||
        req.roll[0] == '\0' || req.name[0] == '\0' || req.password[0] == '\0') {
        gtk_label_set_text(GTK_LABEL(admin_output_label), "Please fill all admin fields.");
        return;
    }

    if (!(req.action == 1 || req.action == 2)) {
        gtk_label_set_text(GTK_LABEL(admin_output_label), "Action must be 1 (Add) or 2 (Update).");
        return;
    }

    if (!send_request_to_worker(&req, &resp)) {
        gtk_label_set_text(GTK_LABEL(admin_output_label), "Failed to process admin request.");
        return;
    }

    gtk_label_set_text(GTK_LABEL(admin_output_label), resp.message);
}

static GtkWidget *create_labeled_entry(GtkWidget *grid, const char *label, int row, gboolean hidden) {
    GtkWidget *lbl = gtk_label_new(label);
    GtkWidget *entry = gtk_entry_new();

    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
    if (hidden) {
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    }
    return entry;
}

static GtkWidget *build_student_tab(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *button = gtk_button_new_with_label("View Result");

    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    student_roll_entry = create_labeled_entry(grid, "Roll Number", 0, FALSE);
    student_password_entry = create_labeled_entry(grid, "Password", 1, TRUE);

    student_output_label = gtk_label_new("Student output will appear here.");
    gtk_label_set_xalign(GTK_LABEL(student_output_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(student_output_label), TRUE);

    g_signal_connect(button, "clicked", G_CALLBACK(on_student_submit), NULL);

    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), student_output_label, TRUE, TRUE, 0);

    return box;
}

static GtkWidget *build_admin_tab(void) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget *grid = gtk_grid_new();
    GtkWidget *button = gtk_button_new_with_label("Save Record");

    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    admin_user_entry = create_labeled_entry(grid, "Admin Username", 0, FALSE);
    admin_password_entry = create_labeled_entry(grid, "Admin Password", 1, TRUE);
    admin_action_entry = create_labeled_entry(grid, "Action (1 Add, 2 Update)", 2, FALSE);
    admin_roll_entry = create_labeled_entry(grid, "Student Roll", 3, FALSE);
    admin_name_entry = create_labeled_entry(grid, "Student Name", 4, FALSE);
    admin_student_password_entry = create_labeled_entry(grid, "Student Password", 5, TRUE);
    admin_marks_entries[0] = create_labeled_entry(grid, "OS Marks", 6, FALSE);
    admin_marks_entries[1] = create_labeled_entry(grid, "Prob Marks", 7, FALSE);
    admin_marks_entries[2] = create_labeled_entry(grid, "TOA Marks", 8, FALSE);
    admin_marks_entries[3] = create_labeled_entry(grid, "CA Marks", 9, FALSE);
    admin_marks_entries[4] = create_labeled_entry(grid, "Psych Marks", 10, FALSE);

    admin_output_label = gtk_label_new("Admin output will appear here.");
    gtk_label_set_xalign(GTK_LABEL(admin_output_label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(admin_output_label), TRUE);

    g_signal_connect(button, "clicked", G_CALLBACK(on_admin_submit), NULL);

    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), admin_output_label, TRUE, TRUE, 0);

    return box;
}

static void on_destroy(GtkWidget *widget, gpointer user_data) {
    (void)widget;
    (void)user_data;
    cleanup_ipc();
    gtk_main_quit();
}

int main(int argc, char **argv) {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *student_tab;
    GtkWidget *admin_tab;

    gtk_init(&argc, &argv);

    if (init_ipc() != 0) {
        g_printerr("Failed to initialize shared memory/semaphores.\n");
        return 1;
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "University Exam Result Portal");
    gtk_window_set_default_size(GTK_WINDOW(window), 680, 500);
    gtk_container_set_border_width(GTK_CONTAINER(window), 12);

    notebook = gtk_notebook_new();
    student_tab = build_student_tab();
    admin_tab = build_admin_tab();

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), student_tab, gtk_label_new("Student View"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), admin_tab, gtk_label_new("Admin View"));

    gtk_container_add(GTK_CONTAINER(window), notebook);
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);

    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
