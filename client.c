/**
 * nonstop_networking
 * CS 241 - Fall 2020
 */
#include "common.h"
#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

char **parse_args(int argc, char **argv);
verb check_args(char **args);
static struct stat st;

//method to connect to the server
int connect_to_server(const char *host, const char *port) {
    struct addrinfo curr, *res;
    memset(&curr, 0, sizeof(curr));
    //desired address fam = Internet Protocol v4 address
    curr.ai_family = AF_INET;
    //preferred socket type = TCP
    curr.ai_socktype = SOCK_STREAM;
    //allocating and intitializing addrinfo structures
    if (getaddrinfo(host, port, &curr, &res) != 0) {
        //if it didn't succeed
        gai_strerror(getaddrinfo(host, port, &curr, &res));
        exit(1);
    }

    //opening socket
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    //if cannot open the socket
    if (sfd == -1 || connect(sfd, res->ai_addr, res->ai_addrlen) == -1) {
        perror(NULL);
        exit(1);
    }
    freeaddrinfo(res);
    return sfd;
}



// void server_response_helper(char* server_file, char* verb_type, char* server_response) {
//     strcat(server_response, verb_type);
//     strcat(server_response, server_file);
//     strcat(server_response, "\n");
// }

void error_check_helper(int sfd) {
    //temp string to check status from the socket
    char temp_buf[7];
    memset(temp_buf, '\0', 7);
    //reading until new line
    read_from_socket(sfd, temp_buf, 0, 0);
    
    int status_ok = 0;
    
    int status_error = 0;
    
    if (strcmp(temp_buf, "OK\n") == 0) {
        //if it was successful
        status_ok = 1;
    } else if (strcmp(temp_buf, "ERROR\n") == 0) {
        //if not then error
        status_error = 1;
    }
    if (status_ok) {
        print_success();
    } else if (status_error) {
        //temp string to get error msg
        char msg[1024];
        read_from_socket(sfd, msg, 0, 0);
        print_error_message(msg);
    } else {
        print_invalid_response();
    }
}
void solve_get(int socket_file_descriptor, char * local, char * remote) {
    int size = strlen(remote) + 6;
    //new string to store server response
    char* server_response = calloc(1, size);
    char* get_str = "GET ";
    // String format = GET remote\n 
    strcat(server_response, get_str);
    strcat(server_response, remote);
    strcat(server_response, "\n");
    //writing server response data to sockets
    write_to_socket(socket_file_descriptor, server_response, size - 1);
    free(server_response);
    //clsoing socket for further transmissions
    shutdown(socket_file_descriptor, SHUT_WR);
    //temp string to store status
    char status[7];
    memset(status, '\0', 7);
    //reading status from the socket
    read_from_socket(socket_file_descriptor, status, 0 ,0);
    int status_ok = 0;
    int status_error = 0;
    if (strcmp(status, "OK\n") == 0) {
        status_ok = 1;
    } else if (strcmp(status, "ERROR\n") == 0) {
        status_error = 1;
    }
    if (status_ok) {
        //status okay
        status_ok = 0;
        char x[sizeof(size_t)];
        //reading from socket and storing in x
        read_from_socket(socket_file_descriptor, x, sizeof(size_t) , 1);
        //if read was unsuccessful
        if ((int) *((size_t *) x) == -1) {
            print_invalid_response();
        }
        //opening the file
        int fd = open(local, O_CREAT | O_RDWR | O_TRUNC, S_IRWXU|S_IRWXG|S_IRWXO);
        int red = 1;
        //temp string to store data
        char bin[1024];
        while (red > 0) {
            //while it still has data
            red = read_from_socket(socket_file_descriptor, bin, 1024, 1);
            write_to_socket(fd, bin, red);
        }
        fstat(fd, &st);
        //checking for too much and too little data
        check_data_error(st.st_size, *((size_t *) x));
        close(fd);
    } else if (status_error) {
        status_error = 0;
        //string to store message for error
        char msg[1024];
        memset(msg, '\0', 1024);
        //reading message from the socket
        read_from_socket(socket_file_descriptor, msg, 0, 0);
        print_error_message(msg);
    } else {
        print_invalid_response();
    }
}

void solve_put(int socket_file_descriptor, char * local, char * remote) {
    //opening local file
    int fd = open(local, O_RDONLY);
    //checking if open was successful
    if (fd == -1) {
        exit(1);
    }
    int remoteFileSize = strlen(remote);
    char* server_response = calloc(1, remoteFileSize + 6);
    // String format = PUT remote\n 
    strcat(server_response, "PUT ");
    strcat(server_response, remote);
    strcat(server_response, "\n");
    //writing server_response to socket
    write_to_socket(socket_file_descriptor, server_response, remoteFileSize + 5);
    //obtaining fd info
    fstat(fd, &st);
    //temp buffer to copy st data
    char buff[8];
    memcpy(buff, &(st.st_size), sizeof(size_t));
    //writing data to socket
    write_to_socket(socket_file_descriptor, buff, sizeof(size_t));
    //string to store file data
    char file_data[1024];
    memset(file_data, '\0', 1024);
    int red = 1;
    while (red > 0) {
        //reading data while it exists and writing it to the socket
        red = read_from_socket(fd, file_data, 1024, 1);
        write_to_socket(socket_file_descriptor, file_data, red);
    }
    free(server_response);
    //closing socket for further transmissions
    shutdown(socket_file_descriptor, SHUT_WR);
    //checking for errors
    error_check_helper(socket_file_descriptor);
}

void solve_delete(int socket_file_descriptor, char * remote) {
    //temp string to store server response
    char* server_response = calloc(1, strlen(remote) + 9);
    // String format = DELETE remote\n 
    strcat(server_response, "DELETE ");
    strcat(server_response, remote);
    strcat(server_response, "\n");
    //writing to socket
    write(socket_file_descriptor, server_response, strlen(remote) + 9);
    free(server_response);
    //closing socket for further transmissions
    shutdown(socket_file_descriptor, SHUT_WR);
    //temp string to store response
    char response[1000];
    int len = read(socket_file_descriptor, response, 999); 
    //making sure to end it with terminating byte
    response[len] = '\0';
    //temp string to store result
    char resultbuf [3];
    memcpy(resultbuf, response, 2);
    resultbuf[2] = '\0';
    //if not okay then error
    if (strcmp(resultbuf, "OK") != 0) {
        print_error_message(response);
        exit(1);
    }
    //else success
    print_success();
}

void solve_list(int socket_file_descriptor) {
    char* list = "LIST\n";
    //don't need remote for this one.
    //+1 for terminating byte
    //using calloc to avoid manually setting mem
    char* server_response = calloc(1, strlen(list) + 1);
    memcpy(server_response, list, strlen(list));
    //writing response to the socket
    write_to_socket(socket_file_descriptor, server_response, strlen(list));
    free(server_response);
    //closing socket for further transmissions
    shutdown(socket_file_descriptor, SHUT_WR);
    //temporary string to store response
    char response_buff[1000];
    //keeping track of the bytes read
    int byte_count = read(socket_file_descriptor, response_buff, 999);
    size_t response_size = 0;
    void * start = response_buff + 3;
    //response_size would be the number of bytes that need to be read
    memcpy(&response_size, start, 8);
    int red = 1;
    while (1) { 
        if (byte_count < (int) response_size + 11) {
            break;
        }
        int red =  read(socket_file_descriptor, response_buff + byte_count, 1);
        if (red == 0) {
            break;
        }
        byte_count += red; 
    }

    if (red == 0) {
        print_too_little_data();
    }
    
    if((size_t)(byte_count - 11) > response_size) {
        print_received_too_much_data();
    }
    //writing result
    write(1, response_buff + 11, response_size);
}
int main(int argc, char **argv) {
    // Good luck!
    char** args = parse_args(argc, argv);
    if (!args) {
        print_client_usage();
		exit(1);
    }
    verb request = check_args(args);
    int sfd = connect_to_server(args[0], args[1]);
    char * remote = args[3];
    char * local = args[4];
    
    if (request == GET) {
        solve_get(sfd, local, remote);
    } else if (request == PUT) {
        solve_put(sfd, local, remote);
    } else if (request == DELETE) {
        solve_delete(sfd, remote);
    } else if (request == LIST) {
        solve_list(sfd);
    }
    free(args);
}

/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
    if (argc < 3) {
        return NULL;
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char **args = calloc(1, 6 * sizeof(char *));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char *temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    char *command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL && args[4] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}