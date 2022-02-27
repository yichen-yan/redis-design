//
// Created by Administrator on 2022/2/27.
//

#include <string.h>
#include "sds.h"
#include "zmalloc.h"

/*
 * 根据给定的初始化字符串 init 和字符串长度 init_len创建一个新的 sds
 *
 * 参数
 *  init ：初始化字符串指针
 *  init_len ：初始化字符串的长度
 *
 * 返回值
 *  sds ：创建成功返回 sds_str 相对应的 sds
 *        创建失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sds_new_len(const void * init, size_t init_len)
{
    struct sds_str * ssp;

    //根据init是否引用初始化内容，进行不同的内存分配
    //init为空指针时，需要按照init_len的长度将内存置零。
    //T = O(N)
    if (init)
    {
        ssp = z_malloc(sizeof(struct sds_str) + init_len + 1);
    }
    else
    {
        ssp = z_calloc(sizeof(struct sds_str) + init_len + 1);
    }

    //内存分配失败
    if (ssp == NULL) return NULL;

    ssp->len = init_len;
    ssp->free = 0;

    //如果有指定初始化内容，将它们复制到sds_str的buf中
    if (init_len && init)
        memcpy_s(ssp->buf,init_len, init, init_len);
    ssp->buf[init_len] = '\0';

    return (sds)ssp->buf;
}

/*
 * 根据给定字符串 init ，创建一个包含同样字符串的 sds
 *
 * 参数
 *  init ：如果输入为 NULL ，那么创建一个空白 sds
 *         否则，新创建的 sds 中包含和 init 内容相同字符串
 *
 * 返回值
 *  sds ：创建成功返回 sds_str 相对应的 sds
 *        创建失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sds_new(const sds init)
{
    size_t init_len = (init == NULL) ? 0 : strlen(init);
    return sds_new_len(init, init_len);
}

/*
 * 创建并返回一个只保存了空字符串 的 sds
 *
 * 返回值
 *  sds ：创建成功返回 sds_str 相对应的 sds
 *        创建失败返回 NULL
 *
 * 复杂度
 *  T = O(1)
 */
sds sds_empty(void)
{
    return sds_new_len("", 0);
}

/*
 * 复制给定 sds 的副本
 *
 * 返回值
 *  sds ：创建成功返回输入 sds 的副本
 *        创建失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sds_dup(const sds s)
{
    return sds_new_len(s, strlen(s));
}

/*
 * 释放给定的 sds
 *
 * 复杂度
 *  T = O(N)
 */
void sds_free(sds s)
{
    if (s == NULL) return;
    z_free(s - sizeof(struct sds_str));
}
