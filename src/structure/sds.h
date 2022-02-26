//
// Created by Administrator on 2022/2/26.
//
/**
 * 用于动态表示字符串的结构声明。
 * 简单动态字符串(Simple Dynamic Strings, SDS)
 * 存储字符串和整型数据
 */
#ifndef REDIS_DESIGN_SDS_H
#define REDIS_DESIGN_SDS_H

struct sds
{
    //记录buf数组中已使用字节的数量，等于SDS所保存字符串的长度
    int len;
    //记录buf数组中未使用字节的数量
    int free;
    //字节数组，用于保存字符串
    char buf[];
};

#endif //REDIS_DESIGN_SDS_H
