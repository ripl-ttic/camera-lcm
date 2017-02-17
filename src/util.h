#ifndef UTIL_H
#define UTIL_H

#include <errno.h>

# ifndef UTIL_DBG
#   error "UTIL_DBG must be defined!"
# endif

# ifndef UTIL_LOG
#   error "UTIL_LOG must be defined!"
# endif

# if defined(UTIL_DBG) && UTIL_DBG
#   define DBG(fmt, ...) do { \
    if (pausing) printf("\n"); \
    printf("["__FILE__":%d] DBG: ", __LINE__); \
    printf(fmt, ##__VA_ARGS__); \
    fflush(stdout); \
} while (0)
# else
#   define DBG(...)
# endif

// Pausing in the output? (with '\r')
static gboolean pausing = FALSE;

/**
 * ERR:
 * @fmt: A %printf()-style format string.
 * @...: The arguments for @fmt.
 *
 * Print an error, prepended with the file and line number where the error
 * occurred.
 */
# define ERR(fmt, ...) do { \
    if (pausing) printf("\n"); \
    fprintf(stderr, "Error ["__FILE__":%d]: ", __LINE__); \
    fprintf(stderr, fmt, ##__VA_ARGS__); \
    fflush(stderr); \
    pausing = FALSE; \
} while (0)

/**
 * WRN:
 * @fmt: A %printf()-style format string.
 * @...: The arguments for @fmt.
 *
 * Print a warning, prepended with the file and line number where the warning
 * occurred.
 */
# define WRN(fmt, ...) do { \
    if (pausing) printf("\n"); \
    printf("Warning ["__FILE__":%d]: ", __LINE__); \
    printf(fmt, ##__VA_ARGS__); \
    fflush(stdout); \
    pausing = FALSE; \
} while (0)

/**
 * VRB_R:
 * @fmt: A %printf()-style format string.
 * @...: The arguments for @fmt.
 *
 * Same as %VRB(), but does not put a newline at the end of the print.  The
 * functions in this file are smart about it, though, because they'll put a
 * newline before the next call.
 */
# define VRB_R(fmt, ...) do { \
    if (self->verbose) { \
        printf(fmt, ##__VA_ARGS__); \
        fflush(stdout); \
        pausing = TRUE; \
    } \
} while (0)

/**
 * VRB:
 * @fmt: A %printf()-style format string.
 * @...: The arguments for @fmt.
 *
 * Prints out @fmt with the variable length arguments in a %prinf()-style
 * format string.  Assumes a struct called %self exists and has a %verbose
 * variable in it.  This will only print if that %verbose value is %TRUE.
 */
# define VRB(fmt, ...) do { \
    if (self->verbose) { \
        if (pausing) printf("\n"); \
        printf(fmt, ##__VA_ARGS__); \
        fflush(stdout); \
        pausing = FALSE; \
    } \
} while (0)

# if defined(UTIL_LOG) && UTIL_LOG
static FILE *log_fd = NULL;
#   define LOG_OPEN(name) do { \
    if(!(log_fd = fopen(name, "w"))) { \
        WRN("Could not open \"%s\": %s", name, \
                strerror(errno)); \
        WRN("Using default name (default.log) instead.\n"); \
        log_fd = fopen(name, "w"); \
    } \
} while (0)
#   define LOG_ENSURE() do { \
    if (log_fd == NULL) { \
        WRN("Logger not init'd, writing to default.log.\n"); \
        LOG_OPEN("default.log"); \
    } \
} while (0)
#   define LOG(fmt, ...) do { \
    LOG_ENSURE(); \
    fprintf(log_fd, fmt, ##__VA_ARGS__); \
    fflush(log_fd); \
} while (0)
#   define LOG_CLOSE() do { fclose(log_fd); } while (0)

# else

#   define LOG_OPEN(name)
#   define LOG_ENSURE()
#   define LOG(fmt, ...)
#   define LOG_CLOSE()

# endif

#else

extern gboolean pausing;

#endif
