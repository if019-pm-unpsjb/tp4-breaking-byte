import socket
import argparse
import threading
import sys

# argumentos
parser = argparse.ArgumentParser()
parser.add_argument("usuario", help="Tu nombre de usuario")
parser.add_argument("--host", default="127.0.0.1", help="IP del servidor")
parser.add_argument("--port", type=int, default=6969, help="Puerto del servidor")
args = parser.parse_args()

BUFFER_SIZE = 1024
cliente = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
cliente.connect((args.host, args.port))

# enviar username
cliente.sendall(args.usuario.encode('utf-8'))

# funcion de recepción
def recibir():
    while True:
        data = cliente.recv(BUFFER_SIZE)
        if not data:
            print("\n[Servidor cerró la conexión]")
            sys.exit(0)
        print("\n>> " + data.decode('utf-8') + "\n> ", end="", flush=True)

# arrancar hilo receptor
threading.Thread(target=recibir, daemon=True).start()

# bucle de envío
print(f"Conectado como {args.usuario}. Escribí y enter para enviar.")
while True:
    msg = input("> ")
    if msg.lower() in ("exit", "quit"):
        break
    cliente.sendall(msg.encode('utf-8'))

cliente.close()
print("Conexión cerrada")
