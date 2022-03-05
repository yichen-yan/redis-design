//
// Created by Administrator on 2022/3/5.
//

#ifndef REDIS_DESIGN_REDIS_ASSERT_H
#define REDIS_DESIGN_REDIS_ASSERT_H

#include <unistd.h>

#define assert(_e) ((_e) ? (void)0 : (_redisAssert(#_e, __FILE__, __LINE__), _exit(1)))

void _redisAssert(char * estr, char * file, int line);

#endif //REDIS_DESIGN_REDIS_ASSERT_H
