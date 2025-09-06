#include "HdlsStatePairQueue.h"
#include "log.h"

Mutex g_mutex = {0};

// 初始化队列
void hdlsStatePairQueueInit(HdlsStatePairQueue *q) {
    mutexLock(&g_mutex);
    if (q->initialized) {
        mutexUnlock(&g_mutex);
        return; // 已初始化
    }
    q->front = 0;
    q->rear = 0;
    q->size = 0;
    semaphoreInit(&q->resource, 0);
    semaphoreInit(&q->space, MAX_SIZE);
    q->initialized = true;
    mutexUnlock(&g_mutex);
}

// 检查队列是否已满
bool hdlsStatePairQueueIsFull(HdlsStatePairQueue *q) {
    return q->size == MAX_SIZE;
    // 或者不用size变量：(rear + 1) % MAX_SIZE == front;
}

// 检查队列是否为空
bool hdlsStatePairQueueIsEmpty(HdlsStatePairQueue *q) {
    return q->size == 0;
    // 或者：rear == front;
}

// 入队操作
bool hdlsStatePairQueueEnqueue(HdlsStatePairQueue *q, HdlsStatePair value) {
    semaphoreWait(&q->space);
    mutexLock(&g_mutex);
    q->statePair[q->rear] = value;
    q->rear = (q->rear + 1) % MAX_SIZE; // 循环利用数组空间
    q->size++;
    log_debug("[HdlsStatePairQueue] Enqueue, size=%d, statePair.count=%d", q->size, value.count);
    mutexUnlock(&g_mutex);
    semaphoreSignal(&q->resource);
    return true;
}

// 出队操作
bool hdlsStatePairQueueDequeue(HdlsStatePairQueue *q, HdlsStatePair *value) {
    semaphoreWait(&q->resource);
    mutexLock(&g_mutex);
    *value = q->statePair[q->front];
    q->front = (q->front + 1) % MAX_SIZE; // 循环利用数组空间
    q->size--;
    log_debug("[HdlsStatePairQueue] Dequeue, size=%d, statePair.count=%d", q->size, value->count);
    semaphoreSignal(&q->space);
    mutexUnlock(&g_mutex);
    return true;
}

// 出队操作
bool hdlsStatePairQueueTryDequeue(HdlsStatePairQueue *q, HdlsStatePair *value) {
    if(!semaphoreTryWait(&q->resource)) return false;
    mutexLock(&g_mutex);
    *value = q->statePair[q->front];
    q->front = (q->front + 1) % MAX_SIZE; // 循环利用数组空间
    q->size--;
    log_debug("[HdlsStatePairQueue] Try Dequeue, size=%d", q->size);
    semaphoreSignal(&q->space);
    mutexUnlock(&g_mutex);
    return true;
}

// 获取队首元素（不删除）
bool hdlsStatePairQueuePeek(HdlsStatePairQueue *q, HdlsStatePair *value) {
    if (hdlsStatePairQueueIsEmpty(q)) {
        printf("Queue is empty!\n");
        return false;
    }
    *value = q->statePair[q->front];
    return true;
}