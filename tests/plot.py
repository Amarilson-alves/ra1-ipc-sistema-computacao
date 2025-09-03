import csv, matplotlib.pyplot as plt

rows=[]
with open("results/results.csv", encoding="utf-8") as f:
    rows=list(csv.DictReader(f))

mechs=[r["mechanism"].upper() for r in rows]
lat_avg=[float(r["lat_avg_ms"]) for r in rows]
lat_p95=[float(r["lat_p95_ms"]) for r in rows]
thr=[float(r["throughput_msg_s"]) for r in rows]

plt.figure(); plt.bar(mechs, lat_avg); plt.title("Latência média (ms)"); plt.ylabel("ms")
plt.savefig("results/latency_avg.png", bbox_inches="tight")

plt.figure(); plt.bar(mechs, lat_p95); plt.title("Latência p95 (ms)"); plt.ylabel("ms")
plt.savefig("results/latency_p95.png", bbox_inches="tight")

plt.figure(); plt.bar(mechs, thr); plt.title("Throughput (msg/s)"); plt.ylabel("msg/s")
plt.savefig("results/throughput.png", bbox_inches="tight")
