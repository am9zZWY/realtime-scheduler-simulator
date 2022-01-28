#ifndef MEMHELPER_H
#define MEMHELPER_H

#include <stdlib.h>
#include <stdio.h>

void *Malloc(size_t size)
{
    void *ptr = malloc(size);
    if (NULL == ptr)
    {
        fprintf(stderr, "create_node: malloc failed.\n");
        exit(-1);
    }
    return ptr;
}

#endif