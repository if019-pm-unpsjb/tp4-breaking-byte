import socket
import sys
import os

# Verificación de argumentos
if len(sys.argv) != 6:
    print(f"Uso: {sys.argv[0]} IP PUERTO READ|WRITE ARCHIVO MODO")
    sys.exit(1)

ip = sys.argv[1]
port = int(sys.argv[2])
operacion = (sys.argv[3]).upper() # READ o WRITE
filename = sys.argv[4]
mode = (sys.argv[5]).lower() # netascii o octet

if not (operacion == "READ" or operacion == "WRITE"):
    print("Error: la operacion debe ser READ o WRITE")
    sys.exit(1)

if not (mode == "netascii" or mode == "octet"):
    print("Error: El modo debe ser netascii u octet")
    sys.exit(1)

if (operacion == "WRITE"):
    if not os.path.exists(filename):
        print(f"Error: El archivo '{filename}' no existe localmente.")
        sys.exit(1)
    opcode = b'\x00\x02'
else:
    opcode = b'\x00\x01'

# Construcción del paquete de solicitud
request_packet = opcode + filename.encode() + b'\x00' + mode.encode() + b'\x00'

# Envío de solicitud RRQ o WRQ por UDP
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.sendto(request_packet, (ip, port))

# Leer un archivo del servidor remoto y copiarlo en un archivo local
if operacion == "READ":
    finish = False
    block_number_expected = 1
    local_file = open("_" + filename, "wb")  # Guardar archivo recibido

    while not finish:
        
        data_packet, server_address = sock.recvfrom(516)  # opcode (2) + block (2) + data (≤512)

        opcode = data_packet[0:2]
        block_number = int.from_bytes(data_packet[2:4], byteorder='big')
        data = data_packet[4:]

        if opcode == b'\x00\x03':  # DATA
            if block_number == block_number_expected:
                local_file.write(data)

                # Enviar ACK
                ack_packet = b'\x00\x04' + block_number.to_bytes(2, byteorder='big')
                sock.sendto(ack_packet, server_address)

                if len(data) < 512:
                    finish = True
                else:
                    block_number_expected += 1
            else:
                # ACK duplicado en caso de reenvío del servidor
                ack_packet = b'\x00\x04' + block_number.to_bytes(2, byteorder='big')
                sock.sendto(ack_packet, server_address)

        elif opcode == b'\x00\x05':  # ERROR
            error_code = int.from_bytes(data_packet[2:4], byteorder='big')
            error_msg = data_packet[4:-1].decode()
            print(f"Error del servidor: {error_msg} (Código {error_code})")
            finish = True

    local_file.close()
    print(f"Archivo recibido como _{filename}")

# Escribir un archivo en el servidor remoto
if (operacion == "WRITE"):
    print("Inicio Operacion Write") # PENDING
