import argparse, csv, json, os, queue, statistics, subprocess, sys, threading, time
from pathlib import Path

# Configura caminho relativo
BASE_DIR = Path(__file__).resolve().parent.parent
DEFAULT_EXE = BASE_DIR / "backend-cpp" / "build" / "bin" / "Release" / "ra1_ipc_backend.exe"

def spawn(exe, verbose):
    if verbose: print(f"[spawn] abrindo {exe}", flush=True)
    return subprocess.Popen(
        [str(exe)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,  # IMPORTANTE: stderr separado agora
        text=True, encoding="utf-8", bufsize=1  # line-buffered
    )

def reader(proc, q, verbose):
    """Lê stdout e filtra apenas JSON válido"""
    for line in proc.stdout:
        s = line.strip()
        if not s:
            continue
        if not s.startswith("{"):
            if verbose:
                print(f"[stdout:nonjson] {s}", flush=True)
            continue
        try:
            ev = json.loads(s)
            q.put(ev)
            if verbose:
                et = ev.get("event")
                mech = ev.get("mechanism")
                print(f"[evt] {et} {mech}", flush=True)
        except Exception as e:
            if verbose:
                print(f"[parse_error] {e} | {s}", flush=True)

def stderr_reader(proc, verbose):
    """Lê stderr separadamente para DEBUG"""
    for line in proc.stderr:
        if verbose:
            print(f"[stderr] {line.strip()}", flush=True)

def send(proc, obj, verbose):
    line = json.dumps(obj) + "\n"
    if verbose:
        print(f"[send] {line.strip()}", flush=True)
    proc.stdin.write(line); proc.stdin.flush()

def wait_for(q, pred, timeout, verbose, label):
    t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            ev = q.get(timeout=0.1)
            if pred(ev):
                return ev
        except queue.Empty:
            pass
    if verbose:
        print(f"[timeout] {label} ({timeout}s)", flush=True)
    return None

def bench_one(exe, mech, warmup, n, start_timeout, recv_timeout, verbose):
    proc = spawn(exe, verbose)
    q = queue.Queue()
    
    # Threads para ler stdout e stderr separadamente
    threading.Thread(target=reader, args=(proc, q, verbose), daemon=True).start()
    threading.Thread(target=stderr_reader, args=(proc, verbose), daemon=True).start()

    # START
    send(proc, {"cmd":"start","mechanism":mech}, verbose)
    
    # Espera pelo evento started com mechanism correto
    ev = wait_for(q, lambda e: e.get("event")=="started" and e.get("mechanism")==mech, 
                 start_timeout, verbose, f"{mech} started")
    
    if not ev:
        if mech == "socket":
            # Fallback para socket (pode ter listener_registered primeiro)
            ev = wait_for(q, lambda e: e.get("event")=="socket_listener_registered", 
                         start_timeout, verbose, "socket_listener_registered")
    
    if not ev:
        cleanup(proc, verbose)
        return {"mechanism":mech, "n":0, "lat_avg_ms":0, "lat_p95_ms":0, "throughput_msg_s":0, "note":"no start"}

    # WARMUP
    if verbose: print(f"[warmup] {mech} x{warmup}", flush=True)
    for i in range(warmup):
        send(proc, {"cmd":"send","text":f"w{i}"}, verbose)
        wait_for(q, lambda e: e.get("event")=="received" and e.get("mechanism")==mech, 
                recv_timeout, verbose, "warmup receive")

    # MEDIÇÃO
    if verbose: print(f"[measure] {mech} x{n}", flush=True)
    lats = []
    for i in range(n):
        t0 = time.perf_counter()
        send(proc, {"cmd":"send","text":f"m{i}"}, verbose)
        ev = wait_for(q, lambda e: e.get("event")=="received" and e.get("mechanism")==mech, 
                     recv_timeout, verbose, "receive")
        if ev:
            lats.append((time.perf_counter()-t0)*1000.0)  # ms

    # STOP
    send(proc, {"cmd":"stop"}, verbose)
    cleanup(proc, verbose)

    if not lats:
        return {"mechanism":mech, "n":0, "lat_avg_ms":0, "lat_p95_ms":0, "throughput_msg_s":0, "note":"no data"}

    lats_sorted = sorted(lats)
    p95 = lats_sorted[max(0, int(len(lats_sorted)*0.95)-1)]
    avg = statistics.mean(lats)
    thr = 1000.0/avg if avg > 0 else 0
    return {"mechanism":mech, "n":len(lats), "lat_avg_ms":round(avg, 3), 
            "lat_p95_ms":round(p95, 3), "throughput_msg_s":round(thr, 3)}

def cleanup(proc, verbose):
    try:
        if verbose: print("[cleanup] closing stdin", flush=True)
        proc.stdin.close()
    except Exception:
        pass
    try:
        if verbose: print("[cleanup] waiting process", flush=True)
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        if verbose: print("[cleanup] kill", flush=True)
        proc.kill()

def main():
    ap = argparse.ArgumentParser(description="RA1 IPC Benchmark")
    ap.add_argument("--exe", default=str(DEFAULT_EXE), help="caminho do ra1_ipc_backend.exe")
    ap.add_argument("--warmup", type=int, default=10)  # Reduzido para testes mais rápidos
    ap.add_argument("--n", type=int, default=100)      # Reduzido para testes mais rápidos
    ap.add_argument("--start-timeout", type=float, default=5.0)
    ap.add_argument("--recv-timeout", type=float, default=3.0)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    exe = os.path.normpath(args.exe)
    if not os.path.exists(exe):
        print(f"[erro] executável não encontrado: {exe}", file=sys.stderr)
        sys.exit(2)

    print(f"=== Benchmark RA1 IPC ===\nEXE: {exe}\n", flush=True)
    
    # Garante que a pasta results existe
    results_dir = BASE_DIR / "tests" / "results"
    os.makedirs(results_dir, exist_ok=True)

    rows = []
    # Testa apenas mecanismos implementados
    for mech in ["pipe", "socket"]:  # Removido "shm" até implementar
        print(f"--- {mech.upper()} ---", flush=True)
        res = bench_one(exe, mech, args.warmup, args.n, args.start_timeout, args.recv_timeout, args.verbose)
        rows.append(res)
        print(json.dumps(res, indent=2), flush=True)
        print("", flush=True)

    out_csv = results_dir / "results.csv"
    with open(out_csv, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader()
        w.writerows(rows)
    
    print(f"\n[ok] CSV salvo em: {out_csv}", flush=True)
    
    # Exibe resumo
    print("\n=== RESUMO ===")
    for row in rows:
        if row["n"] > 0:
            print(f"{row['mechanism']}: {row['n']} msg, avg {row['lat_avg_ms']}ms, thru {row['throughput_msg_s']} msg/s")
        else:
            print(f"{row['mechanism']}: {row['note']}")

if __name__=="__main__":
    main()