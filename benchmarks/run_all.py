"""
PURPOSE:
Master execution script for the CRG Benchmark Suite.
Runs all compilation, benchmarking, and concept plotting scripts sequentially.
"""
import subprocess
import sys
import time
import os

def main():
    # Updated list to include all benchmarks and concept plots
    scripts_to_run = [
        "crg_benchmark_architect.py",    # Performance vs Cache limits
        "crg_benchmark_bar.py",          # Mutation penalty (Bar chart)
        "crg_benchmark_final.py",        # Structural Immunity (Stage 12)
        "crg_compare_perf.py",           # Raw Bandwidth (Dispatch Tax)
        "crg_benchmark_epilogue.py",     # OOP vs CRG energy tax
        "plot_breakeven.py",             # ECS vs CRG Curve
        "plot_tensor_3d.py",             # 3D Tensor Visualization
        "plot_tensor_routing_dark.py"    # README Architecture diagram
    ]

    print("=" * 60)
    print("🚀 STARTING CRG FULL SUITE (Benchmarks & Concepts) 🚀")
    print("=" * 60)
    
    start_total_time = time.time()
    success_count = 0

    # Ensure output directories exist before running
    os.makedirs("bin", exist_ok=True)
    os.makedirs("data", exist_ok=True)
    os.makedirs("../img", exist_ok=True)

    for script in scripts_to_run:
        if not os.path.exists(script):
            print(f"⚠️  SKIPPING: {script} (File not found)")
            continue

        print(f"\n▶️  Running: {script}")
        print("-" * 40)
        
        start_time = time.time()
        try:
            # Execute each script using the current Python interpreter
            subprocess.run([sys.executable, script], check=True)
            elapsed = time.time() - start_time
            print(f"✅ SUCCESS: {script} ({elapsed:.2f}s)")
            success_count += 1
            
        except subprocess.CalledProcessError as e:
            print(f"❌ ERROR: {script} failed with return code {e.returncode}.")

    total_elapsed = time.time() - start_total_time
    
    print("\n" + "=" * 60)
    print("🏁 EXECUTION COMPLETE 🏁")
    print(f"📊 Summary: {success_count}/{len(scripts_to_run)} scripts executed successfully.")
    print(f"⏱️  Total time: {total_elapsed:.2f} seconds.")
    print("🖼️  All assets in 'img/' are now up to date (no timestamps).")
    print("=" * 60)

if __name__ == "__main__":
    main()