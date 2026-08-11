#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t process_id_t;
typedef uint32_t handle_t;

#define PROCESS_HANDLE_INVALID ((handle_t)0u)
#define PROCESS_STANDARD_HANDLE_COUNT 3u

typedef struct process process_t;

/* Create the initial kernel process and its standard terminal handles. */
void process_initialize(void);

/*
 * Process lifetime management for the future program loader.  A process owns
 * its handle table; destroying it closes every handle it still owns.
 */
process_t *process_create(void);
void process_destroy(process_t *process);

/* Allocate and release opaque per-process handles. */
handle_t process_handle_open(process_t *process);
bool process_handle_close(process_t *process, handle_t handle);

/* Live system-wide accounting used by the system information display. */
size_t process_live_count(void);
size_t process_handle_live_count(void);

#endif
