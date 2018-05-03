#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG
#define LOGGING

#ifdef DEBUG
#define DEBUG_TEST 1
#else
#define DEBUG_TEST 0
#endif

/*
#define DEBUG_PRINT(fmt, ...) \
            do { if (DEBUG_TEST) fprintf(stderr, fmt, __VA_ARGS__); } while (0)
*/

#define DEBUG_PRINT(...) \
            do { if (DEBUG_TEST) fprintf(stderr, __VA_ARGS__); } while (0)

#endif