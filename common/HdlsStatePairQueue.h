
#pragma once
#include <switch.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define MAX_SIZE 5 // 定义队列的最大容量


typedef struct {
    HiddbgHdlsState left;
    HiddbgHdlsState right;
    int count;
    u8 padding[0x4];
} HdlsStatePair;

typedef struct {
    HdlsStatePair statePair[MAX_SIZE];
    int front; // 指向队首元素
    int rear;  // 指向下一个要插入的位置（队尾元素的下一个位置）
    int size;  // 当前队列中的元素个数
    Semaphore resource; // 资源信号量
    Semaphore space;    // 空间信号量
    bool initialized;
    u8 padding[0x3];
} HdlsStatePairQueue;

// 初始化队列
void hdlsStatePairQueueInit(HdlsStatePairQueue *q);

// 检查队列是否已满
bool hdlsStatePairQueueIsFull(HdlsStatePairQueue *q);

// 检查队列是否为空
bool hdlsStatePairQueueIsEmpty(HdlsStatePairQueue *q);

// 入队操作
bool hdlsStatePairQueueEnqueue(HdlsStatePairQueue *q, HdlsStatePair value);

// 出队操作
bool hdlsStatePairQueueDequeue(HdlsStatePairQueue *q, HdlsStatePair *value);

// 出队操作
bool hdlsStatePairQueueTryDequeue(HdlsStatePairQueue *q, HdlsStatePair *value);

// 获取队首元素（不删除）
bool hdlsStatePairQueuePeek(HdlsStatePairQueue *q, HdlsStatePair *value);