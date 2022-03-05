//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_DICT_H
#define REDIS_DESIGN_DICT_H

#include <stdint.h>

//字典的操作状态
#define DICT_OK 0   //操作成功
#define DICT_ERR 1  //操作失败

#define DICT_NOT_USED(v) ((void)v)

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
 * 字典类型特定函数
 *
 * dictType结构保存了一簇用于操作特定类型键值对的函数
 * 可以为用途不同的字典设置不同的类型特定函数
 *
 * privData属性保存了需要传给函数的可选参数
*/
typedef struct dictType
{
    //计算哈希值的函数
    unsigned int (*hashFunction)(const void * key);
    //复制键的函数
    void * (*keyDup)(void * privData, const void * key);
    //复制值的函数
    void * (*valDup)(void * privData, const void * obj);
    //对比键的函数
    int (*keyCompare)(void * privData, const void * key1, const void * key2);
    //销毁键的函数
    void (*keyDestructor)(void * privData, void * key);
    //销毁值的函数
    void (*valDestructor)(void * privData, void * obj);
} dictType;

/**
* 哈希表结构的声明
 *
 * 每个字典包含了两个哈希表，从而实现了渐进式rehash
*/
typedef struct dictht
{
    dictEntry ** table;     //哈希表数组
    unsigned long size;     //哈希表大小
    unsigned long sizeMask; //哈希表大小的掩码，用于计算索引值,总是等于size-1
    unsigned long used;     //该哈希表已有节点的数量
} dictht;

 /**
  * 字典结构的声明
  * ht包含两个哈希表，一般使用第一个，只有在对第二个进行rehash是才使用。
  */
 typedef struct dict
 {
     dictType * type;   //类型特定的操作函数
     void * privData;   //私有数据，保存了需要传递给类型特定函数的可选参数
     dictht ht[2];      //哈希表
     int rehashIndex;   //rehash索引，当rehash不在进行时，值为-1
     int iterators;     //目前正在运行的安全迭代器数量
 } dict;

/**
 * 字典迭代器
 *
 * 如果 safe 属性的值为 1 ，那么在迭代进行的过程中，
 * 程序仍然可以执行 dictAdd 、 dictFind 和其他函数，对字典进行修改。
 *
 * 如果 safe 不为 1 ，那么程序只会调用 dictNext 对字典进行迭代，
 * 而不对字典进行修改。
 */
typedef struct dictIterator
{
    dict * d;   //被迭代的字典

    //table : 正在被迭代的哈希表号码，0或者1
    //index : 迭代器目前所指向的哈希表索引位置
    //safe : 标识迭代器是否安全
    int table, index, safe;

    // entry ：当前迭代到的节点的指针
    // nextEntry ：当前迭代节点的下一个节点
    //             因为在安全迭代器运作时， entry 所指向的节点可能会被修改，
    //             所以需要一个额外的指针来保存下一个节点的位置，
    //             从而防止指针丢失
    dictEntry * entry, * nextEntry;

    long long fingerprint;
} dictIterator;

typedef void (dictScanFunction)(void * privData, const dictEntry * de);

/**
 * 哈希表的初始大小
 */
#define DICT_HT_INITIAL_SIZE    4

/* ------------------------------- Macros ------------------------------------*/
// 释放给定字典节点的值
#define dictFreeVal(d, entry) \
    if ((d)->type->valDestructor) \
        (d)->type->valDestructor((d)->privData, (entry)->v.val)

// 设置给定字典节点的值
#define dictSetVal(d, entry, _val_) do { \
    if ((d)->type->valDup) \
        entry->v.val = (d)->type->valDup((d)->privData, _val_); \
    else \
        entry->v.val = (_val_); \
} while(0)

// 将一个有符号整数设为节点的值
#define dictSetSignedIntegerVal(entry, _val_) \
    do { entry->v.s64 = _val_; } while(0)

// 将一个无符号整数设为节点的值
#define dictSetUnsignedIntegerVal(entry, _val_) \
    do { entry->v.u64 = _val_; } while(0)

// 释放给定字典节点的键
#define dictFreeKey(d, entry) \
    if ((d)->type->keyDestructor) \
        (d)->type->keyDestructor((d)->privData, (entry)->key)

// 设置给定字典节点的键
#define dictSetKey(d, entry, _key_) do { \
    if ((d)->type->keyDup) \
        entry->key = (d)->type->keyDup((d)->privData, _key_); \
    else \
        entry->key = (_key_); \
} while(0)

// 比对两个键
#define dictCompareKeys(d, key1, key2) \
    (((d)->type->keyCompare) ? \
        (d)->type->keyCompare((d)->privData, key1, key2) : \
        (key1) == (key2))

// 计算给定键的哈希值
#define dictHashKey(d, key) (d)->type->hashFunction(key)
// 返回获取给定节点的键
#define dictGetKey(he) ((he)->key)
// 返回获取给定节点的值
#define dictGetVal(he) ((he)->v.val)
// 返回获取给定节点的有符号整数值
#define dictGetSignedIntegerVal(he) ((he)->v.s64)
// 返回给定节点的无符号整数值
#define dictGetUnsignedIntegerVal(he) ((he)->v.u64)
// 返回给定字典的大小
#define dictSlots(d) ((d)->ht[0].size+(d)->ht[1].size)
// 返回字典的已有节点数量
#define dictSize(d) ((d)->ht[0].used+(d)->ht[1].used)
// 查看字典是否正在 rehash
#define dictIsRehashing(ht) ((ht)->rehashIndex != -1)

/* API */
dict * dictCreate(dictType * type, void * privDataPtr);
int dictExpand(dict * d, unsigned long size);
int dictAdd(dict * d, void * key, void * val);
dictEntry * dictAddRaw(dict * d, void * key);
int dictReplace(dict * d, void * key, void * val);
dictEntry * dictReplaceRaw(dict * d, void * key);
int dictDelete(dict * d, const void * key);
int dictDeleteNoFree(dict * d, const void * key);
void dictRelease(dict * d);
dictEntry * dictFind(dict * d, const void * key);
void * dictFetchValue(dict * d, const void * key);
int dictResize(dict * d);
dictIterator * dictGetIterator(dict * d);
dictIterator * dictGetSafeIterator(dict * d);
dictEntry * dictNext(dictIterator * iter);
void dictReleaseIterator(dictIterator * iter);
dictEntry * dictGetRandomKey(dict * d);
int dictGetRandomKeys(dict * d, dictEntry ** des, int count);
void dictPrintStats(dict * d);
unsigned int dictGenHashFunction(const void * key, int len);
unsigned int dictGenCaseHashFunction(const unsigned char * buf, int len);
void dictEmpty(dict * d, void(callback)(void *));
void dictEnableResize(void);
void dictDisableResize(void);
int dictRehash(dict * d, int n);
int dictRehashMilliseconds(dict * d, int ms);
void dictSetHashFunctionSeed(unsigned int initVal);
unsigned int dictGetHashFunctionSeed(void);
unsigned long dictScan(dict * d, unsigned long v, dictScanFunction * fn, void * privData);

/* Hash table types */
extern dictType dictTypeHeapStringCopyKey;
extern dictType dictTypeHeapStrings;
extern dictType dictTypeHeapStringCopyKeyValue;

#endif //REDIS_DESIGN_DICT_H
