//
// Created by Administrator on 2022/2/27.
//

#include <assert.h>
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

/*
 * 对 sds 中 buf 的长度进行扩展，确保在函数执行之后，
 * buf 至少会有 add_len + 1 长度的空余空间
 * （额外的 1 字节是为 \0 准备的）
 *
 * 返回值
 *  sds ：扩展成功返回扩展后的 sds
 *        扩展失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sdsMakeRoomFor(sds s, size_t add_len)
{
    struct sds_str * ssp, * new_ssp;

    //获取s的可用空间
    size_t free = sds_avail(s);

    //空间足够，无需扩展
    if (free >= add_len) return s;

    size_t len, new_len;
    len = sds_len(s);
    ssp = (void *)(s - sizeof(struct sds_str));

    //扩展后的最小长度
    new_len = len + add_len;

    //根据SDS的空间预分配优化策略
    if (new_len < SDS_MAX_PREALLOC)
        new_len *= 2;
    else
        new_len += SDS_MAX_PREALLOC;

    //T = O(N)
    new_ssp = z_realloc(ssp, sizeof(struct sds_str) + new_len + 1);

    //内存不足，分配失败
    if (new_ssp == NULL) return NULL;

    //更新sds的空闲块大小
    new_ssp->free = new_len - len;

    return new_ssp->buf;
}

/* Increment the sds length and decrements the left free space at the
 * end of the string according to 'incr'. Also set the null term
 * in the new end of the string.
 *
 * 根据 incr 参数，增加 sds 的长度，缩减空余空间，
 * 并将 \0 放到新字符串的尾端
 *
 * This function is used in order to fix the string length after the
 * user calls sdsMakeRoomFor(), writes something after the end of
 * the current string, and finally needs to set the new length.
 *
 * 这个函数是在调用 sdsMakeRoomFor() 对字符串进行扩展，
 * 然后用户在字符串尾部写入了某些内容之后，
 * 用来正确更新 free 和 len 属性的。
 *
 * Note: it is possible to use a negative increment in order to
 * right-trim the string.
 *
 * 如果 incr 参数为负数，那么对字符串进行右截断操作。
 *
 * Usage example:
 *
 * Using sdsIncrLen() and sdsMakeRoomFor() it is possible to mount the
 * following schema, to cat bytes coming from the kernel to the end of an
 * sds string without copying into an intermediate buffer:
 *
 * 以下是 sdsIncrLen 的用例：
 *
 * oldlen = sdslen(s);
 * s = sdsMakeRoomFor(s, BUFFER_SIZE);
 * nread = read(fd, s+oldlen, BUFFER_SIZE);
 * ... check for nread <= 0 and handle it ...
 * sdsIncrLen(s, nread);
 *
 * 复杂度
 *  T = O(1)
 */
void sdsIncrLen(sds s, int incr)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));

    //确保sds的空间足够
    assert(ssp->free >= incr);

    ssp->len += incr;
    ssp->free -= incr;

    assert(ssp->free >= 0);

    //放置结尾符号
    s[ssp->len] = '\0';
}

/*
 * 回收 sds 中的空闲空间，
 * 回收不会对 sds 中保存的字符串内容做任何修改。
 *
 * 返回值
 *  sds ：内存调整后的 sds
 *
 * 复杂度
 *  T = O(N)
 */
sds sdsRemoveFreeSpace(sds s)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));

    //进行内存分配，让buf的长度刚好保存字符串的内容
    //T = O(N)
    ssp = z_realloc(ssp, sizeof(struct sds_str) + ssp->len + 1);
    ssp->free = 0;

    return ssp->buf;
}

/*
 * 返回给定 sds 分配的内存字节数
 *
 * 复杂度
 *  T = O(1)
 */
size_t sdsAllocSize(sds s)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));

    return sizeof(struct sds_str) + ssp->len + ssp->free + 1;
}

/* Grow the sds to have the specified length. Bytes that were not part of
 * the original length of the sds will be set to zero.
 *
 * if the specified length is smaller than the current length, no operation
 * is performed. */
/*
 * 将 sds 扩充至指定长度，未使用的空间以 0 字节填充。
 *
 * 返回值
 *  sds ：扩充成功返回新 sds ，失败返回 NULL
 *
 * 复杂度：
 *  T = O(N)
 */
sds sds_grow_zero(sds s, size_t len)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));
    size_t tot_len, cur_len = ssp->len;

    if (len <= cur_len) return s;

    //扩展sds
    //T = O(N)
    s = sdsMakeRoomFor(s, len - cur_len);
    if (s == NULL) return NULL;

    // 将新分配的空间用 0 填充，防止出现垃圾内容
    // T = O(N)
    ssp = (void *)(s - sizeof(struct sds_str));
    memset(s + cur_len, 0, (len - cur_len + 1));

    tot_len = ssp->len + ssp-> free;
    ssp->len = len;
    ssp->free = tot_len - ssp->len;

    return s;
}

/*
 * 将长度为 len 的字符串 t 追加到 sds 的字符串末尾
 *
 * 返回值
 *  sds ：追加成功返回新 sds ，失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sds_cat_len(sds s, const void * t, size_t len)
{
    struct sds_str * ssp;

    //原有字符串的长度
    size_t cur_len = sds_len(s);

    //扩展sds空间
    //T = O(N)
    s = sdsMakeRoomFor(s, len);

    //内存分配失败
    if (s == NULL) return NULL;

    ssp = (void *)(s - sizeof(struct sds_str));
    memcpy(s + cur_len, t, len);

    ssp->len = cur_len + len;
    ssp->free = ssp->free - len;

    s[cur_len + len] = '\0';

    return s;
}

/*
 * 将给定字符串 t 追加到 sds 的末尾
 *
 * 返回值
 *  sds ：追加成功返回新 sds ，失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sds_cat(sds s, const char * t)
{
    return sds_cat_len(s, t, strlen(t));
}

/*
 * 将另一个 sds 追加到一个 sds 的末尾
 *
 * 返回值
 *  sds ：追加成功返回新 sds ，失败返回 NULL
 *
 * 复杂度
 *  T = O(N)
 */
sds sds_cat_sds(sds s, const sds t)
{
    return sds_cat_len(s, t, sds_len(t));
}

/*
 * 将字符串 t 的前 len 个字符复制到 sds s 当中，
 * 并在字符串的最后添加终结符。
 *
 * 如果 sds 的长度少于 len 个字符，那么扩展 sds
 *
 * 复杂度
 *  T = O(N)
 *
 * 返回值
 *  sds ：复制成功返回新的 sds ，否则返回 NULL
 */
sds sds_copy_len(sds s, const char * t, size_t len)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));

    //sds的现有长度
    size_t tot_len = ssp->free + ssp->len;

    //如果s的buf长度不满足len，需要扩展buf。
    if (tot_len < len)
    {
        s = sdsMakeRoomFor(s, len - ssp->len);
        if (s == NULL) return NULL;
        ssp = (void *)(s - sizeof(struct sds_str));
        tot_len = ssp->free + ssp->len;
    }

    //复制内容
    //T = O(N)
    memcpy(s, t, len);

    s[len] = '\0';
    ssp->len = len;
    ssp->free = tot_len - len;

    return s;
}

/*
 * 将字符串复制到 sds 当中，
 * 覆盖原有的字符。
 *
 * 如果 sds 的长度少于字符串的长度，那么扩展 sds 。
 *
 * 复杂度
 *  T = O(N)
 *
 * 返回值
 *  sds ：复制成功返回新的 sds ，否则返回 NULL
 */
sds sds_copy(sds s, const char * t)
{
    return sds_copy_len(s, t, strlen(t));
}

/*
 * 对 sds 左右两端进行修剪，清除其中 c 指定的所有字符
 *
 * 比如 sds_trim(xxyyabcyyxy, "xy") 将返回 "abc"
 *
 * 复杂性：
 *  T = O(M*N)，M 为 SDS 长度， N 为 c 长度。
 */
sds sds_trim(sds s, const char * c)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));
    char * start, * end, * sp, *ep;
    size_t len;

    //设置和记录指针
    sp = start = s;
    ep = end = s + sds_len(s) - 1;

    //修剪，T = O(N^2)
    while (sp <= end && strchr(c, *sp)) sp++;
    while (ep > start && strchr(c, *ep)) ep--;

    //计算trim完毕之后剩余的字符串长度
    len = (sp > ep) ? 0 : ((ep - sp) + 1);

    //如果有需要，前移字符串内容
    //T = O(N)
    if (ssp->buf != sp) memmove(ssp->buf, sp, len);

    ssp->buf[len] = '\0';
    ssp->free = ssp->free + (ssp->len - len);
    ssp->len = len;

    return s;
}

/*
 * 按索引对截取 sds 字符串的其中一段
 * start 和 end 都是闭区间（包含在内）
 *
 * 索引从 0 开始，最大为 sds_len(s) - 1
 * 索引可以是负数， sds_len(s) - 1 == -1
 *
 * 复杂度
 *  T = O(N)
 */
void sds_range(sds s, int start, int end)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));
    size_t new_len, len = sds_len(s);

    if (len == 0) return;
    if (start < 0)
    {
        start = len + start;
        if (start < 0) start = 0;
    }
    if (end < 0)
    {
        end = len + end;
        if (end < 0) end = 0;
    }
    new_len = (start > end) ? 0 : (end - start) + 1;
    if (new_len != 0)
    {
        if (start >= (signed)len)
        {
            new_len = 0;
        }
        else if (end >= (signed)len)
        {
            end = len - 1;
            new_len = (start > end) ? 0 : (end - start) + 1;
        }
    }
    else
    {
        start = 0;
    }

    //如果有需要，对字符串进行移动
    //T = O(N)
    if (start && new_len) memmove(ssp->buf, ssp->buf + start, new_len);

    ssp->buf[new_len] = 0;
    ssp->free = ssp->free + (ssp->len - new_len);
    ssp->len = new_len;
}

/*
 * 在不释放 SDS 的字符串空间的情况下，
 * 重置 SDS 所保存的字符串为空字符串。
 *
 * 复杂度
 *  T = O(1)
 */
void sds_clear(sds s)
{
    struct sds_str * ssp = (void *)(s - sizeof(struct sds_str));

    ssp->free += ssp->len;
    ssp->len = 0;

    //惰性地删除了buf的内容
    ssp->buf[0] = '\0';
}

/*
 * 对比两个 sds ， strcmp 的 sds 版本
 *
 * 返回值
 *  int ：相等返回 0 ，s1 较大返回正数， s2 较大返回负数
 *
 * T = O(N)
 */
int sds_cmp(const sds s1, const sds s2)
{
    size_t l1, l2, min_len;
    int cmp;

    l1 = sds_len(s1);
    l2 = sds_len(s2);
    min_len = (l1 < l2) ? l1 : l2;
    cmp = memcmp(s1, s2, min_len);

    if (cmp == 0) return l1 - l2;

    return cmp;
}
