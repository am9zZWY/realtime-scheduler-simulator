#ifndef SIMULATION_H
#define SIMULATION_H

#define MAX_PRIORITIES 20

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "memhelper.h"

typedef enum task_state
{
    ready,
    created,
    blocked,
    removed,
    killed
} task_state;

typedef enum node_type
{
    node_t,
    task_t,
    global_task_resource_t,
    local_task_resource_t,
    global_task_stats_t,
    local_task_stats_t,
    global_blocking_stats_t,
    killed_task_t
} node_type;

typedef struct global_task_resource
{
    char *name;
    struct task_struct *used_by;
} global_task_resource;

typedef struct local_task_resource
{
    char *name;
    unsigned int t_from;
    unsigned int t_from_elapsed;
    unsigned int t_for;
    unsigned int t_for_elapsed;
} local_task_resource;

typedef struct global_task_stats
{
    char *name;             /*!< Name of resource */
    unsigned int t_elapsed; /*!< Elapsed time */
} global_task_stats;

typedef struct global_blocking_stats
{
    char *name; /*!< Name of task> */
    unsigned int t_blocked;
} global_blocking_stats;

typedef struct killed_task
{
    char *name;                  /*!< Name of killed task */
    int task_num;                /*!< Number of killed task instance. */
    unsigned int task_kill_time; /*!< Tick when task was killed. */
} killed_task;

typedef struct local_task_stats
{
    char *resource_name;
    unsigned int t_d;           /*!< Deadline */
    unsigned int t_remaining_d; /*!< Remaining deadline */
} local_task_stats;

typedef struct task_struct
{
    char *name;                 /*!< Name of the task */
    int task_num;               /*!< Number of task instance.\n Tasks can have multiple instances that are differentiated via task_num */
    int task_id;                /*!< Maybe \deprecated Unique identifier for task to avoid strcmp */
    task_state task_state;      /*!< Flag used to differentiate between different task states */
    unsigned int t_ph;          /*!< Phase */
    unsigned int t_per;         /*!< Period */
    unsigned int t_e;           /*!< Execution time */
    unsigned int t_remaining_e; /*!< Remaining execution time */
    unsigned int t_d;           /*!< Deadline */
    unsigned int t_remaining_d; /*!< Remaining deadline */
    unsigned int t_elapsed;     /*!< Elapsed time */
    struct local_task_stats *local_stats;
    struct global_task_stats *global_stats; /*!< Stats to determine reaction time */

    int task_priority; /*!< Task priority */
    struct list_node *task_priorities;

    char *blocked_by_resc;       /*!< Name of resource that causes blocking */
    struct list_node *resources; /*!< List of resources that task will allocate */

    int blocking_time;
} task_struct;

typedef struct list_node
{
    node_type node_type;
    union
    {
        task_struct *task;
        global_task_resource *global_task_resource;
        global_task_stats *global_task_stats;
        local_task_resource *local_task_resource;
        local_task_stats *local_task_stats;
        killed_task *killed_task;
        global_blocking_stats *global_blocking_stats;
        struct list_node *list_node;
    };
    struct list_node *next;
} list_node;

static list_node *nodes = NULL; /*!< Global list of nodes. Every created node is also added here. This avoids unwanted memory leaks or SIGSEGSs. */

void *copy_node(void *src, node_type node_type);

/**
 * @brief Iterates through task list/queue and executes for each
 * task a function passed via reference.
 *
 * @param head is the start of the queue.
 * @param fct is the function that is executed for each task.
 */
void iterator(list_node *head, void (*fct)(list_node *))
{
    list_node *tmp_task = head;
    while (NULL != tmp_task)
    {
        if (tmp_task == tmp_task->next)
        {
            fprintf(stderr, "iterator: Self assigned next pointer. Exiting to avoid recursion.\n");
            exit(-1);
        }
        fct(tmp_task);
        tmp_task = tmp_task->next;
    }
}

/**
 * @brief Return length of list.
 *
 * @param head is the first element of the list
 * @return int is the length of the list.
 */
int length(list_node *head)
{
    int length = 0;
    list_node *tmp = head;
    while (tmp != NULL)
    {
        ++length;
        tmp = tmp->next;
    }
    return length;
}

/**
 * @brief Prints task.
 * @param head is a reference to the task.
 */
void print_task(list_node *head)
{
    if ((NULL != head) && (NULL != head->task))
    {
        printf("%s%d", head->task->name, head->task->task_num);
    }
}

/**
 * @brief Prints task priority.
 * @param head is a reference to the task.
 */
void print_task_priority(list_node *head)
{
    if ((NULL != head) && (NULL != head->task) && (NULL != head->task->name))
    {
        printf("%s%d: %d", head->task->name, head->task->task_num, head->task->task_priority);
        if (NULL != head->next)
        {
            printf(", ");
        }
    }
}

/**
 * @brief Prints task resources.
 * @param head is a reference to the task.
 */
void print_task_resource(list_node *head)
{
    if ((NULL != head) && (NULL != head->local_task_resource) && (NULL != head->local_task_resource->name))
    {
        printf("%s", head->local_task_resource->name);
        if (NULL != head->next)
        {
            printf(", ");
        }
    }
}

/**
 * @brief Prints task resources.
 * @param head is a reference to the task.
 */
void print_task_resources(list_node *head)
{
    if ((NULL != head) && (NULL != head->task) && (NULL != head->task->name) && (NULL != head->task->resources))
    {
        printf("%s%d: ", head->task->name, head->task->task_num);
        iterator(head->task->resources, &print_task_resource);
        if (NULL != head->next)
        {
            printf(", ");
        }
    }
}

/**
 * @brief Prints task global_stats.
 * @param head is a reference to the task global_stats.
 */
void print_task_stats(list_node *head)
{
    if ((NULL != head) && (NULL != head->global_task_stats) && (NULL != head->global_task_stats->name))
    {
        printf("Task: %s\tmax. Reaktionszeit: %d\n", head->global_task_stats->name, head->global_task_stats->t_elapsed);
    }
}

/**
 * @brief Prints killed task.
 * @param head is a reference to the killed task.
 */
void print_killed_task(list_node *head)
{
    if ((NULL != head) && (NULL != head->killed_task) && (NULL != head->killed_task->name))
    {
        printf("%s%d(t:%d)", head->killed_task->name, head->killed_task->task_num, head->killed_task->task_kill_time);
        if (NULL != head->next)
        {
            printf(", ");
        }
    }
}


/**
 * @brief Prints blocking times.
 * @param head is a reference to the killed task.
 */
void print_blocking_stats(list_node *head)
{
    if ((NULL != head) && (NULL != head->global_blocking_stats))
    {
        printf("%s: %d", head->global_blocking_stats->name, head->global_blocking_stats->t_blocked);
        if (NULL != head->next)
        {
            printf(", ");
        }
    }
}

/**
 * @brief Prints linked list by iterating through it and calling a function by reference.
 * @param head is the first element of the list.
 */
void print_list(list_node *head)
{
    if (NULL != head)
    {
        switch (head->node_type)
        {
        case global_task_stats_t:
            iterator(head, &print_task_stats);
            break;
        case killed_task_t:
            iterator(head, &print_killed_task);
            break;
        case global_blocking_stats_t:
            iterator(head, &print_blocking_stats);
            break;
        default:
            iterator(head, &print_task);
            break;
        }
    }
}

/**
 * @brief Creates a new node of a specific type. Can be seen as a constructor.
 * And sets all default values to avoid unitialized values that can cause unintended behaviour!
 *
 * @param node_type is the type of the node that should be created.
 * @return a reference to the new node.
 */
void *create_node(node_type node_type)
{
    list_node *node;
    task_struct *task;
    killed_task *killed_task;
    global_task_resource *global_task_resource;
    local_task_resource *local_task_resource;
    global_task_stats *global_task_stats;
    global_blocking_stats *global_blocking_stats;
    struct local_task_stats *local_task_stats;

    switch (node_type)
    {
    case task_t:
        task = Malloc(sizeof(struct task_struct));
        /* common properties */
        task->name = NULL;
        task->task_state = created;
        task->task_num = 0;

        /* timings */
        task->t_ph = 0;
        task->t_per = 0;
        task->t_e = 0;
        task->t_remaining_e = 0;
        task->t_d = 0;
        task->t_remaining_d = 0;
        task->t_elapsed = 0;
        task->local_stats = NULL;
        task->global_stats = NULL;

        /* priority */
        task->task_priority = 0;
        task->task_priorities = NULL;

        /* resources */
        task->blocked_by_resc = NULL;
        task->resources = NULL;
        return (void *)task;
    case global_task_resource_t:
        global_task_resource = Malloc(sizeof(struct global_task_resource));
        global_task_resource->name = NULL;
        global_task_resource->used_by = NULL;
        return (void *)global_task_resource;
    case global_task_stats_t:
        global_task_stats = Malloc(sizeof(struct global_task_stats));
        global_task_stats->name = NULL;
        global_task_stats->t_elapsed = 0;
        return (void *)global_task_stats;
    case local_task_stats_t:
        local_task_stats = Malloc(sizeof(struct task_struct));
        local_task_stats->t_remaining_d = 0;
        local_task_stats->t_d = 0;
        local_task_stats->resource_name = NULL;
        return (void *)local_task_stats;
    case killed_task_t:
        killed_task = Malloc(sizeof(struct killed_task));
        killed_task->name = NULL;
        killed_task->task_kill_time = 0;
        killed_task->task_num = 0;
        return (void *)killed_task;
    case local_task_resource_t:
        local_task_resource = Malloc(sizeof(struct local_task_resource));
        local_task_resource->name = NULL;
        local_task_resource->t_for = 0;
        local_task_resource->t_for_elapsed = 0;
        local_task_resource->t_from = 0;
        local_task_resource->t_from_elapsed = 0;
        return (void *)local_task_resource;
    case global_blocking_stats_t:
        global_blocking_stats = Malloc(sizeof(struct global_blocking_stats));
        global_blocking_stats->name = NULL;
        global_blocking_stats->t_blocked = 0;
        return (void *)global_blocking_stats;
    default:
        node = Malloc(sizeof(struct list_node));
        node->node_type = node_t;
        node->task = NULL;
        node->killed_task = NULL;
        node->global_task_resource = NULL;
        node->local_task_resource = NULL;
        node->global_task_stats = NULL;
        node->next = NULL;
        node->list_node = NULL;
        return node;
    }
}

list_node *get_last_node(list_node *list)
{
    list_node *tmp_node = list;
    if (tmp_node == NULL)
    {
        return NULL;
    }
    while (NULL != tmp_node->next)
    {
        if (tmp_node == tmp_node->next)
        {
            fprintf(stderr,
                    "create_and_add_node_to_list: Self assigned next pointer. Exiting to avoid recursion.\n");
            exit(-1);
        }
        tmp_node = tmp_node->next;
    }
    return tmp_node;
}

/**
 * @brief Adds a node the end of a list.
 *
 * @param list is a pointer to the start of the list. It traverses through the list till it finds the last node.
 * @param new_node is the node that should be added.
 */
void add_node_to_list(list_node **list, list_node *new_node)
{
    /* if (new_node != NULL)
    {
        new_node->next = NULL;
    } */

#ifdef DEBUG
    if (new_node == NULL)
    {
        printf("WARNING: add_node_to_list: new_node is NULL!\n");
    }
#endif

    if (NULL == *list)
    {
        (*list) = new_node;
    }
    else
    {
        get_last_node(*list)->next = new_node;
    }
}

/**
 * @brief Creates a new node as a wrapper for a struct and adds it to the end of a list.
 *
 * @param task is a reference to the task that has to be added. It is wrapped in `struct list_node`. Must not be NULL!
 * @param list pointer to list.
 */
void create_and_add_node_to_list(list_node **list, void *node, node_type node_type)
{
    list_node *new_node;
    list_node *new_node_of_node;

    if (NULL != node)
    {
        new_node = create_node(node_t);
        new_node->node_type = node_type;

        switch (node_type)
        {
        case killed_task_t:
            new_node->killed_task = (killed_task *)node;
            break;
        case global_task_resource_t:
            new_node->global_task_resource = (global_task_resource *)node;
            break;
        case local_task_resource_t:
            new_node->local_task_resource = (local_task_resource *)node;
            break;
        case global_task_stats_t:
            new_node->global_task_stats = (global_task_stats *)node;
            break;
        default:
            new_node->task = (task_struct *)node;
            break;
        }
        /* add new_node to list with all created nodes. This allows later easy memory management. */
        new_node_of_node = create_node(node_t);
        new_node_of_node->node_type = node_t;
        new_node_of_node->list_node = new_node;

        new_node->next = NULL;
        new_node_of_node->next = NULL;
        new_node_of_node->list_node->next = NULL;

        add_node_to_list(&nodes, new_node_of_node);
        add_node_to_list(list, new_node);
    }
}

/**
 * @brief Creates a deep copy of a list.
 *
 * @param src is the source task.
 * @return a reference to the first element of the list.
 */
list_node *copy_list(list_node *src)
{
    void *node;
    list_node *copy_list = NULL;
    list_node *tmp_src = src;
    while (tmp_src != NULL)
    {
        switch (tmp_src->node_type)
        {
        case node_t:
            node = tmp_src->list_node;
            break;
        case task_t:
            node = tmp_src->task;
            break;
        case global_task_resource_t:
            node = tmp_src->global_task_resource;
            break;
        case local_task_resource_t:
            node = tmp_src->local_task_resource;
            break;
        case global_task_stats_t:
            node = tmp_src->global_task_stats;
            break;
        case killed_task_t:
            node = tmp_src->killed_task;
            break;
        default:
            node = tmp_src->list_node;
            break;
        }
        create_and_add_node_to_list(&copy_list, copy_node(node, tmp_src->node_type), tmp_src->node_type);
        tmp_src = tmp_src->next;
    }
    return copy_list;
}

/**
 * @brief Creates a deep copy of a node.
 *
 * @param src is the source task.
 * @return a reference to the task.
 */
void *copy_node(void *src, node_type node_type)
{
    task_struct *tmp_task;
    local_task_stats *tmp_local_task_stats;
    local_task_resource *tmp_resource_stats;

    if (src == NULL)
    {
        return NULL;
    }

    switch (node_type)
    {
    case task_t:
        tmp_task = create_node(task_t);
        memcpy(tmp_task, ((task_struct *)src), sizeof(task_struct));
        tmp_task->name = strdup(((task_struct *)src)->name);
        if (((task_struct *)src)->blocked_by_resc != NULL)
        {
            tmp_task->blocked_by_resc = strdup(((task_struct *)src)->blocked_by_resc);
        }
        tmp_task->local_stats = copy_node(tmp_task->local_stats, local_task_stats_t);
        tmp_task->resources = copy_list(((task_struct *)src)->resources);
        return (void *)tmp_task;
    case local_task_stats_t:
        tmp_local_task_stats = create_node(local_task_stats_t);
        memcpy(tmp_local_task_stats, ((local_task_stats *)src), sizeof(local_task_stats));
        if (((local_task_stats *)src)->resource_name != NULL)
        {
            tmp_local_task_stats->resource_name = strdup(((local_task_stats *)src)->resource_name);
        }
        return (void *)tmp_local_task_stats;
    case local_task_resource_t:
        tmp_resource_stats = create_node(local_task_resource_t);
        memcpy(tmp_resource_stats, ((local_task_resource *)src), sizeof(local_task_resource));
        tmp_resource_stats->name = strdup(((local_task_resource *)src)->name);
        return (void *)tmp_resource_stats;
    default:
        break;
    }
    return NULL;
}

/**
 * @brief Frees a node of type list_node.
 * If list_node has a reference to another list_node then both are freed.
 *
 * @param node is pointer to a node.
 */
void free_node(list_node *node)
{
    if (NULL != node)
    {
        switch (node->node_type)
        {
        case task_t:
            if (NULL != node->task->blocked_by_resc)
            {
                free(node->task->blocked_by_resc);
            }
            free(node->task->local_stats);
            free(node->task->name);
            free(node->task);
            break;
        case global_task_stats_t:
            free(node->global_task_stats->name); /* strdup */
            free(node->global_task_stats);
            break;
        case killed_task_t:
            free(node->killed_task->name);
            free(node->killed_task);
            break;
        case local_task_resource_t:
            free(node->local_task_resource->name);
            free(node->local_task_resource);
            break;
        case local_task_stats_t:
            // free(node->local_task_stats);
            break;
        case global_blocking_stats_t:
            free(node->global_blocking_stats->name);
            free(node->global_blocking_stats);
            break;
        case node_t : if (node == node->list_node)
            {
                fprintf(stderr, "free_node: recursive free. Aborting!\n");
                exit(-1);
            }
            free_node(node->list_node);
            break;
        default:
            break;
        }
        free(node);
    }
}

/**
 * @brief Removes a node from a list.
 * @param **list is a reference to the origin list.
 * @param *node_to_remove is a pointer of the node that should be removed.
 * @param **prev_node is the previous node in the list.
 */
void remove_node_from_list(list_node **list, list_node *node_to_remove, list_node **prev_node)
{
    if (*list == node_to_remove)
    {
        *list = node_to_remove->next;
        *prev_node = *list;
    }
    else
    {
        (*prev_node)->next = node_to_remove->next;
    }
}

/**
 * @brief Removes a node from list without the need of specifying a previous pointer.
 * @param **list is a reference to the origin list.
 * @param *node_to_remove is a pointer of the node that should be removed.
 */
void remove_node_from_list_noprev(list_node **list, list_node *node_to_remove)
{
    list_node **tmp_prev = list;
    list_node *tmp_list = *list;
    while (tmp_list != NULL)
    {
        if (tmp_list == node_to_remove)
        {
            remove_node_from_list(list, node_to_remove, tmp_prev);
            break;
        }
        *tmp_prev = tmp_list;
        tmp_list = tmp_list->next;
    }
}

/**
 * @brief Frees all entries of a task list to avoid memory leak.
 * @param head is the first element of the list.
 */
void free_list(list_node *head)
{
    list_node *tmp;
    while (NULL != (tmp = head))
    {
        head = head->next;
        free_node(tmp);
    }
}

/**
 * @brief call the free_list() function which frees all nodes created during runtime.
 * All nodes are collected in a global list.
 *
 * This decision was taken because freeing during execution may cause a lot of debugging work.
 */
void free_all()
{
    free_list(nodes);
}

#endif