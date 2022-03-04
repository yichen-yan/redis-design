//
// Created by Administrator on 2022/3/4.
//

#include <ctype.h>
#include <sys/time.h>

#include "dict.h"
#include "zmalloc.h"

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
    }
}
