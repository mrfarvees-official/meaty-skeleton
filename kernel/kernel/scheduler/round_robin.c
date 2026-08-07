#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <kernel/heap.h>
#include <kernel/task.h>
#include <kernel/scheduler/algorithm.h>

#define RR_DEFAULT_QUANTUM_TICKS 5u

typedef struct 
{
    task_t* head;
    task_t* tail;

    uint32_t quantum_ticks;
    uint32_t elapsed_ticks;
} round_robin_state_t;

static void* rr_create(void)
{
    round_robin_state_t* state = kmalloc((sizeof(round_robin_state_t)));

    if (state == NULL)
        return NULL;

    state->head = NULL;
    state->tail = NULL;

    state->quantum_ticks = RR_DEFAULT_QUANTUM_TICKS;

    state->elapsed_ticks = 0;

    return state;
}

static void rr_destroy(void* opaque)
{
    if (opaque != NULL)
        kfree(opaque);
}

static void rr_enqueue(void* opaque, task_t* task)
{
    round_robin_state_t* state = (round_robin_state_t*)opaque;

    if (state == NULL || task == NULL)
        return;

    task->sched_previous = state->tail;

    task->sched_next = NULL;

    if (state->tail != NULL)
        state->tail->sched_next = task;
    else
        state->head = task;

    state->tail = task;
}

static void rr_dequeue(void* opaque, task_t* task)
{
    round_robin_state_t* state = (round_robin_state_t*)opaque;

    if (state == NULL || task == NULL)
        return;

    if (task->sched_previous != NULL)
        task->sched_previous->sched_next = task->sched_next;
    else 
        state->head = task->sched_next;

    if (task->sched_next != NULL)
        task->sched_next->sched_previous = task->sched_previous;
    else
        state->tail = task->sched_previous;

    task->sched_previous = NULL;
    task->sched_next = NULL;
}

static task_t* rr_pick_next(void* opaque)
{
    round_robin_state_t* state = (round_robin_state_t*)opaque;

    if (state == NULL)
        return NULL;

    task_t* task = state->head;

    if (task == NULL)
        return NULL;

    rr_dequeue(state, task);

    return task;
}

static void rr_on_switch_in(void* opaque, task_t* task)
{
    (void)task;

    round_robin_state_t* state = (round_robin_state_t*)opaque;

    if (state == NULL)
        return;

    state->elapsed_ticks = 0;
}

static bool rr_tick(void* opaque, task_t* current)
{
    (void)current;

    round_robin_state_t* state = (round_robin_state_t*)opaque;

    if (state == NULL)
        return false;

    ++state->elapsed_ticks;

    if (state->elapsed_ticks >= state->quantum_ticks)
    {
        state->elapsed_ticks = 0;

        return true;
    }

    return false;
}

const scheduler_algorithm_t scheduler_round_robin_algorithm = 
{
    .name = "round-robin",

    .create = rr_create,
    .destroy = rr_destroy,

    .enqueue = rr_enqueue,
    .dequeue = rr_dequeue,
    .pick_next = rr_pick_next,

    .on_switch_in = rr_on_switch_in,

    .tick = rr_tick
};
