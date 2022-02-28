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

