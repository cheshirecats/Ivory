/******************************************************
 *
 * Ivory is a high-performance HTTP + WebSocket server
 *
 * https://github.com/cheshirecats/Ivory
 *
 ******************************************************/

#define SERVER_NAME "ivory/0.0.1"
#define DEBUG 1

#include "main.h"

#define LISTEN_PORT 8080
#define IO_THREADS 4
#define IO_BUFFER 512
#define LISTEN_BUFFER 8192
#define EVENT_QUEUE 65536
#define MAX_FD 99999
#define MAX_INDEX_HTM_SIZE 655360

static char* index_response;
static int index_response_size;
static char response_101[] = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: HSmrc0sMlYUkAGmm5OPpG2HaGWk=\r\n\r\n";
static int response_101_size = sizeof(response_101) - 1;

static int epoll[IO_THREADS] = {0};
static int event_count[IO_THREADS] = {0};
static int fd_status[MAX_FD] = {0};
static char fd_hash[MAX_FD][28];

void setnonblocking(int fd)
{
  int opt = fcntl(fd, F_GETFL);
  if (opt < 0) die();
  opt = opt | O_NONBLOCK;
  if (fcntl(fd, F_SETFL, opt) < 0) die();
}

void listen_port(int port)
{
  struct sockaddr_in sin;
  if ((server = socket(AF_INET, SOCK_STREAM, 0)) <= 0) die();

  memset(&sin, 0, sizeof(sockaddr_in));
  sin.sin_family = AF_INET;
  sin.sin_port = htons((short)(port));
  sin.sin_addr.s_addr = INADDR_ANY;
  if (bind(server, (sockaddr*) &sin, sizeof(sin)) != 0) die();
  if (listen(server, LISTEN_BUFFER) != 0) die();
}

void close_fd(int& ee, int& f)
{
  static epoll_event ev;
  ev.data.fd = f;
  epoll_ctl(epoll[ee], EPOLL_CTL_DEL, f, &ev);
  event_count[ee]--;
  shutdown(f, SHUT_RDWR);
  close(f);
}

void* loop_io(void* p)
{
  int ee = *(int*)(p);
  epoll_event ev, events[EVENT_QUEUE];
  char in[IO_BUFFER + 100];
  char out[IO_BUFFER + 100];
  while (1) {
    int n = epoll_wait(epoll[ee], events, EVENT_QUEUE, -1);
    for (int i = 0; i < n; i++) {
      int f = events[i].data.fd;
      int& g = fd_status[f];
      echo("thr %d con %d evn %d val %d src %d sta %d ", ee, event_count[ee], i, events[i].events, f, g);

      if (events[i].events & EPOLLIN) {
        int r = recv(f, in, sizeof(in), 0);
        if (r <= 0) {
          if (r == 0)
            echo("\x1b[0;31mclose %d\n\x1b[0m", f);
          else
            echo("\x1b[0;31merror %d\n\x1b[0m", f);
          close_fd(ee, f);
          continue;
        }
        echo("recv %d BYTEs\n", r);
        if (g <= 2) {
          in[r - 2] = 0;
          echo("\x1b[0;34m%s\x1b[0m", in);
        } else {
          in[r] = 0;
          for (int j = 0; j < r; j++) {
            echo("%02x ", (unsigned char)(in[j]));
          }
          out("\n");
          if (((BYTE)in[1]) > 128)
            echo("\x1b[0;34m%s\x1b[0m\n", ws_decode(in));
          else {
            echo("\x1b[0;31mclose %d\n\x1b[0m", f);
            close_fd(ee, f);
          }
        }
        if (g == 0) {
          if ((in[0] != 'G') || (in[1] != 'E') || (in[2] != 'T') || (in[3] != ' ') || (in[4] != '/')) goto _baad_req_;
          if (strstr(in, "Sec-WebSocket-Version: 13")) {
            char* c = strstr(in, "Sec-WebSocket-Key: "); if (!c) goto _baad_req_;
            c += 19; *(c + 24) = 0; ws_hash(c, fd_hash[f]);
            g = 2;
          } else {
            if (in[5] == ' ') g = 1; else goto _baad_req_;
          }
          if (g > 0) {
            ev.data.fd = f;
            ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
            epoll_ctl(epoll[ee], EPOLL_CTL_MOD, f, &ev);
            goto _good_req_;
          }
_baad_req_:
          g = -1;
_good_req_:
          g = g;
        }
        if (g <= 0) {
          echo("bad request \x1b[0;31mclose %d\n\x1b[0m", f);
          close_fd(ee, f);
        }
      }

      if (events[i].events & EPOLLOUT) {
        int r = 0;
        if (g == 1)
          r = send(f, index_response, index_response_size, 0);
        else if (g == 2) {
          strncpy(response_101 + response_101_size - 32, fd_hash[f], 28);
          r = send(f, response_101, response_101_size, 0);
          g = 3;
        } else if (g == 3) {
          r = ws_send(f, "Hello!", out);
          g = 4;
        } else
          continue;
        echo("send %d BYTEs\n", r);
      }
    }
  }
  return 0;
}

int main()
{
  setbuf(stdout, NULL);
  signal(SIGINT, &user_exit);
  signal(SIGABRT, &user_exit);
  signal(SIGTERM, &user_exit);
  printf("\x1b[0;31m\n[[[ " SERVER_NAME " ]]]\n\n\x1b[0m");

  // LOAD HTML

  char buffer[MAX_INDEX_HTM_SIZE];
  FILE *f;
  f = fopen("index.htm", "rb");
  if (!f) die();
  fread(buffer, MAX_INDEX_HTM_SIZE, 1, f);
  fclose(f);
  char str_port[255]; sprintf(str_port, "%d", LISTEN_PORT);
  int n = strlen(buffer) - 8 + strlen(str_port);
  index_response = new char[n + 2000];
  char* p_port = strstr(buffer, "[%PORT%]");
  *p_port = 0;
  sprintf(index_response, "HTTP/1.1 200 OK\r\nServer: " SERVER_NAME "\r\nContent-Length: %d\r\nConnection: close\r\nContent-Type: text/html; charset=utf-8\r\n\r\n%s%s%s", n, buffer, str_port, p_port + 8);
  index_response_size = strlen(index_response);

  // SERVER LOGIC

  pthread_t io_thread[IO_THREADS];
  int io_thread_index[IO_THREADS];
  for (int i = 0; i < IO_THREADS; i++) {
    epoll[i] = epoll_create1(0);
    io_thread_index[i] = i;
    if (pthread_create(&io_thread[i], NULL, loop_io, &io_thread_index[i]) != 0) die();
  }
  listen_port(LISTEN_PORT);
  sockaddr_in sin;
  socklen_t sock_len = sizeof(sockaddr_in);
  int client = 0;
  int ee = 0;
  epoll_event ev;
  while ((client = accept(server, (sockaddr*) &sin, &sock_len)) > 0) {
    setnonblocking(client);
    if (client >= MAX_FD) {
      shutdown(client, SHUT_RDWR);
      close(client);
    }
    ev.data.fd = client;
    fd_status[client] = 0;
    ev.events = EPOLLIN | EPOLLET;
    epoll_ctl(epoll[ee], EPOLL_CTL_ADD, client, &ev);
    event_count[ee]++;
    echo("thr %d con %d add %s:%d \x1b[0;31mto %d\n\x1b[0m", ee, event_count[ee], inet_ntoa(sin.sin_addr), sin.sin_port, client);
    ee = (ee + 1) % IO_THREADS;
  }
  close(server);
  return 0;
}
