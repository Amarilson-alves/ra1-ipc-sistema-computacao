# -*- coding: utf-8 -*-
import tkinter as tk
from tkinter import ttk, scrolledtext
import json
import threading
import time
from pathlib import Path
from ipc_client import IPCClient

class PipeManager:
    def __init__(self):
        self.messages_sent = []
        self.messages_received = []
        self.is_running = False
        self.stats = {"sent": 0, "received": 0}
    
    def add_sent_message(self, message):
        self.messages_sent.append(message)
        self.stats["sent"] += 1
        
    def add_received_message(self, message):
        self.messages_received.append(message)
        self.stats["received"] += 1

class IPCApp:
    def __init__(self, root):
        self.root = root
        self.root.title("RA1 IPC - Demonstração de Mecanismos IPC")
        self.root.geometry("1000x800")

        # Configuração do cliente IPC
        backend_path = Path(r"C:\Users\LorD\Documents\2 - PROJETOS VS - STUDIO\RA1 - IPC - SISTEMA DA COMPUTACAO\backend-cpp\build\bin\Release\ra1_ipc_backend.exe")
        self.ipc_client = IPCClient(backend_path)

        self._polling_thread = None
        self._running = False
        self.current_mechanism = None
        self.pipe_manager = PipeManager()

        self.create_widgets()
        self.setup_ipc()

    def create_widgets(self):
        """Cria a interface gráfica completa."""
        # Frame principal
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.pack(fill=tk.BOTH, expand=True)

        # ===== SEÇÃO DE CONTROLES =====
        control_frame = ttk.LabelFrame(main_frame, text="Controles", padding="10")
        control_frame.pack(fill=tk.X, pady=(0, 10))

        # Botões de mecanismo
        mechanism_frame = ttk.Frame(control_frame)
        mechanism_frame.pack(fill=tk.X, pady=(0, 10))

        ttk.Label(mechanism_frame, text="Mecanismo IPC:").pack(side=tk.LEFT, padx=(0, 10))
        
        self.pipe_btn = ttk.Button(
            mechanism_frame, text="Pipes",
            command=lambda: self.start_mechanism("pipe")
        )
        self.pipe_btn.pack(side=tk.LEFT, padx=(0, 5))

        self.socket_btn = ttk.Button(
            mechanism_frame, text="Sockets",
            command=lambda: self.start_mechanism("socket"),
            state='normal'
        )
        self.socket_btn.pack(side=tk.LEFT, padx=(0, 5))

        self.shm_btn = ttk.Button(
            mechanism_frame, text="Memória Compartilhada",
            command=lambda: self.start_mechanism("shm"),
            state='normal'  # HABILITADO
        )
        self.shm_btn.pack(side=tk.LEFT)

        # Controles de envio de mensagem
        message_frame = ttk.Frame(control_frame)
        message_frame.pack(fill=tk.X)

        ttk.Label(message_frame, text="Mensagem:").pack(side=tk.LEFT, padx=(0, 10))
        
        self.message_var = tk.StringVar()
        self.message_entry = ttk.Entry(message_frame, textvariable=self.message_var, width=30)
        self.message_entry.pack(side=tk.LEFT, padx=(0, 10))
        self.message_entry.bind('<Return>', lambda e: self.send_message())

        self.send_btn = ttk.Button(message_frame, text="Enviar", 
                                  command=self.send_message, state='disabled')
        self.send_btn.pack(side=tk.LEFT, padx=(0, 10))

        self.stop_btn = ttk.Button(message_frame, text="Parar Mecanismo", 
                                  command=self.stop_mechanism, state='disabled')
        self.stop_btn.pack(side=tk.LEFT)

        # ===== SEÇÃO DE VISUALIZAÇÃO DE PIPES =====
        pipe_frame = ttk.LabelFrame(main_frame, text="Visualização dos Pipes", padding="10")
        pipe_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 10))

        # Área de texto para mostrar as filas de mensagens
        self.pipe_text = scrolledtext.ScrolledText(pipe_frame, width=80, height=10, 
                                                  state='disabled', font=("Consolas", 9))
        self.pipe_text.pack(fill=tk.BOTH, expand=True)

        # Botão para limpar a visualização
        clear_btn = ttk.Button(pipe_frame, text="Limpar Visualização", 
                              command=self.clear_pipe_display)
        clear_btn.pack(pady=(5, 0))

        # ===== SEÇÃO DE STATUS =====
        status_frame = ttk.LabelFrame(main_frame, text="Status", padding="10")
        status_frame.pack(fill=tk.X, pady=(0, 10))

        self.status_var = tk.StringVar(value="Nenhum mecanismo ativo")
        status_label = ttk.Label(status_frame, textvariable=self.status_var, 
                                font=("Segoe UI", 10, "bold"))
        status_label.pack()

        # ===== SEÇÃO DE LOG =====
        log_frame = ttk.LabelFrame(main_frame, text="Log de Eventos", padding="10")
        log_frame.pack(fill=tk.BOTH, expand=True)

        self.log_area = scrolledtext.ScrolledText(log_frame, width=100, height=15, 
                                                 state='disabled', font=("Consolas", 9))
        self.log_area.pack(fill=tk.BOTH, expand=True)

        # ===== BARRA DE STATUS =====
        status_bar = ttk.Frame(self.root)
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)

        self.backend_status = tk.StringVar(value="Backend: Parado")
        ttk.Label(status_bar, textvariable=self.backend_status).pack(side=tk.LEFT, padx=5)

        self.messages_count = tk.StringVar(value="Mensagens: 0/0")
        ttk.Label(status_bar, textvariable=self.messages_count).pack(side=tk.RIGHT, padx=5)

    def setup_ipc(self):
        """Configura e inicia a comunicação IPC."""
        self.start_backend()

    def start_backend(self):
        """Inicia o processo do backend."""
        def start_thread():
            success = self.ipc_client.start()
            self.root.after(0, lambda: self.on_backend_started(success))

        threading.Thread(target=start_thread, daemon=True).start()

    def on_backend_started(self, success: bool):
        """Callback chamado quando o backend é iniciado."""
        if success:
            self.backend_status.set("Backend: Executando")
            self.start_event_polling()
            self.log("Backend iniciado com sucesso.")
        else:
            self.backend_status.set("Backend: Falha ao iniciar")
            self.log("Falha ao iniciar backend.")

    def start_mechanism(self, mechanism):
        """Inicia um mecanismo IPC específico."""
        self.current_mechanism = mechanism
        command = {"cmd": "start", "mechanism": mechanism}
        self.ipc_client.send_command(command)
        self.log(f"Iniciando mecanismo: {mechanism}")

        # Atualiza UI - Habilita envio/stop; desabilita apenas o botão do mecanismo ativo
        self.send_btn.config(state='normal')
        self.stop_btn.config(state='normal')

        self.pipe_btn.config(state='disabled' if mechanism == 'pipe' else 'normal')
        self.socket_btn.config(state='disabled' if mechanism == 'socket' else 'normal')
        self.shm_btn.config(state='disabled' if mechanism == 'shm' else 'normal')

        self.status_var.set(f"Mecanismo ativo: {mechanism.upper()}")

    def stop_mechanism(self):
        """Para o mecanismo IPC atual."""
        if self.current_mechanism:
            command = {"cmd": "stop"}
            self.ipc_client.send_command(command)
            self.log(f"Parando mecanismo: {self.current_mechanism}")

            # Atualiza UI - Desabilita envio/stop; reabilita todos os botões de mecanismo
            self.send_btn.config(state='disabled')
            self.stop_btn.config(state='disabled')
            self.pipe_btn.config(state='normal')
            self.socket_btn.config(state='normal')
            self.shm_btn.config(state='normal')
            self.status_var.set("Nenhum mecanismo ativo")
            self.current_mechanism = None

    def send_message(self):
        """Envia uma mensagem através do mecanismo atual."""
        message = self.message_var.get().strip()
        if message and self.current_mechanism:
            command = {"cmd": "send", "text": message}
            self.ipc_client.send_command(command)
            self.message_var.set("")  # Limpa o campo
            self.log(f"Enviando: {message}")

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
        mechanism = event_data.get('mechanism', '')
        
        # Formata o evento para exibição
        formatted_event = json.dumps(event_data, indent=2, ensure_ascii=False)
        self.log(f"Evento: {event_type}\n{formatted_event}")

        # Processa eventos específicos
        if event_type == "sent":
            text = event_data.get('text', '')
            self.pipe_manager.add_sent_message(text)
            self.update_pipe_display()
            
        elif event_type == "received":
            text = event_data.get('text', '')
            self.pipe_manager.add_received_message(text)
            self.update_pipe_display()
            
        elif event_type == "ready":
            self.pipe_manager.is_running = True
            self.status_var.set(f"Mecanismo pronto: {event_data.get('mechanism', 'unknown')}")
            
        elif event_type == "started":
            mech = event_data.get('mechanism', 'unknown')
            self.status_var.set(f"Mecanismo ativo: {mech.upper()}")
            self.send_btn.config(state='normal')
            self.stop_btn.config(state='normal')
            self.pipe_btn.config(state='disabled' if mech == 'pipe' else 'normal')
            self.socket_btn.config(state='disabled' if mech == 'socket' else 'normal')
            self.shm_btn.config(state='disabled' if mech == 'shm' else 'normal')
            
        elif event_type == "stopped":
            mech = event_data.get('mechanism', 'unknown')
            self.pipe_manager.is_running = False
            self.status_var.set("Nenhum mecanismo ativo")
            self.send_btn.config(state='disabled')
            self.stop_btn.config(state='disabled')
            self.pipe_btn.config(state='normal')
            self.socket_btn.config(state='normal')
            self.shm_btn.config(state='normal')
            self.update_pipe_display()

        # Atualiza a barra de status com contagem de mensagens
        self.messages_count.set(f"Mensagens: {self.pipe_manager.stats['sent']}/{self.pipe_manager.stats['received']}")

    def update_pipe_display(self):
        """Atualiza a visualização da fila de mensagens dos pipes."""
        self.pipe_text.config(state='normal')
        self.pipe_text.delete(1.0, tk.END)
        
        # Adiciona mensagens enviadas
        self.pipe_text.insert(tk.END, "=== Mensagens Enviadas ===\n")
        for msg in self.pipe_manager.messages_sent[-10:]:  # Mostra últimas 10
            self.pipe_text.insert(tk.END, f"▶ {msg}\n")
        
        # Adiciona mensagens recebidas
        self.pipe_text.insert(tk.END, "\n=== Mensagens Recebidas ===\n")
        for msg in self.pipe_manager.messages_received[-10:]:  # Mostra últimas 10
            self.pipe_text.insert(tk.END, f"◀ {msg}\n")
        
        # Adiciona estatísticas
        self.pipe_text.insert(tk.END, f"\n=== Estatísticas ===\n")
        self.pipe_text.insert(tk.END, f"Total enviadas: {self.pipe_manager.stats['sent']}\n")
        self.pipe_text.insert(tk.END, f"Total recebidas: {self.pipe_manager.stats['received']}\n")
        
        self.pipe_text.config(state='disabled')

    def clear_pipe_display(self):
        """Limpa a visualização das filas de pipes."""
        self.pipe_manager.messages_sent.clear()
        self.pipe_manager.messages_received.clear()
        self.pipe_manager.stats = {"sent": 0, "received": 0}
        self.update_pipe_display()
        self.messages_count.set("Mensagens: 0/0")

    def log(self, message: str):
        """Adiciona uma mensagem à área de log."""
        self.log_area.config(state='normal')
        self.log_area.insert(tk.END, message + "\n\n")
        self.log_area.see(tk.END)
        self.log_area.config(state='disabled')

    def on_closing(self):
        """Lida com o fechamento da janela."""
        self.stop_event_polling()
        if self.current_mechanism:
            self.stop_mechanism()
        self.ipc_client.stop()
        self.root.destroy()

def main():
    root = tk.Tk()
    app = IPCApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()