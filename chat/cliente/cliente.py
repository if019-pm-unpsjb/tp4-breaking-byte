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

# === Funciones de protocolo ===
def formatear_string(s, length):
    b = s.encode("utf-8")
    if len(b) > length:
        raise ValueError("Texto demasiado largo")
    return b

def cerrar_conexion(mensaje):
    enviar_trama_desconexion()
    messagebox.showerror("Conexión finalizada", mensaje)
    try:
        cliente.close()
    except:
        pass
    ventana.destroy()

def construir_trama_conexion(usuario):
    opcode = 1
    return struct.pack('!H', opcode) + formatear_string(usuario, MAX_USERNAME_LEN) + b'\x00'

def construir_trama_sendmsg(remitente, destinatario, mensaje):
    opcode = 3
    return (
        struct.pack('!H', opcode) +
        formatear_string(remitente, MAX_USERNAME_LEN) + b'\x00' +
        formatear_string(destinatario, MAX_USERNAME_LEN) + b'\x00' +
        mensaje.encode("utf-8") + b'\x00'
    )

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
            data = cliente.recv(BUFFER_SIZE)
            if not data:
                cerrar_conexion("El servidor cerró la conexión.")
                break
            interpretar_mensaje(data)
        except ConnectionResetError:
            cerrar_conexion("Conexión perdida con el servidor.")
            break
        except OSError:
            break

def interpretar_mensaje(data):
    global usuario_seleccionado
    if len(data) < 2:
        return
    opcode = struct.unpack('!H', data[:2])[0]

    if opcode == 3:  # mensaje
        offset = 2
        fin_rem = data.find(b'\x00', offset)
        remitente = data[offset:fin_rem].decode()
        offset = fin_rem + 1

        fin_dest = data.find(b'\x00', offset)
        destinatario = data[offset:fin_dest].decode()
        offset = fin_dest + 1

        fin_mensaje = data.find(b'\x00', offset)
        mensaje = data[offset:fin_mensaje].decode()

        mensajes_por_usuario.setdefault(remitente, []).append(f"{remitente}: {mensaje}")
        if remitente == usuario_seleccionado:
            actualizar_chat()

    elif opcode == 8:  # notificación de conexión/desconexión
        if len(data) < 5:
            return
        accion = struct.unpack('!H', data[2:4])[0]
        offset = 4
        fin_usuario = data.find(b'\x00', offset)
        if fin_usuario == -1:
            return
        usuario = data[offset:fin_usuario].decode()

        if usuario == MI_USUARIO:
            return  # ignorar si soy yo

        if accion == 0:  # conexión
            if usuario not in usuarios_conectados:
                usuarios_conectados.append(usuario)
                mensajes_por_usuario.setdefault(usuario, [])
                actualizar_lista_usuarios()
        elif accion == 1:  # desconexión
            if usuario in usuarios_conectados:
                usuarios_conectados.remove(usuario)
                mensajes_por_usuario.pop(usuario, None)
                if usuario == usuario_seleccionado:
                    limpiar_chat()
                    usuario_seleccionado = None
                actualizar_lista_usuarios()

# === GUI ===
def seleccionar_usuario(evt):
    global usuario_seleccionado
    seleccion = lista_usuarios.curselection()
    if not seleccion:
        return
    usuario = lista_usuarios.get(seleccion[0])
    usuario_seleccionado = usuario
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
    lista_usuarios.delete(0, tk.END)
    for usuario in sorted(usuarios_conectados):
        if usuario != MI_USUARIO:
            lista_usuarios.insert(tk.END, usuario)

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