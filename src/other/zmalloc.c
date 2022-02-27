//
// Created by Administrator on 2022/2/27.
//
#include "zmalloc.h"

void * z_malloc(size_t size)
{
    void * ptr = malloc(size);

    if (ptr)
        return ptr;
    else
        return NULL;
}

void z_free(void * ptr)
{
    if (ptr == NULL)
        return;
    else
        free(ptr);
}
