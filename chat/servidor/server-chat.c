#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define NAME_LEN 32
#define MAX_CLIENTS 100

#define OPCODE_CONNECT 1
#define OPCODE_SENDMSG 3
#define OPCODE_ERROR 6
#define OPCODE_ACK 7
#define OPCODE_USER_EVENT 8

#define USER_EVENT_CONNECT 0
#define USER_EVENT_DISCONNECT 1

#define ACK_CODE_USER_CONNECTED 1
#define ERROR_TOO_LONG_NAME 1
#define ERROR_DUPLICATE_NAME 2

typedef struct
{
    int sockfd;
    char username[NAME_LEN];
} client_t;

static client_t clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static int next_thread_id = 1;

// envía paquete user_event: [opcode][size][action][username\0]
static void send_user_event(int sockfd, uint16_t action, const char *username)
{
    uint16_t username_len = strlen(username);
    uint16_t hdr[3] = {htons(OPCODE_USER_EVENT), htons(2 + username_len), htons(action)};
    unsigned char buf[6 + NAME_LEN];
    memcpy(buf, hdr, 6);
    memcpy(buf + 6, username, username_len);
    send(sockfd, buf, 6 + username_len, 0);
}

// notifica a todos menos idx_exclude
static void broadcast_user_event(uint16_t action, const char *username, int idx_exclude)
{
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (i != idx_exclude && clients[i].sockfd > 0)
        {
            send_user_event(clients[i].sockfd, action, username);
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

static void *client_thread(void *arg)
{
    int idx = *(int *)arg;
    free(arg);
    pthread_mutex_lock(&clients_mutex);
    int thread_id = next_thread_id++;
    int fd = clients[idx].sockfd;
    char name[NAME_LEN];
    strncpy(name, clients[idx].username, NAME_LEN);
    pthread_mutex_unlock(&clients_mutex);

    printf("Thread[%d]: handling '%s' fd=%d\n", thread_id, name, fd);

    while (1)
    {
        uint16_t net_op, net_size;
        if (read(fd, &net_op, 2) != 2)
            break;
        if (read(fd, &net_size, 2) != 2)
            break;

        int opcode = ntohs(net_op);
        int size = ntohs(net_size);
        if (size < 0 || size > BUFFER_SIZE)
            break;

        unsigned char payload[BUFFER_SIZE + 1];
        if (read(fd, payload, size) != size)
            break;
        payload[size] = '\0';

        if (opcode == OPCODE_SENDMSG)
        {
            char *orig = (char *)payload;
            char *dest = orig + strlen(orig) + 1;
            char *msg = dest + strlen(dest) + 1;
            printf("Thread[%d]: %s -> %s : %s\n", thread_id, orig, dest, msg);

            int plen = strlen(orig) + 1 + strlen(dest) + 1 + strlen(msg) + 1;
            uint16_t out_hdr[2] = {htons(OPCODE_SENDMSG), htons(plen)};
            unsigned char outbuf[4 + BUFFER_SIZE];
            int off = 0;
            memcpy(outbuf + off, out_hdr, 4);
            off += 4;
            memcpy(outbuf + off, orig, strlen(orig) + 1);
            off += strlen(orig) + 1;
            memcpy(outbuf + off, dest, strlen(dest) + 1);
            off += strlen(dest) + 1;
            memcpy(outbuf + off, msg, strlen(msg) + 1);
            off += strlen(msg) + 1;

            pthread_mutex_lock(&clients_mutex);
            for (int j = 0; j < MAX_CLIENTS; j++)
            {
                if (clients[j].sockfd > 0 &&
                    strcmp(clients[j].username, dest) == 0)
                {
                    send(clients[j].sockfd, outbuf, off, 0);
                    printf("Thread[%d]: forwarded to '%s' fd=%d (%d bytes)\n",
                           thread_id, dest, clients[j].sockfd, off);
                    break;
                }
            }
            pthread_mutex_unlock(&clients_mutex);
        }
    }

    pthread_mutex_lock(&clients_mutex);
    clients[idx].sockfd = 0;
    clients[idx].username[0] = '\0';
    pthread_mutex_unlock(&clients_mutex);
    broadcast_user_event(USER_EVENT_DISCONNECT, name, idx);

    close(fd);
    printf("Thread[%d]: '%s' disconnected\n", thread_id, name);
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
        exit(1);
    int port = atoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(port)};
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, MAX_CLIENTS);

    printf("Server[%d]: listening on port %d...\n", getpid(), port);

    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
            continue;
        printf("Server: accepted fd=%d\n", client_fd);

        uint16_t net_op, net_size;
        if (read(client_fd, &net_op, 2) != 2 ||
            read(client_fd, &net_size, 2) != 2)
        {
            close(client_fd);
            continue;
        }
        if (ntohs(net_op) != OPCODE_CONNECT)
        {
            close(client_fd);
            continue;
        }

        int size = ntohs(net_size);
        if (size < 1 || size > NAME_LEN)
        {
            uint16_t err[2] = {htons(OPCODE_ERROR),
                               htons(ERROR_TOO_LONG_NAME)};
            const char *txt = "Nombre de usuario excede límite de caracteres";
            send(client_fd, err, 4, 0);
            send(client_fd, txt, strlen(txt) + 1, 0);
            close(client_fd);
            continue;
        }

        char name[NAME_LEN];
        if (read(client_fd, name, size) != size)
        {
            close(client_fd);
            continue;
        }
        printf("Server: connect_request '%s' fd=%d\n", name, client_fd);

        pthread_mutex_lock(&clients_mutex);
        int dup = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].sockfd > 0 &&
                strcmp(clients[i].username, name) == 0)
            {
                dup = 1;
                break;
            }
        }
        if (dup)
        {
            uint16_t err[2] = {htons(OPCODE_ERROR),
                               htons(ERROR_DUPLICATE_NAME)};
            const char *txt = "Ya hay un usuario conectado con el mismo nombre";
            send(client_fd, err, 4, 0);
            send(client_fd, txt, strlen(txt) + 1, 0);
            pthread_mutex_unlock(&clients_mutex);
            close(client_fd);
            continue;
        }

        int idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (clients[i].sockfd == 0)
            {
                idx = i;
                break;
            }
        }
        if (idx < 0)
        {
            pthread_mutex_unlock(&clients_mutex);
            close(client_fd);
            continue;
        }

        clients[idx].sockfd = client_fd;
        strncpy(clients[idx].username, name, NAME_LEN - 1);
        pthread_mutex_unlock(&clients_mutex);

        // enviar lista previa
        pthread_mutex_lock(&clients_mutex);
        for (int k = 0; k < MAX_CLIENTS; k++)
        {
            if (clients[k].sockfd > 0 && k != idx)
            {
                send_user_event(client_fd, USER_EVENT_CONNECT, clients[k].username);
            }
        }
        pthread_mutex_unlock(&clients_mutex);

        broadcast_user_event(USER_EVENT_CONNECT, name, idx);

        uint16_t ack[2] = {htons(OPCODE_ACK),
                           htons(ACK_CODE_USER_CONNECTED)};
        send(client_fd, ack, 4, 0);
        printf("Server: ACK sent to '%s' fd=%d\n", name, client_fd);

        pthread_t tid;
        int *pidx = malloc(sizeof(int));
        *pidx = idx;
        pthread_create(&tid, NULL, client_thread, pidx);
        pthread_detach(tid);
    }

    return 0;
}
