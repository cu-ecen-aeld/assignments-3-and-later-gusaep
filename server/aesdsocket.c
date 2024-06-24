#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define TRUE (!!1)
#define FALSE (!TRUE)
#define AESD_SERVER_SOCKET (9000)
#define AESD_SERVER_SOCKET_CHAR "9000"
#define AESD_SERVER_DATA_LOG_PATH ("/var/tmp/aesdsocketdata")
#define AESD_SERVER_BUFFER_SIZE (1 * 1024)

/**
 * Handle SIGINT and SIGTERM
 * completes any connection operation
 * closes the open socket
 * delete the AESD_SERVER_DATA_LOG_PATH
 */
volatile sig_atomic_t terminate = FALSE;

/**
 * Creates the file AESD_SERVER_DATA_LOG_PATH if it does not
 * exist
 * returns the file descriptor which needs to be closed by
 * the caller
 */
int createdatalog(const char *log_path)
{
    int file_fd = open(log_path, O_CREAT | O_RDWR, 0644);
    if (file_fd == -1)
    {
        syslog(LOG_ERR, "Unable to create/open file %s", log_path);
        return -1;
    }
    return file_fd;
}

void sig_handler(int signal)
{
    switch (signal)
    {
    case SIGINT:
    case SIGTERM:
        terminate = TRUE;
        break;
    default:
        break;
    }
}

int dist_to_char(const char *buffer, char char_to_find, size_t max_len)
{
    int distance = 0;
    while (buffer != NULL && *buffer != '\0' && *buffer != char_to_find)
    {
        buffer++;
        distance++;
        if (distance >= max_len)
        {
            break;
        }
    }
    return distance;
}

int serve(int log_fd, void *buffer, size_t buffer_len)
{
    int status = 1;
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
    if (status != 0)
    {
        syslog(LOG_ERR, "getaddrinfo error: %s", gai_strerror(status));
        return -1;
    }
    int closed = TRUE;
    int sock_fd = socket(server_info->ai_family,
                         server_info->ai_socktype,
                         server_info->ai_protocol);
    int updated_sock_fd; // the new fd returned by accept
    if (sock_fd == -1)
    {
        syslog(LOG_ERR, "Error creating the socket: %s", strerror(errno));
        return -1;
    }
    int enable = 1;
    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    {
        syslog(LOG_ERR, "Error setting REUSEADDR the socket: %s", strerror(errno));
        return -1;
    }
    if (-1 == bind(sock_fd, server_info->ai_addr, server_info->ai_addrlen))
    {
        close(sock_fd);
        syslog(LOG_ERR, "Error binding socket %s", strerror(errno));
    }
    freeaddrinfo(server_info);
    listen(sock_fd, 10);
    socklen_t sin_size = sizeof others_addr;
    while (!terminate)
    {
        if (closed == TRUE)
        {
            updated_sock_fd = accept(sock_fd, (struct sockaddr *)&others_addr, &sin_size);
            if (updated_sock_fd != -1)
            {
                closed = FALSE;
                inet_ntop(others_addr.ss_family, &((struct sockaddr_in *)&others_addr)->sin_addr, ip, sizeof(ip));
                syslog(LOG_DEBUG, "Accepted connection from %s", ip);
            }
            else
            {
                syslog(LOG_ERR, "Error accepting connection");
                terminate = TRUE;
            }
        }
        if (-1 != updated_sock_fd && closed != 1)
        {
            syslog(LOG_DEBUG, "b4 recv");
            ssize_t recvd = recv(updated_sock_fd, buffer, buffer_len, MSG_DONTWAIT);
            syslog(LOG_DEBUG, "afte recv");
            if (recvd == 0 || (recvd == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)))
            {
                int bytes_written = lseek(log_fd, 0, SEEK_END);
                char *read_buffer = (char *)calloc(1, AESD_SERVER_BUFFER_SIZE);
                lseek(log_fd, 0, SEEK_SET);
                syslog(LOG_DEBUG, "bytes writen into file %d", bytes_written);
                while (bytes_written > 0)
                {
                    int read_chunk = read(log_fd, read_buffer, AESD_SERVER_BUFFER_SIZE);
                    while (read_chunk != 0)
                    {
                        syslog(LOG_DEBUG, "read bytes from file log %d", read_chunk);
                        int line_len = dist_to_char(read_buffer, '\n', AESD_SERVER_BUFFER_SIZE);
                        syslog(LOG_DEBUG, "dist to line %d", line_len);
                        int data_sent;
                        if (line_len <= AESD_SERVER_BUFFER_SIZE - 1)
                        {
                            ++line_len;
                        }
                        data_sent = send(updated_sock_fd, read_buffer, line_len, MSG_NOSIGNAL);
                        if (data_sent < 0)
                        {
                            if (errno == EPIPE)
                            {
                                syslog(LOG_DEBUG, "send error EPIPE");
                                break;
                            }
                            syslog(LOG_DEBUG, "Send error %d", data_sent);
                        }
                        syslog(LOG_DEBUG, "data sent %d", data_sent);
                        bytes_written -= data_sent;
                        if (data_sent < read_chunk)
                        {
                            lseek(log_fd, -1 * (read_chunk - data_sent), SEEK_CUR);
                        }
                        read_chunk = read(log_fd, read_buffer, AESD_SERVER_BUFFER_SIZE);
                    }
                }
                close(updated_sock_fd);
                closed = TRUE;
                free(read_buffer);
                memset(buffer, 0, buffer_len);
                syslog(LOG_DEBUG, "Closed connection from %s", ip);
            }
            else
            {
                int data_written = write(log_fd, buffer, recvd);
                syslog(LOG_DEBUG, "Written %d", data_written);
            }
        }
    }
    if (terminate)
    {
        syslog(LOG_DEBUG, "Caught signal, exiting");
    }
    close(sock_fd);
    return status;
}

int main(int argc, char *argv[])
{
    struct sigaction sigact = {
        .sa_handler = sig_handler};
    sigaction(SIGINT, &sigact, 0);
    sigaction(SIGTERM, &sigact, 0);
    openlog("aesdsocket", 0, LOG_USER);
    int ret_val = EXIT_SUCCESS;
    void *data_buffer = calloc(1, AESD_SERVER_BUFFER_SIZE);
    if (argc > 1 && (0 == strncmp(argv[1], "-d", 4)))
    {
        daemon(0, 0);
    }
    int log_fd = createdatalog(AESD_SERVER_DATA_LOG_PATH);
    if (log_fd != -1)
    {
        if (serve(log_fd, data_buffer, AESD_SERVER_BUFFER_SIZE) != 0)
        {
            syslog(LOG_ERR, "Server failed: %s", strerror(errno));
            ret_val = -EXIT_FAILURE;
        }
        close(log_fd);
        if (unlink(AESD_SERVER_DATA_LOG_PATH) != 0)
        {
            syslog(LOG_ERR, "Failed to delete log file: %s", strerror(errno));
        }
    }
    else
    {
        syslog(LOG_ERR, "Failed to create log file");
    }
    free(data_buffer);
    closelog();
    return ret_val;
}
