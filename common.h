/**
 * nonstop_networking
 * CS 241 - Fall 2020
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include "format.h"
#define LOG(...)                      \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
    } while (0);

typedef enum { GET, PUT, DELETE, LIST, V_UNKNOWN } verb;

int write_to_socket(int socket, char* buff, size_t count);
int read_from_socket(int socket, char* buff, size_t count, int full);
void check_data_error(size_t stsize, size_t msg);
int data_error_check(size_t stsize, size_t msg);