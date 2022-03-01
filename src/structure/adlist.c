//
// Created by Administrator on 2022/3/1.
//
#include "adlist.h"
#include "zmalloc.h"

/**
 * 创建一个新的链表
 *
 * T = O(1)
 *
 * @return 创建成功返回链表，失败返回NULL
 */
list * listCreate(void)
{
    list * lt;

    //分配内存
    if ( (lt = z_malloc(sizeof(list))) == NULL)
        return NULL;

    lt->head = lt->tail = NULL;
    lt->len = 0;
    lt->dup = NULL;
    lt->free = NULL;
    lt->match = NULL;

    return lt;
}

/**
 * 将包含value的新节点添加到lt的表头
 *
 * T = O(1)
 *
 * @param lt 给定链表
 * @param value 定值
 * @return 执行成功，返回传入的链表指针
 *         执行失败，返回NULL
 */
list * listAddNodeHead(list * lt, void * value)
{
    listNode * node;

    //分配内存
    if ( (node = z_malloc(sizeof(listNode))) == NULL)
        return NULL;

    node->value = value;

    if (lt->len == 0)   //添加到空链表
    {
        lt->head = lt->tail = node;
        node->prev = node->next = NULL;
    }
    else                //添加到非空链表
    {
        node->prev = NULL;
        node->next = lt->head;
        lt->head->prev = node;
        lt->head = node;
    }

    lt->len++;

    return lt;
}

/**
 * 将包含value的新节点添加到lt的表尾
 *
 * T = O(1)
 *
 * @param lt 给定链表
 * @param value 定值
 * @return 执行成功，返回传入的链表指针
 *         执行失败，返回NULL
 */
list * listAddNodeTail(list * lt, void * value)
{
    listNode * node;

    //分配内存
    if ( (node = z_malloc(sizeof(node))) == NULL)
        return NULL;

    node->value = value;

    if (lt->len == 0)   //目标链表为空
    {
        lt->head = lt->tail = node;
        node->prev = node->next = NULL;
    }
    else                //目标链表非空
    {
        node->prev = lt->tail;
        node->next = NULL;
        lt->tail->next = node;
        lt->tail = node;
    }

    lt->len++;

    return lt;
}

/**
 * 创建一个包含值 value 的新节点，并将它插入到 old_node 的之前或之后
 *
 * T = O(1)
 *
 * @param lt 要插入的链表
 * @param oldNode 要插入的位置
 * @param value 插入节点的值
 * @param after 值为0，将新节点插入到oldNode前；
 *              值为1，将新节点插入到oldNode后。
 * @return 插入成功，返回链表;
 *          失败，则返回NULL
 */
list * listInsertNode(list * lt, listNode * oldNode, void * value, int after)
{
    listNode * node;

    //创建新节点
    if ( (node = z_malloc(sizeof(listNode))) == NULL)
        return NULL;
    node->value = value;

    if (after)  //将新节点添加到给定节点之后
    {
        node->prev = oldNode;
        node->next = oldNode->next;
        if (lt->tail == oldNode)    //原节点是尾节点
            lt->tail = node;
    }
    else        //将新节点添加到给定节点之前
    {
        node->prev = oldNode->prev;
        node->next = oldNode;
        if (lt->head == oldNode)    //原节点是头节点
            lt->head = node;
    }

    if (node->prev != NULL)
        node->prev->next = node;
    if (node->next != NULL)
        node->next->prev = node;

    lt->len++;

    return lt;
}

/**
 * 查找链表 list 中值和 key 匹配的节点。
 *
 * 对比操作由链表的 match 函数负责进行，
 * 如果没有设置 match 函数，
 * 那么直接通过对比值的指针来决定是否匹配。
 *
 * T = O(N)
 *
 * @param lt 链表
 * @param value 要匹配的值
 * @return  如果匹配成功，那么第一个匹配的节点会被返回。
 *          如果没有匹配任何节点，那么返回 NULL 。
 */
listNode * listSearchKey(list * lt, void * key)
{
    listIter * iter;
    listNode * node;

    //迭代整个链表
    iter = listGetIterator(lt, AL_START_HEAD);
    while ( (node = listNext(iter)) != NULL)
    {

    }
}

/**
 * 返回链表在给定索引上的值。
 *
 * 索引以 0 为起始，也可以是负数， -1 表示链表最后一个节点，诸如此类。
 *
 * T = O(N)
 *
 * @param lt
 * @param index
 * @return 如果索引超出范围（out of range），返回 NULL 。
 */
listNode * listIndex(list * lt, long index)
{
    listNode * node;

    //判断索引是否超出范围
    if ( (index >= 0 ? index : -index - 1) >= lt->len)
        return NULL;

    if (index < 0)  //索引为负数，从表尾查找
    {
        index = -index - 1;
        node = lt->tail;
        while (index-- && node) node = node->prev;
    }
    else            //索引为正数，从表头查找
    {
        node = lt->head;
        while (index-- && node) node = node->next;
    }

    return node;
}

/**
 * 从链表 list 中删除给定节点 node
 *
 * 对节点私有值(private value of the node)的释放工作由调用者进行。
 *
 * T = O(1)
 *
 * @param lt 目标链表
 * @param node 给定节点
 */
void listDelNode(list * lt, listNode * node)
{
    //判断是否为头节点
    if (node->prev)
        node->prev->next = node->next;
    else
        lt->head = node->next;

    //判断是否为尾节点
    if (node->next)
        node->next->prev = node->prev;
    else
        lt->tail = node->prev;

    //释放值
    if (lt->free) lt->free(node->value);
    //释放节点
    z_free(node);

    lt->len--;
}

/**
 * 取出链表的表尾节点，并将它移动到表头，成为新的表头节点。
 *
 * T = O(1)
 *
 * @param lt 目标链表
 */
void listRotate(list * lt)
{
    if (lt->len <= 1) return;

    listNode * tail = lt->tail;

    //取出表尾节点
    lt->tail = tail->prev;
    lt->tail->next = NULL;

    //插入到表头
    tail->next = lt->head;
    lt->head->prev = tail;
    tail->prev = NULL;
    lt->head = tail;
}

/**
 * 复制整个链表。
 *
 * 如果链表有设置值复制函数 dup ，那么对值的复制将使用复制函数进行，
 * 否则，新节点将和旧节点共享同一个指针。
 *
 * 无论复制是成功还是失败，输入节点都不会修改。
 *
 * T = O(N)
 *
 * @param lt
 * @return  复制成功返回输入链表的副本，
 *          如果因为内存不足而造成复制失败，返回 NULL 。
 */
list * listDup(list * lt)
{
    list * newList;
    listIter * iter;
    listNode * node;


}
