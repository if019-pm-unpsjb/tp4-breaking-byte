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
sock.settimeout(3)  # REFACTOR: ESPECIFICAR COMO PARAMETRO
sock.sendto(request_packet, (ip, port))

# Leer un archivo del servidor remoto y copiarlo en un archivo local
if operacion == "READ":
    finish = False
    file_created = False
    block_number_expected = 1
    retry_limit = 3
    retries = 0

    while not finish:
        try:
            data_packet, server_address = sock.recvfrom(516)  # opcode (2) + block (2) + data (≤512)
            retries = 0  # Reiniciar el contador si se recibe algo

            opcode = data_packet[0:2]
            block_number = int.from_bytes(data_packet[2:4], byteorder='big')
            data = data_packet[4:]

            if opcode == b'\x00\x03':  # DATA
                if block_number == block_number_expected:
                    if not file_created:
                        local_file = open(filename, "wb")
                        file_created = True

                    local_file.write(data)

                    # Enviar ACK
                    ack_packet = b'\x00\x04' + block_number.to_bytes(2, byteorder='big')
                    sock.sendto(ack_packet, server_address)

                    if len(data) < 512:
                        finish = True
                    else:
                        block_number_expected = (block_number_expected + 1) & 0xFFFF

            elif opcode == b'\x00\x05':  # ERROR
                error_code = int.from_bytes(data_packet[2:4], byteorder='big')
                error_msg = data_packet[4:-1].decode()
                print(f"Error del servidor: {error_msg} (Código {error_code})")
                sys.exit(1)

        except socket.timeout:
            retries += 1
            print(f"Timeout {retries}/3: No se recibió respuesta del servidor.")
            if retries >= retry_limit:
                print("No se pudo recibir respuesta del servidor. Abortando transferencia.")
                if file_created:
                    local_file.close()
                    try:
                        os.remove(filename)
                        print(f"Archivo parcial '{filename}' eliminado.")
                    except Exception as e:
                        print(f"No se pudo eliminar el archivo '{filename}': {e}")
                sys.exit(1)


    if file_created:
        local_file.close()
        print(f"Archivo recibido como {filename}")

# Escribir un archivo en el servidor remoto
if operacion == "WRITE":
    try:
        with open(filename, "rb") as f:
            # --------------------------
            # Timeout 1: ACK(0) esperado
            # --------------------------
            retries = 0
            retry_limit = 3
            ack_received = False

            while retries < retry_limit and not ack_received:
                #sock.sendto(request_packet, (ip, port))  # Enviar WRQ
                try:
                    ack_packet, server_address = sock.recvfrom(516)

                    if ack_packet[0:2] == b'\x00\x05':  # ERROR
                        error_code = int.from_bytes(ack_packet[2:4], byteorder='big')
                        error_msg = ack_packet[4:-1].decode()
                        print(f"Error del servidor: {error_msg} (Código {error_code})")
                        sys.exit(1)

                    if ack_packet[0:2] == b'\x00\x04' and ack_packet[2:4] == b'\x00\x00':
                        print("Se recibió el ACK 0")
                        ack_received = True
                    else:
                        print("ACK inesperado recibido. Reintentando WRQ...")
                        retries += 1

                except socket.timeout:
                    retries += 1
                    print(f"Timeout {retries}/3 esperando ACK(0)... Reintentando WRQ")

            if not ack_received:
                print("No se recibió ACK(0) tras múltiples intentos. Abortando.")
                sys.exit(1)

            # --------------------------------------
            # Timeout 2: ACK esperado tras DATA(n)
            # --------------------------------------
            finish = False
            block_number = 1

            while not finish:
                data = f.read(512)
                data_packet = b'\x00\x03' + block_number.to_bytes(2, byteorder='big') + data

                retries = 0
                ack_received = False

                while retries < retry_limit and not ack_received:
                    sock.sendto(data_packet, server_address)
                    try:
                        ack_packet, _ = sock.recvfrom(516)
                        ack_opcode = ack_packet[0:2]
                        ack_block = int.from_bytes(ack_packet[2:4], byteorder='big')

                        if ack_opcode == b'\x00\x04' and ack_block == (block_number & 0xFFFF):
                            ack_received = True
                        else:
                            print(f"ACK inválido. Esperado: {block_number}, Recibido: {ack_block}")
                            retries += 1

                    except socket.timeout:
                        retries += 1
                        print(f"Timeout {retries}/3 esperando ACK({block_number})... Reintentando DATA")

                if not ack_received:
                    print(f"No se recibió ACK({block_number}) tras múltiples intentos. Abortando.")
                    sys.exit(1)

                if len(data) < 512:
                    print("Archivo enviado correctamente")
                    finish = True

                block_number = (block_number + 1) & 0xFFFF

    except FileNotFoundError:
        print(f"No se pudo abrir el archivo {filename}")
        sys.exit(1)
    except Exception as e:
        print(f"Error durante la escritura: {e}")
        sys.exit(1)