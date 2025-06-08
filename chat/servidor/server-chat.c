#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>     // close()
#include <netinet/in.h> // sockaddr_in
#include <sys/socket.h> // socket(), bind(), accept()
#include <sys/select.h> // select()

#define BUFFER_SIZE 1024
#define MAX_CLIENTS 2
#define NAME_LEN 32

typedef struct
{
    int sockfd;
    char username[NAME_LEN];
} client_t;

int max(int a, int b) { return a > b ? a : b; }

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return EXIT_FAILURE;
    }
    int port = atoi(argv[1]);
    if (port <= 0)
    {
        fprintf(stderr, "Puerto inválido: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    int server_fd, addrlen, new_sock, activity;
    struct sockaddr_in address;
    client_t clients[MAX_CLIENTS];
    int client_count = 0;
    char buffer[BUFFER_SIZE];

    // 1) crear socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 2) bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind");
        exit(EXIT_FAILURE);
    }

    // 3) listen
    if (listen(server_fd, MAX_CLIENTS) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }
    printf("Servidor escuchando en puerto %d …\n", port);

    addrlen = sizeof(address);
    // 4) aceptar exactamente 2 clientes
    while (client_count < MAX_CLIENTS)
    {
        new_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_sock < 0)
        {
            perror("accept");
            continue;
        }

        // leer username
        memset(buffer, 0, BUFFER_SIZE);
        int rd = read(new_sock, buffer, BUFFER_SIZE - 1);
        if (rd <= 0)
        {
            close(new_sock);
            continue;
        }
        buffer[strcspn(buffer, "\r\n")] = 0;

        // guardar cliente
        clients[client_count].sockfd = new_sock;
        strncpy(clients[client_count].username, buffer, NAME_LEN - 1);
        printf("Cliente %d conectado: %s\n", client_count + 1, buffer);
        client_count++;
    }

    // enviar bienvenida a cada uno
    send(clients[0].sockfd,
         clients[1].username,
         strlen(clients[1].username) + 1,
         0);
    send(clients[1].sockfd,
         clients[0].username,
         strlen(clients[0].username) + 1,
         0);

    // 5) loop de reenvío de mensajes
    fd_set readfds;
    int max_fd = max(clients[0].sockfd, clients[1].sockfd);

    while (1)
    {
        FD_ZERO(&readfds);
        FD_SET(clients[0].sockfd, &readfds);
        FD_SET(clients[1].sockfd, &readfds);

        activity = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0)
        {
            perror("select");
            break;
        }

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            int sd = clients[i].sockfd;
            if (FD_ISSET(sd, &readfds))
            {
                memset(buffer, 0, BUFFER_SIZE);
                int len = read(sd, buffer, BUFFER_SIZE - 1);
                if (len <= 0)
                {
                    printf("Cliente %s desconectado\n", clients[i].username);
                    close(sd);
                    // opcional: terminar todo o reajustar array
                    exit(0);
                }
                // reenviar al otro
                int other = clients[i ^ 1].sockfd;
                send(other, buffer, len, 0);
            }
        }
    }

    close(server_fd);
    return 0;
}
