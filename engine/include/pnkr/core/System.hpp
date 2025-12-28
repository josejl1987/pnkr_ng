#pragma once
#include "pnkr/core/ECS.hpp"
#include <vector>
#include <memory>

namespace pnkr::ecs {

    class ISystem {
    public:
        virtual ~ISystem() = default;
        virtual void update(Registry& registry, float dt) = 0;
        virtual const char* name() const = 0;
    };

    class SystemScheduler {
    private:
        std::vector<std::unique_ptr<ISystem>> m_systems;

    public:
        void addSystem(std::unique_ptr<ISystem> system) {
            m_systems.push_back(std::move(system));
        }

        template <typename T, typename... Args>
        void addSystem(Args&&... args) {
            m_systems.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        }

        void update(Registry& registry, float dt) {
            for (auto& system : m_systems) {
                system->update(registry, dt);
            }
        }

        void clear() {
            m_systems.clear();
        }
    };

} // namespace pnkr::ecs
