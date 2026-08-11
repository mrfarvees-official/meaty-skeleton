#ifndef KERNEL_SCHEDULER_ALGORITHM_H
#define KERNEL_SCHEDULER_ALGORITHM_H

#include <stdbool.h>

#include <kernel/task.h>

typedef struct 
{
    const char      *name;

    /*
     * Create private algorithm state.
     *
     * RR will create its run queue here.
     *
     * A future EDF scheduler could create its deadline structure.
     * A future priority scheduler could create priority queues.
     */
    void            *(*create)(void);

    void            (*destroy)(void* state);

    /*
     * Runnable task management.
     */
    void            (*enqueue)(void* state, task_t* task);

    void            (*dequeue)(void* state, task_t* task);

    /*
     * Remove and return the next task to run.
     *
     * NULL means this policy currently has nothing runnable.
     */
    task_t*         (*pick_next)(void* state);

    /*
     * Called when a task starts running.
     */
    void            (*on_switch_in)(void* state, task_t* task);

    /*
     * Timer notification.
     *
     * true = this algorithm would like a reschedule.
     */
    bool            (*tick)(void* state, task_t* current);
} scheduler_algorithm_t;

#endif