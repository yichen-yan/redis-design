//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_ADLIST_H
#define REDIS_DESIGN_ADLIST_H

/**
 * 表示双端链表节点的结构
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
 * 表示双端链表的结构
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

/**
 * 双端链表迭代器
 */
typedef struct listIter
{
    listNode * next;   //当前迭代到的节点
    int direction;     //迭代的方向
} listIter;

//Functions implemented as macros

//返回链表的节点数量、表头节点、表尾节点
//T = O(1)
#define listLength(l) ((l)->len)
#define listFirst(l) ((l)->head)
#define listLast(l) ((l)->tail)

//返回节点的前置节点、后置节点、值
//T = O(1)
#define listPrevNode(n) ((n)->prev)
#define listNextNode(n) ((n)->next)
#define listNodeValue(n) ((n)->value)

//将链表l的值复制、值释放、对比函数设置为m
//T = O(1)
#define listSetDupMethod(l, m) ((l)->dup = (m))
#define listSetFreeMethod(l, m) ((l)->free = (m))
#define listSetMatchMethod(l, m) ((l)->match = (m))

//返回链表的值复制、值释放、对比函数
//T = O(1)
#define listGetDupMethod(l) ((l)->dup)
#define listGetFreeMethod(l) ((l)->free)
#define listGetMatchMethod(l) ((l)->match)

//Prototypes
list * listCreate(void);
list * listAddNodeHead(list * list, void * value);
list * listAddNodeTail(list * list, void * value);
list * listInsertNode(list * list, listNode * oldNode, void * value, int after);
listNode * listSearchKey(list * list, void * key);
listNode * listIndex(list * list, long index);
void listDelNode(list * list, listNode * node);
void listRotate(list * list);
list * listDup(list * list);
void listRelease(list * list);

listIter * listGetIterator(list * list, int direction);
listNode * listNext(listIter * iter);
void listReleaseIterator(listIter * iter);
void listRewind(list * list, listIter * li);
void listRewindTail(list * list, listIter * li);

/**
 * 迭代器进行迭代的方向
 */
//从表头向表尾迭代
#define AL_START_HEAD 0
//从表尾向表头迭代
#define AL_START_TAIL 1

#endif //REDIS_DESIGN_ADLIST_H
