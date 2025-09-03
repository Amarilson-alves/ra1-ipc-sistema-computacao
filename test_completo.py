#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Script de teste completo para o sistema IPC.
Executa todos os mecanismos e gera um log detalhado.
"""

import subprocess
import json
import time
import threading
import queue
from pathlib import Path
import sys

class TestRunner:
    def __init__(self):
        # Configura caminhos
        self.base_dir = Path(__file__).resolve().parent
        self.backend_path = self.base_dir / "backend-cpp" / "build" / "bin" / "Release" / "ra1_ipc_backend.exe"
        
        self.backend_process = None
        self.stdout_queue = queue.Queue()
        self.test_results = []
        self.current_test = ""
        
    def log(self, message, level="INFO"):
        """Adiciona uma mensagem ao log de teste."""
        timestamp = time.strftime("%H:%M:%S")
        log_entry = f"[{timestamp}][{level}] {message}"
        print(log_entry)
        self.test_results.append(log_entry)
        
    def start_backend(self):
        """Inicia o processo do backend."""
        try:
            self.log(f"Iniciando backend: {self.backend_path}")
            
            self.backend_process = subprocess.Popen(
                [str(self.backend_path)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                universal_newlines=True,
            )
            
            # Thread para capturar stdout
            def capture_stdout():
                while True:
                    line = self.backend_process.stdout.readline()
                    if not line:
                        break
                    line = line.strip()
                    if line:  # Só armazena linhas não vazias
                        self.stdout_queue.put(line)
            
            self.stdout_thread = threading.Thread(target=capture_stdout, daemon=True)
            self.stdout_thread.start()
            
            # Thread para capturar stderr
            def capture_stderr():
                while True:
                    line = self.backend_process.stderr.readline()
                    if not line:
                        break
                    self.log(f"BACKEND STDERR: {line.strip()}", "DEBUG")
            
            self.stderr_thread = threading.Thread(target=capture_stderr, daemon=True)
            self.stderr_thread.start()
            
            # Aguarda backend iniciar
            time.sleep(2)
            self.log("Backend iniciado com sucesso")
            return True
            
        except Exception as e:
            self.log(f"Erro ao iniciar backend: {e}", "ERROR")
            return False
    
    def send_command(self, command):
        """Envia comando para o backend."""
        try:
            json_cmd = json.dumps(command) + '\n'
            self.backend_process.stdin.write(json_cmd)
            self.backend_process.stdin.flush()
            self.log(f"Comando enviado: {command}")
            return True
        except Exception as e:
            self.log(f"Erro ao enviar comando: {e}", "ERROR")
            return False
    
    def wait_for_event(self, event_type, timeout=5):
        """Aguarda por um evento específico."""
        start_time = time.time()
        while time.time() - start_time < timeout:
            try:
                event_str = self.stdout_queue.get(timeout=0.1)
                try:
                    event = json.loads(event_str)
                    if event.get('event') == event_type:
                        self.log(f"Evento recebido: {event_type} - {event}")
                        return event
                    else:
                        self.log(f"Evento diferente: {event.get('event')}", "DEBUG")
                except json.JSONDecodeError:
                    self.log(f"JSON inválido: {event_str}", "DEBUG")
            except queue.Empty:
                continue
        self.log(f"Timeout aguardando evento: {event_type}", "WARNING")
        return None
    
    def test_mechanism(self, mechanism_name):
        """Testa um mecanismo IPC específico."""
        self.current_test = f"Teste {mechanism_name.upper()}"
        self.log(f"=== INICIANDO {self.current_test} ===")
        
        # Comando start
        start_cmd = {"cmd": "start", "mechanism": mechanism_name}
        if not self.send_command(start_cmd):
            return False
        
        # Aguarda started
        started_event = self.wait_for_event("started")
        if not started_event or started_event.get('mechanism') != mechanism_name:
            self.log("Falha no evento started", "ERROR")
            return False
        
        # Envia algumas mensagens
        test_messages = [
            "Primeira mensagem de teste",
            "Segunda mensagem com acentuação çãõ",
            "Terceira mensagem - 123 números",
            "{\"json\": \"mensagem formatada\"}"
        ]
        
        sent_count = 0
        received_count = 0
        
        for i, message in enumerate(test_messages, 1):
            # Envia mensagem
            send_cmd = {"cmd": "send", "text": message}
            if not self.send_command(send_cmd):
                continue
            
            # Aguarda evento sent
            sent_event = self.wait_for_event("sent")
            if sent_event:
                sent_count += 1
                if sent_event.get('mechanism') != mechanism_name:
                    self.log(f"ERRO: mechanism incorreto no sent: {sent_event.get('mechanism')}", "ERROR")
            
            # Aguarda evento received (pode demorar um pouco mais)
            received_event = self.wait_for_event("received", timeout=10)
            if received_event:
                received_count += 1
                if received_event.get('mechanism') != mechanism_name:
                    self.log(f"ERRO: mechanism incorreto no received: {received_event.get('mechanism')}", "ERROR")
                
                # Verifica se o texto foi ecoado corretamente
                received_text = received_event.get('text', '')
                if message not in received_text and not received_text.startswith("ECHO:"):
                    self.log(f"ERRO: Texto não ecoado corretamente. Esperado: {message}, Recebido: {received_text}", "ERROR")
        
        # Para o mecanismo
        stop_cmd = {"cmd": "stop"}
        self.send_command(stop_cmd)
        
        # Aguarda stopped
        stopped_event = self.wait_for_event("stopped")
        
        # Resultado do teste
        success = sent_count > 0 and received_count > 0
        status = "PASS" if success else "FAIL"
        
        self.log(f"RESULTADO {mechanism_name}: {status} | Enviadas: {sent_count}/{len(test_messages)} | Recebidas: {received_count}/{len(test_messages)}")
        
        return success
    
    def test_json_validation(self):
        """Testa especificamente a validação de JSON."""
        self.log("=== TESTE DE VALIDAÇÃO JSON ===")
        
        # Testa mecanismo pipe primeiro
        self.send_command({"cmd": "start", "mechanism": "pipe"})
        self.wait_for_event("started")
        
        # Envia mensagem
        self.send_command({"cmd": "send", "text": "teste json validation"})
        
        # Coleta eventos por 3 segundos
        json_events = []
        non_json_lines = []
        start_time = time.time()
        
        while time.time() - start_time < 3:
            try:
                line = self.stdout_queue.get(timeout=0.1)
                try:
                    event = json.loads(line)
                    json_events.append(event)
                except json.JSONDecodeError:
                    if line.strip() and not line.startswith("DEBUG"):
                        non_json_lines.append(line)
            except queue.Empty:
                continue
        
        # Para o mecanismo
        self.send_command({"cmd": "stop"})
        self.wait_for_event("stopped")
        
        # Análise
        self.log(f"Eventos JSON válidos: {len(json_events)}")
        self.log(f"Linhas não-JSON: {len(non_json_lines)}")
        
        if non_json_lines:
            self.log("Linhas não-JSON encontradas (ERRO):", "ERROR")
            for line in non_json_lines[:5]:  # Mostra apenas as primeiras 5
                self.log(f"  {line}", "ERROR")
        
        # Verifica se todos os eventos JSON têm mechanism
        events_without_mechanism = []
        for event in json_events:
            if 'mechanism' not in event and event.get('event') not in ['backend_started', 'backend_stopped']:
                events_without_mechanism.append(event)
        
        if events_without_mechanism:
            self.log(f"Eventos sem mechanism: {len(events_without_mechanism)}", "ERROR")
            for event in events_without_mechanism[:3]:
                self.log(f"  {event}", "ERROR")
        
        success = len(non_json_lines) == 0 and len(events_without_mechanism) == 0
        self.log(f"VALIDAÇÃO JSON: {'PASS' if success else 'FAIL'}")
        
        return success
    
    def run_all_tests(self):
        """Executa todos os testes."""
        self.log("=== INICIANDO TESTE COMPLETO DO SISTEMA IPC ===")
        
        if not self.start_backend():
            self.log("Falha ao iniciar backend - abortando testes", "ERROR")
            return False
        
        # Aguarda backend inicializar
        backend_started = self.wait_for_event("backend_started")
        if not backend_started:
            self.log("Backend não emitiu evento backend_started", "ERROR")
        
        # Executa testes
        test_results = {}
        
        # Teste de validação JSON primeiro
        test_results['json_validation'] = self.test_json_validation()
        
        # Teste dos mecanismos
        mechanisms = ['pipe', 'socket']  # Removi 'shm' até implementar
        
        for mechanism in mechanisms:
            test_results[mechanism] = self.test_mechanism(mechanism)
            time.sleep(1)  # Pequena pausa entre testes
        
        # Para o backend
        self.log("Finalizando backend...")
        if self.backend_process:
            self.backend_process.terminate()
            try:
                self.backend_process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.backend_process.kill()
        
        # Relatório final
        self.log("=== RELATÓRIO FINAL ===")
        
        total_tests = len(test_results)
        passed_tests = sum(1 for result in test_results.values() if result)
        
        self.log(f"TOTAL DE TESTES: {total_tests}")
        self.log(f"TESTES APROVADOS: {passed_tests}")
        self.log(f"TESTES REPROVADOS: {total_tests - passed_tests}")
        
        for test_name, result in test_results.items():
            status = "PASS" if result else "FAIL"
            self.log(f"{test_name.upper():<20} {status}")
        
        # Salva log em arquivo
        log_filename = f"test_log_{time.strftime('%Y%m%d_%H%M%S')}.txt"
        with open(log_filename, 'w', encoding='utf-8') as f:
            f.write("\n".join(self.test_results))
        
        self.log(f"Log salvo em: {log_filename}")
        
        return passed_tests == total_tests

def main():
    """Função principal."""
    print("Teste Automatizado do Sistema IPC")
    print("=" * 50)
    
    tester = TestRunner()
    success = tester.run_all_tests()
    
    print("\n" + "=" * 50)
    if success:
        print("? TODOS OS TESTES PASSARAM!")
        return 0
    else:
        print("? ALGUNS TESTES FALHARAM!")
        return 1

if __name__ == "__main__":
    sys.exit(main())