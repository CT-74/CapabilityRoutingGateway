// Copyright (c) 2026 Cyril Tissier. All rights reserved.
// =============================================================================
// CAPABILITY ROUTING GATEWAY (CRG) - EPILOGUE EDITION: GREEN IT & OOP vs CRG
// =============================================================================

#include "raylib.h"
#include "raymath.h"
#include "imgui.h"
#include "rlImGui.h"
#include "json.hpp"         
#include <box2d/box2d.h>    
#include <box2d/math_functions.h>
#include <entt/entt.hpp>
#include <vector>
#include <tuple>
#include <typeinfo>
#include <algorithm>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <chrono>
#include <type_traits>
#include <utility>
#include <atomic>           
#include <array>
#include <memory>

// --- ENERGY ESTIMATION CONSTANTS ---
const double CPU_WATT_ACTIVE = 35.0; // Consommation en crête (Watts)
const double CPU_WATT_IDLE   = 2.0;  // Consommation au repos (C-States)

struct ProfileResult { 
    double physics_ms = 0; 
    double ai_ms = 0; 
    double struct_ms = 0; 
    double energy_microjoules = 0; 
};

enum class EngineMode { OOP, ECS, CRG };

using json = nlohmann::json;

// =============================================================================
// [ ENGINE CORE ] 1. IDENTITY & DLL-SAFE INFRASTRUCTURE
// =============================================================================
template<class T> struct UniversalAnchor {
    static T& Get() { static T s_Value{}; return s_Value; }
};

using ModelTypeID = std::size_t; 
template<class T> struct TypeIDOf { static ModelTypeID Get() { return typeid(T).hash_code(); } };

struct DenseModelID {
    std::size_t index;
    explicit DenseModelID(std::size_t i) : index(i) {}
    static constexpr std::size_t Invalid = static_cast<std::size_t>(-1);
    bool IsValid() const { return index != Invalid; }
    operator std::size_t() const { return index; }
};

using ModelMap = std::unordered_map<ModelTypeID, std::size_t>; 
struct ModelHandle {
    DenseModelID denseID;
    explicit ModelHandle(ModelTypeID hash);
    template<class T> static ModelHandle FromType() { return ModelHandle(TypeIDOf<T>::Get()); }
};

template<class TNode> using NodeListAnchor = UniversalAnchor<const TNode*>; 
template<class TNode, class TInterface>
struct NodeList : public TInterface {
    const TNode* m_Next = nullptr;
    NodeList() {
        m_Next = NodeListAnchor<TNode>::Get();
        NodeListAnchor<TNode>::Get() = static_cast<const TNode*>(this);
    }
};

// =============================================================================
// [ ENGINE CORE ] 2. TENSOR MATH (HORNER'S METHOD)
// =============================================================================
template<typename T> struct EnumTraits;
struct GlobalState { constexpr operator std::size_t() const { return 0; } };
template<> struct EnumTraits<GlobalState> { static constexpr std::size_t Count = 1; };

template<class... TAxes>
struct CapabilitySpace {
    using AxisTuple = std::tuple<TAxes...>;
    static constexpr std::size_t Dimensions = sizeof...(TAxes);
    static constexpr std::size_t Volume = (Dimensions == 0) ? 1 : (EnumTraits<TAxes>::Count * ... * 1);

    template<std::size_t DimIdx>
    static constexpr std::size_t GetStride() {
        if constexpr (Dimensions == 0) return 1;
        else {
            constexpr std::size_t dims[] = { EnumTraits<TAxes>::Count... };
            std::size_t stride = 1;
            for (std::size_t i = DimIdx + 1; i < Dimensions; ++i) stride *= dims[i];
            return stride;
        }
    }

    template<std::size_t DimIdx>
    static constexpr auto GetCoordAtIndex(std::size_t index) {
        if constexpr (Dimensions == 0) return 0;
        else {
            using AxisT = std::tuple_element_t<DimIdx, AxisTuple>;
            return static_cast<AxisT>((index / CapabilitySpace<TAxes...>::template GetStride<DimIdx>()) % EnumTraits<AxisT>::Count);
        }
    }

    template<typename... TArgs>
    static constexpr std::size_t ComputeOffset(const TArgs&... args) {
        if constexpr (Dimensions == 0) return 0;
        else return ComputeInternal(std::tie(args...), std::make_index_sequence<Dimensions>{});
    }

private:
    template<typename TupleT, std::size_t... Is>
    static constexpr std::size_t ComputeInternal(const TupleT& t, std::index_sequence<Is...>) {
        const std::size_t coords[] = { static_cast<std::size_t>(std::get<const std::tuple_element_t<Is, AxisTuple>&>(t))... };
        constexpr std::size_t dims[] = { EnumTraits<std::tuple_element_t<Is, AxisTuple>>::Count... };
        std::size_t offset = 0;
        for (std::size_t i = 0; i < Dimensions; ++i) offset = offset * dims[i] + coords[i];
        return offset;
    }
};

template<class TContract> struct CapabilityRoutingTraits { using SpaceType = CapabilitySpace<GlobalState>; }; 
template<auto... Values> struct At {};

template<class TSpace, std::size_t Index, class IdxSeq> struct MakeAt;
template<class TSpace, std::size_t Index, std::size_t... DimIs>
struct MakeAt<TSpace, Index, std::index_sequence<DimIs...>> {
    using Type = At<TSpace::template GetCoordAtIndex<DimIs>(Index)...>;
};
template<class TSpace, std::size_t Index>
using MakeAt_t = typename MakeAt<TSpace, Index, std::make_index_sequence<TSpace::Dimensions>>::Type;

// =============================================================================
// [ ENGINE CORE ] 3. ROUTING TYPES & DOD DESCRIPTOR
// =============================================================================
struct NullContext { NullContext() = default; template<typename... Args> NullContext(const Args&...) {} };
template<typename T, typename = void> struct ContextSelector { using Type = NullContext; };
template<typename T> struct ContextSelector<T, std::void_t<typename CapabilityRoutingTraits<T>::RuleContext>> { using Type = typename CapabilityRoutingTraits<T>::RuleContext; };
template<typename T> using ContextTypeOf = typename ContextSelector<T>::Type;

template<typename T, typename = void> struct IsDODContract : std::false_type {};
template<typename T> struct IsDODContract<T, std::void_t<typename T::Params>> : std::bool_constant<!std::is_polymorphic_v<T>> {};

template<class TContract>
struct DODDescriptor { void (*pfnExecute)(typename TContract::Params&); const char* debugName = "Unknown"; };

template<class TContract>
struct Rule {
    using ContextT = ContextTypeOf<TContract>;
    using PredicatePtr = bool (*)(const void*, const ContextT&);
    DODDescriptor<TContract> descriptor; 
    const void* configData;
    PredicatePtr predicate;
    int priority;
    bool Matches(const ContextT& ctx) const { return !predicate || predicate(configData, ctx); }
};

template<class TContract>
struct DispatchCell { std::vector<Rule<TContract>> dynamicRules; DODDescriptor<TContract> fallback; bool hasFallback = false; };
template<class TContract> using TensorArena = UniversalAnchor<std::vector<DispatchCell<TContract>>>; 

template<class TContract, class TConfig = void> struct Capability { using InterfaceType = TContract; using ConfigType = TConfig; TConfig config; };
template<class TContract> struct Capability<TContract, void> { using InterfaceType = TContract; using ConfigType = void; };

template<typename T, typename = void> struct HasConfigType : std::false_type {};
template<typename T> struct HasConfigType<T, std::void_t<typename T::ConfigType>> { static constexpr bool value = !std::is_same_v<typename T::ConfigType, void>; };

// =============================================================================
// [ ENGINE CORE ] 4. THE BAKER
// =============================================================================
struct IAssembler { virtual void Bake() const = 0; };
struct IBindingNode : public NodeList<IBindingNode, IAssembler> {};
UniversalAnchor<const IBindingNode*> g_BindingAnchor;

template<class TModel, template<class, class> class Cap, class TIdxSeq> struct CapabilityNode;
template<class TModel, template<class, class> class Cap, std::size_t... Is>
struct CapabilityNode<TModel, Cap, std::index_sequence<Is...>> 
    : public Cap<TModel, MakeAt_t<typename CapabilityRoutingTraits<typename Cap<TModel, At<>>::InterfaceType>::SpaceType, Is>>... 
{
    using ContractT = typename Cap<TModel, At<>>::InterfaceType;
    using TSpace = typename CapabilityRoutingTraits<ContractT>::SpaceType;
    using ContextT = ContextTypeOf<ContractT>;

    void FillArena(DenseModelID denseID) const {
        auto& arena = TensorArena<ContractT>::Get();
        std::size_t baseIdx = denseID.index * TSpace::Volume;
        if (arena.size() < baseIdx + TSpace::Volume) arena.resize(baseIdx + TSpace::Volume);

        ([&]() {
            using Impl = Cap<TModel, MakeAt_t<TSpace, Is>>;
            auto& cell = arena[baseIdx + Is];
            DODDescriptor<ContractT> desc { &Impl::Execute, typeid(Impl).name() };

            if constexpr (HasConfigType<Impl>::value) {
                auto trampoline = [](const void* obj, const ContextT& ctx) -> bool { return static_cast<const Impl*>(obj)->config.Condition(ctx); };
                cell.dynamicRules.push_back({ desc, &static_cast<const Impl*>(this)->config, trampoline, static_cast<const Impl*>(this)->config.priority });
                std::sort(cell.dynamicRules.begin(), cell.dynamicRules.end(), [](auto& a, auto& b){ return a.priority > b.priority; });
            } else { cell.fallback = desc; cell.hasFallback = true; }
        }(), ...);
    }
};

template<class TModel, template<class, class> class... TCapabilities>
struct CapabilityBinding : public IBindingNode {
    struct Unit : public CapabilityNode<TModel, TCapabilities, std::make_index_sequence<CapabilityRoutingTraits<typename TCapabilities<TModel, At<>>::InterfaceType>::SpaceType::Volume>>... {
        void Fill(DenseModelID slot) const { (CapabilityNode<TModel, TCapabilities, std::make_index_sequence<CapabilityRoutingTraits<typename TCapabilities<TModel, At<>>::InterfaceType>::SpaceType::Volume>>::FillArena(slot), ...); }
    } m_unit{};
    void Bake() const override {
        ModelTypeID hash = TypeIDOf<TModel>::Get(); auto& map = UniversalAnchor<ModelMap>::Get();
        if (map.find(hash) == map.end()) map[hash] = map.size(); m_unit.Fill(DenseModelID(map[hash]));
    }
};

class CapabilityRouter {
public:
    static void EnsureBaked() { static struct StaticGuard { StaticGuard() { for (const IBindingNode* b = UniversalAnchor<const IBindingNode*>::Get(); b; b = b->m_Next) b->Bake(); } } s_guard; } 
    template<class TContract, class... Coords>
    static const DODDescriptor<TContract>* Find(ModelHandle handle, const ContextTypeOf<TContract>& ctx, Coords... coords) {
        EnsureBaked();
        if (!handle.denseID.IsValid()) return nullptr;
        const auto& arena = TensorArena<TContract>::Get(); 
        using TSpace = typename CapabilityRoutingTraits<TContract>::SpaceType;
        std::size_t idx = (handle.denseID.index * TSpace::Volume) + TSpace::ComputeOffset(coords...);
        if (idx >= arena.size()) return nullptr;
        const auto& cell = arena[idx];
        for (const auto& rule : cell.dynamicRules) if (rule.Matches(ctx)) return &rule.descriptor;
        return cell.hasFallback ? &cell.fallback : nullptr;
    }
};

template<class T, bool IsDOD = IsDODContract<T>::value> struct ActiveCapability;
template<class T> struct ActiveCapability<T, true> {
    const DODDescriptor<T>* resolved = nullptr;
    std::size_t last_offset = 0; 
    inline ActiveCapability& operator=(const DODDescriptor<T>* desc) { resolved = desc; return *this; }
    inline void operator()(typename T::Params& p) const { if (resolved && resolved->pfnExecute) resolved->pfnExecute(p); }
    inline explicit operator bool() const { return resolved != nullptr; }
};

inline ModelHandle::ModelHandle(ModelTypeID hash) : denseID(DenseModelID::Invalid) {
    CapabilityRouter::EnsureBaked(); 
    const auto& map = UniversalAnchor<ModelMap>::Get();
    auto it = map.find(hash);
    if (it != map.end()) denseID = DenseModelID(it->second);
}

// =============================================================================
// [ GAMEPLAY ] 1. DATA, BOX2D & COMPONENTS
// =============================================================================
struct PhysicsProps { float mass = 1.0f; float radius = 3.0f; float max_speed = 300.0f; };
struct BehaviorSettings { float perception_melee = 100.0f; float perception_ranged = 300.0f; float flee_hp = 30.0f; PhysicsProps phys; };
struct RigidBody { b2BodyId id; };
struct Weapon { float cooldown; float fire_rate; };
struct Health { float current = 100.0f; bool is_dead = false; };
struct Renderable { Color color; float size; };
struct Projectile { float lifespan = 1.5f; };
struct AggressiveTag {}; 

enum class CombatState { Idle, Aggressive };
template<> struct EnumTraits<CombatState> { static constexpr std::size_t Count = 2; };
enum class PerceptionRange { Melee, Ranged, Safe };
template<> struct EnumTraits<PerceptionRange> { static constexpr std::size_t Count = 3; };
enum class GroupStrategy { Formation, Scatter };
template<> struct EnumTraits<GroupStrategy> { static constexpr std::size_t Count = 2; };

struct CRGIdentity { 
    CombatState current_state = CombatState::Idle; PerceptionRange current_perception = PerceptionRange::Safe;
    GroupStrategy current_strategy = GroupStrategy::Formation; ModelHandle handle = ModelHandle(TypeIDOf<void>::Get()); 
};

struct ForcedBehavior { bool active = false; CombatState f_state = CombatState::Idle; PerceptionRange f_range = PerceptionRange::Safe; GroupStrategy f_strategy = GroupStrategy::Formation; };

// --- MODE OOP : L'antithèse écologique (Héritage + Virtuel + Pointeur) ---
class IOOPUnit {
public:
    virtual ~IOOPUnit() = default;
    virtual void UpdateAI(float dt, b2BodyId bodyId) = 0;
};
class OOPDrone : public IOOPUnit {
public:
    void UpdateAI(float dt, b2BodyId bodyId) override {
        // Logique "Naïve OOP" pour stresser le CPU avec des indirections virtuelles.
        if (b2Body_IsValid(bodyId)) {
            b2Vec2 vel = b2Body_GetLinearVelocity(bodyId);
            if (b2LengthSquared(vel) > 0) { vel = b2Normalize(vel); vel.x *= 300.0f; vel.y *= 300.0f; }
            b2Vec2 current_vel = b2Body_GetLinearVelocity(bodyId);
            b2Vec2 steering = {vel.x - current_vel.x, vel.y - current_vel.y};
            b2Body_ApplyForceToCenter(bodyId, {steering.x * 5.0f, steering.y * 5.0f}, true);
        }
    }
};
// Allocation dynamique gérée intelligemment via shared_ptr pour éviter les fuites
struct OOPComponent { std::shared_ptr<IOOPUnit> ptr; };

struct LockFreeCommandBuffer {
    enum Type { SpawnProj, DestroyEnt };
    struct Cmd { Type type; entt::entity target; Vector2 p; Vector2 v; };
    static constexpr size_t MAX_COMMANDS = 10000; std::array<Cmd, MAX_COMMANDS> queue; std::atomic<size_t> count{0};

    void PushSpawnProjectile(Vector2 pos, Vector2 vel) { size_t idx = count.fetch_add(1, std::memory_order_relaxed); if (idx < MAX_COMMANDS) queue[idx] = {SpawnProj, entt::null, pos, vel}; }
    void PushDestroy(entt::entity e) { size_t idx = count.fetch_add(1, std::memory_order_relaxed); if (idx < MAX_COMMANDS) queue[idx] = {DestroyEnt, e, {0,0}, {0,0}}; }
    void Flush(entt::registry& reg, b2WorldId worldId, entt::entity& cmdr, entt::entity& sel) {
        size_t total = std::min(count.load(std::memory_order_relaxed), MAX_COMMANDS);
        for(size_t i = 0; i < total; ++i) { auto& c = queue[i];
            if(c.type == SpawnProj) {
                auto proj = reg.create(); b2BodyDef bd = b2DefaultBodyDef(); bd.type = b2_dynamicBody; bd.position = {c.p.x, c.p.y};
                b2BodyId bodyId = b2CreateBody(worldId, &bd); b2ShapeDef sd = b2DefaultShapeDef(); b2Circle circle = {{0.0f, 0.0f}, 2.0f}; b2CreateCircleShape(bodyId, &sd, &circle);
                b2Body_SetLinearVelocity(bodyId, {c.v.x, c.v.y}); reg.emplace<RigidBody>(proj, bodyId); reg.emplace<Projectile>(proj, 1.5f); reg.emplace<Renderable>(proj, YELLOW, 2.0f);
            } else if (c.type == DestroyEnt && reg.valid(c.target)) {
                if(c.target == cmdr) cmdr = entt::null; if(c.target == sel) sel = entt::null;
                if(auto* rb = reg.try_get<RigidBody>(c.target)) b2DestroyBody(rb->id); reg.destroy(c.target);
            }
        } count.store(0, std::memory_order_relaxed);
    }
};

struct UnitAIContract {
    struct Params { LockFreeCommandBuffer& cmd; entt::entity e; RigidBody& rb; Weapon& w; Renderable& r; const BehaviorSettings& s; float dt; };
    struct RuleContext { const Health& health; const BehaviorSettings& settings; }; 
};
template<> struct CapabilityRoutingTraits<UnitAIContract> { using SpaceType = CapabilitySpace<CombatState, PerceptionRange, GroupStrategy>; using RuleContext = UnitAIContract::RuleContext; };

// =============================================================================
// [ GAMEPLAY ] 2. LOGIC IMPLEMENTATION (VIA PHYSICS FORCES)
// =============================================================================
inline void ApplySteeringForce(b2BodyId bodyId, b2Vec2 desired_vel, float max_speed, float mass) {
    if (b2LengthSquared(desired_vel) > 0) { desired_vel = b2Normalize(desired_vel); desired_vel.x *= max_speed; desired_vel.y *= max_speed; }
    b2Vec2 current_vel = b2Body_GetLinearVelocity(bodyId); b2Vec2 steering = {desired_vel.x - current_vel.x, desired_vel.y - current_vel.y};
    b2Body_ApplyForceToCenter(bodyId, {steering.x * mass * 5.0f, steering.y * mass * 5.0f}, true);
}

template<class T, class TAt = At<>> struct DroneLogic : Capability<UnitAIContract> {
    static void Execute(UnitAIContract::Params& p) { p.r.color = SKYBLUE; ApplySteeringForce(p.rb.id, {0.0f, 0.0f}, p.s.phys.max_speed, p.s.phys.mass); if (p.w.cooldown > 0) p.w.cooldown -= p.dt; }
};
template<class T, auto... R> struct DroneLogic<T, At<CombatState::Aggressive, PerceptionRange::Ranged, R...>> : Capability<UnitAIContract> {
    static void Execute(UnitAIContract::Params& p) { p.r.color = WHITE; b2Vec2 pos = b2Body_GetPosition(p.rb.id); b2Vec2 vel = b2Body_GetLinearVelocity(p.rb.id);
        if (p.w.cooldown <= 0) { p.w.cooldown = p.w.fire_rate; Vector2 dir = { vel.x, vel.y }; if (b2LengthSquared(dir) > 0) dir = b2Normalize(dir); else dir = {1.0f,0}; p.cmd.PushSpawnProjectile({pos.x, pos.y}, {dir.x*800.0f, dir.y*800.0f}); }
        if (p.w.cooldown > 0) p.w.cooldown -= p.dt; ApplySteeringForce(p.rb.id, vel, p.s.phys.max_speed * 0.8f, p.s.phys.mass); }
};
template<class T, auto... R> struct DroneLogic<T, At<GroupStrategy::Scatter, R...>> : Capability<UnitAIContract> {
    static void Execute(UnitAIContract::Params& p) { p.r.color = VIOLET; b2Vec2 vel = b2Body_GetLinearVelocity(p.rb.id); ApplySteeringForce(p.rb.id, {vel.x * 2.0f, vel.y * 2.0f}, p.s.phys.max_speed * 1.5f, p.s.phys.mass); }
};
template<class T, class TAt = At<>> struct CommanderLogic : Capability<UnitAIContract> {
    static void Execute(UnitAIContract::Params& p) { p.r.color = GOLD; p.r.size = p.s.phys.radius; ApplySteeringForce(p.rb.id, {0.0f, 0.0f}, p.s.phys.max_speed, p.s.phys.mass); }
};
struct PanicConfig { int priority = 100; bool Condition(const UnitAIContract::RuleContext& ctx) const { return ctx.health.current < ctx.settings.flee_hp; } };
template<class T, class TAt = At<>> struct DroneFlee : Capability<UnitAIContract, PanicConfig> {
    static void Execute(UnitAIContract::Params& p) { p.r.color = ORANGE; b2Vec2 vel = b2Body_GetLinearVelocity(p.rb.id); ApplySteeringForce(p.rb.id, {vel.x, vel.y}, p.s.phys.max_speed * 1.8f, p.s.phys.mass); }
};

struct Drone {}; struct Commander {};
static const CapabilityBinding<Drone, DroneLogic, DroneFlee> g_DroneBinding{};
static const CapabilityBinding<Commander, CommanderLogic> g_CommanderBinding{};

// =============================================================================
// [ GAMEPLAY ] 3. BATTLEFIELD ENGINE
// =============================================================================
class BattlefieldEngine {
    entt::registry reg;
    b2WorldId worldId;
    LockFreeCommandBuffer cmdBuffer;

    float mutation_rate = 5.0f;
    size_t frame_count = 0; 
    GroupStrategy global_strategy = GroupStrategy::Formation;
    entt::entity commander_entity = entt::null;
    entt::entity selected_entity = entt::null;

    std::vector<float> history_ai, history_mut, history_phys, history_energy;
    std::unordered_map<ModelTypeID, BehaviorSettings> m_ModelSettings;

    bool show_debug_zones = false;
    bool show_debug_phys = false;

public:
    void Init() { 
        b2WorldDef worldDef = b2DefaultWorldDef(); worldDef.gravity = {0.0f, 0.0f}; worldId = b2CreateWorld(&worldDef);
        CreateWalls(); LoadSettings("settings.json"); AddWave(1000); DesignateCommander(); 
    }

    ~BattlefieldEngine() { b2DestroyWorld(worldId); }

    void CreateWalls() {
        b2BodyDef bd = b2DefaultBodyDef(); b2BodyId groundId = b2CreateBody(worldId, &bd); b2ShapeDef sd = b2DefaultShapeDef();
        b2Segment s1 = {{0,0}, {1280,0}}; b2CreateSegmentShape(groundId, &sd, &s1); b2Segment s2 = {{0,720}, {1280,720}}; b2CreateSegmentShape(groundId, &sd, &s2);
        b2Segment s3 = {{0,0}, {0,720}}; b2CreateSegmentShape(groundId, &sd, &s3); b2Segment s4 = {{1280,0}, {1280,720}}; b2CreateSegmentShape(groundId, &sd, &s4); 
    }

    void LoadSettings(const char* path) {
        std::ifstream f(path);
        if (f.is_open()) {
            try { json data = json::parse(f);
                auto parse_s = [&](const char* key, ModelTypeID id) {
                    if (data.contains(key)) { auto& s = m_ModelSettings[id]; s.perception_melee = data[key]["perception"].value("melee", 100.0f); s.perception_ranged = data[key]["perception"].value("ranged", 300.0f);
                        s.flee_hp = data[key].value("flee_threshold", 30.0f); if (data[key].contains("physics")) { s.phys.mass = data[key]["physics"].value("mass", 1.0f); s.phys.radius = data[key]["physics"].value("radius", 3.0f); s.phys.max_speed = data[key]["physics"].value("max_speed", 300.0f); } }
                }; parse_s("Drone", TypeIDOf<Drone>::Get()); parse_s("Commander", TypeIDOf<Commander>::Get());
            } catch (...) {}
        } else { m_ModelSettings[TypeIDOf<Drone>::Get()] = BehaviorSettings{}; m_ModelSettings[TypeIDOf<Commander>::Get()] = BehaviorSettings{100, 300, 30, {5.0f, 8.0f, 150.0f}}; }
    }

    void DesignateCommander() {
        auto view = reg.view<CRGIdentity>();
        if (!view.empty()) { commander_entity = view.front(); reg.get<CRGIdentity>(commander_entity).handle = ModelHandle::FromType<Commander>();
            auto s = m_ModelSettings[TypeIDOf<Commander>::Get()]; reg.emplace_or_replace<BehaviorSettings>(commander_entity, s); reg.emplace_or_replace<Renderable>(commander_entity, GOLD, s.phys.radius); }
    }

    void AddWave(size_t count) {
        auto ds = m_ModelSettings[TypeIDOf<Drone>::Get()];
        for(size_t i = 0; i < count; i++) {
            auto e = reg.create();
            b2BodyDef bd = b2DefaultBodyDef(); bd.type = b2_dynamicBody; bd.position = {(float)GetRandomValue(50, 1230), (float)GetRandomValue(50, 670)};
            b2BodyId bodyId = b2CreateBody(worldId, &bd); b2ShapeDef sd = b2DefaultShapeDef(); b2Circle circle = {{0.0f, 0.0f}, ds.phys.radius}; b2CreateCircleShape(bodyId, &sd, &circle);
            b2Body_SetLinearVelocity(bodyId, {(float)GetRandomValue(-150, 150), (float)GetRandomValue(-150, 150)});
            
            reg.emplace<RigidBody>(e, bodyId); reg.emplace<Weapon>(e, 0.0f, 0.5f); reg.emplace<Health>(e, 100.0f); reg.emplace<BehaviorSettings>(e, ds); reg.emplace<Renderable>(e, SKYBLUE, ds.phys.radius); 
            reg.emplace<CRGIdentity>(e, CombatState::Idle, PerceptionRange::Safe, global_strategy, ModelHandle::FromType<Drone>());
            reg.emplace<ActiveCapability<UnitAIContract>>(e); reg.emplace<ForcedBehavior>(e); 
            
            // Allocation OOP Fragmentée pour le stress test CPU[span_0](start_span)[span_0](end_span)
            reg.emplace<OOPComponent>(e, std::make_shared<OOPDrone>());
        }
    }

    ProfileResult Update(float dt, EngineMode mode, bool immortal) {
        ProfileResult p; frame_count++;
        
        // 1. PHYSICS
        auto t_phys = std::chrono::high_resolution_clock::now();
        b2World_Step(worldId, dt, 4); 
        reg.view<Projectile, RigidBody>().each([&](auto entity, auto& proj, auto& rb) { proj.lifespan -= dt; if (proj.lifespan <= 0) cmdBuffer.PushDestroy(entity); });
        p.physics_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t_phys).count();

        // 2. MUTATION / CHURN
        auto t_mut = std::chrono::high_resolution_clock::now();
        size_t total_ents = reg.storage<entt::entity>().size(); size_t to_mutate = static_cast<size_t>(total_ents * (mutation_rate / 100.0f)); size_t mutated = 0;
        reg.view<CRGIdentity>().each([&](auto entity, auto& id) {
            if (mutated++ < to_mutate) {
                CombatState next = (GetRandomValue(0, 100) > 50) ? CombatState::Aggressive : CombatState::Idle;
                if (next != id.current_state) {
                    id.current_state = next;
                    if (mode == EngineMode::ECS) { // SIMULATING ARCHETYPE CHURN[span_1](start_span)[span_1](end_span)
                        if (id.current_state == CombatState::Aggressive) reg.emplace_or_replace<AggressiveTag>(entity); else reg.remove<AggressiveTag>(entity);
                    }
                }
            }
        });
        p.struct_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t_mut).count();

        // 3. AI WORKLOAD (The Core Comparison)
        auto t_ai = std::chrono::high_resolution_clock::now();
        b2Vec2 leaderPos = reg.valid(commander_entity) ? b2Body_GetPosition(reg.get<RigidBody>(commander_entity).id) : b2Vec2{640, 360};
        
        if (mode == EngineMode::OOP) {
            // OOP: Virtual Call Overhead & Cache Misses
            reg.view<OOPComponent, RigidBody>().each([&](auto& oop, auto& rb) {
                oop.ptr->UpdateAI(dt, rb.id);
            });
        }
        else if (mode == EngineMode::ECS) {
            // ECS: Multi-System Passes
            auto viewAggro = reg.view<AggressiveTag, RigidBody, Weapon, Renderable, BehaviorSettings>();
            viewAggro.each([&](auto& rb, auto& w, auto& r, auto& s) { p.r.color = WHITE; ApplySteeringForce(rb.id, b2Body_GetLinearVelocity(rb.id), s.phys.max_speed, s.phys.mass); });
            auto viewIdle = reg.view<RigidBody, Weapon, Renderable, BehaviorSettings>(entt::exclude<AggressiveTag>);
            viewIdle.each([&](auto& rb, auto& w, auto& r, auto& s) { ApplySteeringForce(rb.id, {0,0}, s.phys.max_speed, s.phys.mass); });
        }
        else if (mode == EngineMode::CRG) {
            // CRG: Tenseur O(1) & Dense execution
            auto route_view = reg.view<CRGIdentity, RigidBody, Health, ActiveCapability<UnitAIContract>, BehaviorSettings, ForcedBehavior>();
            route_view.each([&](auto entity, auto& id, auto& rb, auto& h, auto& cap, auto& s, auto& fb) {
                if ((static_cast<uint32_t>(entity) % 6) == (frame_count % 6)) {
                    CombatState fs = id.current_state; PerceptionRange fr = id.current_perception; GroupStrategy fg = global_strategy;
                    if (fb.active) { fs = fb.f_state; fr = fb.f_range; fg = fb.f_strategy; } 
                    else { b2Vec2 pos = b2Body_GetPosition(rb.id); float d2 = (pos.x - leaderPos.x)*(pos.x - leaderPos.x) + (pos.y - leaderPos.y)*(pos.y - leaderPos.y);
                        if (d2 < (s.perception_melee * s.perception_melee)) fr = PerceptionRange::Melee; else if (d2 < (s.perception_ranged * s.perception_ranged)) fr = PerceptionRange::Ranged; else fr = PerceptionRange::Safe;
                        id.current_perception = fr; id.current_strategy = fg; }
                    cap = CapabilityRouter::Find<UnitAIContract>(id.handle, {h, s}, fs, fr, fg);
                    cap.last_offset = CapabilityRoutingTraits<UnitAIContract>::SpaceType::ComputeOffset(fs, fr, fg);
                }
            });
            auto exec_view = reg.view<ActiveCapability<UnitAIContract>, RigidBody, Weapon, Renderable, BehaviorSettings>();
            exec_view.each([&](auto entity, auto& cap, auto& rb, auto& w, auto& r, auto& s) { UnitAIContract::Params params { cmdBuffer, entity, rb, w, r, s, dt }; cap(params); });
        }
        p.ai_ms = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - t_ai).count();

        // 4. ENERGY MODEL (JOULES CALCULATION)
        double ai_seconds = p.ai_ms / 1000.0;
        double energy_joules = ai_seconds * CPU_WATT_ACTIVE;
        p.energy_microjoules = energy_joules * 1000000.0;

        if (!immortal) { reg.view<Health>().each([&](auto entity, auto& h) { h.current -= 5.0f * dt; if (h.current <= 0) cmdBuffer.PushDestroy(entity); }); }
        else { reg.view<Health>().each([](auto& h) { h.current = 100.0f; }); }
        cmdBuffer.Flush(reg, worldId, commander_entity, selected_entity); if (!reg.valid(commander_entity)) DesignateCommander();

        history_phys.push_back((float)p.physics_ms); if(history_phys.size() > 100) history_phys.erase(history_phys.begin());
        history_ai.push_back((float)p.ai_ms); if(history_ai.size() > 100) history_ai.erase(history_ai.begin());
        history_mut.push_back((float)p.struct_ms); if(history_mut.size() > 100) history_mut.erase(history_mut.begin());
        history_energy.push_back((float)p.energy_microjoules); if(history_energy.size() > 100) history_energy.erase(history_energy.begin());
        return p;
    }

    void Render(ProfileResult p, EngineMode& mode, bool& immortal) {
        BeginDrawing(); ClearBackground(Color{10, 10, 15, 255});
        b2Vec2 leaderPos = reg.valid(commander_entity) ? b2Body_GetPosition(reg.get<RigidBody>(commander_entity).id) : b2Vec2{-1.0f, -1.0f};

        reg.view<RigidBody, Renderable, Health, CRGIdentity>().each([&](auto entity, const auto& rb, const auto& r, const auto& h, const auto& id) {
            b2Vec2 pos = b2Body_GetPosition(rb.id); DrawCircleV({pos.x, pos.y}, (entity == selected_entity) ? r.size + 4 : r.size, Fade(r.color, std::max(0.3f, h.current/100.0f)));
        });

        if (show_debug_zones && reg.valid(commander_entity)) { auto s = reg.get<BehaviorSettings>(commander_entity); DrawCircleLines((int)leaderPos.x, (int)leaderPos.y, s.perception_melee, RED); DrawCircleLines((int)leaderPos.x, (int)leaderPos.y, s.perception_ranged, YELLOW); }
        
        rlImGuiBegin();
        ImGui::Begin("CRG Command Center");
        if (ImGui::Button("Reload Settings")) LoadSettings("settings.json");
        ImGui::Separator(); 
        
        // --- PARADIGM SWITCHER ---
        const char* modes[] = { "OOP (Virtual Pointers)", "ECS (Data-Oriented)", "CRG (Horner Tensor)" };
        int current_idx = (int)mode;
        if (ImGui::Combo("Engine Paradigm", &current_idx, modes, 3)) mode = (EngineMode)current_idx;
        
        ImGui::Checkbox("Immortal Swarm", &immortal);
        ImGui::SliderFloat("Mutation Rate", &mutation_rate, 0, 100); if (ImGui::Button("Spawn 500 Drones")) AddWave(500);
        int strat = (int)global_strategy; if (ImGui::Combo("Global Strategy", &strat, "Formation\0Scatter\0\0")) global_strategy = (GroupStrategy)strat;
        ImGui::Separator(); ImGui::Checkbox("Show AI Zones", &show_debug_zones); ImGui::Checkbox("Show Hitboxes", &show_debug_phys);
        ImGui::End();

        ImGui::Begin("Telemetry & Energy Bench");
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Entities: %zu", reg.storage<entt::entity>().size());
        ImGui::Separator();
        if (!history_phys.empty()) ImGui::PlotLines("Phys (ms)", history_phys.data(), (int)history_phys.size(), 0, nullptr, 0, 2.0f, ImVec2(0, 40));
        if (!history_mut.empty()) ImGui::PlotLines("Churn (ms)", history_mut.data(), (int)history_mut.size(), 0, nullptr, 0, 2.0f, ImVec2(0, 40));
        if (!history_ai.empty()) ImGui::PlotLines("AI Logic (ms)", history_ai.data(), (int)history_ai.size(), 0, nullptr, 0, 2.0f, ImVec2(0, 40));
        
        ImGui::Separator();
        ImGui::Text("IMPACT ECOLOGIQUE (MicroJoules/Frame)");
        if (!history_energy.empty()) ImGui::PlotLines("##energy", history_energy.data(), (int)history_energy.size(), 0, nullptr, 0, 50000.0f, ImVec2(0, 60));
        if (mode == EngineMode::OOP) ImGui::TextColored(ImVec4(1, 0.2f, 0.2f, 1), "P0 Max Power: %.0f uJ", p.energy_microjoules);
        else if (mode == EngineMode::ECS) ImGui::TextColored(ImVec4(1, 0.6f, 0, 1), "Heavy Load: %.0f uJ", p.energy_microjoules);
        else ImGui::TextColored(ImVec4(0, 1, 1, 1), "C-State Sleep: %.0f uJ", p.energy_microjoules);
        
        ImGui::End();

        if (reg.valid(selected_entity)) {
            ImGui::Begin("Entity Inspector"); auto& id = reg.get<CRGIdentity>(selected_entity); auto& cap = reg.get<ActiveCapability<UnitAIContract>>(selected_entity); auto& fb = reg.get<ForcedBehavior>(selected_entity);
            ImGui::Text("ID: %d", (int)selected_entity);
            if (ImGui::CollapsingHeader("GPP Overdrive", ImGuiTreeNodeFlags_DefaultOpen)) { ImGui::Checkbox("Force Behavior", &fb.active);
                if (fb.active) { int st = (int)fb.f_state; if (ImGui::Combo("State", &st, "Idle\0Aggressive\0\0")) fb.f_state = (CombatState)st;
                    int rg = (int)fb.f_range; if (ImGui::Combo("Range", &rg, "Melee\0Ranged\0Safe\0\0")) fb.f_range = (PerceptionRange)rg; } }
            ImGui::Separator(); ImGui::Text("Cell: %zu", cap.last_offset); ImGui::TextColored(ImVec4(0, 1, 0, 1), "Logic: %s", cap.resolved ? cap.resolved->debugName : "NONE");
            ImGui::End();
        }
        rlImGuiEnd(); EndDrawing();
    }
};

int main() { 
    InitWindow(1280, 720, "CRG BATTLEFIELD - EPILOGUE (OOP vs ECS vs CRG)"); 
    rlImGuiSetup(true); 
    BattlefieldEngine engine; engine.Init();
    
    EngineMode mode = EngineMode::CRG; 
    bool immortal = false;
    
    while (!WindowShouldClose()) { 
        engine.Render(engine.Update(GetFrameTime(), mode, immortal), mode, immortal); 
    }
    
    rlImGuiShutdown(); CloseWindow(); return 0; 
}