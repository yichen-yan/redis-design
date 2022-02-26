//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_ZIPLIST_H
#define REDIS_DESIGN_ZIPLIST_H

#include <stdint.h>

/**
 * 压缩列表的定义。
 * 一个压缩列表可以包含任意数量的节点，每个节点可以保存一个字节数组或者一个整数值。
 */
typedef struct ziplist
{
    uint32_t zlbytes;   //压缩列表占用的内存字节数
    uint32_t zltail;    //压缩列表表尾的偏移量
    uint16_t zllen;     //压缩列表的节点总数
                        //压缩列表的节点
    uint8_t zlend;      //特殊值0xFF（255），用于标记压缩列表的末端
};

/**
 * 压缩列表节点的定义。
 * 每个节点由previous_entry_length、encoding、content三部分组成。
 */
// typedef struct zl_entry
// {
//
// };

#endif //REDIS_DESIGN_ZIPLIST_H
