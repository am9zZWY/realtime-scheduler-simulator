#define _GNU_SOURCE

#include <limits.h>
#include <errno.h>
#include <stdbool.h>

#include <math.h>

#include "simulator.h"

/* scheduler type */
static bool flag_dm;  /*!< flag_dm activates the deadline monotonic scheduler */
static bool flag_edf; /*!< flag_edf activates the earliest deadline first scheduler */

/* protocol */
static bool flag_pip;

/* scheduler tick */
static unsigned int tick; /*!< scheduler tick is updated in sim_scheduler() */

/* scheduler flags, params,... */
static unsigned int t_start; /*!< start of simulation */
static int t_stop;           /*!< stop of simulation */
static bool flag_nokill;
static bool flag_kill;

#define STATUS_STRING_LENGTH 100
#define STATUS_STRING_AMOUNT 10

/* Deadlines, etc. */
static char actions_status[STATUS_STRING_AMOUNT][STATUS_STRING_LENGTH]; /*!< Buffer for action message.\n Is reset every tick and set if e.g., a task is preempted or aborted because it missed its deadline */
int actions_status_index;

/* Resources */
static char resource_status[STATUS_STRING_AMOUNT][STATUS_STRING_LENGTH];
int resource_status_index;

/* tasks */
static list_node *tasks; /*!< List with all tasks.\n New instances of tasks are created from this list. */
static list_node *untouched_tasks;
static int task_ids;
static list_node *stats;         /*!< List with all global_stats.\n Each task has its own global_stat. Every instance of a task has a reference to the same global_stats */
static list_node *killed_tasks;  /*!< List with all tasks that were killed during schedule simulator. */
static list_node *ready_queue;   /*!< Queue with all runnable/running tasks */
static list_node *pending_queue; /*!< Queue with all ready tasks. Because tick scheduling is used, these task must be put into ready_queue */
static list_node *blocking_times;

/* resources */
static list_node *blocked_queue; /*!< Queue with all blocked tasks depending on the ressource */
static list_node *resources;     /*!< List with all (available) resources */

/**
 * Prints the content of a buffer.
 * @param status is the buffer that is read.
 * @param index gives information about how "full" the buffer is.
 */
void print_status(char status[STATUS_STRING_AMOUNT][STATUS_STRING_LENGTH], int index)
{
    int print_index;
    for (print_index = 0; print_index < index; ++print_index)
    {
        printf("%s", status[print_index]);
        if (print_index < index - 1)
        {
            printf(" + ");
        }
    }
}

/**
 * Adds a status message to a buffer.
 * @param dst is a pointer to the buffer.
 * @param src is where the status message is read from.
 * @param dst_index points to the next free slot.
 */
void add_status(char (*dst)[STATUS_STRING_LENGTH], const char src[STATUS_STRING_LENGTH], int *dst_index)
{
    strcpy(dst[(*dst_index)++], src);
}

/**
 * @brief handle_pending_queue() prepares the pending_queue for the ready_queue.
 *
 * It iterates through all tasks in tasks and checks if they are ready to be executed.
 * If so a deep copy of the task is created and added to the pending queue.
 *
 * This function only adds entries to the pending_queue, but it does not alter in any other way.
 * When sim_schedule() is called pending_queue is reset because it expects all tasks to be transferred to ready_queue.
 */
void handle_pending_queue()
{
    list_node *tmp_task_list = tasks;
    while (tmp_task_list != NULL)
    {
        if (((tick >= (tmp_task_list->task->t_ph + tmp_task_list->task->t_per)) &&
             (0 == ((tick - tmp_task_list->task->t_ph) % tmp_task_list->task->t_per))) ||
            ((tick < (tmp_task_list->task->t_ph + tmp_task_list->task->t_per)) && (tick == tmp_task_list->task->t_ph)))
        {
            tmp_task_list->task->task_id = task_ids;
            create_and_add_node_to_list(&pending_queue, copy_node(tmp_task_list->task, task_t), task_t);
            ++task_ids;
            ++tmp_task_list->task->task_num;
        }
        tmp_task_list = tmp_task_list->next; /* select next task */
    }
}

/**
 * @brief handle_ready_queue() prepares the ready queue for pick_next_task().
 *
 * It adds all "new" tasks from the pending_queue to the ready_queue.\n
 * This function only adds items but does not remove them.
 */
void handle_ready_queue()
{
    list_node *tmp_pending_queue = pending_queue;
    while (tmp_pending_queue != NULL)
    {
        create_and_add_node_to_list(&ready_queue, copy_node(tmp_pending_queue->task, task_t), task_t);
        tmp_pending_queue = tmp_pending_queue->next;
    }
}

/**
 * @brief Prioritizes tasks by calling a flag dependant algorithm passed via prioritize_task as reference.
 *
 * @param list is the reference in which the tasks are prioritized.
 * @param prioritize_task is the algorithm that prioritizes the tasks.
 */
void prioritize_tasks(list_node *list, void (*prioritize_task)(list_node *, list_node *))
{
    list_node *tmp_queue_outer;
    list_node *tmp_queue_inner;

    tmp_queue_outer = list;
    while (tmp_queue_outer != NULL)
    {
        tmp_queue_outer->task->task_priority = 0;
        tmp_queue_inner = list;
        while (tmp_queue_inner != NULL)
        {
            prioritize_task(tmp_queue_inner, tmp_queue_outer);
            tmp_queue_inner = tmp_queue_inner->next;
        }
        tmp_queue_outer = tmp_queue_outer->next;
    }
}

/**
 * @brief Prioritizes task according to the Deadline Monotonic scheduling algorithm.
 * This algorithm prioritizes the task with the smallest deadline.
 * This function can be seen as a comparator between two tasks.
 * It increases the priority of only one task und must thus be used in a loop over all tasks.
 *
 * @param task is the reference to the first task.
 * @param task2 is the reference to another task.
 */
void prioritize_task_dm(list_node *task, list_node *task2)
{
    list_node *tmp_priorities;
    unsigned int min_deadline;
    if ((task != NULL) && (task2 != NULL))
    {
        min_deadline = task->task->t_d;
#ifdef DEBUG
        printf("%u prioritize_task_dm: %s%d has deadline %u\n", tick, task->task->name, task->task->task_num, task->task->t_d);
#endif
        /* check if task has inherited task_priorities.*/
        if (flag_pip && NULL != task->task->task_priorities)
        {
            tmp_priorities = task->task->task_priorities;
            while (tmp_priorities != NULL)
            {
                /* select smallest deadline from all inherited priorities */
                if (min_deadline > tmp_priorities->local_task_stats->t_d)
                {
                    min_deadline = tmp_priorities->local_task_stats->t_d;
                }
                tmp_priorities = tmp_priorities->next;
            }
#ifdef DEBUG
            printf("%u prioritize_task_dm: %s%d has (new) minimal deadline %u\n", tick, task->task->name, task->task->task_num, min_deadline);
#endif
        }
        if (min_deadline > task2->task->t_d)
        {
            task2->task->task_priority++;
        }
    }
}

/**
 * @brief Prioritizes task according to the Earliest Deadline First scheduling algorithm.
 * This algorithm prioritizes the task with the smallest remaining deadline.
 * This function can be seen as a comparator between two tasks.
 * It increases the priority of only one task und must thus be used in a loop over all tasks.
 *
 * @param task is the reference to the first task.
 * @param task2 is the reference to another task.
 */
void prioritize_task_edf(list_node *task, list_node *task2)
{
    list_node *tmp_priorities;
    int min_remaining_deadline;
    if ((task != NULL) && (task2 != NULL))
    {
        min_remaining_deadline = task->task->local_stats->t_remaining_d;
#ifdef DEBUG
        printf("%u prioritize_task_edf: %s%d has deadline %u\n", tick, task->task->name, task->task->task_num, task->task->t_remaining_d);
#endif
        /* check if task has inherited task_priorities.*/
        if ((flag_pip) && (NULL != task->task->task_priorities))
        {
            tmp_priorities = task->task->task_priorities;
            while (tmp_priorities != NULL)
            {
                /* select smallest deadline from all inherited priorities */
                if (min_remaining_deadline > tmp_priorities->local_task_stats->t_remaining_d && 0 >= tmp_priorities->local_task_stats->t_remaining_d)
                {
                    min_remaining_deadline = tmp_priorities->local_task_stats->t_remaining_d;
                }
                tmp_priorities = tmp_priorities->next;
            }
#ifdef DEBUG
            printf("%u prioritize_task_edf: %s%d has (new) minimal deadline %u\n", tick, task->task->name, task->task->task_num, min_remaining_deadline);
#endif
        }
        if (min_remaining_deadline > task2->task->local_stats->t_remaining_d)
        {
            task2->task->task_priority++;
        }
    }
}

/**
 * @brief Picks next task with highest priority. Priorities start from 0 (lowest).
 *
 * @param list is the list where a prioritized task should be picked.
 * @return task_struct* is a reference to a task_struct. It is the task that should run next.
 */
task_struct *pick_next_task(list_node *list)
{
    list_node *tmp_queue;
    task_struct *next_task;

    if ((NULL == list) || (NULL == (list->task)))
    {
        return NULL;
    }

    tmp_queue = list;
    next_task = tmp_queue->task;

    while (tmp_queue != NULL)
    {
        if (tmp_queue->next != NULL)
        {
            if (next_task->task_priority < tmp_queue->next->task->task_priority)
            {
                next_task = tmp_queue->next->task;
            }
        }
        tmp_queue = tmp_queue->next;
    }
    return next_task;
}

/**
 * @brief Checks if tasks finished running and sets status `removed`.
 * @param task is a reference to task.
 */
void remove_task_if_finished_execution(task_struct *task)
{
    /* mark task with REMOVE_TASK flag. It is later removed from ready_queue. */
    if ((task != NULL) && ((task->t_remaining_e - 1) <= 0) && (task->task_state != killed))
    {
#ifdef DEBUG
        printf("%u remove_task_if_finished_execution: %s%d is now marked to be removed.", tick, task->name, task->task_num);
#endif
        task->task_state = removed;
    }
}

/**
 * @brief Checks if any task in a list has passed its deadline.
 * If so, depending on the kill flag, it is either killed or only removed from ready_queue.
 *
 * @param task_node
 * @param kill_from_queue
 */
void handle_deadline(list_node *task_node, list_node *kill_from_queue)
{
    list_node *tmp_queue;
    task_struct *task;
    char buffer[STATUS_STRING_LENGTH];

    task = task_node->task;
#ifdef DEBUG
    printf("%d handle_deadline: %s%d: deadline %d, exec %d!\n", tick, task->name, task->task_num, task->local_stats->t_remaining_d, task->t_remaining_e);
#endif
    if ((0 >= task->local_stats->t_remaining_d) && (0 < task->t_remaining_e))
    {
#ifdef DEBUG
        printf("%d handle_deadline: %s%d missed deadline!\n", tick, task->name, task->task_num);
#endif
        if (flag_kill)
        {
            snprintf(buffer, STATUS_STRING_LENGTH, "%s%d missed Deadline (aborted)",
                     task->name,
                     task->task_num);
            add_status(&actions_status[0], buffer, &actions_status_index);
            task->task_state = killed;

            /* all other instances of task should be killed too. */
            tmp_queue = kill_from_queue;
            while (tmp_queue != NULL)
            {
                if (0 == strcmp(task->name, tmp_queue->task->name))
                {
                    tmp_queue->task->task_state = killed;
                }
                tmp_queue = tmp_queue->next;
            }
        }
        else if (flag_nokill)
        {
            snprintf(buffer, STATUS_STRING_LENGTH, "%s%d missed Deadline", task->name,
                     task->task_num);
            add_status(&actions_status[0], buffer, &actions_status_index);
            task->task_state = removed;
        }
    }
}

/**
 * @brief Decrements all remaining deadlines. That means not only will the task deadline be decremented but also the inherited deadlines
 * in order to calculate the priority correctly.
 *
 * @param task is a reference to the task whose deadline should be decremented.
 */
void decrement_deadline(list_node *task)
{
    list_node *tmp_priorities;
    if ((NULL != task) && (task->node_type == task_t))
    {
        task->task->t_remaining_d--;
        task->task->local_stats->t_remaining_d--;
        tmp_priorities = task->task->task_priorities;
        while (tmp_priorities != NULL)
        {
#ifdef DEBUG
            printf("%u decrement_deadline: %s%d decrementing inherited deadline from %u ", tick, task->task->name, task->task->task_num, tmp_priorities->local_task_stats->t_remaining_d);
#endif
            tmp_priorities->local_task_stats->t_remaining_d--;
#ifdef DEBUG
            printf("to %u.\n", tmp_priorities->local_task_stats->t_remaining_d);
#endif
            tmp_priorities = tmp_priorities->next;
        }
    }
}

/**
 * @brief Checks if tasks in ready_queue missed their deadline.
 *
 * Depending on the flag they are either removed from ready_queue or completely aborted.
 *
 * if `flag_kill` is set, the task is completely removed from the task list and is never added again to pending_queue\n
 * if `flag_nokill` is set, the task is only removed from ready_queue but can still continue its execution in another instance.
 */
void handle_deadline_queues()
{
    /* iterate through ready_queue and check every entry */
    list_node *tmp_ready_queue;
    list_node *tmp_blocked_queue;

    tmp_ready_queue = ready_queue;
    while (tmp_ready_queue != NULL)
    {
        handle_deadline(tmp_ready_queue, ready_queue);
        decrement_deadline(tmp_ready_queue);
        tmp_ready_queue = tmp_ready_queue->next;
    }

    tmp_blocked_queue = blocked_queue;
    while (tmp_blocked_queue != NULL)
    {
        handle_deadline(tmp_blocked_queue, blocked_queue);
        decrement_deadline(tmp_blocked_queue);
        tmp_blocked_queue = tmp_blocked_queue->next;
    }
}

/**
 * @brief Locks resource.
 * It iterates through all available resources and checks if a task needs the resource.
 * Before this happens it is checked if t_for and t_from are within range.
 * @param task is a reference to the task that allocates the resource.
 * @param resource is a reference to the task resources that gets allocated.
 */
void lock_resource(task_struct *task, local_task_resource *resource)
{
    list_node *tmp_ready_queue;
    list_node *tmp_prev_ready_queue;
    list_node *tmp_resource;

    char buffer[STATUS_STRING_LENGTH];

#ifdef DEBUG
    if (task != NULL)
    {
        printf("%d lock_resource: %s%d wants to lock resource %s. It can lock it from %d (it has %d) and holds it for %d (it has %d)!\n", tick, task->name, task->task_num, resource->name, resource->t_from, resource->t_from_elapsed, resource->t_for, resource->t_for_elapsed);
    }
#endif
    if ((NULL == task) || (blocked == task->task_state))
    {
#ifdef DEBUG
        if (task != NULL)
        {
            printf("%d lock_resource: %s%d wanted to lock resource %s. But task is already blocked\n", tick, task->name, task->task_num, resource->name);
        }
#endif
        return;
    }
    else if ((resource->t_from_elapsed < resource->t_from) ||
             (resource->t_for_elapsed >= resource->t_for))
    {
#ifdef DEBUG
        if (task != NULL)
        {
            printf("%d lock_resource: %s%d wanted to lock resource %s. But it is too early or too late\n", tick, task->name, task->task_num, resource->name);
        }
#endif
        resource->t_from_elapsed++;
        return;
    }

    resource->t_from_elapsed++;

    tmp_resource = resources;
    while (tmp_resource != NULL)
    {
        if (0 == (strcmp(tmp_resource->global_task_resource->name, resource->name)))
        {
            if (NULL == tmp_resource->global_task_resource->used_by)
            {
                /* if resource is free */
                task->task_priorities = NULL;
                tmp_resource->global_task_resource->used_by = task;
                snprintf(buffer, STATUS_STRING_LENGTH, "%s%d locks %s -> ok",
                         task->name,
                         task->task_num,
                         resource->name);
                add_status(&resource_status[0], buffer, &resource_status_index);
#ifdef DEBUG
                printf("%d lock_resource: %s%d locks resource %s! Resource now used by %s%d\n", tick, task->name, task->task_num, resource->name, tmp_resource->global_task_resource->used_by->name, tmp_resource->global_task_resource->used_by->task_num);
#endif
            }
            else if (0 != (strcmp(tmp_resource->global_task_resource->used_by->name, task->name)))
            {
                /* if resource is blocked by another task */
                if (task->blocked_by_resc != NULL)
                {
                    free(task->blocked_by_resc);
                }
                task->blocked_by_resc = strdup(tmp_resource->global_task_resource->name);
                task->task_state = blocked;
#ifdef DEBUG
                printf("%d lock_resource: %s%d was blocked because resource %s is locked and used by %s%d!\n", tick, task->name, task->task_num, resource->name, tmp_resource->global_task_resource->used_by->name, tmp_resource->global_task_resource->used_by->task_num);
#endif
                /* if blocked task has lower priority it gets a new priority */
                if (flag_pip && tmp_resource->global_task_resource->used_by->task_priority < task->task_priority)
                {
                    /*
                     * pass all needed attributes to task in order to calculate new priority.
                     * Because of EDF, priority can change every tick! Therefore deadlines of blocked tasks must be checked! */
                    task->local_stats->resource_name = strdup(resource->name);
                    create_and_add_node_to_list(&tmp_resource->global_task_resource->used_by->task_priorities, copy_node(task->local_stats, local_task_stats_t), local_task_stats_t);
                }
                create_and_add_node_to_list(&blocked_queue, copy_node(task, task_t), task_t);
                snprintf(buffer, STATUS_STRING_LENGTH, "%s%d locks %s -> blocked",
                         task->name,
                         task->task_num,
                         resource->name);
                add_status(&resource_status[0], buffer, &resource_status_index);

                /* remove blocked task from ready_queue */
                tmp_ready_queue = ready_queue;
                tmp_prev_ready_queue = tmp_ready_queue;
                while (tmp_ready_queue != NULL)
                {
                    if (tmp_ready_queue->task == task)
                    {
#ifdef DEBUG
                        printf("%u lock_resource: %s%d is going to be removed from ready_queue because it was blocked\n", tick, task->name, task->task_num);
#endif
                        remove_node_from_list(&ready_queue, tmp_ready_queue, &tmp_prev_ready_queue);
                    }
                    tmp_prev_ready_queue = tmp_ready_queue;
                    tmp_ready_queue = tmp_ready_queue->next;
                }
            }
            else
            {
#ifdef DEBUG
                printf("%d lock_resource: %s%d locks already resource %s!\n", tick, task->name, task->task_num, resource->name);
#endif
                resource->t_for_elapsed++;
            }
        }
        tmp_resource = tmp_resource->next;
    }
    /*
        printf("Task %s%d: \n", task->name, task->task_num);
        iterator(task->resources, &print_task_resource);
        printf("\n"); */
}

/**
 * @brief lock_resources() checks the resources that are dedicated to the task.
 * This function only iterates over a task's resources list.
 *
 * @param task is a reference to task.
 */
void lock_resources(task_struct *task)
{
    if (task != NULL)
    {
        list_node *task_resources = task->resources;
        while (task_resources != NULL)
        {
            lock_resource(task, task_resources->local_task_resource);
            task_resources = task_resources->next;
        }
    }
}

/**
 * @brief Frees the resource without any further checking.
 *
 * @param task that should release its resources.
 * @param resource which is blocked by the task.
 */
void unlock_resource(task_struct *task, list_node *resource)
{
    char buffer[STATUS_STRING_LENGTH];
    list_node *tmp_resource;
    list_node *tmp_priority;
    list_node *tmp_prev_priority;

#ifdef DEBUG
    printf("%d unlock_resource: unlocking resource %s from task %s%d!\n", tick, resource->local_task_resource->name, task->name, task->task_num);
#endif

    tmp_resource = resources;
    while (tmp_resource != NULL)
    {
#ifdef DEBUG
        printf("%d unlock_resource: iterating through resources. Checking resource %s! ", tick, tmp_resource->global_task_resource->name);
        if (tmp_resource->global_task_resource->used_by != NULL)
        {
            printf("It is used by %s%d!", tmp_resource->global_task_resource->used_by->name, tmp_resource->global_task_resource->used_by->task_num);
        }
        printf("\n");
#endif
        if ((0 == strcmp(tmp_resource->global_task_resource->name, resource->local_task_resource->name)) &&
            (tmp_resource->global_task_resource->used_by == task))
        {
#ifdef DEBUG
            printf("%d unlock_resource: %s%d unlocks resource %s!\n", tick, task->name, task->task_num, resource->local_task_resource->name);
#endif
            snprintf(buffer, STATUS_STRING_LENGTH, "%s%d unlocks: %s",
                     task->name,
                     task->task_num,
                     resource->local_task_resource->name);
            add_status(&resource_status[0], buffer, &resource_status_index);

            /* remove resource from task so that it cannot relock it */
#ifdef DEBUG
            printf("%d unlock_resource: removing %s from %s%d!\n", tick, resource->local_task_resource->name, task->name, task->task_num);
#endif
            /* remove inherited priority */
            tmp_priority = task->task_priorities;
            tmp_prev_priority = tmp_priority;
            while (tmp_priority != NULL)
            {
                if (0 == strcmp(tmp_priority->local_task_stats->resource_name, resource->local_task_resource->name))
                {
#ifdef DEBUG
                    printf("%u unlock_resource: %s%d: %s has now been removed from inherited priority!\n", tick, task->name, task->task_num, tmp_priority->local_task_stats->resource_name);
#endif
                    remove_node_from_list(&task->task_priorities, tmp_priority, &tmp_prev_priority);
                }
                tmp_prev_priority = tmp_priority;
                tmp_priority = tmp_priority->next;
            }
#ifdef DEBUG
            printf("%u unlock_resource: %s%d: %s has been now removed resource!\n", tick, task->name, task->task_num, resource->local_task_resource->name);
#endif
            remove_node_from_list_noprev(&task->resources, resource);
            tmp_resource->global_task_resource->used_by = NULL;
            break;
        }
        tmp_resource = tmp_resource->next;
    }
}

/**
 * @brief Frees all resources of a task.
 * @param task is a reference to the task.
 */
void unlock_resources(task_struct *task)
{
    list_node *tmp_resource = task->resources;
    while (tmp_resource != NULL)
    {
#ifdef DEBUG
        printf("%d unlock_resources: %s%d should unlock resource %s!\n", tick, task->name, task->task_num, tmp_resource->local_task_resource->name);
#endif
        unlock_resource(task, tmp_resource);
        tmp_resource = tmp_resource->next;
    }
}

/**
 * @brief Iterates through task resources and checks if a resource can be unlocked.
 * @param task is a reference to the task.
 */
void unlock_resources_if_finished(task_struct *task)
{
    list_node *task_resource;
#ifdef DEBUG
    printf("\n");
#endif
    if (task != NULL)
    {
        task_resource = task->resources; /* global_task_resource of task */
        while (NULL != task_resource)
        {
            if (task_resource->local_task_resource->t_from_elapsed >= task_resource->local_task_resource->t_from &&
                task_resource->local_task_resource->t_for_elapsed >= task_resource->local_task_resource->t_for)
            {
#ifdef DEBUG
                printf("%d unlock_resources_if_finished: %s%d unlocks resource %s because [t_for_elapsed] %d >= %d [t_for] !\n", tick, task->name, task->task_num, task_resource->local_task_resource->name, task_resource->local_task_resource->t_for_elapsed, task_resource->local_task_resource->t_for);
#endif
                unlock_resource(task, task_resource);
            }
            task_resource = task_resource->next;
        }
        /*
        printf("Task %s%d: \n", task->name, task->task_num);
        iterator(task->resources, &print_task_resource);
        printf("\n"); */
    }
}

void reset_used_by_references(task_struct *task)
{
    list_node *tmp_resource;
    if (task != NULL)
    {
        tmp_resource = resources;
        while (tmp_resource != NULL)
        {
#ifdef DEBUG
            if (NULL != tmp_resource->global_task_resource->used_by)
            {
                printf("%u reset_used_by_references: %s: %s == %s\n", tick, tmp_resource->global_task_resource->name, tmp_resource->global_task_resource->used_by->name, task->name);
            }
#endif
            if ((tmp_resource->global_task_resource->used_by != NULL) && (0 == strcmp(tmp_resource->global_task_resource->used_by->name, task->name)) && (tmp_resource->global_task_resource->used_by->task_num == task->task_num))
            {
#ifdef DEBUG
                printf("%u reset_used_by_references: resetting used_by from %p to %p\n", tick, tmp_resource->global_task_resource->used_by, task);
#endif
                tmp_resource->global_task_resource->used_by = task;
            }
            tmp_resource = tmp_resource->next;
        }
    }
}

/**
 * @brief Iterates through the blocked_queue and puts all tasks that wait for a specific resource
 * into a separate temporary queue. After that the "right" task is selected depending on the scheduler.
 * This selected task is put into ready state and readded to ready_queue.
 *
 * @param pick_next_task is the task selection algorithm. It depends on the scheduling flag.
 */
void add_tasks_from_blocked_queue_to_ready_queue(void (*prioritize_task)(list_node *, list_node *))
{
    list_node *tmp_blocked_queue;
    list_node *tmp_blocked;

    list_node *tmp_resource;

    task_struct *copied_task;

    task_struct *picked_task;

    tmp_resource = resources;
#ifdef DEBUG
    printf("\n");
#endif
    while (tmp_resource != NULL)
    {
#ifdef DEBUG
        printf("%d add_tasks_from_blocked_queue_to_ready_queue: checking resource %s.\n", tick, tmp_resource->global_task_resource->name);
#endif
#ifdef DEBUG
        if (tmp_resource->global_task_resource->used_by != NULL)
        {
            printf("%d add_tasks_from_blocked_queue_to_ready_queue: resource %s is used by %s%d.\n", tick,
                   tmp_resource->global_task_resource->name,
                   tmp_resource->global_task_resource->used_by->name,
                   tmp_resource->global_task_resource->used_by->task_num);
        }
#endif

        if (NULL == tmp_resource->global_task_resource->used_by)
        {
            tmp_blocked_queue = NULL;
            tmp_blocked = blocked_queue;
            while (tmp_blocked != NULL)
            {
#ifdef DEBUG
                printf("%d add_tasks_from_blocked_queue_to_ready_queue: %s%d waits for %s.\n", tick, tmp_blocked->task->name, tmp_blocked->task->task_num, tmp_blocked->task->blocked_by_resc);
#endif
                if (0 == strcmp(tmp_blocked->task->blocked_by_resc, tmp_resource->global_task_resource->name))
                {
#ifdef DEBUG
                    printf("%d add_tasks_from_blocked_queue_to_ready_queue: adding %s%d to queue.\n", tick, tmp_blocked->task->name, tmp_blocked->task->task_num);
#endif
                    create_and_add_node_to_list(&tmp_blocked_queue, copy_node(tmp_blocked->task, task_t), task_t);
                }

                tmp_blocked = tmp_blocked->next;
            }

            if (tmp_blocked_queue != NULL)
            {
                prioritize_tasks(tmp_blocked_queue, prioritize_task);
                picked_task = pick_next_task(tmp_blocked_queue);
#ifdef DEBUG
                printf("%d add_tasks_from_blocked_queue_to_ready_queue: %s%d was picked.\n", tick, picked_task->name, picked_task->task_num);
#endif
                /* add picked task to ready_queue */
                picked_task->task_state = ready;
                copied_task = copy_node(picked_task, task_t);
                create_and_add_node_to_list(&ready_queue, copied_task, task_t);
                reset_used_by_references(copied_task);

                tmp_blocked = blocked_queue;
                while (tmp_blocked != NULL)
                {
                    if (0 == (strcmp(tmp_blocked->task->name, picked_task->name)) && tmp_blocked->task->task_num == picked_task->task_num)
                    {
                        tmp_blocked->task->task_state = removed;
                    }
                    tmp_blocked = tmp_blocked->next;
                }
            }
        }
        tmp_resource = tmp_resource->next;
    }
}

/**
 * @brief handle_tasks_to_delete() iterates through ready_queue and checks if there are tasks that have a set task state.
 *
 * if task state is set to `removed` then the task is removed from the ready_queue\n
 * if task state is set to `killed` then the task is completely aborted and removed from the tasks list.
 */
void handle_tasks_to_delete(list_node **list)
{
    list_node *tmp_list;
    list_node *tmp_prev_list;

    list_node *tmp_task_tasks_list;
    list_node *tmp_prev_tasks_list;

    killed_task *kill_task;

    tmp_list = *list;
    tmp_prev_list = *list;

#ifdef DEBUG
    printf("\n");
#endif

    while (tmp_list != NULL)
    {
#ifdef DEBUG
        printf("%d handle_tasks_to_delete: %s%d has %u state\n", tick, tmp_list->task->name, tmp_list->task->task_num, tmp_list->task->task_state);
#endif
        if ((tmp_list->task->task_state == killed) || (tmp_list->task->task_state == removed))
        {
            if (killed == (tmp_list->task->task_state))
            {
                /* add killed task to list */
                kill_task = create_node(killed_task_t);
                kill_task->name = strdup(tmp_list->task->name);
                kill_task->task_kill_time = tick;
                kill_task->task_num = tmp_list->task->task_num;
                create_and_add_node_to_list(&killed_tasks, kill_task, killed_task_t);

                /* iterate through tasks and remove task */
                tmp_task_tasks_list = tasks;
                tmp_prev_tasks_list = tasks;
                while (NULL != tmp_task_tasks_list)
                {
                    if (0 == strcmp(tmp_task_tasks_list->task->name, tmp_list->task->name))
                    {
                        unlock_resources(tmp_task_tasks_list->task);
                        if (tmp_list->task->t_elapsed > tmp_list->task->global_stats->t_elapsed)
                        {
                            tmp_list->task->global_stats->t_elapsed = tmp_list->task->t_elapsed;
                        }
#ifdef DEBUG
                        printf("%d handle_tasks_to_delete: %s%d is going to be removed\n", tick, tmp_task_tasks_list->task->name, tmp_task_tasks_list->task->task_num);
#endif
                        remove_node_from_list(&tasks, tmp_task_tasks_list, &tmp_prev_tasks_list);
                    }
                    tmp_prev_tasks_list = tmp_task_tasks_list;
                    tmp_task_tasks_list = tmp_task_tasks_list->next;
                }
            }
            unlock_resources(tmp_list->task);
            if (tmp_list->task->t_elapsed > tmp_list->task->global_stats->t_elapsed)
            {
                tmp_list->task->global_stats->t_elapsed = tmp_list->task->t_elapsed;
            }
#ifdef DEBUG
            printf("%d handle_tasks_to_delete: %s%d is going to be removed\n", tick, tmp_list->task->name, tmp_list->task->task_num);
#endif
            remove_node_from_list(list, tmp_list, &tmp_prev_list);
        }
        tmp_prev_list = tmp_list;
        tmp_list = tmp_list->next;
    }
}

void increment_elapsed(list_node *task)
{
    if (NULL != task)
    {
        task->task->t_elapsed++;
    }
}

/**
 * @brief handle_reaction_time() iterates through all tasks in ready_queue
 * @param running_task
 */
void handle_reaction_time()
{
    iterator(ready_queue, &increment_elapsed);
    iterator(blocked_queue, &increment_elapsed);
}

/**
 * @brief Returns the minimal value.
 * @param a
 * @param b
 * @return int is the minimal value. Can bei either a or b.
 */
int minimal_value(int a, int b)
{
    if (a > b)
    {
        return b;
    }
    else
    {
        return a;
    }
}

/**
 * @brief Calculates the blocking time of each task. Should be called at the before the simulation.
 */
void blocking_time()
{
    list_node *tmp_task_resources;
    list_node *tmp_task;
    task_struct *tmp_higher_prio_task;
    task_struct *tmp_higher_prio_task2;

    list_node *tmp_global_resources;
    list_node *tmp_global_resource;
    list_node *tmp_local_resource;
    local_task_resource *tmp_copy_local_resource;
    list_node *tmp_resources; /* resources of highest prio tasks */

    list_node *tmp_task_list;  /* copy of all tasks */
    list_node *tmp_task_list2; /* 2nd copy of all tasks */

    global_blocking_stats *global_blocking_stats;

    int t_BT_max;
    int t_BR_max;
    int tmp_BT_max;
    int tmp_BR_max;

    printf("Blocking Time:\n");

#ifdef DEBUG
    printf("blocking_time: copying tasks!\n");
#endif
    tmp_task_list = copy_list(untouched_tasks);
    tmp_task_list2 = copy_list(untouched_tasks);

    /* calculate */
    while (tmp_task_list != NULL)
    {

#ifdef DEBUG
        printf("blocking_time: prioritizing tasks!\n");
#endif
        tmp_global_resources = NULL;
        prioritize_tasks(tmp_task_list, &prioritize_task_dm);
        prioritize_tasks(tmp_task_list2, &prioritize_task_dm);
        tmp_higher_prio_task = pick_next_task(tmp_task_list);

#ifdef DEBUG
        printf("blocking_time: choosing resources!\n");
#endif

        tmp_task = tmp_task_list2;
        while (tmp_task != NULL)
        {
            if (tmp_task->task->task_priority >= tmp_higher_prio_task->task_priority)
            {
#ifdef DEBUG
                printf("blocking_time: adding resources from %s!\n", tmp_task->task->name);
#endif
                tmp_local_resource = tmp_task->task->resources;
                while (tmp_local_resource != NULL)
                {
                    tmp_copy_local_resource = copy_node(tmp_local_resource->local_task_resource, local_task_resource_t);
                    create_and_add_node_to_list(&tmp_global_resources, tmp_copy_local_resource, local_task_resource_t);
                    tmp_local_resource = tmp_local_resource->next;
                }
            }
            tmp_task = tmp_task->next;
        }

#ifdef DEBUG
        printf("Resources:\n");
        iterator(tmp_global_resources, &print_task_resource);
        printf("\n");
#endif

#ifdef DEBUG
        printf("blocking_time: picked task with highest priority!\n");
        printf("blocking_time: calculating now BT!\n");
#endif
        /* BT */
        t_BT_max = 0;
        tmp_task = tmp_task_list;
        /* iterate through all tasks to calulculate BT for tmp_higher_prio_task */
        while (tmp_task != NULL)
        {
            tmp_BT_max = 0;
            /* check if task has lower priority than highest priority task */
            if (tmp_task->task->task_priority < tmp_higher_prio_task->task_priority)
            {
#ifdef DEBUG
                printf("blocking_time: analyzing %s!\n", tmp_task->task->name);
#endif
                tmp_global_resource = tmp_global_resources;
                while (tmp_global_resource != NULL)
                {

                    tmp_task_resources = tmp_task->task->resources;
                    /* iterate through all resources of a task */
                    while (tmp_task_resources != NULL)
                    {
                        if (0 == strcmp(tmp_global_resource->local_task_resource->name, tmp_task_resources->local_task_resource->name) && (tmp_task_resources->local_task_resource->t_for > tmp_BT_max))
                        {
                            tmp_BT_max = tmp_task_resources->local_task_resource->t_for;
                        }
                        tmp_task_resources = tmp_task_resources->next;
                    }
                    tmp_global_resource = tmp_global_resource->next;
                }
                t_BT_max += tmp_BT_max;
            }
            tmp_task = tmp_task->next;
        }

#ifdef DEBUG
        printf("blocking_time: BT %d!\n", t_BT_max);
#endif

#ifdef DEBUG
        printf("blocking_time: calculating now BR!\n");
#endif
        t_BR_max = 0;
        tmp_global_resource = tmp_global_resources;
        while (tmp_global_resource != NULL)
        {
            /* BR */
            /* iterate through all global resources */
            tmp_BR_max = 0;
            tmp_task = tmp_task_list;
            while (tmp_task != NULL)
            {
                if (tmp_task->task->task_priority < tmp_higher_prio_task->task_priority)
                {
                    tmp_task_resources = tmp_task->task->resources;
                    while (tmp_task_resources != NULL)
                    {
                        if (0 == strcmp(tmp_global_resource->local_task_resource->name, tmp_task_resources->local_task_resource->name) && (tmp_task_resources->local_task_resource->t_for > tmp_BR_max))
                        {
                            tmp_BR_max = tmp_task_resources->local_task_resource->t_for;
                        }
                        tmp_task_resources = tmp_task_resources->next;
                    }
                }
                tmp_task = tmp_task->next;
            }
            t_BR_max += tmp_BR_max;
            tmp_global_resource = tmp_global_resource->next;
        }

#ifdef DEBUG
        printf("blocking_time: BR %d!\n", t_BR_max);
#endif

        global_blocking_stats = create_node(global_blocking_stats_t);
        global_blocking_stats->name = strdup(tmp_higher_prio_task->name);
        global_blocking_stats->t_blocked = minimal_value(t_BR_max, t_BT_max);

        create_and_add_node_to_list(&blocking_times, global_blocking_stats, global_blocking_stats_t);

#ifdef DEBUG
        printf("%s: ", tmp_higher_prio_task->name);
        if (t_BT_max > t_BR_max)
        {
            printf("%d", t_BR_max);
        }
        else
        {
            printf("%d", t_BT_max);
        }
        printf("\n");
#endif
        tmp_task = tmp_task_list;
        while (tmp_task != NULL)
        {
            if (tmp_task->task == tmp_higher_prio_task)
            {
                remove_node_from_list_noprev(&tmp_task_list, tmp_task);
            }
            tmp_task = tmp_task->next;
        }
    }
}

/**
 * @brief Calculates necessary condition for scheduling tests.
 * @param head is the start of the task list.
 * @param task is the current task.
 * @param n specifies how many iterations should be done.
 * @return int is a value that is checked in the scheduling tests.
 */
int u(list_node *head, list_node *task, int n)
{
    int u;
    int j;
    unsigned int t_block_i;
    list_node *tmp_task;
    list_node *tmp_blocking_times;

    tmp_blocking_times = blocking_times;
    while (NULL != tmp_blocking_times)
    {
        if (0 == strcmp(task->task->name, tmp_blocking_times->global_blocking_stats->name))
        {
            t_block_i = tmp_blocking_times->global_blocking_stats->t_blocked;
            break;
        }
        tmp_blocking_times = tmp_blocking_times->next;
    }

    u = t_block_i / minimal_value(task->task->t_d, task->task->t_per);

    j = 1;
    tmp_task = head;
    while (tmp_task != NULL && j++ <= n)
    {
        if (tmp_task->task != task->task)
        {
            u += tmp_task->task->t_e + minimal_value(tmp_task->task->t_d, tmp_task->task->t_per);
        }
        tmp_task = tmp_task->next;
    }
    return u;
}

/**
 * @brief Calculates the necessary scheduling test for DM.
 */
void realtime_notwendig_dm(list_node *task)
{
    unsigned int t;
    unsigned int t_C;
    unsigned int t_block;
    list_node *tmp_task;
    list_node *tmp_blocking_times;

    tmp_blocking_times = blocking_times;
    while (NULL != tmp_blocking_times)
    {
        if (0 == strcmp(task->task->name, tmp_blocking_times->global_blocking_stats->name))
        {
            t_block = tmp_blocking_times->global_blocking_stats->t_blocked;
            break;
        }
        tmp_blocking_times = tmp_blocking_times->next;
    }

    t = task->task->t_e;
    while (true)
    {
        t_C = task->task->t_e + (ceil(t / task->task->t_per) * t_block);
        tmp_task = untouched_tasks;
        while (tmp_task != NULL)
        {
            if (tmp_task->task->t_d < task->task->t_d && tmp_task->task != task->task)
            {
                t_C += ceil(t / tmp_task->task->t_per) + tmp_task->task->t_e;
            }
            tmp_task = tmp_task->next;
        }

        if (t == t_C)
        {
            printf("Task: %s max. Reaktionszeit: %d", task->task->name, t);
            if (t > task->task->t_d)
            {
                printf(" ist damit ungültig!");
            }
            printf("\n");
            break;
        }

        t = t_C;
    }
}

/**
 * @brief Calculates the sufficent scheduling test for EDF.
 */
void realtime_hinreichend_dm()
{
    list_node *tmp_task;
    int n;

    n = 1;
    tmp_task = untouched_tasks;
    while (tmp_task != NULL)
    {
        if (u(untouched_tasks, tmp_task, n) > n * (pow(2, 1 / n) - 1))
        {
            printf("Task: %s failed! ", tmp_task->task->name);
            realtime_notwendig_dm(tmp_task);
            break;
        }
        ++n;
        tmp_task = tmp_task->next;
    }
}

/**
 * @brief Calculates greatest common divisor.
 * @param a is the first value.
 * @param b is the second value.
 * @return unsigned int unsigned int is the greatest common divisor of a and b.
 */
unsigned int ggT(unsigned int a, unsigned int b)
{
    unsigned int c;
    do
    {
        c = a % b;
        a = b;
        b = c;
    } while (c != 0);
    return a;
}

/**
 * @brief Calculates the least common multiple.
 * @param a is the first value.
 * @param b is the second value.
 * @return unsigned int is the least common multiple of a and b.
 */
unsigned int kgV(unsigned int a, unsigned int b)
{
    return a * b / ggT(a, b);
}

/**
 * @brief Calculates the necessary scheduling test for EDF.
 */
void realtime_notwendig_edf()
{
    list_node *tmp_task;
    list_node *tmp_blocking_times;
    int t_Cges;
    unsigned int kgV;
    unsigned int t_block_i;
    int i;

    tmp_task = untouched_tasks;
    kgV = tmp_task->task->t_per;
    while (tmp_task != NULL)
    {
        kgV = kgV * tmp_task->task->t_per / ggT(kgV, tmp_task->task->t_per);
        tmp_task = tmp_task->next;
    }

    for (i = 0; i < kgV; ++i)
    {
        tmp_task = untouched_tasks;
        t_Cges = 0;
        while (tmp_task != NULL)
        {
            tmp_blocking_times = blocking_times;
            t_block_i = 0;
            while (NULL != tmp_blocking_times)
            {
                if (0 == strcmp(tmp_task->task->name, tmp_blocking_times->global_blocking_stats->name))
                {
                    t_block_i = tmp_blocking_times->global_blocking_stats->t_blocked;
                    break;
                }
                tmp_blocking_times = tmp_blocking_times->next;
            }
            t_Cges += (floor((i + tmp_task->task->t_per - tmp_task->task->t_d - tmp_task->task->t_ph) / tmp_task->task->t_per) * (tmp_task->task->t_e + t_block_i));
            if (t_Cges > i)
            {
                printf("Failed, because %d > %d", t_Cges, i);
                break;
            }
            tmp_task = tmp_task->next;
        }
        if (t_Cges > i)
        {
            break;
        }
    }
}

/**
 * @brief Calculates the sufficent scheduling test for EDF.
 */
void realtime_hinreichend_edf()
{
    list_node *tmp_task;
    int n;

    n = length(untouched_tasks);
    tmp_task = untouched_tasks;
    while (tmp_task != NULL)
    {
        if (u(untouched_tasks, tmp_task, n) > 1)
        {
            printf("Task: %s failed! ", tmp_task->task->name);
            realtime_notwendig_edf();
            break;
        }
        tmp_task = tmp_task->next;
    }
}

/**
 * @brief handle_preemption() checks whether the next task[] the previous task.
 *
 * This happens when the remaining execution time of prev is above 0 and
 * is not the same as next.*When prev is preempted it must not be blocked.If it is blocked there is no preemption.
 * @param next is the actual task.*@param prev is the previous task in the queue.
*/
void handle_preemption(const task_struct *next, task_struct *prev)
{
    char buffer[STATUS_STRING_LENGTH];
    if ((NULL != next) && (NULL != prev) && (blocked != prev->task_state) && (removed != prev->task_state) &&
        (killed != prev->task_state) && (prev != next) &&
        (prev->t_remaining_e > 0))
    {
        snprintf(buffer, STATUS_STRING_LENGTH, "[%s%d preempted]", prev->name, prev->task_num);
        add_status(&actions_status[0], buffer, &actions_status_index);
    }
}

/**
 * @brief sim_scheduler() is the main scheduler function.
 *
 * It works with a timer tick and thus is called `tick scheduling`.
 * Following things happen here each tick:
 *  - pending_queue is updated via handle_pending_queue(). This retrieves all tasks that are released.
 *  - ready_queue is updated via handle_ready_queue(). This gets all tasks from pending_queue.
 *  - all deadlines are checked. Tasks that miss their deadline are marked with a flag.
 *  - to be executed task is picked via pick_next_task(). The algorithm for picking the task is passed as a pointer via arguments.
 *  - after the task has been picked preemption is checked in handle_preemption().
 *  - then the task is "executed" and printed to the output.
 *
 *  After its execution (which is determined by t_stop), sim_scheduler() calculates each maximum reaction time.
 */
void sim_scheduler(void (*prioritize_task)(list_node *, list_node *))
{
    task_struct *prev;
    task_struct *next;

    blocking_time();

    next = NULL;
    prev = NULL;
    printf("Time\tJob\t\tReady-Q\t\tBlocked-Q\t\tActions\n");
    for (tick = 0; tick <= t_stop; ++tick)
    {
        handle_pending_queue();
        handle_ready_queue();
        handle_deadline_queues();
        handle_tasks_to_delete(&ready_queue);   /* remove all tasks marked with killed or removed flag */
        handle_tasks_to_delete(&blocked_queue); /* remove all tasks marked with killed or removed flag */

        pending_queue = NULL; /* reset pending queue */

        prev = next;
        do
        {
            prioritize_tasks(blocked_queue, prioritize_task);
            prioritize_tasks(ready_queue, prioritize_task);
            next = pick_next_task(ready_queue);
            lock_resources(next);
        } while ((next != NULL) && (next->task_state == blocked));
        remove_task_if_finished_execution(next); /* tasks execution */
        handle_reaction_time();                  /* tasks reactiontime */

        if (tick >= t_start)
        {
            /* Task */
            printf("%d\t", tick); /* tick */
            if (next != NULL)
            {
                --next->t_remaining_e;
                printf("%s%d", next->name, next->task_num); /* job */
            }
            else
            {
                printf("I");
            }
            /* Ready-Q */
            printf("\t\t");
            print_list(ready_queue);

            /* Blocked-Q */
            printf("\t\t");
            print_list(blocked_queue);
        }

        /* Actions */
        unlock_resources_if_finished(next);
        handle_preemption(next, prev);
        add_tasks_from_blocked_queue_to_ready_queue(prioritize_task);
        if (tick >= t_start)
        {
            printf("\t\t\t");
            print_status(actions_status, actions_status_index);
            if (resource_status_index > 0 && actions_status_index > 0)
            {
                printf(" + ");
            }
            print_status(resource_status, resource_status_index);
            printf("\n");
        }
        /* remove tasks from ready_queue and blocked_queue if deadline is exceeded */
        resource_status_index = 0;
        actions_status_index = 0;
    }
    printf("-----------------------------------------------\n");

    /* print reaction time */
    print_list(stats);

    /* print all killed tasks from scheduler simulator */
    printf("Killed Jobs: ");
    print_list(killed_tasks);
    printf("\n");

    printf("Blocking times: ");
    print_list(blocking_times);
    printf("\n");

    if (flag_dm)
    {
        printf("Realzeitnachweis DM: \n");
        realtime_hinreichend_dm();
        printf("\n");
    }
    else if (flag_edf)
    {
        printf("Realzeitnachweis EDF: \n");
        realtime_hinreichend_edf();
    }
    printf("\n");

    /* free all lists */
    free_all();
}

/**
 * @brief Extracts a number from a string. This is useful because strtol returns 0 when failed.
 * Then errno must be checked.
 * @param token is a reference to the string.
 * @return the number if successful or -1 if it failed.
 */
int get_number(const char *token)
{
    long number;
    char *endptr; /* endpointer for strtol error checking */

    errno = 0;
    number = strtol(token, &endptr, 10);
    if (((ERANGE == errno) && ((LONG_MAX == number) || (LONG_MIN == number))) ||
        ((0 != errno) && (0 == number)) || (endptr == token))
    {
        number = -1;
        errno = 0;
    }
    return (int)number;
}

/**
 * @brief Checks if a resource is already in resources list.
 * @param resc_name is the name of the resource.
 * @return is either true if it already exists or false if not.
 */
bool check_if_resource_exists(const char *resc_name)
{
    list_node *tmp = resources;
    while (NULL != tmp)
    {
        if (0 == strcmp(tmp->global_task_resource->name, resc_name))
        {
            return true;
        }
        tmp = tmp->next;
    }
    return false;
}

typedef enum state
{
    start_st,
    stop_st,
    task_st,
    resc_st,
    finished_st,
    failed_st,
    idle_st
} state;

/**
 * read_and_parse_input() reads and parses the input file.
 *
 * 1. it goes through each line
 * 2. it checks for Start & Stop
 * 3. if neither of those are found it must be at the table
 * 4. with the help of tokens, each string separated by whitespaces is parsed.
 *  Each successful parsed number from the table updates a token_counter and a state.
 *  If a number is not parsed successfully token_counter is set to -1 and the state to failed.
 *
 *  It can handle tables with and without resources.
 *
 * @param input_fp is the file path to the input file.
 */
void read_and_parse_input(const char *input_fp)
{
    FILE *fp; /* file pointer for input file */
    size_t line_length;
    const char *const delimiter = "\f\v\t\r ";
    char *line;        /* read line */
    char *saveptr1;    /* saveptr1 for strtok_r */
    const char *token; /* token for strtok_t */

    state line_state;
    state task_state;
    state resc_state;

    int task_token_counter;
    int tok_num_val;

    /* global_task_resource */
    int resc_token_counter;
    int resc_num;
    int resc_index;
    char *resc_name;
    unsigned int t_from;

    task_struct *task;
    global_task_stats *global_stats;
    global_task_resource *global_resources;
    local_task_resource *local_resources;
    local_task_stats *local_stats;
    list_node *resc_list_task;

    if (NULL == input_fp)
    {
        fprintf(stderr, "read_and_parse_input: no file path provided.\n");
        exit(EXIT_FAILURE);
    }

    fp = fopen(input_fp, "r");
    if (NULL == fp)
    {
        fprintf(stderr, "simulator: %s.", input_fp);
        exit(EXIT_FAILURE);
    }

    /* initialize lists/queues */
    tasks = NULL;
    stats = NULL;
    pending_queue = NULL;
    ready_queue = NULL;
    killed_tasks = NULL;
    blocked_queue = NULL;
    resources = NULL;
    resc_list_task = NULL;

    line = 0;

    line_length = 0;
    while (getline(&line, &line_length, fp) != -1)
    {
        resc_token_counter = 0;
        task_token_counter = 0;
        resc_index = 0;
        resc_num = -1;
        line_state = idle_st;
        task_state = idle_st;
        resc_state = idle_st;

        /* global_task_resource */
        resc_name = NULL;
        t_from = 0;

        task = (task_struct *)create_node(task_t);
        task->name = NULL;
        token = strtok_r(line, delimiter, &saveptr1);
        while (token != NULL)
        {
            if (line_state == idle_st)
            {
                /* Start & Stop */
                if (0 == strcmp(token, "Start"))
                {
                    line_state = start_st;
                }
                else if (0 == strcmp(token, "Stop"))
                {
                    line_state = stop_st;
                }
                else
                {
                    line_state = task_st;
                }
            }

            tok_num_val = get_number(token);
            if ((line_state == start_st) && (-1 != tok_num_val))
            {
                t_start = tok_num_val;
                line_state = idle_st;
                task_state = idle_st;
                resc_state = idle_st;
            }
            else if ((line_state == stop_st) && (-1 != tok_num_val))
            {
                t_stop = tok_num_val;
                line_state = idle_st;
                task_state = idle_st;
                resc_state = idle_st;
            }
            else if ((line_state == task_st) && (task_state != failed_st))
            {
                if (0 == task_token_counter)
                {
                    task->name = strdup(token);
                    task_token_counter = 1;
                }
                else
                {
                    if (-1 == tok_num_val)
                    {
                        task_state = failed_st;
                        task_token_counter = -1;
                    }
                    switch (task_token_counter)
                    {
                    case 1:
                        task->t_ph = tok_num_val;
                        task_token_counter = 2;
                        break;
                    case 2:
                        task->t_per = tok_num_val;
                        task_token_counter = 3;
                        break;
                    case 3:
                        task->t_e = tok_num_val;
                        task->t_remaining_e = task->t_e;
                        task_token_counter = 4;
                        break;
                    case 4:
                        task->t_d = tok_num_val;
                        task->t_remaining_d = task->t_d;
                        task_token_counter = 5;
                        task_state = finished_st;
                        line_state = resc_st;
                        break;
                    default:
                        task_token_counter = -1;
                        task_state = failed_st;
                        break;
                    }
                }
            }
            else if ((line_state == resc_st) && (resc_state != failed_st))
            {
                if ((resc_num == -1) && (-1 != tok_num_val))
                {
                    resc_num = tok_num_val;
                    resc_token_counter = 0;
                }
                else
                {
                    if (0 == resc_token_counter)
                    {
                        resc_name = strdup(token);
                        resc_token_counter = 1;
                    }
                    else if (1 == resc_token_counter)
                    {
                        if (tok_num_val == -1)
                        {
                            resc_state = failed_st;
                        }
                        t_from = tok_num_val;
                        resc_token_counter = 2;
                    }
                    else
                    {
                        if (-1 == tok_num_val)
                        {
                            resc_state = failed_st;
                        }
                        ++resc_index;
                        if (!check_if_resource_exists(resc_name))
                        {
                            global_resources = create_node(global_task_resource_t);
                            if (resc_name != NULL)
                            {
                                global_resources->name = strdup(resc_name);
                            }
                            global_resources->used_by = NULL;
                            create_and_add_node_to_list(&resources, global_resources, global_task_resource_t);
                        }

                        local_resources = create_node(local_task_resource_t);
                        local_resources->name = resc_name;
                        local_resources->t_from = t_from;
                        local_resources->t_from_elapsed = 0;
                        local_resources->t_for = tok_num_val;
                        local_resources->t_for_elapsed = 0;

                        resc_token_counter = 0;
                        create_and_add_node_to_list(&resc_list_task, local_resources, local_task_resource_t);

                        if (resc_index <= resc_num)
                        {
                            task_state = finished_st;
                            resc_state = finished_st;
                        }
                    }
                }
            }
            token = strtok_r(NULL, delimiter, &saveptr1);
        }

        if (task_state == finished_st)
        {
            global_stats = create_node(global_task_stats_t);
            global_stats->name = strdup(task->name);
            task->global_stats = global_stats;
            task->resources = resc_list_task;
            task->task_state = created;

            local_stats = create_node(local_task_stats_t);
            local_stats->t_remaining_d = task->t_remaining_d;
            local_stats->t_d = task->t_d;
            task->local_stats = local_stats; /* TODO: should be freed! Check if it really is*/

            create_and_add_node_to_list(&stats, global_stats, global_task_stats_t);
            create_and_add_node_to_list(&tasks, task, task_t);
            resc_list_task = NULL;
        }
        else
        {
            if (task->name != NULL)
            {
                free(task->name);
            }
            free(task);
        }
    }
    untouched_tasks = copy_list(tasks);
    free(line);
    fclose(fp);
}

/**
 * @brief the main function prepares everything for the simulation.
 *
 * following tasks are handled by this function:
 * - it parses the flags and makes sure there is no wrong combination
 * - it calls the right scheduler based on the flags
 *
 * @param argc is the argument counter that gives information about how many arguments are passed
 * @param argv is the argument vector that holds all passed arguments
 * @return zero, if execution was successful.
 */
int main(int argc, char **argv)
{
    int arg_index;
    const char *input_fp; /* input file path */

    /* scheduler simulator flags */
    const char *nokill_string;
    const char *kill_string;
    const char *dm_string;
    const char *edf_string;
    const char *pip_string;

    void (*scheduler)(list_node *, list_node *);

    input_fp = NULL;
    if (argc > 1)
    {
        nokill_string = (char const *)"-nokill";
        kill_string = (char const *)"-kill";
        dm_string = (char const *)"-dm";
        edf_string = (char const *)"-edf";
        pip_string = (char const *)"-pip";

        /* flags */
        for (arg_index = 1; arg_index < argc; ++arg_index)
        {
            if (0 == strcmp(argv[arg_index], nokill_string))
            {
                flag_nokill = true;
            }
            else if (0 == strcmp(argv[arg_index], kill_string))
            {
                flag_kill = true;
            }
            else if (0 == strcmp(argv[arg_index], dm_string))
            {
                flag_dm = true;
            }
            else if (0 == strcmp(argv[arg_index], edf_string))
            {
                flag_edf = true;
            }
            else if (0 == strcmp(argv[arg_index], pip_string))
            {
                flag_pip = true;
            }
            else
            {
                input_fp = argv[arg_index];
            }
        }
    }
    else
    {
        fprintf(stderr, "simulator: please provide some arguments.\n");
        exit(EXIT_FAILURE);
    }

    if (flag_kill && flag_nokill)
    {
        fprintf(stderr, "simulator: cannot have both nokill_string and kill_string flags.\n");
        exit(EXIT_FAILURE);
    }

    if (NULL == input_fp)
    {
        fprintf(stderr, "simulator: please provide an input file.\n");
        exit(EXIT_FAILURE);
    }

    if (flag_dm)
    {
        scheduler = &prioritize_task_dm;
    }
    else if (flag_edf)
    {
        scheduler = &prioritize_task_edf;
    }
    else
    {
        scheduler = NULL;
    }

    read_and_parse_input(input_fp);
    if (scheduler != NULL)
    {
        sim_scheduler(scheduler);
    }

    return 0;
}
