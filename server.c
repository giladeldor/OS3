#include "request.h"
#include "segel.h"
#include <assert.h>

volatile RequestQueue *queue;
volatile int numCurrentWorkers;
pthread_mutex_t queueLock;
pthread_cond_t queueNotEmptyCond;
pthread_cond_t queueNotFullCond;

typedef enum {
  BLOCK,
  DROP_TAIL,
  DROP_HEAD,
  DROP_RANDOM,
} OverloadPolicy;

//
// server.c: A very, very simple web server
//
// To run:
//  ./server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

void threadWorker(void *arg) {
  ThreadStatistics statistics;
  statistics.id = (int)arg;
  statistics.handleCount = 0;
  statistics.handleStaticCount = 0;
  statistics.handleDynamicCount = 0;

  RequestInfo info;

  while (1) {
    pthread_mutex_lock(&queueLock);

    // Wait until there is a job.
    while (queue->size == 0) {
      pthread_cond_wait(&queueNotEmptyCond, &queueLock);
    }
    assert(queue->size > 0);
    numCurrentWorkers++;

    info = QueueRemoveFirst(queue);
    info.handleTime = getTime();

    pthread_mutex_unlock(&queueLock);

    // printf("Dispatch Time: %lu.%06lu\n",
    //        getTimeDiff(&info.arrivalTime, &info.handleTime).tv_sec,
    //        getTimeDiff(&info.arrivalTime, &info.handleTime).tv_usec);

    // Handle request.
    statistics.handleCount++;
    requestHandle(&info, &statistics);
    Close(info.fd);

    pthread_mutex_lock(&queueLock);
    // Signal to main thread that there is space in queue.
    numCurrentWorkers--;
    pthread_mutex_unlock(&queueLock);
    pthread_cond_signal(&queueNotFullCond);
  }
}

// HW3: Parse the new arguments too
void getargs(int *port, int *numThreads, int *queueSize, OverloadPolicy *policy,
             int argc, char *argv[]) {
  if (argc < 5) {
    fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <drop_policy>\n",
            argv[0]);
    exit(1);
  }
  *port = atoi(argv[1]);
  *numThreads = atoi(argv[2]);
  *queueSize = atoi(argv[3]);

  if (strcmp(argv[4], "block") == 0) {
    *policy = BLOCK;
  } else if (strcmp(argv[4], "dt") == 0) {
    *policy = DROP_TAIL;
  } else if (strcmp(argv[4], "dh") == 0) {
    *policy = DROP_HEAD;
  } else if (strcmp(argv[4], "random") == 0) {
    *policy = DROP_RANDOM;
  } else {
    fprintf(stderr, "Usage: %s <port> <threads> <queue_size> <drop_policy>\n",
            argv[0]);
    exit(1);
  }
}

int main(int argc, char *argv[]) {
  int listenfd, connfd, port, numThreads, queueSize, clientlen;
  OverloadPolicy policy;
  struct sockaddr_in clientaddr;
  numCurrentWorkers = 0;

  getargs(&port, &numThreads, &queueSize, &policy, argc, argv);

  queue = QueueCreate(queueSize);
  pthread_mutex_init(&queueLock, NULL);
  pthread_cond_init(&queueNotEmptyCond, NULL);
  pthread_cond_init(&queueNotFullCond, NULL);

  //   printf("NumThreads: %d, QueueSize: %d\n", numThreads, queueSize);

  // Create thread pool.
  for (int i = 0; i < numThreads; i++) {
    pthread_t thread;
    pthread_create(&thread, NULL, threadWorker, (void *)i);
  }

  listenfd = Open_listenfd(port);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
    struct timeval arrival = getTime();
    // printf("\tGOT CONNECTION\n");

    // Prevent access to the queue.
    pthread_mutex_lock(&queueLock);

    // Wait until there is room in the queue.
    // printf("current + size = %d\n", numCurrentWorkers + queue->size);
    if (numCurrentWorkers + queue->size == queue->maxSize) {
      //   printf("\tFULL QUEUE\n");
      int size = (queue->size + 1) / 2;
      RequestInfo droppedInfo;

      switch (policy) {
      case BLOCK:
        pthread_cond_wait(&queueNotFullCond, &queueLock);
        break;
      case DROP_HEAD:
        if (queue->size == 0) {
          Close(connfd);
          pthread_mutex_unlock(&queueLock);
          //   printf("Dropped1: %lu.%06lu\n", arrival.tv_sec, arrival.tv_usec);
          continue;
        } else {
          droppedInfo = QueueRemoveFirst(queue);
          //   printf("Dropped2: %lu.%06lu\n", droppedInfo.arrivalTime.tv_sec,
          //          droppedInfo.arrivalTime.tv_usec);
          Close(droppedInfo.fd);
        }
        break;
      case DROP_TAIL:
        Close(connfd);
        pthread_mutex_unlock(&queueLock);
        continue;
      case DROP_RANDOM:
        if (queue->size == 0) {
          Close(connfd);
          pthread_mutex_unlock(&queueLock);
          continue;
        } else {
          for (int i = 0; i < size; i++) {
            droppedInfo = QueueRemoveRandom(queue);
            Close(droppedInfo.fd);
          }
        }
        break;
      default:
        break;
      }
    }

    RequestInfo info;
    info.fd = connfd;
    info.arrivalTime = arrival;
    // printf("BEFORE ADD: current + size = %d\n",
    //        numCurrentWorkers + queue->size);
    QueueAdd(queue, info);
    // printf("AFTER ADD: current + size = %d\n", numCurrentWorkers +
    // queue->size);

    // Unlock the queue and signal to any waiting threads that there are new
    // jobs.
    pthread_mutex_unlock(&queueLock);
    pthread_cond_signal(&queueNotEmptyCond);
  }
}
