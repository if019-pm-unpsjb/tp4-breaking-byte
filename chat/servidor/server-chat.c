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
#define OPCODE_ACK 7
#define USER_SUCCESFULLY_CONNECTED_ACK_CODE 1
#define OPCODE_ERROR 6
#define DUPLICATE_USERNAME_ERROR_CODE 2
#define OPCODE_USER_EVENT 8
#define ACTION_CONNECT 0
#define ACTION_DISCONNECT 1

typedef struct
{
    int sockfd;
    char username[NAME_LEN];
} client_t;

static client_t clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static int next_thread_id = 1;

// lee hasta '\0'
int read_null_string(int fd, char *buf, int max_len)
{
    int i = 0;
    while (i < max_len)
    {
        if (read(fd, &buf[i], 1) != 1)
            return -1;
        if (buf[i++] == '\0')
            return i;
    }
    return -2;
}

// envía paquete user_event a un socket dado
void send_user_event(int sockfd, uint16_t action, const char *username)
{
    uint16_t hdr[2] = {htons(OPCODE_USER_EVENT), htons(action)};
    int ulen = strlen(username) + 1;
    unsigned char buf[4 + NAME_LEN + 1];
    memcpy(buf, hdr, 4);
    memcpy(buf + 4, username, ulen);
    send(sockfd, buf, 4 + ulen, 0);
}

// notifica a todos excepto idx_exclude
void broadcast_user_event(uint16_t action, const char *username, int idx_exclude)
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

void *client_thread(void *arg)
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

    // loop de mensajes
    while (1)
    {
        uint16_t net_op;
        if (read(fd, &net_op, 2) <= 0)
            break;
        if (ntohs(net_op) != OPCODE_SENDMSG)
            continue;

        char orig[NAME_LEN] = {0}, dest[NAME_LEN] = {0}, msg[BUFFER_SIZE] = {0};
        if (read_null_string(fd, orig, NAME_LEN - 1) <= 0)
            break;
        if (read_null_string(fd, dest, NAME_LEN - 1) <= 0)
            break;
        if (read_null_string(fd, msg, BUFFER_SIZE - 1) <= 0)
            break;

        printf("Thread[%d]: %s -> %s : %s\n", thread_id, orig, dest, msg);

        pthread_mutex_lock(&clients_mutex);
        // reenviar a destinatario
        for (int j = 0; j < MAX_CLIENTS; j++)
        {
            if (clients[j].sockfd > 0 && strcmp(clients[j].username, dest) == 0)
            {
                unsigned char buf[BUFFER_SIZE];
                int off = 0;
                memcpy(buf + off, &net_op, 2);
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
                send(clients[j].sockfd, buf, off, 0);
                printf("Thread[%d]: forwarded to '%s' fd=%d %d bytes\n",
                       thread_id, dest, clients[j].sockfd, off);
                break;
            }
        }
        pthread_mutex_unlock(&clients_mutex);
    }

    // desconexión
    pthread_mutex_lock(&clients_mutex);
    clients[idx].sockfd = 0;
    clients[idx].username[0] = '\0';
    pthread_mutex_unlock(&clients_mutex);

    // notificar a todos de desconexión
    broadcast_user_event(ACTION_DISCONNECT, name, idx);

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

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, MAX_CLIENTS);

    printf("Server[%d]: listening on port %d...\n", getpid(), port);

    while (1)
    {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0)
            continue;
        printf("Server: accepted fd=%d\n", client_fd);

        // connect_request
        uint16_t net_op;
        if (read(client_fd, &net_op, 2) != 2 || ntohs(net_op) != OPCODE_CONNECT)
        {
            close(client_fd);
            continue;
        }
        char name[NAME_LEN] = {0};
        if (read_null_string(client_fd, name, NAME_LEN - 1) <= 0)
        {
            close(client_fd);
            continue;
        }
        printf("Server: connect_request '%s' fd=%d\n", name, client_fd);

        pthread_mutex_lock(&clients_mutex);
        // duplicate check
        int duplicate = 0;
        for (int j = 0; j < MAX_CLIENTS; j++)
        {
            if (clients[j].sockfd > 0 && strcmp(clients[j].username, name) == 0)
            {
                duplicate = 1;
                break;
            }
        }
        if (duplicate)
        {
            uint16_t err_hdr[2] = {htons(OPCODE_ERROR), htons(DUPLICATE_USERNAME_ERROR_CODE)};
            const char *err_txt = "Username taken";
            send(client_fd, err_hdr, 4, 0);
            send(client_fd, err_txt, strlen(err_txt) + 1, 0);
            close(client_fd);
            printf("Server: duplicate username '%s', rejected\n", name);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }

        // find free slot
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
            close(client_fd);
            pthread_mutex_unlock(&clients_mutex);
            continue;
        }

        // registrar cliente
        clients[idx].sockfd = client_fd;
        strncpy(clients[idx].username, name, NAME_LEN - 1);

        // enviar lista de usuarios existentes
        for (int k = 0; k < MAX_CLIENTS; k++)
        {
            if (clients[k].sockfd > 0 && k != idx)
            {
                send_user_event(client_fd, ACTION_CONNECT, clients[k].username);
            }
        }

        pthread_mutex_unlock(&clients_mutex);

        // notificar a todos el nuevo usuario
        broadcast_user_event(ACTION_CONNECT, name, idx);

        // send ACK
        uint16_t ack_msg[2] = {htons(OPCODE_ACK), htons(USER_SUCCESFULLY_CONNECTED_ACK_CODE)};
        send(client_fd, ack_msg, sizeof(ack_msg), 0);
        printf("Server: ACK sent to '%s' fd=%d\n", name, client_fd);

        // spawn thread
        pthread_t tid;
        int *pidx = malloc(sizeof(int));
        *pidx = idx;
        pthread_create(&tid, NULL, client_thread, pidx);
        pthread_detach(tid);
        printf("Server: thread %d created for '%s' idx=%d\n", next_thread_id - 1, name, idx);
    }

    return 0;
}
