import socket

# Configuración del servidor
IP_SERVIDOR = '127.0.0.1'  # Cambia esto por la IP real del servidor
PUERTO_SERVIDOR = 6969    # Cambia esto por el puerto correcto

# Crear el socket
cliente = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

try:
    # Conectarse al servidor
    cliente.connect((IP_SERVIDOR, PUERTO_SERVIDOR))
    print(f"Conectado al servidor en {IP_SERVIDOR}:{PUERTO_SERVIDOR}")

    # Enviar mensaje
    mensaje = "hola"
    cliente.sendall(mensaje.encode('utf-8'))
    print(f"Mensaje enviado: {mensaje}")

except Exception as e:
    print(f"Error al conectar o enviar: {e}")

finally:
    cliente.close()
    print("Conexión cerrada")