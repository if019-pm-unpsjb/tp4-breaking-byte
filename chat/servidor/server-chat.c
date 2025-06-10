#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>

#define BUFFER_SIZE 1024
#define NAME_LEN 32
#define MAX_CLIENTS 100
#define OPCODE_CONNECT 1
#define OPCODE_SENDMSG 3
#define OPCODE_ACK 7

int read_null_string(int fd, char *buf, int max_len)
{
    int i = 0;
    while (i < max_len)
    {
        char c;
        if (read(fd, &c, 1) != 1)
            return -1;
        buf[i++] = c;
        if (c == '\0')
            return i;
    }
    return -2;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        exit(1);
    int port = atoi(argv[1]);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, MAX_CLIENTS);
    int client_fds[MAX_CLIENTS] = {0};
    char usernames[MAX_CLIENTS][NAME_LEN] = {{0}};
    fd_set readfds;
    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int maxfd = server_fd;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int fd = client_fds[i];
            if (fd > 0)
            {
                FD_SET(fd, &readfds);
                if (fd > maxfd)
                    maxfd = fd;
            }
        }
        select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (FD_ISSET(server_fd, &readfds))
        {
            int fd = accept(server_fd, NULL, NULL);
            uint16_t op;
            read(fd, &op, 2);
            if (ntohs(op) == OPCODE_CONNECT)
            {
                char name[NAME_LEN];
                if (read_null_string(fd, name, NAME_LEN - 1) > 0)
                {
                    for (int i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (client_fds[i] == 0)
                        {
                            client_fds[i] = fd;
                            strncpy(usernames[i], name, NAME_LEN - 1);
                            uint16_t ack[2] = {htons(OPCODE_ACK), htons(1)};
                            send(fd, ack, 4, 0);
                            break;
                        }
                    }
                }
                else
                    close(fd);
            }
            else
                close(fd);
        }
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int fd = client_fds[i];
            if (fd > 0 && FD_ISSET(fd, &readfds))
            {
                uint16_t op2;
                if (read(fd, &op2, 2) <= 0)
                {
                    close(fd);
                    client_fds[i] = 0;
                    continue;
                }
                if (ntohs(op2) == OPCODE_SENDMSG)
                {
                    char orig[NAME_LEN], dest[NAME_LEN], msg[BUFFER_SIZE];
                    if (read_null_string(fd, orig, NAME_LEN - 1) <= 0)
                        continue;
                    if (read_null_string(fd, dest, NAME_LEN - 1) <= 0)
                        continue;
                    if (read_null_string(fd, msg, BUFFER_SIZE - 1) <= 0)
                        continue;
                    for (int j = 0; j < MAX_CLIENTS; j++)
                    {
                        if (client_fds[j] > 0 && strcmp(usernames[j], dest) == 0)
                        {
                            unsigned char buf[BUFFER_SIZE];
                            int off = 0;
                            memcpy(buf + off, &op2, 2);
                            off += 2;
                            int l = strlen(orig) + 1;
                            memcpy(buf + off, orig, l);
                            off += l;
                            l = strlen(dest) + 1;
                            memcpy(buf + off, dest, l);
                            off += l;
                            l = strlen(msg) + 1;
                            memcpy(buf + off, msg, l);
                            off += l;
                            send(client_fds[j], buf, off, 0);
                            break;
                        }
                    }
                }
            }
        }
    }
    return 0;
}
