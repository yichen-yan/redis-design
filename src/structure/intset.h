//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_INTSET_H
#define REDIS_DESIGN_INTSET_H

#include <stdint.h>

/**
 * 用于保存整数值的整数集合，保存类型为int16_t、int32_t、int64_t的整数值，
 * 并保证不会出现重复元素。
 */
typedef struct intset
{
    uint32_t encoding;  //编码方式
    uint32_t length;    //集合包含的元素数量
    int8_t contents[];  //保存元素的数组
} intset;

intset * intsetNew(void);
intset * intsetAdd(intset * is, int64_t value, uint8_t * success);
intset * intsetRemove(intset * is, int64_t value, int * success);
uint8_t intsetFind(intset * is, int64_t value);
int64_t intsetRandom(intset * is);
uint8_t intsetGet(intset * is, uint32_t pos, int64_t * value);
uint32_t intsetLen(intset * is);
size_t intsetBlobLen(intset * is);

#endif //REDIS_DESIGN_INTSET_H
