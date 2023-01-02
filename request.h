#ifndef __REQUEST_H__
#include "segel.h"
#include <pthread.h>

typedef struct {
  int id;
  int handleCount;
  int handleStaticCount;
  int handleDynamicCount;
} ThreadStatistics;

typedef struct {
  int fd;
  struct timeval arrivalTime;
  struct timeval handleTime;
} RequestInfo;

typedef struct request_node {
  RequestInfo info;
  struct request_node *next;
  struct request_node *prev;
} RequestNode;

typedef struct {
  RequestNode *head;
  int size;
  int maxSize;

} RequestQueue;

extern volatile RequestQueue *queue;
extern pthread_mutex_t queueLock;
extern pthread_cond_t queueNotEmptyCond;

RequestQueue *QueueCreate(int maxSize);

void QueueFree(RequestQueue *queue);

void QueueAdd(RequestQueue *queue, RequestInfo info);

RequestInfo QueueGetFirst(RequestQueue *queue);

RequestInfo QueueRemoveFirst(RequestQueue *queue);

RequestInfo QueueRemoveLast(RequestQueue *queue);

RequestInfo QueueRemoveRandom(RequestQueue *queue);

void requestHandle(RequestInfo *info, ThreadStatistics *stats);

struct timeval getTime();

struct timeval getTimeDiff(struct timeval *start, struct timeval *stop);

#endif
