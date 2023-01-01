#include "request.h"
#include "segel.h"
#include <assert.h>

RequestQueue *queue;
pthread_mutex_t queueLock;
pthread_cond_t queueCond;

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

void threadWorker(void *_arg) {
  RequestInfo info;

  while (1) {
    pthread_mutex_lock(&queueLock);

    // Wait until there is a job.
    if (queue->size == 0) {
      pthread_cond_wait(&queueCond, &queueLock);
    }
    assert(queue->size > 0);

    info = QueueRemoveFirst(queue);

    pthread_mutex_unlock(&queueLock);

    // Handle request.
    requestHandle(info.fd);
    Close(info.fd);
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

  getargs(&port, &numThreads, &queueSize, &policy, argc, argv);

  queue = QueueCreate(queueSize);
  pthread_mutex_init(&queueLock, NULL);
  pthread_cond_init(&queueCond, NULL);

  // Create thread pool.
  for (int i = 0; i < numThreads; i++) {
    pthread_t thread;
    pthread_create(&thread, NULL, threadWorker, NULL);
  }

  listenfd = Open_listenfd(port);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);

    // Prevent access to the queue.
    pthread_mutex_lock(&queueLock);

    // Wait until there is room in the queue.
    if (queue->size == queue->maxSize) {
      int size = queue->size / 2;
      switch (policy) {
      case BLOCK:
        pthread_cond_wait(&queueCond, &queueLock);
        break;
      case DROP_HEAD:
        QueueRemoveFirst(queue);
      case DROP_TAIL:
        Close(connfd);
        pthread_mutex_unlock(&queueLock);
        continue;
      case DROP_RANDOM:
        for (int i = 0; i < size; i++) {
          QueueRemoveRandom(queue);
        }
      default:
        break;
      }
    }
    assert(queue->size < queue->maxSize);

    RequestInfo info;
    info.fd = connfd;
    QueueAdd(queue, info);

    // Unlock the queue and signal to any waiting threads that there are new
    // jobs.
    pthread_mutex_unlock(&queueLock);
    pthread_cond_signal(&queueCond);
  }
}
