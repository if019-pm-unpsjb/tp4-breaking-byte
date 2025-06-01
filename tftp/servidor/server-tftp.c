#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // close()
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h> // struct sockaddr_in
#include <errno.h>

#define TFTP_MAX_PAYLOAD_SIZE 514

#define CANT_MAX_DATA 512

typedef struct
{
    uint16_t opcode; /* 2 bytes en network byte order */
    char payload[TFTP_MAX_PAYLOAD_SIZE];
} tftp_packet_t;

int crear_socket()
{
    // 1) Crear socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

void bind_socket(int sockfd, const char *puerto_str)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(puerto_str)); // convierte string → int → network order
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int socketfd = crear_socket();

    bind_socket(socketfd, argv[1]);

    printf("Servidor TFTP escuchando en puerto %s …\n", argv[1]);

    // 3) Loop de recepción de paquetes
    while (1)
    {
        tftp_packet_t pkt;
        struct sockaddr_in client;
        socklen_t client_len = sizeof(client);

        // 3.1) Recibir un paquete completo
        ssize_t n = recvfrom(socketfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client, &client_len);
        if (n < 0)
        {
            perror("recvfrom");
            continue;
            ;
        }
        if (n < 2)
        { // mínimo debe traer 2 bytes para el opcode
            fprintf(stderr, "Paquete demasiado corto (%zd bytes)\n", n);
            continue;
        }

        // 3.2) Convertir opcode a host byte order
        uint16_t opcode = ntohs(pkt.opcode);

        // 3.3) Mostrar quién envió (IP:puerto)
        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.sin_addr, ipstr, sizeof(ipstr));
        printf("Paquete de %s:%u → ", ipstr, ntohs(client.sin_port));

        // 3.4) Lógica básica según opcode
        switch (opcode)
        {
        case 1: /* RRQ */
        {
            /* En RRQ/WRQ el payload es:
             *   Filename\0Mode\0
             */
            char *filename = pkt.payload;                 // Puntero a donde empieza el string filename
            char *mode = filename + strlen(filename) + 1; // Puntero al final del filename + 1 = mode
            printf("filename=\"%s\", mode=\"%s\"\n", filename, mode);

            FILE *fd = fopen(filename, "r");
            if (fd == NULL)
            {
                perror("Error al abrir el archivo RRQ");

                // Construir paquete de error
                tftp_packet_t error_pkt;
                error_pkt.opcode = htons(5); // Opcode de error

                uint16_t error_code = htons(1); // File not found
                memcpy(error_pkt.payload, &error_code, 2);

                const char *msg = "File not found";
                strcpy(error_pkt.payload + 2, msg); // Copiar el mensaje
                size_t msg_len = strlen(msg);

                error_pkt.payload[2 + msg_len] = '\0'; // Terminador

                // Calcular longitud total: 2 (opcode) + 2 (code) + msg_len + 1 (null)
                ssize_t error_len = 2 + 2 + msg_len + 1;

                sendto(socketfd, &error_pkt, error_len, 0, (struct sockaddr *)&client, client_len);

                continue;
            }

            size_t cantidad_bytes = CANT_MAX_DATA;
            char buffer[CANT_MAX_DATA];

            uint16_t bloque = 1;
            size_t leidos = 0;
            do
            {
                leidos = fread(buffer, 1, cantidad_bytes, fd);

                tftp_packet_t data_pkt;
                data_pkt.opcode = htons(3); // Opcode para DATA

                // Número de bloque (ej: bloque 1, 2, 3, etc.)
                uint16_t block_number = htons(bloque); // Incrementalo en cada iteración

                // Copiar el número de bloque (2 bytes) al principio del payload
                memcpy(data_pkt.payload, &block_number, 2);

                // Copiar los datos leídos desde el archivo (hasta 512 bytes)
                memcpy(data_pkt.payload + 2, buffer, leidos);

                // Enviar el paquete
                ssize_t total_len = 2 /*opcode*/ + 2 /*block*/ + leidos;
                sendto(socketfd, &data_pkt, total_len, 0, (struct sockaddr *)&client, client_len);

                // Esperar ACK
                tftp_packet_t ack_pkt;
                ssize_t ack_len = recvfrom(socketfd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr *)&client, &client_len);
                if (ack_len < 4 || ntohs(ack_pkt.opcode) != 4) // verifica que el paquete sea de minimo 4 bytes y que el opcde sea 4, o sea, un ACK
                {
                    fprintf(stderr, "ACK inválido o error de recepción\n");
                    break;
                }

                // extrae los dos primeros bytes del payload que son el numero de bloque de ACK que envia el cliente
                uint16_t ack_block;
                memcpy(&ack_block, ack_pkt.payload, 2);
                ack_block = ntohs(ack_block);

                if (ack_block != bloque)
                {
                    fprintf(stderr, "ACK inesperado. Se esperaba %d, pero llegó %d\n", bloque, ack_block);
                    break;
                }

                bloque++;

            } while (leidos == cantidad_bytes);

            if (feof(fd))
            {
                printf("Se llegó al final del archivo\n");
            }
            break;
        }

        case 2: /* WRQ */
        {
            /* En RRQ/WRQ el payload es:
             *   Filename\0Mode\0
             */
            char *filename = pkt.payload;                 // Puntero a donde empieza el string filename
            char *mode = filename + strlen(filename) + 1; // Puntero al final del filename + 1 = mode
            printf("filename=\"%s\", mode=\"%s\"\n", filename, mode);

            FILE *fd = fopen(filename, "r");
            if (fd != NULL)
            {
                perror("Error al abrir el archivo WRQ");

                // Construir paquete de error
                tftp_packet_t error_pkt;
                error_pkt.opcode = htons(5); // Opcode de error

                uint16_t error_code = htons(6); // File not found
                memcpy(error_pkt.payload, &error_code, 2);

                const char *msg = "File already exists";
                strcpy(error_pkt.payload + 2, msg); // Copiar el mensaje
                size_t msg_len = strlen(msg);

                error_pkt.payload[2 + msg_len] = '\0'; // Terminador

                // Calcular longitud total: 2 (opcode) + 2 (code) + msg_len + 1 (null)
                ssize_t error_len = 2 + 2 + msg_len + 1;

                sendto(socketfd, &error_pkt, error_len, 0, (struct sockaddr *)&client, client_len);

                continue;
            }

            fd = fopen(filename, "w");

            // ACK 0
            tftp_packet_t data_pkt;
            data_pkt.opcode = htons(4);                          // Opcode para DATA
            uint16_t block_number_expected = htons(0);           // Incrementalo en cada iteración
            memcpy(data_pkt.payload, &block_number_expected, 2); // Copiar el número de bloque (2 bytes) al principio del payload === 0
            // Enviar el ack 0
            ssize_t total_len = 2 /*opcode*/ + 2 /*block*/;
            sendto(socketfd, &data_pkt, total_len, 0, (struct sockaddr *)&client, client_len);

            block_number_expected++;
            uint16_t block_number_received;

            int eof = 0;
            while (!eof)
            {
                ssize_t n = recvfrom(socketfd, &pkt, sizeof(pkt), 0, (struct sockaddr *)&client, &client_len);

                if (n < CANT_MAX_DATA)
                {
                    eof = 1;
                }

                uint16_t opcode = ntohs(pkt.opcode);
                if (opcode != 3)
                {
                    fprintf(stderr, "Esperaba un paquete de DATA y recibió de otro tipo | opcode: %d\n", opcode);
                    break;
                }

                memcpy(&block_number_received, &pkt.payload, 2);

                block_number_received = ntohs(block_number_received);

                if (block_number_received != block_number_expected)
                {
                    perror("block expected != block revcieved");
                    exit(EXIT_FAILURE);
                }

                uint8_t data[CANT_MAX_DATA];
                int cant_a_leer = n /*tamaño pkt*/ - 2 /*opcode*/ - 2 /*block*/;
                memcpy(data, pkt.payload + 2 /*block*/, cant_a_leer);

                size_t bytes_escritos = fwrite(pkt.payload + 2, 1, cant_a_leer, fd);
                if (bytes_escritos != (size_t)cant_a_leer)
                {
                    perror("fwrite");
                    break;
                }

                // ACK N
                tftp_packet_t ack_pkt;
                ack_pkt.opcode = htons(4); // Opcode para DATA
                uint16_t block_ack = htons(block_number_expected);
                memcpy(ack_pkt.payload, &block_ack, 2); // Copiar el número de bloque (2 bytes) al principio del payload === 0
                // Enviar el ack N
                ssize_t total_len = 2 /*opcode*/ + 2 /*block*/;
                sendto(socketfd, &ack_pkt, total_len, 0, (struct sockaddr *)&client, client_len);

                block_number_expected++;
            }

            printf("Se llegó al final del archivo\n");
            fclose(fd);
            break;
        }

        default:
            printf("Opcode desconocido: %u\n", opcode);
            break;
        }
    }
    close(socketfd);
    return 0;
}
