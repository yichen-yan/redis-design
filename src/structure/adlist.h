//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_ADLIST_H
#define REDIS_DESIGN_ADLIST_H

/**
 * 表示链表节点的结构
 */
typedef struct listNode
{
    //前置节点
    struct listNode * prev;
    //后置节点
    struct listNode * next;
    //节点的值
    void * value;
} listNode;

/**
 * 表示链表的结构
 */
typedef struct list
{
    listNode * head;    //表头节点
    listNode * tail;    //表尾节点
    unsigned long len;  //链表所包含的节点数量

    void * (*dup)(void * ptr);              //节点值复制函数
    void (*free)(void * ptr);               //节点值释放函数
    int (*match)(void * ptr, void * key);   //节点值对比函数
} list;

#endif //REDIS_DESIGN_ADLIST_H
