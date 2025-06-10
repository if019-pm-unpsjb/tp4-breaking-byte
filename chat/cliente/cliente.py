import socket
import argparse
import threading
import sys
import struct

# === Configuración ===
MAX_USERNAME_LEN = 32
BUFFER_SIZE = 1024

# === Argumentos ===
parser = argparse.ArgumentParser()
parser.add_argument("usuario", help="Tu nombre de usuario")
parser.add_argument("--host", default="127.0.0.1", help="IP del servidor")
parser.add_argument("--port", type=int, default=6969, help="Puerto del servidor")
args = parser.parse_args()
usuario = args.usuario

# === Funciones auxiliares ===
def formatear_string(s, length):
    b = s.encode("utf-8")
    if len(b) > length:
        raise ValueError("Texto demasiado largo")
    return b

def construir_trama_conexion(usuario):
    opcode = 1
    usuario_bytes = formatear_string(usuario, MAX_USERNAME_LEN)
    return struct.pack('!H', opcode) + usuario_bytes + b'\x00'

def construir_trama_desconexion(usuario):
    opcode = 2
    usuario_bytes = formatear_string(usuario, MAX_USERNAME_LEN)
    return struct.pack('!H', opcode) + usuario_bytes + b'\x00'

def construir_trama_sendmsg(remitente, destinatario, mensaje):
    opcode = 3
    remitente_bytes = formatear_string(remitente, MAX_USERNAME_LEN)
    destinatario_bytes = formatear_string(destinatario, MAX_USERNAME_LEN)
    return (
        struct.pack('!H', opcode) +
        remitente_bytes + b'\x00' +
        destinatario_bytes + b'\x00' +
        mensaje.encode('utf-8') + b'\x00'
    )

def interpretar_mensaje(data):
    if len(data) < 2:
        return
    opcode = struct.unpack('!H', data[:2])[0]

    if opcode == 3:  # sendmsg
        offset = 2

        # Leer remitente hasta \0
        fin_remitente = data.find(b'\x00', offset)
        remitente = data[offset:fin_remitente].decode()
        offset = fin_remitente + 1

        # Leer destinatario hasta \0
        fin_destinatario = data.find(b'\x00', offset)
        destinatario = data[offset:fin_destinatario].decode()
        offset = fin_destinatario + 1

        # Leer mensaje hasta \0
        fin_mensaje = data.find(b'\x00', offset)
        if fin_mensaje == -1:
            mensaje = data[offset:].decode()
        else:
            mensaje = data[offset:fin_mensaje].decode()

        print(f"\n📨 Mensaje de {remitente} -> {mensaje}\n> ", end="", flush=True)

    elif opcode == 6:  # error
        if len(data) < 4:
            print("\n[Error: trama de error mal formada]")
            return
        error_code = struct.unpack('!H', data[2:4])[0]
        mensaje = data[4:].rstrip(b'\x00').decode()
        print(f"\n❌ Error ({error_code}): {mensaje}\n> ", end="", flush=True)

    elif opcode == 7:  # ack
        if len(data) < 4:
            print("\n[ACK mal formado]")
            return
        ack_code = struct.unpack('!H', data[2:4])[0]
        print(f"\n✅ ACK recibido (código {ack_code})\n> ", end="", flush=True)

    else:
        print(f"\n[Mensaje con opcode desconocido: {opcode}]\n> ", end="", flush=True)

# === Conexión al servidor ===
cliente = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
cliente.connect((args.host, args.port))

# Enviar trama de conexión
try:
    cliente.sendall(construir_trama_conexion(usuario))
except ValueError as e:
    print(f"❌ Error: {e}")
    cliente.close()
    sys.exit(1)

# === Función receptora ===
def recibir():
    while True:
        try:
            data = cliente.recv(BUFFER_SIZE)
            if not data:
                print("\n[Servidor cerró la conexión]")
                sys.exit(0)

            # Debug: mostrar tamaño y contenido crudo
            print(f"\n[DEBUG] Recibido {len(data)} bytes")
            print(f"[DEBUG] Datos crudos (hex): {data.hex(' ')}")

            interpretar_mensaje(data)

        except ConnectionResetError:
            print("\n[Conexión perdida con el servidor]")
            sys.exit(1)


# === Arrancar hilo receptor ===
threading.Thread(target=recibir, daemon=True).start()

# === Bucle de envío ===
print(f"Conectado como {usuario}. Escribí comandos:")
print("- /sendmsg <usuario_destino> <mensaje>")
print("- /exit")

while True:
    try:
        entrada = input("> ").strip()
        if not entrada:
            continue

        if entrada.startswith("/sendmsg "):
            partes = entrada.split(' ', 2)
            if len(partes) < 3:
                print("[Uso correcto: /sendmsg <usuario> <mensaje>]")
                continue
            destino, mensaje = partes[1], partes[2]
            try:
                trama = construir_trama_sendmsg(usuario, destino, mensaje)
                cliente.sendall(trama)
            except ValueError as e:
                print(f"[Error: {e}]")

        elif entrada == "/exit":
            cliente.sendall(construir_trama_desconexion(usuario))
            cliente.close()
            print("Conexión cerrada")
            break

        else:
            print("[Comando no reconocido]")

    except KeyboardInterrupt:
        print("\n[Finalizando conexión por Ctrl+C]")
        cliente.sendall(construir_trama_desconexion(usuario))
        cliente.close()
        break
