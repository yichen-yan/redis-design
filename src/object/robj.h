//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_ROBJ_H
#define REDIS_DESIGN_ROBJ_H

/**
 * Redis中的对象结构定义。
 */
typedef struct redis_object
{
    unsigned int type;  //类型
    unsigned encoding;  //编码
    void * ptr;         //指向底层实现数据结构的指针
} robj;

#endif //REDIS_DESIGN_ROBJ_H
