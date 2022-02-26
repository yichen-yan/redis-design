//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_DICT_H
#define REDIS_DESIGN_DICT_H

#include <stdint.h>

/**
 * 哈希表结构的声明
 */
typedef struct dictht
{
    dictEntry ** table;     //哈希表数组
    unsigned long size;     //哈希表大小
    unsigned long sizemask; //哈希表大小的掩码，用于计算索引值,总是等于size-1
    unsigned long used;     //该哈希表已有节点的数量
} dictht;

/**
 * 哈希表节点的声明
 */
 typedef struct dictEntry
 {
     void * key;    //键
     union {
         void * val;
         uint64_t u64;
         int64_t  s64;
     } v;           //值
     struct dictEntry * next;   //指向下个哈希表节点，形成链表，解决了键冲突
 } dictEntry;

 /**
  * 字典结构的声明
  * ht包含两个哈希表，一般使用第一个，只有在对第二个进行rehash是才使用。
  */
 typedef struct dict
 {
     dictType * type;   //类型特定的操作函数
     void * privdata;   //私有数据，保存了需要传递给类型特定函数的可选参数
     dictht ht[2];      //哈希表
     int rehashidx;     //rehash索引，当rehash不在进行时，值为-1
 } dict;

 /**
  * dictType结构保存了一簇用于操作特定类型键值对的函数
  * 可以为用途不同的字典设置不同的类型特定函数
  */
 typedef struct dictType
 {
     unsighed int (*hashFunction)(const void * key);        //计算哈希值的函数
     void * (*keyDup)(void * privdata, const void * key);   //复制键的函数
     void * (*valDup)(void * privdata, const void * obj);   //复制值的函数
     int (*keyCompare)(void * privdata, const void * key1, const void * key2);  //对比键的函数
     void (*keyDestructor)(void * privdata, void * key);    //销毁键的函数
     void (*valDestructor)(void * privdata, void * obj);    //销毁值的函数
 } dictType;

#endif //REDIS_DESIGN_DICT_H
