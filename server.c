/**
 * nonstop_networking
 * CS 241 - Fall 2020
 */
#include "vector.h"
#include "dictionary.h"
#include "format.h"
#include "common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>


static dictionary * dict;
static struct stat st;
typedef struct dirent dirent;
static struct epoll_event* event;
// static dictionary *client_data;


typedef enum ERROR {
    NO_FILE = 0,
    INVALID_RESPONSE = 1,
} ERROR;


typedef struct client_status {
    char* header;
    char* buffer;
    verb action;
    //0 - parsing, 1 - executing, 2- error
    int current_activity;
    ERROR err;
    ssize_t header_size;
    char* file_path;
    int curr_file_desc;
    size_t put_size;
} client_status;

int open_server(char* port) {
    struct addrinfo curr, *res;
    memset(&curr, 0, sizeof(curr));
    curr.ai_family = AF_INET;
    curr.ai_socktype = SOCK_STREAM;
    curr.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, port, &curr, &res) != 0) {
        gai_strerror(getaddrinfo(NULL, port, &curr, &res));
        exit(1);
    }

    int sfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    int optval = 1;

    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    if (bind(sfd, res->ai_addr, res->ai_addrlen) < 0 || listen(sfd, 1000) == 1) {
        perror(NULL);
        exit(1);
    }

    freeaddrinfo(res);
    return sfd;
}

void epoll_mod(int client_fd, int epoll_fd) {
	struct epoll_event eve_temp;
  	eve_temp.events = EPOLLOUT;
  	eve_temp.data.fd = client_fd;
	epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &eve_temp);
}

void handle_error(client_status* curr_status) {
    write_to_socket(curr_status -> curr_file_desc, "ERROR\n", 6);
    if (curr_status -> err == INVALID_RESPONSE) {
        print_invalid_response();
    } else if (curr_status -> err == NO_FILE) {
        perror(err_no_such_file);
    }
}

void clean_up(client_status* status) {
    close(status-> curr_file_desc);
    dictionary_remove(dict, &(status-> curr_file_desc));
    shutdown(status-> curr_file_desc, SHUT_WR);
    free(status -> header);
    free(status -> buffer);
    free(status -> file_path);
    free(status);
}

void handle_get(client_status* status) {
    int file_get = 0;
    int temp = open(status-> file_path, O_RDONLY);
    if (temp == -1) {
        status-> current_activity = 2;
        status-> err = NO_FILE;
    } else {
        file_get = temp;
    }

    if (status -> current_activity == 2) {
        handle_error(status);
    }

    write_to_socket(status-> curr_file_desc, "OK\n", 3);

    if (fstat(file_get, &st) == -1) {
        exit(1);
    }

    char temp_buffer[sizeof(size_t)];
    memcpy(temp_buffer, &(st.st_size), sizeof(size_t));
    write_to_socket(status-> curr_file_desc, temp_buffer, sizeof(size_t));
    int red = 1;
    while (red > 0) {
        red = read_from_socket(file_get, status-> buffer, 1024, 1);
        write_to_socket(status-> curr_file_desc, status-> buffer, red);
    }

    clean_up(status);
}

void handle_put(client_status* status) {

    int file_put = 0;
    int openn = open(status-> file_path, O_RDWR | O_TRUNC | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
    if (openn == -1) {
        exit(1);
    } else {
        file_put = openn;
    }

    write_to_socket(status-> curr_file_desc, "OK\n", 3);

    bool zero_put_size = (status-> put_size == 0);

    if (zero_put_size) {
        char file_size[sizeof(size_t)];
        memset(file_size, '\0', sizeof(size_t));
        read_from_socket(status-> curr_file_desc, file_size, sizeof(size_t), 1);
        size_t* s_file_size = (size_t*) file_size;
        status-> put_size = *(s_file_size);
    }

    int red = 1;
    while (red > 0) {
        red = read_from_socket(status-> curr_file_desc, status-> buffer, status-> put_size, 1);
        write_to_socket(file_put, status-> buffer, red);
    }

    clean_up(status);
}

void handle_list(client_status* status) {
    DIR* cwd = opendir(".");
    dirent* curr;

    write_to_socket(status-> curr_file_desc, "OK\n", 3);

    off_t total_size = 0;

    while ((curr = readdir(cwd))) {
        if (!strcmp(curr -> d_name, ".")) {
            continue;
        }
        if (strcmp(curr -> d_name, "..")) {
            continue;
        }
        stat(curr -> d_name, &st);
        total_size += st.st_size;
        memset(&st, 0, sizeof(struct stat));
        strcat(status-> buffer, curr -> d_name);
        strcat(status-> buffer, "\n");
    }

    status-> buffer[strlen(status-> buffer) - 1] = '\0';
    char sz[sizeof(off_t)];
    memcpy(sz, &total_size, sizeof(off_t));
    write_to_socket(status-> curr_file_desc, sz, sizeof(off_t));
    write_to_socket(status-> curr_file_desc, status-> buffer, strlen(status-> buffer));

    clean_up(status);
}

int check_verb(char* verb_) {
    if (!strcmp(verb_, "GET")) {
        return 0;
    } else if (!strcmp(verb_, "PUT")) {
        return 1;
    } else if (!strcmp(verb_, "LIST")) {
        return 2;
    } else if (!strcmp(verb_, "DELETE")) {
        return 3;
    } else {
        return 4;
    }
}
client_status * handle_from_client_fd(client_status* client_info, ssize_t read, int fd, int epoll_fd) {
    if (read == 1024) {
        client_info -> current_activity = -1;
        epoll_mod(fd, epoll_fd);
        return client_info;
    }
    client_info -> header_size += read;
    if (client_info -> header[strlen(client_info -> header) - 1] == '\n') {
        char* verb_ = strtok(client_info -> header, " ");
        char* temp_file = strtok(NULL, " ");
        client_info -> file_path = strdup(temp_file);
        size_t len = strlen(client_info -> file_path);
        client_info -> file_path[len - 1] = '\0';
        bool activity = false;
        int vb = check_verb(verb_);
        switch(vb) {
            case 0 :
                activity = true;
                client_info -> action = GET;
                strcpy(client_info->file_path, client_info -> header + 4);
                client_info->file_path[len - 1] = '\0';
                break;
            case 1 :
                activity = true;
                client_info -> action = PUT;
                strcpy(client_info->file_path, client_info -> header + 4);
                client_info->file_path[len - 1] = '\0';
                break;
            case 2 :
                activity = true;
                client_info -> action = LIST;
                break;
            case 3 :
                activity = true;
                client_info -> action = DELETE;
                strcpy(client_info->file_path, client_info -> header + 7);
                client_info->file_path[len - 1] = '\0';
            case 4 :
                client_info -> current_activity = 2;
                client_info -> err = INVALID_RESPONSE;
                epoll_mod(fd, epoll_fd);
                return client_info;
        }
        if (activity) {
            client_info -> current_activity = 1;
        }
    }
    epoll_mod(fd, epoll_fd);
    return client_info;
}

client_status* init_client_status(client_status* cs, int conn_socket) {
    cs -> header = calloc(1, 1024);
    cs -> buffer = calloc (1, 1024);
    cs -> put_size = 0;
    cs -> current_activity = 0;
    cs -> file_path = NULL;
    cs -> header_size = 0;
    cs -> curr_file_desc = conn_socket;
    return cs;
}
void process_clients(int epfd, int server_socket) {
    while (true) {
        struct epoll_event client_event[1000];
        int num_conn = epoll_wait(epfd, client_event, 1000, 10000);
        if (num_conn == -1) {
            exit(1);
        }
        if (num_conn == 0) {
            continue;
        }
        for (int i = 0; i < num_conn; i++) {
            int fd = client_event[i].data.fd;

            if (fd == server_socket) {

                int conn_socket = accept(server_socket, NULL, NULL);
                if (conn_socket < 0) {
                    perror("accepted");
	                exit(1);
                }
                client_status* cs = calloc(1, sizeof(client_status));

                cs = init_client_status(cs, conn_socket);

                dictionary_set(dict, &conn_socket, cs);
                event -> events = EPOLLIN | EPOLLET;
                event -> data.fd = conn_socket;
                epoll_ctl(epfd, EPOLL_CTL_ADD, conn_socket, event);
            } else {
                client_status * client_info = (client_status*) dictionary_get(dict, &fd);
                if (client_info -> current_activity == 0) {
                    char* buff = client_info -> header + client_info -> header_size;
                    ssize_t read = read_from_socket(fd, buff, 0, 0);
                    client_info = handle_from_client_fd(client_info, read, fd, epfd);
                } else if (client_info -> current_activity == 1) {
                    switch(client_info -> action) {
                        case GET :
                            handle_get(client_info);
                            break;
                        case LIST :
                            handle_list(client_info);
                            break;
                        case PUT :
                            handle_put(client_info);
                            break;
                        case DELETE :
                            break;
                        case V_UNKNOWN :
                            handle_error(client_info);
                            break;
                    }
                } else if (client_info ->current_activity == 2) {
                    handle_error(client_info);
                }
            }
        }
    }
}

void sig_int_handler() {
    printf("SIGINT\n");
    dictionary_destroy(dict);
    free(event);
    exit(1);
}

void sig_pipe() {

}

int main(int argc, char **argv) {
    // good luck!

    signal(SIGPIPE, sig_pipe);
    if (argc != 2) {
        print_server_usage();
        exit(1);
    }

    struct sigaction sig_action;
    memset(&sig_action, '\0', sizeof(sig_action));
    sig_action.sa_handler = sig_int_handler;

    sigaction(SIGINT, &sig_action, NULL);


    dict = int_to_shallow_dictionary_create();

    int socket = open_server(argv[1]);
    char temp[7];
    memset(temp, '\0', 7);
    strcat(temp, "XXXXXX");
    char* temp_directory = strdup(mkdtemp(temp));
    chdir(temp_directory);
    print_temp_directory(temp_directory);

    int epoll_fd = epoll_create(100);
    event = calloc(1, sizeof(struct epoll_event));
    event -> events = EPOLLIN;
    event -> data.fd = socket;

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, socket, event);

    process_clients(epoll_fd, socket);
}
