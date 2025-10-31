#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
// Linked list implementation
#include "queue.h"
// Needed for the periodic task
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timerfd.h>

#define TRUE (true)
#define FALSE (false)
#define AESD_SERVER_SOCKET (9000)
#define AESD_SERVER_SOCKET_CHAR "9000"
#define AESD_SERVER_DATA_LOG_PATH ("/var/tmp/aesdsocketdata")
#define AESD_SERVER_BUFFER_SIZE (1 * 1024)
#define THREAD_STACK_SIZE ()

/**
 * Handle SIGINT and SIGTERM
 * completes any connection operation
 * closes the open socket
 * delete the AESD_SERVER_DATA_LOG_PATH
 */
volatile sig_atomic_t terminate = FALSE;

SLIST_HEAD(slisthead, thread_list_t)
thread_id_list;
/**
 * Synchronize access to socket data
 */
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct thread_params_t {
  int conn_id;
  int log_fd;
} thread_params_t;

typedef struct thread_ts_params_t {
  struct timespec ts;
  int log_fd;
} thread_ts_params_t;

typedef struct thread_list_t {
  pthread_t id;
  SLIST_ENTRY(thread_list_t)
  next;
} thread_list_t;

pthread_t thread_id;
/**
 * Creates the file AESD_SERVER_DATA_LOG_PATH if it does not
 * exist
 * returns the file descriptor which needs to be closed by
 * the caller
 */
int createdatalog(const char *log_path) {
  int file_fd = open(log_path, O_CREAT | O_RDWR, 0644);
  if (file_fd == -1) {
    syslog(LOG_ERR, "Unable to create/open file %s", log_path);
    return file_fd;
  }
  return file_fd;
}

void sig_handler(int signal) {
  switch (signal) {
  case SIGINT:
  case SIGTERM:
    terminate = TRUE;
    break;
  default:
    break;
  }
}

size_t dist_to_char(const char *buffer, char char_to_find, size_t max_len) {
  size_t distance = 0;
  while (buffer != NULL && *buffer != '\0' && *buffer != char_to_find) {
    buffer++;
    distance++;
    if (distance >= max_len) {
      break;
    }
  }
  return distance;
}

void *timestamp_logger(void *thread_params) {
  thread_ts_params_t *params = thread_params;
  int log_fd = params->log_fd;
  free(thread_params);
  int64_t missed = 0;
  char date[100];
  time_t tm;
  struct tm *tmp;
  struct pollfd polld;

  int tfd = timerfd_create(CLOCK_MONOTONIC, 0);
  if (tfd == -1) {
    syslog(LOG_ERR, "Failed to create timer %s", strerror(errno));
    pthread_exit(&tfd);
  }
  polld.fd = tfd;
  polld.events = POLLIN;

  struct itimerspec itspec = {.it_interval.tv_sec = 10,
                              .it_interval.tv_nsec = 0,
                              .it_value.tv_sec = 10,
                              .it_value.tv_nsec = 0};
  if (timerfd_settime(tfd, 0, &itspec, NULL) != 0) {
    syslog(LOG_ERR, "timerfd_settime failed: %s", strerror(errno));
  }
  while (!terminate) {
    // wait
    int poll_rv;
    if ((poll_rv = poll(&polld, 1, 250)) == -1) {
      syslog(LOG_ERR, "Error polling timer: %s", strerror(errno));
    } else if (poll_rv == 0) {
      //    syslog(LOG_ERR, "Poll timeout");
    } else if (polld.revents & POLLIN) {
      int ret = read(tfd, &missed, sizeof(missed));
      if (ret == -1) {
        syslog(LOG_ERR, "Error reading timer: %s", strerror(errno));
      }
      tm = time(NULL);
      tmp = localtime(&tm);
      strftime(date, sizeof(date), "timestamp:%a, %d %b %y %T %z\n", tmp);
      pthread_mutex_lock(&data_mutex);
      int data_written = write(log_fd, date, strlen(date));
      pthread_mutex_unlock(&data_mutex);
      syslog(LOG_ERR, "Writen date: %s : chars: %d", date, data_written);
    } else {
      syslog(LOG_DEBUG, "Poll interrutped");
    }
  }
  pthread_exit(nullptr);
}

void *handle_connection(void *thread_params) {
  thread_params_t *params = thread_params;
  int conn_id = params->conn_id;
  int log_fd = params->log_fd;

  free(thread_params); // better free earlier than forget to free
  void *data_buffer = calloc(1, AESD_SERVER_BUFFER_SIZE);
  bool recv_continue = true;
  while (recv_continue) {
    syslog(LOG_DEBUG, "%lu: b4 recv: conn: %d : log: %d", pthread_self(),
           conn_id, log_fd);
    ssize_t recvd =
        recv(conn_id, data_buffer, AESD_SERVER_BUFFER_SIZE, MSG_DONTWAIT);
    syslog(LOG_DEBUG, "%lu: afte recv: %ld", pthread_self(), recvd);
    if (recvd == 0 ||
        (recvd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
      char *read_buffer = (char *)calloc(1, AESD_SERVER_BUFFER_SIZE);
      struct stat fst = {0};
      fstat(log_fd, &fst);
      int bytes_written = fst.st_size; // get the file size w/o seek
      off_t this_thread_offset = 0;

      syslog(LOG_DEBUG, "%lu: bytes writen into file %d", pthread_self(),
             bytes_written);
      while (bytes_written > 0) {
        syslog(LOG_DEBUG, "%lu: bytes writen %d", pthread_self(),
               bytes_written);
        int read_chunk = pread(log_fd, read_buffer, AESD_SERVER_BUFFER_SIZE,
                               this_thread_offset);
        int sent_offset = 0;
        // Send the data stored in read_buffer
        syslog(LOG_DEBUG, "%lu: read bytes from file log %d", pthread_self(),
               read_chunk);
        while (read_chunk >= 0 && sent_offset < read_chunk) {
          int line_len = dist_to_char(read_buffer + sent_offset, '\n',
                                      AESD_SERVER_BUFFER_SIZE - sent_offset);
          if (line_len <= AESD_SERVER_BUFFER_SIZE - (sent_offset + 1)) {
            ++line_len;
          } else if (line_len == 0) {
            // the char was not found, is it a long line?
            // Send the remaining of the buffer and continue a read
            line_len = read_chunk - sent_offset;
          }
          // else: dont add a new line
          int data_sent =
              send(conn_id, read_buffer + sent_offset, line_len, MSG_NOSIGNAL);
          if (data_sent < 0) {
            if (errno == EPIPE) {
              syslog(LOG_DEBUG, "send error EPIPE");
              break;
            }
            syslog(LOG_DEBUG, "Send error %d", data_sent);
          }
          syslog(LOG_DEBUG, "%lu data sent %d :: written: %i", pthread_self(),
                 data_sent, bytes_written);
          bytes_written -= data_sent;
          sent_offset += data_sent;
        }
        this_thread_offset += read_chunk;
      }
      syslog(LOG_DEBUG, "%lu: done? %d", pthread_self(), bytes_written);
      recv_continue = false;
      free(read_buffer);
      syslog(LOG_DEBUG, "Closed connection");
    } else {
      pthread_mutex_lock(&data_mutex);
      int data_written = write(log_fd, data_buffer, recvd);
      pthread_mutex_unlock(&data_mutex);
      syslog(LOG_DEBUG, "Written %d :: Errno: %s", data_written,
             strerror(errno));
    }
  }
  close(conn_id);
  free(data_buffer);
  pthread_exit(NULL);
}

int serve(int log_fd) {

  int status = 1;
  // Start the timer thread
  thread_ts_params_t *th_ts_params = malloc(sizeof(thread_ts_params_t));
  th_ts_params->ts.tv_sec = 10;
  th_ts_params->log_fd = log_fd;
  pthread_t timer_thread_id;
  if (pthread_create(&timer_thread_id, NULL, timestamp_logger, th_ts_params) ==
      -1) {
    free(th_ts_params);
  } else {
    syslog(LOG_DEBUG, "Timer thread created: %ld", timer_thread_id);
    thread_list_t *th = malloc(sizeof(thread_list_t));
    th->id = timer_thread_id;
    SLIST_INSERT_HEAD(&thread_id_list, th, next);
  }

  struct sockaddr_storage others_addr;
  struct addrinfo hints = {
      .ai_family = AF_UNSPEC,
      .ai_socktype = SOCK_STREAM,
      .ai_flags = AI_PASSIVE,
  };
  struct addrinfo *server_info;
  char ip[INET6_ADDRSTRLEN];
  memset(ip, 0, INET6_ADDRSTRLEN);
  status = getaddrinfo(NULL, AESD_SERVER_SOCKET_CHAR, &hints, &server_info);
  if (status != 0) {
    syslog(LOG_ERR, "getaddrinfo error: %s", gai_strerror(status));
    return -1;
  }
  int sock_fd = socket(server_info->ai_family, server_info->ai_socktype,
                       server_info->ai_protocol);
  int updated_sock_fd; // the new fd returned by accept
  if (sock_fd == -1) {
    syslog(LOG_ERR, "Error creating the socket: %s", strerror(errno));
    return -1;
  }
  int enable = 1;
  if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
    syslog(LOG_ERR, "Error setting REUSEADDR the socket: %s", strerror(errno));
    return -1;
  }
  if (-1 == bind(sock_fd, server_info->ai_addr, server_info->ai_addrlen)) {
    close(sock_fd);
    syslog(LOG_ERR, "Error binding socket %s", strerror(errno));
  }
  freeaddrinfo(server_info);
  listen(sock_fd, 100);
  socklen_t sin_size = sizeof others_addr;
  while (!terminate) {

    syslog(LOG_DEBUG, "wait for connection");
    updated_sock_fd =
        accept(sock_fd, (struct sockaddr *)&others_addr, &sin_size);
    if (updated_sock_fd != -1) {
      inet_ntop(others_addr.ss_family,
                &((struct sockaddr_in *)&others_addr)->sin_addr, ip,
                sizeof(ip));
      syslog(LOG_DEBUG, "Accepted connection from %s : accpt: %d", ip,
             updated_sock_fd);
      // We cannot pass the updated_sock_fd since it will be
      // out of scope for the thread, then the thread needs to
      // free it
      thread_params_t *thread_params =
          (thread_params_t *)malloc(sizeof(thread_params_t));
      thread_params->conn_id = updated_sock_fd;
      thread_params->log_fd = log_fd;
      if (pthread_create(&thread_id, NULL, handle_connection,
                         (void *)thread_params) == -1) {
        syslog(LOG_ERR, "Unable to create thread");
        close(updated_sock_fd);
        free(thread_params);
      } else {
        // append thread id to the list
        thread_list_t *th = malloc(sizeof(thread_list_t));
        th->id = thread_id;
        SLIST_INSERT_HEAD(&thread_id_list, th, next);
        syslog(LOG_DEBUG, "Thread created %ld", thread_id);
      }
      updated_sock_fd = -1;
    } else {
      syslog(LOG_ERR, "Error accepting connection");
      terminate = TRUE;
    }
  }
  if (terminate) {
    syslog(LOG_DEBUG, "Caught signal, exiting");
  }
  // TODO: List is cleaned at the end, even when
  //   threads are done already. An improvement would
  //   clean them as soon as they are done
  thread_list_t *node;
  SLIST_FOREACH(node, &thread_id_list, next) {
    syslog(LOG_DEBUG, "Join thread: %ld", node->id);
    pthread_join(node->id, NULL);
    syslog(LOG_DEBUG, "Joined: %ld", node->id);
  }
  // Clean the list itself
  while (!SLIST_EMPTY(&thread_id_list)) {
    thread_list_t *node = SLIST_FIRST(&thread_id_list);
    SLIST_REMOVE_HEAD(&thread_id_list, next);
    free(node);
  }
  close(sock_fd);
  return status;
}

int main(int argc, char *argv[]) {
  SLIST_INIT(&thread_id_list);
  struct sigaction sigact = {.sa_handler = sig_handler};
  sigaction(SIGINT, &sigact, 0);
  sigaction(SIGTERM, &sigact, 0);
  openlog("aesdsocket", 0, LOG_USER);
  int ret_val = EXIT_SUCCESS;
  syslog(LOG_DEBUG, "Starting AESD socker server");
  if (argc > 1 && (0 == strncmp(argv[1], "-d", 4))) {
    daemon(0, 0);
  }
  int log_fd = createdatalog(AESD_SERVER_DATA_LOG_PATH);
  if (log_fd == -1) {
    syslog(LOG_ERR, "Failed to create log file");
    goto out0;
  }

  if (serve(log_fd) != 0) {
    syslog(LOG_ERR, "Server failed: %s", strerror(errno));
    ret_val = -EXIT_FAILURE;
  }
  close(log_fd);
  if (unlink(AESD_SERVER_DATA_LOG_PATH) != 0) {
    syslog(LOG_ERR, "Failed to delete log file: %s", strerror(errno));
  }
out0:
  closelog();
  pthread_mutex_destroy(&data_mutex);
  return ret_val;
}
