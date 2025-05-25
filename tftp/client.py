import socket
import sys
import os

# Verificación de argumentos
if len(sys.argv) != 6:
    print(f"Uso: {sys.argv[0]} IP PUERTO READ|WRITE ARCHIVO MODO")
    sys.exit(1)

ip = sys.argv[1]
port = int(sys.argv[2])
operacion = upper(sys.argv[3]) # READ o WRITE
filename = sys.argv[4]
mode = lower(sys.argv[5]) # netascii o octet

if not (operacion == "READ" or operacion == "WRITE")
    print(f"Error: la operacion debe ser READ o WRITE")
    sys.exit(1)

if not (mode == "netascii" or mode == "octet")
    print(f"Error: El modo debe ser netascii u octet")
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
if (operacion == "READ"):
    return # PENDING

# Escribir un archivo en el servidor remoto
if (operacion == "WRITE"):
    return # PENDING
