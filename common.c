/**
 * nonstop_networking
 * CS 241 - Fall 2020
 */
#include "common.h"
#include <string.h>

int write_to_socket(int socket, char* buff, size_t count) {
    size_t idx = 0;
    ssize_t wrote = 1;
    while (1) {
        wrote = write(socket, buff + idx, count - idx);
        if (idx >= count) {
            break;
        }
        if (wrote == 0) {
            break;
        }
        if (wrote == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                return -1;
            }
        }
        idx += wrote;
    }
    return idx;
}

int read_from_socket(int socket, char* buff, size_t count, int full) {
    size_t idx = 0;
    ssize_t ct = 1;
    //for full reading
    if (full == 1) {
        while (1) {
            if (idx >= count) {
                break;
            }
            ct = read(socket, buff + idx, count - idx);
            if (ct == 0) {
                break;
            }
            if (ct == -1) {
                if (errno == EINTR) {
                    continue;
                } else {
                    return -1;
                }
            }
        idx += ct;
        }
    } else if (full == 2) {
        while (1) {
            if (idx >= count) {
                break;
            }
            ssize_t ct = read(socket, buff + idx, 1);
            if (ct == 0 || buff[strlen(buff) - 1] == '\n') {
                break;
            }
            if (ct == -1){
                if (errno == EINTR) {
                    continue;
                } else {
                    perror("error in reading");
                }
            }
		idx += ct;
	    }
    } else {
        //only until new line
        while (1) {
            ct = read(socket, buff + idx, 1);
            if (buff[idx] == '\n') {
                break;
            }
            idx += ct;
        }
    }
    return idx;
}

int data_error_check(size_t stsize, size_t msg) {
    if (stsize > msg) {
        print_received_too_much_data();
        return 1;
    } else if (stsize < msg) {
        print_too_little_data();
        return 1;
    }

    return 0;
}
void check_data_error(size_t stsize, size_t msg) {
    if (stsize > msg) {
        print_received_too_much_data();
    } else if (stsize < msg) {
        print_too_little_data();
    }
}