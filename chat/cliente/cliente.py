import socket
import struct
import threading
import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import argparse

# === Configuración general ===
MAX_USERNAME_LEN = 32
BUFFER_SIZE = 1024

# === Datos de conexión ===
parser = argparse.ArgumentParser()
parser.add_argument("--host", "-H", default="127.0.0.1", help="IP del servidor")
parser.add_argument("--port", "-p", type=int, default=6969, help="Puerto del servidor")
parser.add_argument("--user", "-u", required=True, help="Tu nombre de usuario")
args = parser.parse_args()

HOST = args.host
PORT = args.port
MI_USUARIO = args.user

cliente = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
cliente.connect((HOST, PORT))

# === Estado dinámico ===
usuarios_conectados = []  # lista actualizada desde el servidor
mensajes_por_usuario = {}
usuario_seleccionado = None
notificaciones_pendientes = {}

# === Funciones de protocolo ===
def formatear_string(s, length):
    b = s.encode("utf-8")
    if len(b) > length:
        raise ValueError("Texto demasiado largo")
    return b

def cerrar_conexion(mensaje):
    #enviar_trama_desconexion()
    messagebox.showerror("Conexión finalizada", mensaje)
    try:
        cliente.close()
    except:
        pass
    ventana.destroy()

def construir_trama_conexion(usuario):
    opcode = 1
    payload = usuario.encode('utf-8') + b'\x00'
    size = len(payload)
    return struct.pack('!HH', opcode, size) + payload

def construir_trama_sendmsg(remitente, destinatario, mensaje):
    opcode = 3
    payload = (
        formatear_string(remitente, MAX_USERNAME_LEN) + b'\x00' +
        formatear_string(destinatario, MAX_USERNAME_LEN) + b'\x00' +
        mensaje.encode("utf-8") + b'\x00'
    )
    size = len(payload)
    return struct.pack('!HH', opcode, size) + payload

def recibir_bytes(sock, n):
    datos = b''
    while len(datos) < n:
        parte = sock.recv(n - len(datos))
        if not parte:
            return None
        datos += parte
    return datos

def recibir_trama_completa(sock):
    # Leer 2 bytes: opcode
    opcode_bytes = recibir_bytes(sock, 2)
    if not opcode_bytes:
        return None
    opcode = struct.unpack('!H', opcode_bytes)[0]

    if opcode == 7:  # ACK: opcode (2) + ack_code (2)
        ack_code_bytes = recibir_bytes(sock, 2)
        if not ack_code_bytes:
            return None
        payload = ack_code_bytes  # payload son solo los 2 bytes del ack_code
        return opcode, payload
    else:
        # Leer los siguientes 2 bytes como size
        size_bytes = recibir_bytes(sock, 2)
        if not size_bytes:
            return None
        size = struct.unpack('!H', size_bytes)[0]
        payload = recibir_bytes(sock, size)
        if payload is None:
            return None
        return opcode, payload

# === Enviar mensaje ===
def enviar_mensaje():
    global usuario_seleccionado
    if not usuario_seleccionado:
        return
    mensaje = input_mensaje.get()
    if not mensaje:
        return
    trama = construir_trama_sendmsg(MI_USUARIO, usuario_seleccionado, mensaje)
    cliente.sendall(trama)
    mensajes_por_usuario.setdefault(usuario_seleccionado, []).append(f"Yo: {mensaje}")
    input_mensaje.delete(0, tk.END)
    actualizar_chat()

def enviar_trama_desconexion():
    try:
        opcode = 8
        accion = 1  # desconexión
        trama = (
            struct.pack('!H', opcode) +
            struct.pack('!H', accion) +
            MI_USUARIO.encode("utf-8") + b'\x00'
        )
        cliente.sendall(trama)
    except:
        pass  # en caso de que ya esté cerrada la conexión

# === Recibir mensajes ===
def recibir():
    while True:
        try:
            trama = recibir_trama_completa(cliente)
            if trama is None:
                cerrar_conexion("El servidor cerró la conexión.")
                break
            opcode, payload = trama
            interpretar_mensaje(opcode, payload)
        except ConnectionResetError:
            cerrar_conexion("Conexión perdida con el servidor.")
            break
        except OSError:
            break

def interpretar_mensaje(opcode, payload):
    global usuario_seleccionado

    OPCODE_ACK = 7
    ACK_CODE_USER_CONNECTED = 1

    if opcode == 3:  # mensaje
        # payload: remitente\0 destinatario\0 mensaje\0
        offset = 0
        fin_rem = payload.find(b'\x00', offset)
        remitente = payload[offset:fin_rem].decode()
        offset = fin_rem + 1

        fin_dest = payload.find(b'\x00', offset)
        destinatario = payload[offset:fin_dest].decode()
        offset = fin_dest + 1

        fin_mensaje = payload.find(b'\x00', offset)
        mensaje = payload[offset:fin_mensaje].decode()

        mensajes_por_usuario.setdefault(remitente, []).append(f"{remitente}: {mensaje}")
        if remitente == usuario_seleccionado:
            actualizar_chat()
        else:
            notificaciones_pendientes[remitente] = True
            actualizar_lista_usuarios()

    elif opcode == 8:  # notificación de conexión/desconexión
        try:
            if len(payload) < 3:
                print("[WARN] Payload demasiado corto")
                return

            accion = struct.unpack('!H', payload[:2])[0]
            usuario = payload[2:].decode('utf-8')

            if usuario == MI_USUARIO:
                return  # ignorar si soy yo

            if accion == 0:  # conexión
                print(f"[INFO] Usuario conectado: {usuario}")
                if usuario not in usuarios_conectados:
                    usuarios_conectados.append(usuario)
                    mensajes_por_usuario.setdefault(usuario, [])
                    notificaciones_pendientes[usuario] = False
                    actualizar_lista_usuarios()

            elif accion == 1:  # desconexión
                print(f"[INFO] Usuario desconectado: {usuario}")
                if usuario in usuarios_conectados:
                    usuarios_conectados.remove(usuario)
                    mensajes_por_usuario.pop(usuario, None)
                    if usuario == usuario_seleccionado:
                        limpiar_chat()
                        usuario_seleccionado = None
                    actualizar_lista_usuarios()
            else:
                print(f"[WARN] Acción desconocida en opcode 8: {accion}")
        except Exception as e:
            print(f"[ERROR] Al interpretar USER_EVENT: {e}")

    elif opcode == OPCODE_ACK:  # ACK
        if len(payload) != 2:
            cerrar_conexion("ACK inválido: tamaño incorrecto")
            return
        ack_code = struct.unpack('!H', payload)[0]
        if ack_code == ACK_CODE_USER_CONNECTED:
            print("Conexión aceptada por el servidor.")
            # Aquí podrías actualizar algún estado si necesitas
        else:
            cerrar_conexion(f"ACK inválido: código {ack_code}")

# === GUI ===
def seleccionar_usuario(evt):
    global usuario_seleccionado
    seleccion = lista_usuarios.curselection()
    if not seleccion:
        return
    visual = lista_usuarios.get(seleccion[0])
    usuario = visual.replace("🔵 ", "")
    usuario_seleccionado = usuario
    notificaciones_pendientes[usuario] = False
    actualizar_lista_usuarios()
    actualizar_chat()
    input_mensaje.focus_set()

def actualizar_chat():
    area_chat.config(state='normal')
    area_chat.delete(1.0, tk.END)
    if usuario_seleccionado:
        mensajes = mensajes_por_usuario.get(usuario_seleccionado, [])
        for linea in mensajes:
            area_chat.insert(tk.END, linea + "\n")
    area_chat.config(state='disabled')
    area_chat.see(tk.END)

def limpiar_chat():
    area_chat.config(state='normal')
    area_chat.delete(1.0, tk.END)
    area_chat.config(state='disabled')

def actualizar_lista_usuarios():
    seleccion_actual = usuario_seleccionado  # 👈 recordar la selección real
    lista_usuarios.delete(0, tk.END)

    nombres_visibles = []
    for usuario in sorted(usuarios_conectados):
        if usuario != MI_USUARIO:
            label = f"🔵 {usuario}" if notificaciones_pendientes.get(usuario, False) else usuario
            nombres_visibles.append(label)
            lista_usuarios.insert(tk.END, label)

    # 🔧 Volver a aplicar la selección visual si corresponde
    if seleccion_actual:
        for i, nombre in enumerate(nombres_visibles):
            if nombre.replace("🔵 ", "") == seleccion_actual:
                lista_usuarios.selection_set(i)
                break

# === Ventana principal ===
ventana = tk.Tk()
ventana.title(f"Chat - {MI_USUARIO}")
ventana.geometry("600x400")

frame_usuarios = ttk.Frame(ventana)
frame_usuarios.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=5)

tk.Label(frame_usuarios, text="Usuarios").pack()
lista_usuarios = tk.Listbox(frame_usuarios)
lista_usuarios.pack(fill=tk.BOTH, expand=True)
lista_usuarios.bind("<<ListboxSelect>>", seleccionar_usuario)

frame_chat = ttk.Frame(ventana)
frame_chat.pack(side=tk.RIGHT, fill=tk.BOTH, expand=True, padx=5, pady=5)

area_chat = scrolledtext.ScrolledText(frame_chat, state='disabled', wrap=tk.WORD, height=15)
area_chat.pack(side=tk.TOP, fill=tk.BOTH, expand=True)

frame_input = ttk.Frame(frame_chat)
frame_input.pack(side=tk.BOTTOM, fill=tk.X)

input_mensaje = tk.Entry(frame_input)
input_mensaje.pack(side=tk.LEFT, fill=tk.X, expand=True)
input_mensaje.bind("<Return>", lambda event: enviar_mensaje())

btn_enviar = tk.Button(frame_input, text="Enviar", command=enviar_mensaje)
btn_enviar.pack(side=tk.RIGHT)

# === Enviar conexión inicial y arrancar recepción ===
cliente.sendall(construir_trama_conexion(MI_USUARIO))
threading.Thread(target=recibir, daemon=True).start()

ventana.protocol("WM_DELETE_WINDOW", lambda: cerrar_conexion("Conexion cerrada"))
ventana.mainloop()