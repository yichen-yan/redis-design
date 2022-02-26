//
// Created by Administrator on 2022/2/26.
//

#ifndef REDIS_DESIGN_REDIS_H
#define REDIS_DESIGN_REDIS_H

/**
 * 跳跃表节点的实现。
 */
typedef struct zskiplistNode
{
    struct zxkiplistLevel
    {
        struct zskiplistNode * forward; //前进指针
        unsigned int span;              //跨度
    } level[];                          //层
    struct zskiplistNode * backward;    //后退指针
    double score;       //分值
    robj * obj;         //成员对象
} zskiplistNode;

/**
 * 跳跃表的结构定义。
 */
 typedef struct zskiplist
 {
     zskiplistNode * header;    //表头结点
     zskiplistNode * tail;      //表尾节点
     unsigned long length;      //表中节点的数量
     int level;                 //表中层数最大的节点的层数
 } zskiplist;

#endif //REDIS_DESIGN_REDIS_H
