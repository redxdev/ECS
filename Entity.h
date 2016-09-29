#pragma once

#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <functional>
#include <vector>
#include <algorithm>

namespace ECS
{
	namespace Internal
	{
		struct BaseComponentContainer
		{
		};

		template<typename T>
		struct ComponentContainer : public BaseComponentContainer
		{
			ComponentContainer() {}

			T data;
		};
	}

	template<typename T>
	class ComponentHandle
	{
	public:
		ComponentHandle(T* component)
			: component(component)
		{
		}

		T* operator->() const
		{
			return component;
		}

		T& get()
		{
			return *component;
		}

		bool isValid() const
		{
			return component != nullptr;
		}

	private:
		T* component;
	};

	class Entity
	{
	public:
		Entity(class World* world)
			: world(world)
		{
		}

		~Entity()
		{
			for (auto kv : components)
			{
				delete kv.second;
			}
		}

		class World* getWorld() const
		{
			return world;
		}

		// Does this entity have a component?
		template<typename T>
		bool has() const
		{
			auto index = std::type_index(typeid(T));
			return components.find(index) != components.end();
		}

		// Does this entity have these components?
		template<typename T, typename V, typename... Types> // whyyyyyyy
		bool has() const
		{
			bool hasT = components.find(std::type_index(typeid(T))) != components.end();

			return hasT && has<V, Types...>();
		}

		// Assign a new component (or replace an old one)
		template<typename T, typename... Args>
		ComponentHandle<T> assign(Args... args)
		{
			auto found = components.find(std::type_index(typeid(T)));
			if (found != components.end())
			{
				Internal::ComponentContainer<T>* container = reinterpret_cast<Internal::ComponentContainer<T>*>(found->second);
				container->data = T(args...);

				return ComponentHandle<T>(&container->data);
			}
			else
			{
				Internal::ComponentContainer<T>* container = new Internal::ComponentContainer<T>();
				container->data = T(args...);
				components.insert({ std::type_index(typeid(T)), container });

				return ComponentHandle<T>(&container->data);
			}
		}

		template<typename T>
		bool remove()
		{
			return components.erase(std::type_index(typeid(T))) > 0;
		}

		template<typename T>
		ComponentHandle<T> get()
		{
			auto found = components.find(std::type_index(typeid(T)));
			if (found != components.end())
			{
				return ComponentHandle<T>(&reinterpret_cast<Internal::ComponentContainer<T>*>(found->second)->data);
			}
			else
			{
				return ComponentHandle<T>(nullptr);
			}
		}

		template<typename... Types>
		bool with(std::function<void(ComponentHandle<Types>...)> view)
		{
			if (!has<Types...>())
				return false;

			view(get<Types>()...); // variadic template expansion wtf
			return true;
		}

	private:
		std::unordered_map<std::type_index, Internal::BaseComponentContainer*> components;
		World* world;
	};

	class EntitySystem
	{
	public:
		virtual ~EntitySystem() {}

		virtual void configure(class World* world)
		{
		}

		virtual void tick(class World* world, float deltaTime)
		{
		}
	};

	class World
	{
	public:
		~World()
		{
			for (auto* ent : entities)
			{
				delete ent;
			}

			for (auto* system : systems)
			{
				delete system;
			}
		}

		Entity* create()
		{
			Entity* ent = new Entity(this);
			entities.push_back(ent);
			return ent;
		}

		void destroy(Entity* ent)
		{
			if (ent == nullptr)
				return;

			entities.erase(std::remove(entities.begin(), entities.end(), ent), entities.end());
			delete ent;
		}

		void registerSystem(EntitySystem* system)
		{
			systems.push_back(system);
			system->configure(this);
		}

		void unregisterSystem(EntitySystem* system)
		{
			systems.erase(std::remove(systems.begin(), systems.end(), system), systems.end());
		}

		template<typename... Types>
		void each(std::function<void(Entity*, ComponentHandle<Types>...)> view)
		{
			for (auto* ent : entities)
			{
				if (!ent->has<Types...>())
					continue;

				view(ent, ent->get<Types>()...);
			}
		}

		const std::vector<Entity*> getEntities() const
		{
			return entities;
		}

		void tick(float deltaTime)
		{
			for (auto* system : systems)
			{
				system->tick(this, deltaTime);
			}
		}

	private:
		std::vector<Entity*> entities;
		std::vector<EntitySystem*> systems;
	};
}