import subprocess
import json
import threading
import queue
from pathlib import Path
from typing import Optional, Callable

class IPCClient:
    def __init__(self, executable_path: Path):
        self.executable_path = executable_path
        self.process: Optional[subprocess.Popen] = None
        self.stdout_queue: queue.Queue = queue.Queue()
        self._stdout_thread: Optional[threading.Thread] = None
        self._stderr_thread: Optional[threading.Thread] = None
        self._running = False

    def start(self):
        """Inicia o processo do backend."""
        if self.process is not None:
            print("Backend já está em execução.")
            return False

        try:
            # Inicia o processo, configurando os pipes para stdin, stdout, stderr
            self.process = subprocess.Popen(
                [str(self.executable_path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True, # Abre os pipes em modo texto
                bufsize=1, # Buffering linha-a-linha
                universal_newlines=True,
            )
            self._running = True

            # Inicia thread para ler stdout não bloqueante
            self._stdout_thread = threading.Thread(target=self._read_stdout, daemon=True)
            self._stdout_thread.start()

            # Inicia thread para ler stderr (opcional, para logging)
            self._stderr_thread = threading.Thread(target=self._read_stderr, daemon=True)
            self._stderr_thread.start()

            print(f"Backend iniciado (PID: {self.process.pid})")
            return True
        except FileNotFoundError:
            print(f"Erro: Executável não encontrado em {self.executable_path}")
            return False
        except Exception as e:
            print(f"Erro ao iniciar backend: {e}")
            return False

    def stop(self):
        """Para o processo do backend."""
        self._running = False
        if self.process:
            # Envia um comando de parada gentil (será implementado depois)
            # self.send_command({"cmd": "stop"})
            # Espera um pouco e termina o processo se não sair
            try:
                self.process.terminate()
                self.process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                self.process.kill()
            finally:
                self.process = None

    def send_command(self, command_dict: dict):
        """Envia um comando JSON para o stdin do backend."""
        if self.process is None or self.process.stdin is None:
            print("Backend não está em execução. Comando não enviado.")
            return

        try:
            json_str = json.dumps(command_dict)
            self.process.stdin.write(json_str + '\n')
            self.process.stdin.flush()
        except BrokenPipeError:
            print("Erro: Pipe quebrado. O backend pode ter terminado.")

    def _read_stdout(self):
        """Lê stdout do backend linha a linha e coloca na fila."""
        if self.process is None:
            return
        while self._running:
            try:
                line = self.process.stdout.readline()
                if not line: # EOF
                    break
                line = line.strip()
                if line: # Linha não vazia
                    self.stdout_queue.put(line)
            except ValueError: # Isso pode acontecer se o pipe for fechado durante a leitura
                break
        print("Thread de leitura de stdout terminou.")

    def _read_stderr(self):
        """Lê stderr do backend e apenas imprime no console."""
        if self.process is None:
            return
        while self._running:
            try:
                line = self.process.stderr.readline()
                if not line:
                    break
                print(f"[BACKEND STDERR] {line.strip()}")
            except ValueError:
                break
        print("Thread de leitura de stderr terminou.")

    def get_event(self, block=True, timeout=None):
        """Tenta obter um evento da fila. Retorna None se a fila estiver vazia ou após timeout."""
        try:
            return self.stdout_queue.get(block=block, timeout=timeout)
        except queue.Empty:
            return None