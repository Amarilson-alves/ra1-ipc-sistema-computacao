import json
import subprocess
import threading
import queue
import time
import sys
import os

# Ajuste o caminho para o executável do backend
EXE = r"..\backend-cpp\build\bin\Release\ra1_ipc_backend.exe"

def reader_thread(proc, q):
    """Thread para ler a saída do processo e colocar JSONs na fila."""
    while True:
        line = proc.stdout.readline()
        if not line:  # Processo terminou
            break
        try:
            decoded_line = line.decode("utf-8").strip()
            if decoded_line:  # Ignora linhas vazias
                q.put(json.loads(decoded_line))
        except json.JSONDecodeError:
            # Ignora linhas que não são JSON válido
            pass
        except Exception as e:
            print(f"Erro na thread de leitura: {e}")

def run_case(mech, text="hello", timeout=10):
    """Executa um caso de teste para um mecanismo específico."""
    proc = subprocess.Popen(
        [EXE], 
        stdin=subprocess.PIPE, 
        stdout=subprocess.PIPE, 
        stderr=subprocess.STDOUT,
        bufsize=0  # Sem buffering
    )
    q = queue.Queue()
    t = threading.Thread(target=reader_thread, args=(proc, q), daemon=True)
    t.start()

    def send(obj):
        """Envia um comando JSON para o processo."""
        s = (json.dumps(obj) + "\n").encode("utf-8")
        proc.stdin.write(s)
        proc.stdin.flush()
        time.sleep(0.2)  # Pequena pausa entre comandos

    # Pequena pausa inicial para o processo iniciar
    time.sleep(0.5)
    
    # Inicia o mecanismo
    send({"cmd": "start", "mechanism": mech})
    time.sleep(1.0)  # Pausa maior para inicialização completa
    
    # Envia a mensagem
    send({"cmd": "send", "text": text})

    got_received = False
    got_started = False
    t0 = time.time()
    
    # Espera pelos eventos
    while time.time() - t0 < timeout:
        try:
            ev = q.get(timeout=0.1)
            
            if ev.get("event") == "started" and ev.get("mechanism") == mech:
                got_started = True
                print(f"[{mech}] Mecanismo iniciado")
            
            elif ev.get("event") == "received":
                # Tratamento especial para Pipes - o texto pode conter JSON stringificado
                received_text = ev.get("text", "")
                
                # Para Pipes, o texto pode ser um JSON stringificado
                if mech == "pipe" and received_text.startswith('{') and received_text.endswith('}'):
                    try:
                        # Tenta parsear o JSON dentro do texto
                        inner_json = json.loads(received_text.strip())
                        inner_text = inner_json.get("text", "")
                        if "ECHO: " in inner_text:
                            got_received = True
                            print(f"[{mech}] Recebido: {inner_text}")
                            break
                    except json.JSONDecodeError:
                        # Se não for JSON válido, verifica se contém "ECHO:"
                        if "ECHO: " in received_text:
                            got_received = True
                            print(f"[{mech}] Recebido: {received_text}")
                            break
                else:
                    # Para outros mecanismos, verifica normalmente
                    if "ECHO: " in received_text:
                        got_received = True
                        print(f"[{mech}] Recebido: {received_text}")
                        break
            
            elif ev.get("event") == "error":
                print(f"[{mech}] Erro: {ev.get('message', '')}")
                break
                
        except queue.Empty:
            pass

    # Para o mecanismo e finaliza o processo
    send({"cmd": "stop"})
    time.sleep(0.5)
    proc.stdin.close()
    
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    return got_received

def main():
    """Função principal que executa todos os casos de teste."""
    # Verifica se o executável existe
    if not os.path.exists(EXE):
        print(f"Erro: Executável não encontrado em {EXE}")
        print("Certifique-se de compilar o backend primeiro")
        sys.exit(1)
    
    cases = ["socket", "pipe", "shm"]
    results = {}
    ok = True
    
    print("=== Teste de Fumaça - Mecanismos IPC ===")
    print(f"Executável: {EXE}")
    print()
    
    for mech in cases:
        print(f"Testando {mech.upper()}...")
        try:
            res = run_case(mech, text=f"Hello {mech.upper()}!")
            results[mech] = res
            status = "PASS" if res else "FAIL"
            print(f"[{mech}] {status}")
            ok &= res
        except Exception as e:
            print(f"[{mech}] ERROR: {e}")
            results[mech] = False
            ok = False
        print("-" * 40)
    
    # Relatório final
    print("=== RESULTADOS ===")
    for mech, result in results.items():
        status = "PASS" if result else "FAIL"
        print(f"{mech.upper():<10}: {status}")
    
    if ok:
        print("\n✅ Todos os testes PASSARAM!")
        sys.exit(0)
    else:
        print("\n❌ Alguns testes FALHARAM!")
        sys.exit(1)

if __name__ == "__main__":
    main()