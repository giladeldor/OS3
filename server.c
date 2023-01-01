#include "request.h"
#include "segel.h"
#include <assert.h>

RequestQueue *queue;
pthread_mutex_t queueLock;
pthread_cond_t queueCond;

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
void getargs(int *port, int *numThreads, int *queueSize, int argc,
             char *argv[]) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <port> <threads> <queue_size>\n", argv[0]);
    exit(1);
  }
  *port = atoi(argv[1]);
  *numThreads = atoi(argv[2]);
  *queueSize = atoi(argv[3]);
}

int main(int argc, char *argv[]) {
  int listenfd, connfd, port, numThreads, queueSize, clientlen;
  struct sockaddr_in clientaddr;

  getargs(&port, &numThreads, &queueSize, argc, argv);

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
      pthread_cond_wait(&queueCond, &queueLock);
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
