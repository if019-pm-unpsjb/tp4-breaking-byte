#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>           // close()
#include <netinet/in.h>       // sockaddr_in
#include <sys/socket.h>       // socket functions

#define PORT 6969
#define BUFFER_SIZE 1024

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // 1. Crear socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 2. Bind (asociar IP y puerto)
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);        // puerto 6969

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 3. Escuchar
    if (listen(server_fd, 3) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Esperando conexión del cliente...\n");

    // 4. Aceptar conexión
    new_socket = accept(server_fd, (struct sockaddr*)&address, &addrlen);
    if (new_socket < 0) {
        perror("accept failed");
        exit(EXIT_FAILURE);
    }

    // 5. Leer mensaje del cliente
    read(new_socket, buffer, BUFFER_SIZE);
    printf("Mensaje recibido: %s\n", buffer);
    printf("%d",new_socket);

    // 6. Cerrar sockets
    close(new_socket);
    close(server_fd);

    return 0;
}
