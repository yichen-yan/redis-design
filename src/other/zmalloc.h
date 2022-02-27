//
// Created by Administrator on 2022/2/27.
//

#ifndef REDIS_DESIGN_ZMALLOC_H
#define REDIS_DESIGN_ZMALLOC_H

#include <sys/types.h>
#include <malloc.h>

void * z_malloc(size_t size);
void * z_calloc(size_t size);
void * z_realloc(void * ptr, size_t size);
void z_free(void * ptr);

#endif //REDIS_DESIGN_ZMALLOC_H
