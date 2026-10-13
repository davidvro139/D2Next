#include "Backend.h"
#include "Simulation.h"
#include <algorithm>
#include <stdexcept>

namespace d2 {
const EntitySnapshot& WorldSnapshot::player() const {
    const auto it = std::find_if(entities.begin(), entities.end(),
        [&](const auto& e) { return e.id != 0 && e.id == localPlayer; });
    if(it == entities.end()) throw std::runtime_error("Backend snapshot has no local player");
    return *it;
}
namespace {
class PrototypeBackend final : public GameBackend {
    CollisionGrid grid_;
    World world_;
    static constexpr EntityId playerId = 1;
public:
    PrototypeBackend(const CollisionGrid& grid, GridPoint spawn) : grid_(grid), world_(grid_, spawn) {}
    CommandResult apply(EntityId actor, const Command& command) override {
        if(actor != playerId) return CommandResult::UnknownEntity;
        return world_.apply(command) ? CommandResult::Accepted : CommandResult::Rejected;
    }
    void tick() override { world_.tick(); }
    WorldSnapshot snapshot() const override {
        const auto& p = world_.player();
        return {world_.tickCount(), playerId, 0, {},
            {{playerId, p.position, p.previous, p.facing, p.moving, p.stateTicks}}};
    }
};
}
std::unique_ptr<GameBackend> makePrototypeBackend(const CollisionGrid& grid, GridPoint spawn) {
    return std::make_unique<PrototypeBackend>(grid, spawn);
}
std::array<float,2> renderPosition(const EntitySnapshot& e, float alpha) {
    return interpolatePosition(e.previous, e.position, alpha);
}
}
