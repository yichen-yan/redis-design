//
// Created by Administrator on 2022/3/4.
//

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"
#include "redisassert.h"

/* Using dictEnableResize() / dictDisableResize() we make possible to
 * enable/disable resizing of the hash table as needed. This is very important
 * for Redis, as we use copy-on-write and don't want to move too much memory
 * around when there is a child performing saving operations.
 *
 * 通过 dictEnableResize() 和 dictDisableResize() 两个函数，
 * 程序可以手动地允许或阻止哈希表进行 rehash ，
 * 这在 Redis 使用子进程进行保存操作时，可以有效地利用 copy-on-write 机制。
 *
 * Note that even when dict_can_resize is set to 0, not all resizes are
 * prevented: a hash table is still allowed to grow if the ratio between
 * the number of elements and the buckets > dict_force_resize_ratio.
 *
 * 需要注意的是，并非所有 rehash 都会被 dictDisableResize 阻止：
 * 如果已使用节点的数量和字典大小之间的比率，
 * 大于字典强制 rehash 比率 dict_force_resize_ratio ，
 * 那么 rehash 仍然会（强制）进行。
 */
//指示字典是否启用rehash的标识
static int dict_can_resize = 1;
//强制rehash的比率
static unsigned int dict_force_resize_ratio = 5;

/* private prototypes */
static int _dictExpandIfNeeded(dict * ht);
static unsigned long _dictNextPower(unsigned long size);
static int _dictKeyIndex(dict * ht, const void * key);
static int _dictInit(dict * ht, dictType * type, void * privDataPtr);

/* Thomas Wang's 32 bit Mix Function */
unsigned int dictIntHashFunction(unsigned int key)
{
    key += ~(key << 15);
    key ^= (key >> 10);
    key += (key << 3);
    key ^= (key >> 6);
    key += ~(key << 11);
    key ^= (key >> 16);
    return key;
}

//Identity hash function for integer keys
unsigned int dictIdentityHashFunction(unsigned int key)
{
    return key;
}

static uint32_t dict_hash_function_seed = 5381;

void dictSetHashFunctionSeed(uint32_t seed)
{
    dict_hash_function_seed = seed;
}

uint32_t dictGetHashFunctionSeed(void)
{
    return dict_hash_function_seed;
}

/** MurmurHash2, by Austin Appleby
 * Note - This code makes a few assumptions about how your machine behaves -
 * 1. We can read a 4-byte value from any address without crashing
 * 2. sizeof(int) == 4
 *
 * And it has a few limitations -
 *
 * 1. It will not work incrementally.
 * 2. It will not produce the same results on little-endian and big-endian
 *    machines.
 */
unsigned int dictGenHashFunction(const void * key, int len)
{
    //'m' and 'r' are mixing constants generated offline.
    //They're not really 'magic', they just happen to work well.
    uint32_t seed = dict_hash_function_seed;
    const uint32_t m = 0x5bd1e995;
    const int r = 24;

    //Initialize the hash to a 'random' value
    uint32_t h = seed ^ len;

    //Mix 4 bytes at a time into the hash
    const unsigned char * data = (const unsigned char *)key;

    while (len >= 4)
    {
        uint32_t k = *(uint32_t *)data;

        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        data += 4;
        len -= 4;
    }

    //Handle the last few bytes of the input array
    switch (len)
    {
        case 3:
            h ^= data[2] << 16;
        case 2:
            h ^= data[1] << 8;
        case 1:
            h ^= data[0];
            h *= m;
    };

    //Do a few final mixes of the hash to ensure the last few bytes are well-incorporated.
    h ^= h >> 13;
    h *= m;
    h ^= h >> 15;

    return (unsigned int)h;
}

//And a case insensitive hash function (based on jdb hash)
unsigned int dictGenCaseHashFunction(const unsigned char * buf, int len)
{
    unsigned int hash = (unsigned int)dict_hash_function_seed;

    while (len--)
        hash = ((hash << 5) + hash) + (tolower(*buf++));
    return hash;
}

//API implementation

/**
 * 重置（或初始化）哈希表的各项属性值为零
 * @param ht 给定的哈希表
 */
static void _dictReset(dictht * ht)
{
    ht->table = NULL;
    ht->size = 0;
    ht->sizeMask = 0;
    ht->used = 0;
}

/**
 * 创建一个新的字典
 *
 * T = O(1)
 *
 * @param type  特定类型函数
 * @param privData  需要传递给函数的可选参数
 * @return  创建字典的指针
 */
dict * dictCreate(dictType * type, void * privData)
{
    dict * d = z_malloc(sizeof(dict));

    _dictInit(d, type, privData);

    return d;
}

/**
 * 初始化字典
 *
 * T = O(1)
 *
 * @param d 目标字典
 * @param type 特定类型函数
 * @param privDataPtr 需要的可选参数
 * @return 成功返回0；失败返回1
 */
int _dictInit(dict * d, dictType * type, void * privDataPtr)
{
    _dictReset(&d->ht[0]);
    _dictReset(&d->ht[1]);

    d->type = type;
    d->privData = privDataPtr;
    d->rehashIndex = -1;
    d->iterators = 0;

    return DICT_OK;
}

/**
 * 缩小给定字典
 * 让它的已用节点数和字典大小之间的比率接近 1:1
 *
 * T = O(N)
 *
 * @param d 给定字典
 * @return  返回 DICT_ERR 表示字典已经在 rehash ，或者 dict_can_resize 为假。
 *          成功创建体积更小的 ht[1] ，可以开始 resize 时，返回 DICT_OK。
 */
int dictResize(dict * d)
{
    int minimal;

    //不能在关闭rehash或者正在rehash时调用
    if (!dict_can_resize || dictIsRehashing(d)) return DICT_ERR;

    minimal = d->ht[0].used;
    if (minimal < DICT_HT_INITIAL_SIZE)
        minimal = DICT_HT_INITIAL_SIZE;

    //T = O(N)
    return dictExpand(d, minimal);
}

/**
 * 创建一个新的哈希表，并根据字典的情况，选择以下其中一个动作来进行：
 *
 * 1) 如果字典的 0 号哈希表为空，那么将新哈希表设置为 0 号哈希表
 * 2) 如果字典的 0 号哈希表非空，那么将新哈希表设置为 1 号哈希表，
 *    并打开字典的 rehash 标识，使得程序可以开始对字典进行 rehash
 *
 * T = O(N)
 *
 * @param d 给定字典
 * @param size 要创建的字典大小
 * @return size 参数不够大，或者 rehash 已经在进行时，返回 DICT_ERR 。
 *              成功创建 0 号哈希表，或者 1 号哈希表时，返回 DICT_OK 。
 */
int dictExpand(dict * d, unsigned long size)
{
    dictht n;

    //根据size参数，计算哈希表的大小
    //T = O(1)
    unsigned long realSize = _dictNextPower(size);

    //不能在字典正在rehash时进行，size的值也应该大于0号哈希表已使用的大小
    if (dictIsRehashing(d) || d->ht[0].used > size)
        return DICT_ERR;

    n.size = realSize;
    n.sizeMask = realSize - 1;
    n.table = z_calloc(realSize * sizeof(dictEntry *));
    n.used = 0;

    if (d->ht[0].table == NULL)
    {
        d->ht[0] = n;   //初始化
    } else {
        d->ht[1] = n;   //rehash
        d->rehashIndex = 0;
    }

    return DICT_OK;
}

/**
 * 执行 N 步渐进式 rehash 。
 *
 * 注意，每步 rehash 都是以一个哈希表索引（桶）作为单位的，
 * 一个桶里可能会有多个节点，
 * 被 rehash 的桶里的所有节点都会被移动到新哈希表。
 *
 * T = O(N)
 *
 * @param d 要rehash的字典
 * @param n 要进行rehash的步数
 * @return  返回 1 表示仍有键需要从 0 号哈希表移动到 1 号哈希表，
 *          返回 0 则表示所有键都已经迁移完毕。
 */
int dictRehash(dict * d, int n)
{
    //只有在rehash进行中时执行
    if (!dictIsRehashing(d))
        return 0;

    //进行N步迁移， T = O(N)
    while (n--)
    {
        dictEntry * de, * nextDe;

        //如果0号哈希表为空，那么表示rehash执行完成
        if (d->ht[0].used == 0)
        {
            z_free(d->ht[0].table);
            //此处存在性能上优化的可能
            //可以通过互换指针的值，从而避免了复制哈希表的开销
            d->ht[0] = d->ht[1];
            _dictReset(&d->ht[1]);
            d->rehashIndex = -1;

            return 0;
        }

        //确保rehashIndex没有越界
        //assert
        assert(d->ht[0].size > (unsigned)d->rehashIndex);

        //略过数组中为空的索引，找到下一个非空索引
        while (d->ht[0].table[d->rehashIndex] == NULL)
            d->rehashIndex++;

        //指向该索引的链表表头节点
        de = d->ht[0].table[d->rehashIndex];
        //将链表中的所有节点迁移到新的哈希表
        //T = O(N)
        while (de)
        {
            unsigned int h;

            //保存下一个节点的指针
            nextDe = de->next;

            //计算新哈希表的哈希值，以及节点插入的索引位置
            h = dictHashKey(d, de->key) & d->ht[1].sizeMask;

            //插入节点到新哈希表
            de->next = d->ht[1].table[h];
            d->ht[1].table[h] = de;

            d->ht[0].used--;
            d->ht[1].used++;

            de = nextDe;
        }
        //将刚迁移完的哈希表索引的指针设置为空
        d->ht[0].table[d->rehashIndex] = NULL;
        d->rehashIndex++;
    }

    return 1;
}

/**
 * 返回以毫秒为单位的UNIX时间戳
 *
 * T = O(1)
 *
 * @return
 */
long long timeInMilliseconds(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

/**
 * 在给定毫秒数内，以100步为单位，对字典进行rehash。
 *
 * T = O(N)
 *
 * @param d 要rehash的字典
 * @param ms 给定的毫秒数
 * @return rehash的次数
 */
int dictRehashMilliseconds(dict * d, int ms)
{
    long long start = timeInMilliseconds();
    int rehashes = 0;

    while (dictRehash(d, 100))
    {
        rehashes += 100;
        if (timeInMilliseconds() - start > ms) break;
    }

    return rehashes;
}

/**
 * 在字典不存在安全迭代器的情况下，对字典进行单步 rehash 。
 *
 * 字典有安全迭代器的情况下不能进行 rehash ，
 * 因为两种不同的迭代和修改操作可能会弄乱字典。
 *
 * 这个函数被多个通用的查找、更新操作调用，
 * 它可以让字典在被使用的同时进行 rehash 。
 *
 * T = O(1)
 *
 * @param d 单步迭代的字典
 */
static void _dictRehashStep(dict * d)
{
    if (d->iterators == 0) dictRehash(d, 1);
}

/**
 * 尝试将给定键值对添加到字典中
 *
 * 只有给定键 key 不存在于字典时，添加操作才会成功
 *
 * 最坏 T = O(N) ，平摊 O(1)
 *
 * @param d 要添加的字典
 * @param key 键
 * @param val 值
 * @return 添加成功返回 DICT_OK ，失败返回 DICT_ERR
 */
int dictAdd(dict * d, void * key, void * val)
{
    dictEntry * entry = dictAddRaw(d, key);

    //键已存在，添加失败
    if (!entry)
        return DICT_ERR;

    //键不存在，设置节点的值
    dictSetVal(d, entry, val);

    return DICT_OK;
}

/**
 * 尝试将键插入到字典中
 *
 * T = O(N)
 *
 * @param d 目标字典
 * @param key 要添加的键
 * @return
 *      如果键已经在字典存在，那么返回 NULL；
 *      如果键不存在，那么程序创建新的哈希节点，将节点和键关联，并插入到字典，然后返回节点本身。
 */
dictEntry * dictAddRaw(dict * d, void * key)
{
    int index;
    dictEntry * entry;
    dictht * ht;

    //如果条件允许，进行单步rehash
    //T = O(1)
    if (dictIsRehashing(d))
        _dictRehashStep(d);

    //计算键在哈希表中的索引值
    //如果值为-1，那么表示键已经存在
    //T = O(N)
    if ( (index = _dictKeyIndex(d, key)) == -1)
        return NULL;

    //如果字典正在rehash，那么将新键添加到1号哈希表中，否则添加到0号哈希表中
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry = z_malloc(sizeof(dictEntry));
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ht->used++;

    //设置新节点的值
    dictSetKey(d, entry, key);

    return entry;
}

/**
 * 将给定的键值对添加到字典中，如果键已经存在，那么删除旧有的键值对。
 *
 * T = O(N)
 *
 * @param d 给定字典
 * @param key 键
 * @param val 值
 * @return 如果键值对为全新添加，那么返回 1 。
 * 如果键值对是通过对原有的键值对更新得来的，那么返回 0 。
 */
int dictReplace(dict * d, void * key, void * val)
{
    dictEntry * entry, auxEntry;

    // 尝试直接将键值对添加到字典
    // 如果键 key 不存在的话，添加会成功
    // T = O(N)
    if (dictAdd(d, key, val) == DICT_OK)
        return 1;

    // 运行到这里，说明键 key 已经存在，那么找出包含这个 key 的节点
    // T = O(1)
    entry = dictFind(d, key);
    auxEntry = *entry;
    dictSetVal(d, entry, val);
    dictFreeVal(d, &auxEntry);

    return 0;
}

/**
 * dictAddRaw() 根据给定 key 释放存在，执行以下动作：
 *
 * 1) key 已经存在，返回包含该 key 的字典节点
 * 2) key 不存在，那么将 key 添加到字典
 *
 * T = O(N)
 *
 * @param d 目标字典
 * @param key 给定的键
 * @return 不论发生以上的哪一种情况，dictAddRaw() 都总是返回包含给定 key 的字典节点。
 */
dictEntry * dictReplaceRaw(dict * d, void * key)
{
    dictEntry * entry = dictFind(d, key);

    //如果找到节点直接返回该节点，否则添加并返回一个新节点
    // T = O(N)
    return entry ? entry : dictAddRaw(d, key);
}

/**
 * 查找并删除包含给定键的节点
 *
 * 参数 nofree 决定是否调用键和值的释放函数
 * 0 表示调用，1 表示不调用
 *
 * T = O(1)
 *
 * @param d
 * @param key
 * @param nofree
 * @return 找到并成功删除返回 DICT_OK ，没找到则返回 DICT_ERR
 */
static int dictGenericDelete(dict * d, const void * key, int nofree)
{
    unsigned int h, index;
    dictEntry * he, *prevHe;
    int table;

    //字典的哈希表为空
    if (d->ht[0].size == 0) return DICT_ERR;

    //进行单步rehash，T = O(1)
    if (dictIsRehashing(d)) _dictRehashStep(d);
    //计算哈希值
    h = dictHashKey(d, key);

    //遍历哈希表
    for (table = 0; table <= 1; table++)
    {
        index = h & d->ht[table].sizeMask;
        he = d->ht[table].table[index];
        prevHe = NULL;

        //遍历链表上的所有节点
        while (he)
        {
            if (dictCompareKeys(d, key,he->key))
            {
                if (prevHe)
                    prevHe->next = he->next;
                else
                    d->ht[table].table[index] = he->next;

                //调用释放键和值的函数
                if (!nofree)
                {
                    dictFreeKey(d, he);
                    dictFreeVal(d, he);
                }

                z_free(he);
                d->ht[table].used--;

                return DICT_OK;
            }

            prevHe = he;
            he = he->next;
        }

        // 如果执行到这里，说明在 0 号哈希表中找不到给定键
        // 那么根据字典是否正在进行 rehash ，决定要不要查找 1 号哈希表
        if (!dictIsRehashing(d))
            break;
    }

    //未找到
    return DICT_ERR;
}

/**
 * 从字典中删除包含给定键的节点
 *
 * 并且调用键值的释放函数来删除键值
 *
 * T = O(1)
 *
 * @param d 目标字典
 * @param key 键
 * @return 找到并成功删除返回 DICT_OK ，没找到则返回 DICT_ERR
 */
int dictDelete(dict * d, const void * key)
{
    return dictGenericDelete(d, key, 0);
}

/**
 *  从字典中删除包含给定键的节点
 *
 * 但不调用键值的释放函数来删除键值
 *
 * T = O(1)
 *
 * @param d 字典
 * @param key 要删除的键
 * @return 找到并成功删除返回 DICT_OK ，没找到则返回 DICT_ERR
 */
int dictDeleteNoFree(dict * d, const void * key)
{
    return dictGenericDelete(d, key, 1);
}

/**
 * 删除哈希表上的所有节点，并重置哈希表的各项属性
 *
 * T = O(N)
 *
 * @param d 目标字典
 * @param ht
 * @param callback
 * @return
 */
int _dictClear(dict * d, dictht * ht, void (callback)(void *))
{
    unsigned long l;

    //遍历整个哈希表
    for (l = 0; l < ht->size && ht->used > 0; l++)
    {
        dictEntry * de, * nextDe;

        if (callback && (l & 65535) == 0)
            callback(d->privData);

        if ( (de = ht->table[l]) == NULL)   continue;

        //遍历整个链表
        while (de)
        {
            nextDe = de->next;
            dictFreeKey(d, de);
            dictFreeVal(d, de);
            z_free(de);

            ht->used--;
            de = nextDe;
        }
    }

    //释放哈希表
    z_free(ht->table);
    _dictReset(ht);

    return DICT_OK;
}

/**
 * 删除并释放整个字典
 *
 * T = O(N)
 *
 * @param d 要释放的字典
 */
void dictRelease(dict * d)
{
    //删除并清空两个哈希表
    _dictClear(d, &d->ht[0], NULL);
    _dictClear(d, &d->ht[1], NULL);

    z_free(d);
}

/**
 * 返回字典中包含键 key 的节点
 *
 * T = O(1)
 *
 * @param d 要查找的字典
 * @param key 目标键
 * @return 找到返回节点，找不到返回 NULL
 */
dictEntry * dictFind(dict * d, const void * key)
{
    dictEntry * de;
    unsigned int h, index, table;

    //字典的哈希表为空
    if (d->ht[0].size == 0) return NULL;

    if (dictIsRehashing(d)) _dictRehashStep(d);

    h = dictHashKey(d, key);
    //在字典的哈希表中查找这个键
    for (table = 0; table <= 1; table++)
    {
        //计算索引值
        index = h & d->ht[table].sizeMask;

        //遍历给定索引上的链表的所有节点， 查找key
        de = d->ht[table].table[index];
        while (de)
        {
            if (dictCompareKeys(d, de->key, key))
                return de;

            de = de->next;
        }

        // 如果程序遍历完 0 号哈希表，仍然没找到指定的键的节点
        // 那么程序会检查字典是否在进行 rehash ，
        // 然后才决定是直接返回 NULL ，还是继续查找 1 号哈希表
        if (!dictIsRehashing(d)) return NULL;
    }

    // 进行到这里时，说明两个哈希表都没找到
    return NULL;
}

/**
 * 获取包含给定键的节点的值
 *
 * T = O(1)
 *
 * @param d 目标字典
 * @param key 给定键
 * @return 如果节点不为空，返回节点的值; 否则返回 NULL
 */
void * dictFetchValue(dict * d, const void * key)
{
    dictEntry * de;
    de = dictFind(d, key);

    return de ? dictGetVal(de) : NULL;
}

/**
 * 指纹是一个64位的数字，表示字典的状态
 * 在给定的时间，它只是几个dict属性xor在一起。
 * 当一个不安全的迭代器被初始化时，我们得到dict指纹，并检查
 * 当迭代器被释放时，再次使用指纹。
 * 如果两个指纹不同，意味着迭代器的用户
 * 迭代时对字典执行禁止的操作。
 *
 * @param d 字典
 * @return 给定字典的指纹
 */
long long dictFingerprint(dict * d)
{
    long long integers[6], hash = 0;
    int j;

    integers[0] = (long)d->ht[0].table;
    integers[1] = d->ht[0].size;
    integers[2] = d->ht[0].used;
    integers[3] = (long)d->ht[1].table;
    integers[4] = d->ht[1].size;
    integers[5] = d->ht[1].used;

    for (j = 0; j < 6; j++)
    {
        hash += integers[j];
        hash = (~hash) + (hash << 21);
        hash = hash ^ (hash >> 24);
        hash = (hash + (hash << 3)) + (hash << 8);  //hash * 265
        hash = hash ^ (hash >> 14);
        hash = (hash + (hash << 2)) + (hash << 4);  //hash * 21
        hash = hash ^ (hash >> 28);
        hash = hash + (hash << 31);
    }
    return hash;
}

/**
 * 创建并返回给定字典的不安全迭代器
 *
 * T = O(1)
 *
 * @param d 给定字典
 * @return 字典的不安全迭代器
 */
dictIterator * dictGetIterator(dict * d)
{
    dictIterator * iter = z_malloc(sizeof(dictIterator));

    iter->d = d;
    iter->table = 0;
    iter->index = -1;
    iter->safe = 0;
    iter->entry = NULL;
    iter->nextEntry = NULL;

    return iter;
}

/**
 * 创建并返回给定节点的安全迭代器
 *
 * T = O(1)
 *
 * @param d 给定字典
 * @return 返回字典的安全迭代器
 */
dictIterator * dictGetSafeIterator(dict * d)
{
    dictIterator * iter = dictGetIterator(d);
    iter->safe = 1;

    return iter;
}

/**
 * 返回迭代器指向的当前节点
 *
 * T = O(1)
 *
 * @param iter 目标迭代器
 * @return 字典迭代完毕时，返回 NULL
 */
dictEntry * dictNext(dictIterator * iter)
{
    while (1)
    {
        // 进入这个循环有两种可能：
        // 1) 这是迭代器第一次运行
        // 2) 当前索引链表中的节点已经迭代完（NULL 为链表的表尾）
        if (iter->entry == NULL)
        {
            //指向被迭代的哈希表
            dictht * ht = &iter->d->ht[iter->table];
            //初次迭代时执行
            if (iter->index == -1 && iter->table == 0)
            {
                if (iter->safe) //如果是安全迭代器，那么更新安全迭代器计数器
                    iter->d->iterators++;
                else            //如果不是安全迭代器，那么计算指纹
                    iter->fingerprint = dictFingerprint(iter->d);
            }
            iter->index++;

            // 如果迭代器的当前索引大于当前被迭代的哈希表的大小
            // 那么说明这个哈希表已经迭代完毕
            if (iter->index >= (signed) ht->size)
            {
                // 如果正在 rehash 的话，那么说明 1 号哈希表也正在使用中
                // 那么继续对 1 号哈希表进行迭代
                if (dictIsRehashing(iter->d) && iter->table == 0)
                {
                    iter->table++;
                    iter->index = 0;
                    ht = &iter->d->ht[1];
                }
                // 如果没有 rehash ，那么说明迭代已经完成
                else
                    break;
            }

            // 如果进行到这里，说明这个哈希表并未迭代完
            // 更新节点指针，指向下个索引链表的表头节点
            iter->entry = ht->table[iter->index];
        }
        else
        {
            // 执行到这里，说明程序正在迭代某个链表
            // 将节点指针指向链表的下一个节点
            iter->entry = iter->nextEntry;
        }

        // 如果当前节点不为空，那么也记录下该节点的下个节点
        // 因为安全迭代器有可能会将迭代器返回的当前节点删除
        if (iter->entry)
        {
            iter->nextEntry = iter->entry->next;
            return iter->entry;
        }
    }

    return NULL;
}

/**
 * 释放给定字典迭代器
 *
 * T = O(1)
 *
 * @param iter 要释放的字典
 */
void dictReleaseIterator(dictIterator * iter)
{
    if (!(iter->index == -1 && iter->table == 0))
    {
        if (iter->safe) //释放安全迭代器
            iter->d->iterators--;
        else
            assert(iter->fingerprint == dictFingerprint(iter->d));//assert
    }
    z_free(iter);
}

/**
 * 随机返回字典中任意一个节点。
 *
 * 可用于实现随机化算法。
 *
 * T = O(N)
 *
 * @param d 给定字典
 * @return 如果字典为空，返回 NULL 。
 */
dictEntry * dictGetRandomKey(dict * d)
{
    dictEntry * de, * origin;
    unsigned int h;
    int listLen, listEle;

    //字典为空
    if (dictSize(d) == 0) return NULL;

    if (dictIsRehashing(d)) _dictRehashStep(d);

    // 如果正在 rehash ，那么将 1 号哈希表也作为随机查找的目标
    if (dictIsRehashing(d))
    {
        do
        {
            h = rand() % (d->ht[0].size + d->ht[1].size);
            //random()
            de = (h >= d->ht[0].size) ? d->ht[1].table[h - d->ht[0].size] : d->ht[0].table[h];
        }
        while (de == NULL);
    }
    else    //只检查0号的哈希表
    {
        do
        {
            h = rand() & d->ht[0].sizeMask;
            de = d->ht[0].table[h];
        }
        while (de == NULL);
    }

    // 目前 he 已经指向一个非空的节点链表
    // 程序将从这个链表随机返回一个节点
    listLen = 0;
    origin = de;
    while (de)  //计算节点数量
    {
        de = de->next;
        listLen++;
    }
    listEle = rand() % listLen;
    de = origin;

    //按索引查找节点
    while (listEle--) de = de->next;

    return de;
}

/**
 * This is a version of dictGetRandomKey() that is modified in order to
 * return multiple entries by jumping at a random place of the hash table
 * and scanning linearly for entries.
 *
 * Returned pointers to hash table entries are stored into 'des' that
 * points to an array of dictEntry pointers. The array must have room for
 * at least 'count' elements, that is the argument we pass to the function
 * to tell how many random elements we need.
 *
 * The function returns the number of items stored into 'des', that may
 * be less than 'count' if the hash table has less than 'count' elements
 * inside.
 *
 * Note that this function is not suitable when you need a good distribution
 * of the returned items, but only when you need to "sample" a given number
 * of continuous elements to run some kind of algorithm or to produce
 * statistics. However the function is much faster than dictGetRandomKey()
 * at producing N elements, and the elements are guaranteed to be non
 * repeating.
 *
 * @param d
 * @param des
 * @param count
 * @return
 */
int dictGetRandomKeys(dict * d, dictEntry ** des, int count)
{
    int j;
    int stored = 0;

    if (dictSize(d) < count) count = dictSize(d);
    while (stored < count)
    {
        for (j = 0; j < 2; j++)
        {
            unsigned int i = rand() & d->ht[j].sizeMask;
            int size = d->ht[j].size;

            while (size--)
            {
                dictEntry * de = d->ht[j].table[i];
                while (de)
                {
                    *des = de;
                    des++;
                    de = de->next;
                    stored++;
                    if (stored == count) return stored;
                }
                i = (i + 1) & d->ht[j].sizeMask;
            }

            /* If there is only one table and we iterated it all, we should
             * already have 'count' elements. Assert this condition. */
            assert(dictIsRehashing(d) != 0);
        }
    }

    return stored;
}

/* Function to reverse bits. Algorithm from:
 * http://graphics.stanford.edu/~seander/bithacks.html#ReverseParallel */
static unsigned long rev(unsigned long v) {
    unsigned long s = 8 * sizeof(v); // bit size; must be power of 2
    unsigned long mask = ~0;
    while ((s >>= 1) > 0) {
        mask ^= (mask << s);
        v = ((v >> s) & mask) | ((v << s) & ~mask);
    }
    return v;
}

/**
 * dictScan() is used to iterate over the elements of a dictionary.
 *
 * dictScan() 函数用于迭代给定字典中的元素。
 *
 * Iterating works in the following way:
 *
 * 迭代按以下方式执行：
 *
 * 1) Initially you call the function using a cursor (v) value of 0.
 *    一开始，你使用 0 作为游标来调用函数。
 * 2) The function performs one step of the iteration, and returns the
 *    new cursor value that you must use in the next call.
 *    函数执行一步迭代操作，
 *    并返回一个下次迭代时使用的新游标。
 * 3) When the returned cursor is 0, the iteration is complete.
 *    当函数返回的游标为 0 时，迭代完成。
 *
 * The function guarantees that all the elements that are present in the
 * dictionary from the start to the end of the iteration are returned.
 * However it is possible that some element is returned multiple time.
 *
 * 函数保证，在迭代从开始到结束期间，一直存在于字典的元素肯定会被迭代到，
 * 但一个元素可能会被返回多次。
 *
 * For every element returned, the callback 'fn' passed as argument is
 * called, with 'privdata' as first argument and the dictionar entry
 * 'de' as second argument.
 *
 * 每当一个元素被返回时，回调函数 fn 就会被执行，
 * fn 函数的第一个参数是 privdata ，而第二个参数则是字典节点 de 。
 *
 * HOW IT WORKS.
 * 工作原理
 *
 * The algorithm used in the iteration was designed by Pieter Noordhuis.
 * The main idea is to increment a cursor starting from the higher order
 * bits, that is, instead of incrementing the cursor normally, the bits
 * of the cursor are reversed, then the cursor is incremented, and finally
 * the bits are reversed again.
 *
 * 迭代所使用的算法是由 Pieter Noordhuis 设计的，
 * 算法的主要思路是在二进制高位上对游标进行加法计算
 * 也即是说，不是按正常的办法来对游标进行加法计算，
 * 而是首先将游标的二进制位翻转（reverse）过来，
 * 然后对翻转后的值进行加法计算，
 * 最后再次对加法计算之后的结果进行翻转。
 *
 * This strategy is needed because the hash table may be resized from one
 * call to the other call of the same iteration.
 *
 * 这一策略是必要的，因为在一次完整的迭代过程中，
 * 哈希表的大小有可能在两次迭代之间发生改变。
 *
 * dict.c hash tables are always power of two in size, and they
 * use chaining, so the position of an element in a given table is given
 * always by computing the bitwise AND between Hash(key) and SIZE-1
 * (where SIZE-1 is always the mask that is equivalent to taking the rest
 *  of the division between the Hash of the key and SIZE).
 *
 * 哈希表的大小总是 2 的某个次方，并且哈希表使用链表来解决冲突，
 * 因此一个给定元素在一个给定表的位置总可以通过 Hash(key) & SIZE-1
 * 公式来计算得出，
 * 其中 SIZE-1 是哈希表的最大索引值，
 * 这个最大索引值就是哈希表的 mask （掩码）。
 *
 * For example if the current hash table size is 16, the mask is
 * (in binary) 1111. The position of a key in the hash table will be always
 * the last four bits of the hash output, and so forth.
 *
 * 举个例子，如果当前哈希表的大小为 16 ，
 * 那么它的掩码就是二进制值 1111 ，
 * 这个哈希表的所有位置都可以使用哈希值的最后四个二进制位来记录。
 *
 * WHAT HAPPENS IF THE TABLE CHANGES IN SIZE?
 * 如果哈希表的大小改变了怎么办？
 *
 * If the hash table grows, elements can go anyway in one multiple of
 * the old bucket: for example let's say that we already iterated with
 * a 4 bit cursor 1100, since the mask is 1111 (hash table size = 16).
 *
 * 当对哈希表进行扩展时，元素可能会从一个槽移动到另一个槽，
 * 举个例子，假设我们刚好迭代至 4 位游标 1100 ，
 * 而哈希表的 mask 为 1111 （哈希表的大小为 16 ）。
 *
 * If the hash table will be resized to 64 elements, and the new mask will
 * be 111111, the new buckets that you obtain substituting in ??1100
 * either 0 or 1, can be targeted only by keys that we already visited
 * when scanning the bucket 1100 in the smaller hash table.
 *
 * 如果这时哈希表将大小改为 64 ，那么哈希表的 mask 将变为 111111 ，
 *
 * By iterating the higher bits first, because of the inverted counter, the
 * cursor does not need to restart if the table size gets bigger, and will
 * just continue iterating with cursors that don't have '1100' at the end,
 * nor any other combination of final 4 bits already explored.
 *
 * Similarly when the table size shrinks over time, for example going from
 * 16 to 8, If a combination of the lower three bits (the mask for size 8
 * is 111) was already completely explored, it will not be visited again
 * as we are sure that, we tried for example, both 0111 and 1111 (all the
 * variations of the higher bit) so we don't need to test it again.
 *
 * WAIT... YOU HAVE *TWO* TABLES DURING REHASHING!
 * 等等。。。在 rehash 的时候可是会出现两个哈希表的阿！
 *
 * Yes, this is true, but we always iterate the smaller one of the tables,
 * testing also all the expansions of the current cursor into the larger
 * table. So for example if the current cursor is 101 and we also have a
 * larger table of size 16, we also test (0)101 and (1)101 inside the larger
 * table. This reduces the problem back to having only one table, where
 * the larger one, if exists, is just an expansion of the smaller one.
 *
 * LIMITATIONS
 * 限制
 *
 * This iterator is completely stateless, and this is a huge advantage,
 * including no additional memory used.
 * 这个迭代器是完全无状态的，这是一个巨大的优势，
 * 因为迭代可以在不使用任何额外内存的情况下进行。
 *
 * The disadvantages resulting from this design are:
 * 这个设计的缺陷在于：
 *
 * 1) It is possible that we return duplicated elements. However this is usually
 *    easy to deal with in the application level.
 *    函数可能会返回重复的元素，不过这个问题可以很容易在应用层解决。
 * 2) The iterator must return multiple elements per call, as it needs to always
 *    return all the keys chained in a given bucket, and all the expansions, so
 *    we are sure we don't miss keys moving.
 *    为了不错过任何元素，
 *    迭代器需要返回给定桶上的所有键，
 *    以及因为扩展哈希表而产生出来的新表，
 *    所以迭代器必须在一次迭代中返回多个元素。
 * 3) The reverse cursor is somewhat hard to understand at first, but this
 *    comment is supposed to help.
 *    对游标进行翻转（reverse）的原因初看上去比较难以理解，
 *    不过阅读这份注释应该会有所帮助。
 */
unsigned long dictScan(dict * d, unsigned long v, dictScanFunction * fn, void * privData)
{
    dictht * t0, * t1;
    const dictEntry * de;
    unsigned long m0, m1;

    //跳过空字典
    if (dictSize(d) == 0) return 0;

    //迭代只有一个哈希表的字典
    if (!dictIsRehashing(d))
    {
        t0 = &(d->ht[0]);
        m0 = t0->sizeMask;

        //指向哈希桶
        de = t0->table[v & m0];
        while (de)
        {
            fn(privData, de);
            de = de->next;
        }
    }
    else    //迭代有两个哈希表的字典
    {
        t0 = &d->ht[0];
        t1 = &d->ht[1];

        //确保t0比t1要小
        if (t0->size > t1->size)
        {
            t0 = &d->ht[1];
            t1 = &d->ht[0];
        }

        m0 = t0->sizeMask;
        m1 = t1->sizeMask;

        //指向桶，并迭代桶中的所有节点
        de = t0->table[v & m0];
        while (de)
        {
            fn(privData, de);
            de = de->next;
        }

        // Iterate over indices in larger table             // 迭代大表中的桶
        // that are the expansion of the index pointed to   // 这些桶被索引的 expansion 所指向
        do
        {
            de = t1->table[v & m1];
            while (de)
            {
                fn(privData, de);
                de = de->next;
            }

            v = (((v | m0) + 1) & ~m0) | (v & m0);
        }
        while (v & (m0 ^ m1));
    }

    v |= ~m0;
    v = rev(v);
    v++;
    v = rev(v);

    return v;
}

/* private function */

/**
 * 根据需要，初始化字典（的哈希表），或者对字典（的现有哈希表）进行扩展
 *
 * T = O(N)
 *
 * @param d 目标字典
 * @return 扩展后的字典
 */
static int _dictExpandIfNeeded(dict * d)
{
    //渐进式rehash已经在进行了，直接返回
    if (dictIsRehashing(d)) return DICT_OK;

    // 如果字典（的 0 号哈希表）为空，那么创建并返回初始化大小的 0 号哈希表
    // T = O(1)
    if (d->ht[0].size == 0) return dictExpand(d, DICT_HT_INITIAL_SIZE);

    // 一下两个条件之一为真时，对字典进行扩展
    // 1）字典已使用节点数和字典大小之间的比率接近 1：1
    //    并且 dict_can_resize 为真
    // 2）已使用节点数和字典大小之间的比率超过 dict_force_resize_ratio
    if (d->ht[0].used >= d->ht[0].size &&
        (dict_can_resize || d->ht[0].used / d->ht[0].size > dict_force_resize_ratio))
    {
        return dictExpand(d, d->ht[0].used * 2);
    }

    return DICT_OK;
}

/**
 * 计算第一个大于等于 size 的 2 的 N 次方，用作哈希表的值
 *
 * T = O(1)
 *
 * @param size 要扩展的字典的大小
 * @return 扩展后的字典大小
 */
static unsigned long _dictNextPower(unsigned long size)
{
    unsigned long i = DICT_HT_INITIAL_SIZE;

    if (size >= LONG_MAX) return LONG_MAX;
    while (1)
    {
        if (i >= size)
            return i;
         i *= 2;
    }
}

/**
 * 返回可以将 key 插入到哈希表的索引位置
 * 如果 key 已经存在于哈希表，那么返回 -1
 *
 * 注意，如果字典正在进行 rehash ，那么总是返回 1 号哈希表的索引。
 * 因为在字典进行 rehash 时，新节点总是插入到 1 号哈希表。
 *
 * T = O(N)
 *
 * @param d 要插入的字典
 * @param key 键
 * @return 返回可以将 key 插入到哈希表的索引位置；
 *          如果 key 已经存在于哈希表，那么返回 -1
 */
static int _dictKeyIndex(dict * d, const void * key)
{
    unsigned int h, index, table;
    dictEntry * he;

    if (_dictExpandIfNeeded(d) == DICT_ERR)
        return -1;

    //计算哈希值
    h = dictHashKey(d, key);
    for (table = 0; table <= 1; table++)
    {
        index = h & d->ht[table].sizeMask;

        he = d->ht[table].table[index];
        while (he)
        {
            if (dictCompareKeys(d, key, he->key))
                return -1;
            he = he->next;
        }

        // 如果运行到这里时，说明 0 号哈希表中所有节点都不包含 key
        // 如果这时 rehahs 正在进行，那么继续对 1 号哈希表进行 rehash
        if (!dictIsRehashing(d)) break;
    }

    return index;
}

/**
 * 清空字典上的所有哈希表节点，并重置字典属性
 *
 * T = O(N)
 *
 * @param d 要清空的字典
 * @param callback
 */
void dictEmpty(dict * d, void(callback)(void *))
{
    _dictClear(d, &d->ht[0], callback);
    _dictClear(d, &d->ht[1], callback);

    d->rehashIndex = -1;
    d->iterators = 0;
}

/**
 * 开启自动 rehash
 *
 * T = O(1)
 *
 */
void dictEnableResize(void)
{
    dict_can_resize = 1;
}

/**
 * 关闭自动 rehash
 *
 * T = O(1)
 *
 */
void dictDisableResize(void)
{
    dict_can_resize = 0;
}

#if 0

/* The following is code that we don't use for Redis currently, but that is part
of the library. */

/* ----------------------- Debugging ------------------------*/

#define DICT_STATS_VECTLEN 50
static void _dictPrintStatsHt(dictht *ht) {
    unsigned long i, slots = 0, chainlen, maxchainlen = 0;
    unsigned long totchainlen = 0;
    unsigned long clvector[DICT_STATS_VECTLEN];

    if (ht->used == 0) {
        printf("No stats available for empty dictionaries\n");
        return;
    }

    for (i = 0; i < DICT_STATS_VECTLEN; i++) clvector[i] = 0;
    for (i = 0; i < ht->size; i++) {
        dictEntry *he;

        if (ht->table[i] == NULL) {
            clvector[0]++;
            continue;
        }
        slots++;
        /* For each hash entry on this slot... */
        chainlen = 0;
        he = ht->table[i];
        while(he) {
            chainlen++;
            he = he->next;
        }
        clvector[(chainlen < DICT_STATS_VECTLEN) ? chainlen : (DICT_STATS_VECTLEN-1)]++;
        if (chainlen > maxchainlen) maxchainlen = chainlen;
        totchainlen += chainlen;
    }
    printf("Hash table stats:\n");
    printf(" table size: %ld\n", ht->size);
    printf(" number of elements: %ld\n", ht->used);
    printf(" different slots: %ld\n", slots);
    printf(" max chain length: %ld\n", maxchainlen);
    printf(" avg chain length (counted): %.02f\n", (float)totchainlen/slots);
    printf(" avg chain length (computed): %.02f\n", (float)ht->used/slots);
    printf(" Chain length distribution:\n");
    for (i = 0; i < DICT_STATS_VECTLEN-1; i++) {
        if (clvector[i] == 0) continue;
        printf("   %s%ld: %ld (%.02f%%)\n",(i == DICT_STATS_VECTLEN-1)?">= ":"", i, clvector[i], ((float)clvector[i]/ht->size)*100);
    }
}

void dictPrintStats(dict *d) {
    _dictPrintStatsHt(&d->ht[0]);
    if (dictIsRehashing(d)) {
        printf("-- Rehashing into ht[1]:\n");
        _dictPrintStatsHt(&d->ht[1]);
    }
}

/* ----------------------- StringCopy Hash Table Type ------------------------*/

static unsigned int _dictStringCopyHTHashFunction(const void *key)
{
    return dictGenHashFunction(key, strlen(key));
}

static void *_dictStringDup(void *privdata, const void *key)
{
    int len = strlen(key);
    char *copy = zmalloc(len+1);
    DICT_NOTUSED(privdata);

    memcpy(copy, key, len);
    copy[len] = '\0';
    return copy;
}

static int _dictStringCopyHTKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcmp(key1, key2) == 0;
}

static void _dictStringDestructor(void *privdata, void *key)
{
    DICT_NOTUSED(privdata);

    zfree(key);
}

dictType dictTypeHeapStringCopyKey = {
    _dictStringCopyHTHashFunction, /* hash function */
    _dictStringDup,                /* key dup */
    NULL,                          /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    NULL                           /* val destructor */
};

/* This is like StringCopy but does not auto-duplicate the key.
 * It's used for intepreter's shared strings. */
dictType dictTypeHeapStrings = {
    _dictStringCopyHTHashFunction, /* hash function */
    NULL,                          /* key dup */
    NULL,                          /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    NULL                           /* val destructor */
};

/* This is like StringCopy but also automatically handle dynamic
 * allocated C strings as values. */
dictType dictTypeHeapStringCopyKeyValue = {
    _dictStringCopyHTHashFunction, /* hash function */
    _dictStringDup,                /* key dup */
    _dictStringDup,                /* val dup */
    _dictStringCopyHTKeyCompare,   /* key compare */
    _dictStringDestructor,         /* key destructor */
    _dictStringDestructor,         /* val destructor */
};
#endif