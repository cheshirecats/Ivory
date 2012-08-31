#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>

// !!! WARNING: SELF-DESTRUCTION AFTER 999999 HTTP CONNECTIONS !!!

#define EVENT_QUEUE 65536
#define RECV_BUFFER 65536
#define LISTEN_BUFFER 8192
#define DIE_DIE 999999
#define THREAD_IO 1

const static char* HTTP_200 = "HTTP/1.1 200 OK\r\nServer: ivory\r\nContent-Length: 5\r\nConnection: close\r\nContent-Type: text/plain\r\n\r\nHello";

static int e[THREAD_IO] = {0 };
static int ec[THREAD_IO] = {0 };
static pthread_t worker;
static int xi = 0;
static int xd[DIE_DIE];

void die()
{
  printf("error\n");
  exit(-1);
}

void setnonblocking(int fd)
{
  int opt = fcntl(fd, F_GETFL);
  if (opt < 0) die();
  opt = opt | O_NONBLOCK;
  if (fcntl(fd, F_SETFL, opt) < 0) die();
}

int listen_port(int port)
{
  struct sockaddr_in sin;
  int opt = 1;
  int server = 0;
  if ((server = socket(AF_INET, SOCK_STREAM, 0)) <= 0) die();
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const void*) &opt, sizeof(opt));

  memset(&sin, 0, sizeof(struct sockaddr_in));
  sin.sin_family = AF_INET;
  sin.sin_port = htons((short)(port));
  sin.sin_addr.s_addr = INADDR_ANY;
  if (bind(server, (struct sockaddr*) &sin, sizeof(sin)) != 0) die();
  if (listen(server, LISTEN_BUFFER) != 0) die();
  return server;
}

void* loop_io(void* p)
{
  int ee = *(int*)(p);
  struct epoll_event ev, events[EVENT_QUEUE];
  char buffer[RECV_BUFFER];
  while (1) {
    int n = epoll_wait(e[ee], events, EVENT_QUEUE, -1);
    for (int i = 0; i < n; i++) {
      int f = events[i].data.fd;
      /*printf("%d %d %d %d %d\n", i, events[i].events, ee, ec[ee], f);
      if (events[i].events & EPOLLIN) {
        int r = recv(f, buffer, sizeof(buffer), 0);
      	printf("%s", buffer);
      }*/
      if (events[i].events & EPOLLOUT) {
        int r = send(f, HTTP_200, strlen(HTTP_200), 0);
        ev.data.fd = f;
        epoll_ctl(e[ee], EPOLL_CTL_DEL, f, &ev);
        ec[ee]--;
        shutdown(f, SHUT_RDWR);
        close(f);
      }
    }
  }
  return 0;
}

void* worker_accept(void* p)
{
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &set, NULL);

  int i = 0;
  int client = 0;
  int sig = 0;
  int ee = 0;
  struct epoll_event ev;

  while (1) {
    sigwait(&set, &sig);
    while (i < xi) {
      client = xd[i];
      i++;
      setnonblocking(client);
      ev.data.fd = client;
      ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
      epoll_ctl(e[ee], EPOLL_CTL_ADD, client, &ev);
      ec[ee]++;
      ee = (ee + 1) % THREAD_IO;
    }
  }
  return 0;
}

int main(int argc, char* argv[])
{
  setbuf(stdout, NULL);
  int server = listen_port(80);

  pthread_t t[THREAD_IO];
  int ti[THREAD_IO];
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

  for (int i = 0; i < THREAD_IO; i++) {
    e[i] = epoll_create1(0);
    ti[i] = i;
    if (pthread_create(&t[i], &attr, loop_io, &ti[i]) != 0) die();
  }

  struct sockaddr_in sin;
  socklen_t sock_len = sizeof(struct sockaddr_in);
  int client = 0;

  if (pthread_create(&worker, &attr, worker_accept, NULL) != 0) die();
  while ((client = accept(server, (struct sockaddr*) &sin, &sock_len)) > 0) {
    xd[xi] = client;
    xi++;
    if (xi >= DIE_DIE) die();
    pthread_kill(worker, SIGUSR1);
  }

  close(server);
  return 0;
}