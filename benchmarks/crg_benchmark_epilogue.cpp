// Copyright (c) 2026 Cyril Tissier. All rights reserved.
// =============================================================================
// CRG EPILOGUE BENCHMARK - OOP vs ECS vs CRG (25% vs 100% Frequency)
// =============================================================================

#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>
#include <memory>
#include <numeric>
#include <algorithm>
#include <random>
#include <cmath>

const double CPU_WATT_ACTIVE = 35.0; 

struct Vector3 { float x, y, z; };
struct IPhysics { 
    Vector3 pos; 
    Vector3 vel; 
    Vector3 target; 
    float hp;
    int ammo;
    int state; 
    char pad[20]; // 64 bytes aligned
};

// =============================================================================
// 1. OOP PARADIGM
// =============================================================================
class IOOPUnit {
public:
    virtual ~IOOPUnit() = default;
    virtual void Muscle() = 0;
    virtual int Brain(IPhysics& e) = 0;
    virtual void Update(int frame, int id, int brain_mod) = 0;
};

// =============================================================================
// 2. CRG PARADIGM
// =============================================================================
struct PhysicsContract { struct Params { IPhysics* d; }; };

struct IdleLogic   { static void Execute(PhysicsContract::Params& p) { p.d->vel.x *= 0.9f; } };
struct PatrolLogic { static void Execute(PhysicsContract::Params& p) { p.d->pos.x += p.d->vel.x; } };
struct CombatLogic { static void Execute(PhysicsContract::Params& p) { p.d->pos.x += p.d->vel.x * 2.0f; p.d->target.x = p.d->pos.x; } };
struct FleeLogic   { 
    static void Execute(PhysicsContract::Params& p) { 
        float dist = std::sqrt(p.d->pos.x * p.d->pos.x + p.d->pos.y * p.d->pos.y);
        p.d->pos.x -= (p.d->vel.x * 3.0f) / (dist + 0.1f); 
    } 
};

using PFN_Logic = void (*)(PhysicsContract::Params&);

struct ActiveCapability {
    PFN_Logic pfn = nullptr;
    inline void operator()(PhysicsContract::Params& p) const { if (pfn) pfn(p); }
};

// --- LE CERVEAU (Commun) ---
inline int EvaluateBehaviorTree(const IPhysics& e) {
    // CORRECTION : e.target.x et e.pos.x
    float dx = e.target.x - e.pos.x;
    float dy = e.target.y - e.pos.y;
    float dist_sq = dx*dx + dy*dy;

    if (e.hp < 25.0f) return 3; 
    else if (dist_sq < 10000.0f) {
        if (e.ammo > 0) return 2; 
        else return 0; 
    } 
    return 1; 
}

inline PFN_Logic StateToPointer(int state) {
    switch(state) {
        case 0: return &IdleLogic::Execute;
        case 1: return &PatrolLogic::Execute;
        case 2: return &CombatLogic::Execute;
        case 3: return &FleeLogic::Execute;
    }
    return &IdleLogic::Execute;
}

// --- OOP IMPLEMENTATION ---
class OOPDrone : public IOOPUnit {
    IPhysics data;
public:
    // CORRECTION : std::uniform_real_distribution
    OOPDrone(int i, std::mt19937& rng, std::uniform_real_distribution<float>& dist) { 
        data.hp = 100; data.ammo = 30; data.state = 1; 
        data.target.x = dist(rng); data.target.y = dist(rng);
    }
    int Brain(IPhysics& e) override { return EvaluateBehaviorTree(e); }
    void Muscle() override {
        switch(data.state) {
            case 0: data.vel.x *= 0.9f; break;
            case 1: data.pos.x += data.vel.x; break;
            case 2: data.pos.x += data.vel.x * 2.0f; data.target.x = data.pos.x; break;
            case 3: {
                float d = std::sqrt(data.pos.x * data.pos.x + data.pos.y * data.pos.y);
                data.pos.x -= (data.vel.x * 3.0f) / (d + 0.1f); break;
            }
        }
    }
    void Update(int frame, int id, int brain_mod) override {
        Muscle(); 
        if ((id % brain_mod) == (frame % brain_mod)) { data.state = Brain(data); } 
    }
};

// =============================================================================
// BENCHMARK ENGINE
// =============================================================================
void RunEpilogueTest(size_t N, int brain_mod, std::ofstream& out) {
    const int FRAMES = 100;
    int freq_percent = (brain_mod == 1) ? 100 : 25;
    std::mt19937 rng(42);
    
    // CORRECTION : std::uniform_real_distribution
    std::uniform_real_distribution<float> dist_rand(0.0f, 200.0f);
    
    // --- SETUP OOP ---
    std::vector<std::unique_ptr<IOOPUnit>> oop_swarm(N);
    for(size_t i=0; i<N; ++i) oop_swarm[i] = std::make_unique<OOPDrone>(i, rng, dist_rand);
    std::shuffle(oop_swarm.begin(), oop_swarm.end(), rng);

    // --- SETUP ECS & CRG ---
    std::vector<IPhysics> data(N);
    std::vector<ActiveCapability> caps(N);
    for(size_t i=0; i<N; ++i) {
        data[i].hp = 100; data[i].ammo = 30; data[i].state = 1;
        data[i].target.x = dist_rand(rng); data[i].target.y = dist_rand(rng);
        caps[i].pfn = &PatrolLogic::Execute;
    }

    auto run_benchmark = [&](const std::string& name, auto logic_lambda) {
        std::vector<double> frame_times(FRAMES);
        for (int f = 0; f < FRAMES; ++f) {
            data[(f * 13) % N].hp -= 50.0f; // Asynchronous mutation trigger
            
            auto t0 = std::chrono::high_resolution_clock::now();
            logic_lambda(f);
            auto t1 = std::chrono::high_resolution_clock::now();
            
            frame_times[f] = std::chrono::duration<double, std::milli>(t1 - t0).count();
        }
        
        double sum = std::accumulate(frame_times.begin(), frame_times.end(), 0.0);
        double avg_ms = sum / FRAMES;
        double max_ms = *std::max_element(frame_times.begin(), frame_times.end());
        double min_ms = *std::min_element(frame_times.begin(), frame_times.end());
        double jitter_ms = max_ms - min_ms; 
        double energy_uJ = (avg_ms / 1000.0) * CPU_WATT_ACTIVE * 1000000.0;

        out << name << "," << freq_percent << "," << N << "," << avg_ms << "," << jitter_ms << "," << energy_uJ << "\n";
    };

    // 1. OOP BENCHMARK
    run_benchmark("OOP", [&](int frame) {
        for(size_t i = 0; i < N; ++i) oop_swarm[i]->Update(frame, i, brain_mod);
    });

    // 2. ECS BENCHMARK (With Archetype Mutation Penalty)
    run_benchmark("ECS", [&](int frame) {
        for(size_t i = 0; i < N; ++i) {
            switch(data[i].state) {
                case 0: data[i].vel.x *= 0.9f; break;
                case 1: data[i].pos.x += data[i].vel.x; break;
                case 2: data[i].pos.x += data[i].vel.x * 2.0f; data[i].target.x = data[i].pos.x; break;
                case 3: {
                    float d = std::sqrt(data[i].pos.x * data[i].pos.x + data[i].pos.y * data[i].pos.y);
                    data[i].pos.x -= (data[i].vel.x * 3.0f) / (d + 0.1f); break;
                }
            }

            if ((i % brain_mod) == (frame % brain_mod)) {
                int new_state = EvaluateBehaviorTree(data[i]);
                if (new_state != data[i].state) {
                    data[i].state = new_state;
                    IPhysics temp = data[i];
                    data[i] = data[N - 1];
                    data[N - 1] = temp;
                }
            }
        }
    });

    // 3. CRG BENCHMARK (Value Mutation Only)
    run_benchmark("CRG", [&](int frame) {
        for(size_t i = 0; i < N; ++i) {
            PhysicsContract::Params p { &data[i] };
            caps[i](p); 
            
            if ((i % brain_mod) == (frame % brain_mod)) {
                int new_state = EvaluateBehaviorTree(data[i]);
                if (new_state != data[i].state) {
                    data[i].state = new_state;
                    caps[i].pfn = StateToPointer(new_state);
                }
            }
        }
    });
}

int main() {
    std::ofstream out("data/crg_benchmark_epilogue.csv");
    if (!out.is_open()) return 1;

    out << "Paradigm,Freq,N,Avg_ms,Jitter_ms,Energy_uJ\n";
    std::vector<size_t> counts = {10000, 50000, 100000, 500000, 1000000};
    
    for (size_t n : counts) {
        std::cout << "Benchmarking N=" << n << " at 25% Frequency...\n";
        RunEpilogueTest(n, 4, out); // 25% Frequency (1 frame out of 4)
        
        std::cout << "Benchmarking N=" << n << " at 100% Frequency...\n";
        RunEpilogueTest(n, 1, out); // 100% Frequency (Every frame)
    }
    
    std::cout << "Done! Results saved in data/crg_benchmark_epilogue.csv\n";
    return 0;
}