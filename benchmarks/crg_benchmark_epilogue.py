import subprocess, os, platform, csv
import matplotlib.pyplot as plt
import numpy as np

CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
BIN_DIR = os.path.join(CURRENT_DIR, "bin")
DATA_DIR = os.path.join(CURRENT_DIR, "data")
IMG_DIR = os.path.join(CURRENT_DIR, "..", "img")

SOURCE = os.path.join(CURRENT_DIR, "crg_benchmark_epilogue.cpp")
EXE = os.path.join(BIN_DIR, "epilogue.bin" if platform.system() != "Windows" else "epilogue.exe")
CSV = os.path.join(DATA_DIR, "crg_benchmark_epilogue.csv")

def run_suite():
    if not os.path.exists(SOURCE):
        print(f"Error: {SOURCE} not found.")
        return

    os.makedirs(BIN_DIR, exist_ok=True)
    os.makedirs(DATA_DIR, exist_ok=True)
    os.makedirs(IMG_DIR, exist_ok=True)
    
    print(f"🔨 Compiling {os.path.basename(SOURCE)}...")
    subprocess.run(["clang++", "-O3", "-march=native", "-std=c++17", SOURCE, "-o", EXE], check=True)
    
    print("🚀 Running Epilogue Benchmark (25% & 100% Frequencies)...")
    subprocess.run([EXE], check=True, cwd=CURRENT_DIR)

    # --- DATA READING ---
    # Structure: data[Paradigm][Frequency]['N'/'avg'/'jit'/'nrg']
    data = {
        'OOP': {25: {'N':[], 'avg':[], 'jit':[], 'nrg':[]}, 100: {'N':[], 'avg':[], 'jit':[], 'nrg':[]}},
        'ECS': {25: {'N':[], 'avg':[], 'jit':[], 'nrg':[]}, 100: {'N':[], 'avg':[], 'jit':[], 'nrg':[]}},
        'CRG': {25: {'N':[], 'avg':[], 'jit':[], 'nrg':[]}, 100: {'N':[], 'avg':[], 'jit':[], 'nrg':[]}}
    }
    
    with open(CSV, 'r') as f:
        reader = csv.reader(f)
        next(reader)
        for row in reader:
            p = row[0]
            freq = int(row[1])
            data[p][freq]['N'].append(int(row[2]))
            data[p][freq]['avg'].append(float(row[3]))
            data[p][freq]['jit'].append(float(row[4]))
            data[p][freq]['nrg'].append(float(row[5]))

    # --- VISUALIZATION ---
    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(20, 7))
    fig.patch.set_facecolor('#f8f9fa')
    colors = {'OOP': '#e74c3c', 'ECS': '#f39c12', 'CRG': '#2ecc71'}

    # 1. THROUGHPUT
    for p in ['OOP', 'ECS', 'CRG']:
        ax1.plot(data[p][100]['N'], data[p][100]['avg'], 'o-', color=colors[p], linewidth=2.5, label=f"{p} (100%)")
        ax1.plot(data[p][25]['N'], data[p][25]['avg'], 's--', color=colors[p], linewidth=2.0, alpha=0.6, label=f"{p} (25%)")
    
    ax1.set_title("1. Raw Throughput (ms/frame)\n(Lower is better)", fontweight='bold')
    ax1.set_xscale('log')
    ax1.set_ylabel("Milliseconds")
    ax1.grid(True, alpha=0.3)
    ax1.legend(fontsize=9)

    # 2. STABILITY (Jitter)
    for p in ['OOP', 'ECS', 'CRG']:
        ax2.plot(data[p][100]['N'], data[p][100]['jit'], 'o-', color=colors[p], linewidth=2.5, label=f"{p} (100%)")
        ax2.plot(data[p][25]['N'], data[p][25]['jit'], 's--', color=colors[p], linewidth=2.0, alpha=0.6, label=f"{p} (25%)")
    
    ax2.set_title("2. Stability & Jitter (1% Lows)\n(Max variability between frames)", fontweight='bold')
    ax2.set_xscale('log')
    ax2.set_ylabel("Jitter Amplitude (ms)")
    ax2.grid(True, alpha=0.3)
    ax2.legend(fontsize=9)

    # 3. ENERGY (Bar Chart for N=1,000,000)
    target_N = 1000000
    labels = ['OOP', 'ECS', 'CRG']
    x = np.arange(len(labels))
    width = 0.35

    nrg_25 = [data['OOP'][25]['nrg'][-1], data['ECS'][25]['nrg'][-1], data['CRG'][25]['nrg'][-1]]
    nrg_100 = [data['OOP'][100]['nrg'][-1], data['ECS'][100]['nrg'][-1], data['CRG'][100]['nrg'][-1]]

    rects1 = ax3.bar(x - width/2, nrg_25, width, label='25% Brain', color=['#e74c3c', '#f39c12', '#2ecc71'], alpha=0.5, edgecolor='black')
    rects2 = ax3.bar(x + width/2, nrg_100, width, label='100% Brain', color=['#c0392b', '#d35400', '#27ae60'], edgecolor='black')

    ax3.set_title(f"3. Ecological Impact (N={target_N:,})\n(MicroJoules spent per frame)", fontweight='bold')
    ax3.set_ylabel("MicroJoules (µJ)")
    ax3.set_xticks(x)
    ax3.set_xticklabels(labels, fontweight='bold')
    ax3.legend()

    # Add text labels on bars
    for rects in [rects1, rects2]:
        for rect in rects:
            height = rect.get_height()
            ax3.text(rect.get_x() + rect.get_width()/2., height + (height * 0.02),
                     f'{int(height)} µJ', ha='center', va='bottom', fontsize=9, fontweight='bold', rotation=0)

    plt.suptitle("THE EPILOGUE: The 3 Metrics of Victory (Time-Sliced vs Stress Test)", fontsize=20, fontweight='bold', y=1.05)
    plt.tight_layout()
    
    out_path = os.path.join(IMG_DIR, "crg_epilogue_victory_metrics.png")
    plt.savefig(out_path, dpi=200, bbox_inches='tight')
    print(f"✅ Epilogue graph generated successfully: {out_path}")

if __name__ == "__main__":
    run_suite()