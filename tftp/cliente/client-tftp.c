#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// ./cliente ip puerto mensaje.
int main(int argc, char* argv[]) 
{
    // Crear socket
    int socket_udp; // es un int porque devuelve un file descriptor

    socket_udp = socket(AF_INET, SOCK_DGRAM, 0); // AF_INET indica al socket que se comunicará a traves de una red, indicar si es orientado o no orientado, 0 (protocolo especifico, pero siempre va 0)

    char* dir = argv[1];
    char* port = argv[2];
    char* msg = argv[3];

    struct sockaddr_in dest;

    memset(&dest, 0, sizeof(dest));

    dest.sin_family = AF_INET;
    dest.sin_port = htons(atoi(port)); // host to network short
    dest.sin_addr.s_addr = inet_addr(dir);

    sendto(socket_udp, msg, strlen(msg) + 1, 0, (struct sockaddr*) &dest, sizeof(dest));

    exit(EXIT_SUCCESS);
}
