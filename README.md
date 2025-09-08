# RA1 — IPC (Sistemas da Computação)

Implementação do trabalho RA1 (comunicação entre processos) usando **C++23** no backend (Pipes, Sockets e Memória Compartilhada) e **Python/Tkinter** no frontend. A troca de mensagens é feita em **JSON** via stdin/stdout ou sockets locais.

## 🎯 Objetivo
- Demonstrar 3 mecanismos de IPC (Pipes, Sockets, Memória Compartilhada) em C++.
- Fornecer uma interface Tkinter que envia comandos/JSON e exibe resultados (logs, latência, throughput, erros).
- Entrega alinhada à rubrica: organização, documentação, testes mínimos e execução reproduzível.

## 🧱 Estrutura
```
backend-cpp/
  ├─ src/            # C++: implementação de pipes/sockets/shm
  ├─ include/        # headers .h/.hpp
  ├─ build/          # saída de compilação
  └─ CMakeLists.txt

frontend-python/
  ├─ main.py         # Tkinter principal (UI)
  ├─ ui/             # frames/telas
  ├─ utils/          # helpers (JSON, validação, IPC clients)
  └─ requirements.txt

docs/
  ├─ README.md       # relatório técnico (detalhes internos, decisões e testes)
  └─ fluxograma.png  # diagrama de comunicação (opcional)
```

## ⚙️ Requisitos
- **Windows 10/11** (ambiente alvo)
- **C++23** (MSVC/MinGW) + **CMake** 3.20+
- **Python 3.10+**
- Git

### Python (Tkinter)
Tkinter já vem com Python no Windows. Dependências extra (se usar):
```
pip install -r frontend-python/requirements.txt
```

## 🏗️ Compilação (Backend C++)
### Opção A — CMake (MSVC)
```bash
cd backend-cpp
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Opção B — CMake (MinGW)
```bash
cd backend-cpp
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

> Ajuste o `CMakeLists.txt` conforme a sua estrutura de fontes/headers.

## ▶️ Execução
1. **Backend:** iniciar o servidor/mecanismo desejado (pipe/socket/shm).
2. **Frontend:** executar a UI:
```bash
python frontend-python/main.py
```
3. A UI envia JSON para o backend e exibe resposta, além de métricas (tempo de ida/volta, tamanho da mensagem, etc.).

## 🔬 Testes
- Scripts em `tests/` (unitários/integração) validando:
  - Envio/recebimento JSON.
  - Conexão socket local e pipe nomeado.
  - Escrita/leitura em memória compartilhada (com sincronização).

## ✅ Checklist da Rubrica
- [✅] Organização do repositório
- [✅] README com como compilar/rodar
- [✅] Implementação dos 3 IPCs (pipes, sockets, shm)
- [✅] UI Tkinter funcional (JSON i/o)
- [✅] Testes básicos
- [✅] Logs/Métricas
- [✅] Demonstração (gif/print) no README

## 🚀 Como publicar
```bash
git init
git branch -M main
git add .
git commit -m "Estrutura inicial do projeto RA1 IPC"
git remote add origin https://github.com/<seu-usuario>/ra1-ipc-sistema-computacao.git
git push -u origin main
```
