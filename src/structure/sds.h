//
// Created by Administrator on 2022/2/26.
//
/**
 * 用于动态表示字符串的结构声明。
 * 简单动态字符串(Simple Dynamic Strings, SDS)
 * 存储字符串和整型数据
 * 整个结构分配的字节数等于len+free+1(\0)
 */

#ifndef REDIS_DESIGN_SDS_H
#define REDIS_DESIGN_SDS_H

/*
 * 最大的预分配长度（字节）
 * 小于该值，分配与当前的len属性值相同的空闲空间
 * 大于该值，分配1MB
 */
#define SDS_MAX_PREALLOC (1024 * 1024)

#include <sys/types.h>
#include <stdarg.h>

typedef char * sds;

/*
 * 保存字符串对象的结构
 */
struct sds_str
{
    //记录buf数组中已使用字节的数量，等于SDS所保存字符串的长度
    int len;
    //记录buf数组中未使用字节的数量
    int free;
    //字节数组，用于保存字符串
    char buf[];
};

/*
 * 根据保存的字符串指针
 * 返回sds实际保存的字符串长度
 *
 * t = O(1)
 */
static inline size_t sds_len(const sds s)
{
    struct sds_str * ssp = (s - (sizeof(struct sds_str)));
    return ssp->len;
}

/*
 * 同上
 * 返回sds可用空间的长度
 *
 * t = O(1)
 */
static inline size_t sds_avail(const sds s)
{
    struct sds_str * ssp = (s - (sizeof(struct sds_str)));
    return ssp->free;
}

sds sds_new_len(const void * init, size_t init_len);
sds sds_new(const sds init);
sds sds_empty(void);
sds sds_dup(const sds s);
void sds_free(sds s);
sds sds_grow_zero(sds s, size_t len);
sds sds_cat_len(sds s, const void * t, size_t len);
sds sds_cat(sds s, const char * t);
sds sds_cat_sds(sds s, const sds t);
sds sds_copy_len(sds s, const char * t, size_t len);
sds sds_copy(sds s, const char * t);

sds sds_trim(sds s, const char * c);
void sds_range(sds s, int start, int end);
void sds_clear(sds s);
int sds_cmp(const sds s1, const sds s2);

//Low level functions exposed to the user API
sds sdsMakeRoomFor(sds s, size_t add_len);
void sdsIncrLen(sds s, int incr);
sds sdsRemoveFreeSpace(sds s);
size_t sdsAllocSize(sds s);

/*
 * redis5.0的更新。
 */
//struct __attribute__((_packed_))sdshdr5
//{
//    //低3位存储类型，高5位存储长度
//    unsigned char flags;
//    char buf[];
//};

#endif //REDIS_DESIGN_SDS_H
