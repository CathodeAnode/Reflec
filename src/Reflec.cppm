module;
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <meta>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

export module Reflec;

// GCC reflection builtins look this name up in the importing translation unit.
export namespace std
{
    using std::vector;
}

export namespace Reflec
{
    struct Entity
    {
        std::size_t index{};
        std::uint64_t generation{};
        std::uint64_t worldId{};
        bool operator==(const Entity&) const = default;
    };

    // Each declared member is its own component, including equal-typed members.
    template <std::meta::info member>
    using Component = [: std::meta::type_of(member) :];

    class World
    {
        struct PoolBase
        {
            virtual ~PoolBase() = default;
            virtual void Remove(std::size_t index) noexcept = 0;
        };

        template <std::meta::info member>
        struct Pool final : PoolBase
        {
            std::deque<std::optional<Component<member>>> values;

            void Remove(std::size_t index) noexcept override
            {
                if (index < values.size())
                    values[index].reset();
            }
        };

        static constexpr std::size_t noSlot = std::numeric_limits<std::size_t>::max();

        struct Slot
        {
            std::uint64_t generation = 1;
            bool alive = false;
            std::size_t nextFree = noSlot;
        };

        inline static std::atomic<std::uint64_t> nextWorldId = 1;
        inline static std::atomic<std::size_t> nextComponentId = 0;
        const std::uint64_t worldId = nextWorldId.fetch_add(1);
        std::vector<Slot> slots;
        std::vector<std::unique_ptr<PoolBase>> pools;
        std::vector<PoolBase*> poolById;
        std::size_t freeHead = noSlot;
        std::size_t count = 0;
        std::size_t iterationDepth = 0;

        void RequireStructuralChange() const
        {
            if (iterationDepth != 0)
                throw std::logic_error("Create/Destroy cannot run inside Each; defer structural changes.");
        }

        void RequireAlive(Entity entity) const
        {
            if (!IsAlive(entity))
                throw std::out_of_range("Entity is stale, destroyed, or belongs to another world.");
        }

        template <std::meta::info member>
        static std::size_t GetComponentId()
        {
            // One shared ID per reflected member across translation units and worlds.
            // Initialization is synchronized; normal accesses only read the cached ID.
            static const std::size_t id = nextComponentId.fetch_add(1, std::memory_order_relaxed);
            return id;
        }

        template <std::meta::info member>
        Pool<member>* FindPool() const
        {
            const auto id = GetComponentId<member>();
            return id < poolById.size() ? static_cast<Pool<member>*>(poolById[id]) : nullptr;
        }

        template <std::meta::info member>
        Pool<member>& EnsurePool()
        {
            static_assert(std::meta::is_nonstatic_data_member(member), "Components must be non-static data members.");
            using Value = Component<member>;
            static_assert(std::is_object_v<Value> && !std::is_array_v<Value> && !std::is_const_v<Value> && !std::is_volatile_v<Value>,
                          "Use mutable value members; wrap C arrays in std::array and references in std::reference_wrapper.");
            const auto id = GetComponentId<member>();
            if (id < poolById.size() && poolById[id])
                return *static_cast<Pool<member>*>(poolById[id]);
            auto pool = std::make_unique<Pool<member>>();
            auto* result = pool.get();
            if (poolById.size() <= id)
                poolById.resize(id + 1, nullptr);
            pools.push_back(std::move(pool));
            poolById[id] = result;
            return *result;
        }

        void RemoveComponents(std::size_t index) noexcept
        {
            for (auto& pool : pools)
                pool->Remove(index);
        }

    public:
        World() = default;
        World(const World&) = delete;
        World& operator=(const World&) = delete;
        World(World&&) = delete;
        World& operator=(World&&) = delete;

        [[nodiscard]] std::size_t Count() const noexcept { return count; }

        [[nodiscard]] bool IsAlive(Entity entity) const noexcept
        {
            return entity.worldId == worldId && entity.index < slots.size()
                && slots[entity.index].alive && slots[entity.index].generation == entity.generation;
        }

        template <typename T>
        [[nodiscard]] Entity Create(T&& object)
        {
            using Object = std::remove_cvref_t<T>;
            static_assert(std::is_class_v<Object> && !std::is_union_v<Object>, "Create expects a class or struct.");
            constexpr auto context = std::meta::access_context::unchecked();
            static_assert(std::meta::bases_of(^^Object, context).empty(), "Use composition instead of inheritance for entity descriptions.");
            static constexpr auto members = [] consteval
            {
                constexpr auto access = std::meta::access_context::unchecked();
                constexpr auto size = std::meta::nonstatic_data_members_of(^^Object, access).size();
                std::array<std::meta::info, size> result{};
                auto reflected = std::meta::nonstatic_data_members_of(^^Object, access);
                if constexpr (size != 0)
                    for (std::size_t index = 0; index < size; ++index)
                        result[index] = reflected[index];
                return result;
            }();
            RequireStructuralChange();
            const std::size_t index = freeHead == noSlot ? slots.size() : freeHead;
            if (freeHead == noSlot)
                slots.emplace_back();
            else
                freeHead = slots[index].nextFree;
            try
            {
                template for (constexpr auto member : members)
                {
                    static_assert(std::meta::is_public(member), "Entity data members must be public.");
                    static_assert(!std::meta::is_bit_field(member), "Bit fields are not supported; use ordinary value members.");
                    auto& pool = EnsurePool<member>();
                    if (pool.values.size() <= index)
                        pool.values.resize(index + 1);
                    pool.values[index].emplace(std::forward<T>(object).[:member:]);
                }
            }
            catch (...)
            {
                RemoveComponents(index);
                slots[index].nextFree = freeHead;
                freeHead = index;
                throw;
            }
            slots[index].alive = true;
            ++count;
            return {index, slots[index].generation, worldId};
        }

        void Destroy(Entity entity)
        {
            RequireStructuralChange();
            RequireAlive(entity);
            RemoveComponents(entity.index);
            slots[entity.index].alive = false;
            ++slots[entity.index].generation;
            if (slots[entity.index].generation != std::numeric_limits<std::uint64_t>::max())
            {
                slots[entity.index].nextFree = freeHead;
                freeHead = entity.index;
            }
            --count;
        }

        template <std::meta::info member>
        [[nodiscard]] bool Has(Entity entity) const
        {
            auto* pool = FindPool<member>();
            return IsAlive(entity) && pool && entity.index < pool->values.size() && pool->values[entity.index].has_value();
        }

        template <std::meta::info member>
        Component<member>& Get(Entity entity)
        {
            RequireAlive(entity);
            auto* pool = FindPool<member>();
            if (!pool || entity.index >= pool->values.size() || !pool->values[entity.index])
                throw std::out_of_range("Entity does not have this component.");
            return *pool->values[entity.index];
        }

        template <std::meta::info member>
        const Component<member>& Get(Entity entity) const
        {
            RequireAlive(entity);
            auto* pool = FindPool<member>();
            if (!pool || entity.index >= pool->values.size() || !pool->values[entity.index])
                throw std::out_of_range("Entity does not have this component.");
            return *pool->values[entity.index];
        }

        template <std::meta::info... members, typename Function>
        void Each(Function&& function)
        {
            static_assert(sizeof...(members) > 0, "Each requires at least one component.");
            struct IterationGuard
            {
                std::size_t& depth;
                explicit IterationGuard(std::size_t& depth) : depth(depth) { ++depth; }
                ~IterationGuard() { --depth; }
            } guard(iterationDepth);
            // Structural changes are forbidden during iteration, so these pool pointers
            // and column bounds remain valid for the entire query, including nested queries.
            std::apply([&](auto*... selectedPools)
            {
                if ((!selectedPools || ...))
                    return;
                std::size_t limit = slots.size();
                ((limit = selectedPools->values.size() < limit ? selectedPools->values.size() : limit), ...);
                for (std::size_t index = 0; index < limit; ++index)
                {
                    if (slots[index].alive && (selectedPools->values[index].has_value() && ...))
                    {
                        Entity entity{index, slots[index].generation, worldId};
                        std::invoke(function, entity, *selectedPools->values[index]...);
                    }
                }
            }, std::tuple{FindPool<members>()...});
        }
    };
}
