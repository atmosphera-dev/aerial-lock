#ifndef AERIAL_TEST_CHECK_H
#define AERIAL_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>

/* Test setup and verification must execute in NDEBUG builds too. */
#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
        abort(); \
    } \
} while (0)

#endif
