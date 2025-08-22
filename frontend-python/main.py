# teste de commit pelo Visual Studio
# -*- coding: utf-8 -*-
import tkinter as tk
from tkinter import ttk, scrolledtext
import json
import threading
import time
from pathlib import Path

# Importa a classe IPCClient do arquivo que você criou
from ipc_client import IPCClient

class IPCApp:
    def __init__(self, root):
        self.root = root
        self.root.title("RA1 IPC - Frontend")
        self.root.geometry("700x500")

        # Configuração do cliente IPC
        # AJUSTE ESTE CAMINHO PARA O LOCAL DO SEU EXECUTÁVEL!
        backend_path = Path(__file__).parent.parent / "backend-cpp" / "build" / "bin" / "Release" / "ra1_ipc_backend.exe"

        # Linhas de debug para verificar o caminho
        print(f"Procurando backend em: {backend_path}")
        print(f"O arquivo existe? {backend_path.exists()}")
        self.ipc_client = IPCClient(backend_path)

        self._polling_thread = None
        self._running = False

        self.create_widgets()

    def create_widgets(self):
        """Cria a interface gráfica básica."""
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # Área de log
        self.log_area = scrolledtext.ScrolledText(main_frame, width=80, height=20, state='disabled')
        self.log_area.pack(fill=tk.BOTH, expand=True, pady=(0, 10))

        # Botões de controle
        button_frame = ttk.Frame(main_frame)
        button_frame.pack(fill=tk.X)

        self.start_button = ttk.Button(button_frame, text="Iniciar Backend", command=self.start_backend)
        self.start_button.pack(side=tk.LEFT, padx=(0, 5))

        self.stop_button = ttk.Button(button_frame, text="Parar Backend", command=self.stop_backend, state='disabled')
        self.stop_button.pack(side=tk.LEFT)

        # Status bar
        self.status_var = tk.StringVar(value="Pronto")
        status_bar = ttk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN, anchor=tk.W)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)

    def start_backend(self):
        """Inicia o processo do backend em uma thread separada."""
        def start_thread():
            success = self.ipc_client.start()
            self.root.after(0, lambda: self.on_backend_started(success))

        threading.Thread(target=start_thread, daemon=True).start()
        self.start_button.config(state='disabled')
        self.status_var.set("Iniciando backend...")

    def on_backend_started(self, success: bool):
        """Callback chamado quando a tentativa de iniciar o backend termina."""
        if success:
            self.stop_button.config(state='normal')
            self.log("Backend iniciado com sucesso.")
            self.status_var.set("Backend em execução")
            self.start_event_polling()
        else:
            self.start_button.config(state='normal')
            self.log("Falha ao iniciar o backend.")
            self.status_var.set("Falha ao iniciar backend")

    def stop_backend(self):
        """Para o processo do backend."""
        def stop_thread():
            self.ipc_client.stop()
            self.root.after(0, self.on_backend_stopped)

        threading.Thread(target=stop_thread, daemon=True).start()
        self.stop_button.config(state='disabled')
        self.status_var.set("Parando backend...")

    def on_backend_stopped(self):
        """Callback chamado quando o backend é parado."""
        self.start_button.config(state='normal')
        self.stop_event_polling()
        self.log("Backend parado.")
        self.status_var.set("Backend parado")

    def start_event_polling(self):
        """Inicia uma thread para buscar eventos do backend."""
        self._running = True
        def poll_events():
            while self._running:
                event_str = self.ipc_client.get_event(block=False)
                if event_str:
                    try:
                        event_data = json.loads(event_str)
                        self.root.after(0, lambda: self.handle_event(event_data))
                    except json.JSONDecodeError:
                        self.log(f"Erro ao decodificar JSON: {event_str}")
                time.sleep(0.1)

        self._polling_thread = threading.Thread(target=poll_events, daemon=True)
        self._polling_thread.start()

    def stop_event_polling(self):
        """Para a thread de polling de eventos."""
        self._running = False

    def handle_event(self, event_data: dict):
        """Processa um evento recebido do backend."""
        event_type = event_data.get('event', 'unknown')
        self.log(f"Evento: {event_type} -> {json.dumps(event_data)}")

    def log(self, message: str):
        """Adiciona uma mensagem à área de log."""
        self.log_area.config(state='normal')
        self.log_area.insert(tk.END, message + "\n")
        self.log_area.see(tk.END)
        self.log_area.config(state='disabled')

    def on_closing(self):
        """Lida com o fechamento da janela."""
        self.stop_event_polling()
        self.stop_backend()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = IPCApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()