#pragma once

#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <functional>
#include <vector>
#include <algorithm>

namespace ECS
{
	// Define what you want to pass to the tick() function by defining ECS_TICK_TYPE before including this header,
	// or leave it as default (float).
	// This is really messy to do but the alternative is some sort of slow custom event setup for ticks, which is silly.

	// Add this before including this header if you don't want to pass anything to tick()
	//#define ECS_TICK_TYPE_VOID

	typedef float DefaultTickData;

#ifndef ECS_TICK_TYPE
#define ECS_TICK_TYPE ECS::DefaultTickData
#endif

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


		class BaseEventSubscriber
		{
		public:
			virtual ~BaseEventSubscriber() {};
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

		virtual void unconfigure(class World* world)
		{
		}

#ifdef ECS_TICK_TYPE_VOID
		virtual void tick(class World* world)
#else
		virtual void tick(class World* world, ECS_TICK_TYPE data)
#endif
		{
		}
	};

	template<typename T>
	class EventSubscriber : public Internal::BaseEventSubscriber
	{
	public:
		virtual ~EventSubscriber() {}

		virtual void receive(const T& event) = 0;
	};

	namespace Events
	{
		// Called when a new entity is created.
		struct OnEntityCreated
		{
			Entity* entity;
		};

		// Called when an entity is about to be destroyed. This is not called when the world is destroyed.
		struct OnEntityDestroyed
		{
			Entity* entity;
		};
	}

	/**
	 * The world creates, destroys, and manages entities. The lifetime of entities and _registered_ systems are handled by the world
	 * (don't delete a system without unregistering it from the world first!), while event subscribers have their own lifetimes
	 * (the world doesn't delete them automatically when the world is deleted).
	 */
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

			emit<Events::OnEntityCreated>({ ent });

			return ent;
		}

		void destroy(Entity* ent)
		{
			if (ent == nullptr)
				return;

			entities.erase(std::remove(entities.begin(), entities.end(), ent), entities.end());

			emit<Events::OnEntityDestroyed>({ ent });

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
			system->unconfigure(this);
		}

		template<typename T>
		void subscribe(EventSubscriber<T>* subscriber)
		{
			auto found = subscribers.find(std::type_index(typeid(T)));
			if (found == subscribers.end())
			{
				std::vector<Internal::BaseEventSubscriber*> subList;
				subList.push_back(subscriber);

				subscribers.insert({ std::type_index(typeid(T)), subList });
			}
			else
			{
				found->second.push_back(subscriber);
			}
		}

		template<typename T>
		void unsubscribe(EventSubscriber<T>* subscriber)
		{
			auto found = subscribers.find(std::type_index(typeid(T)));
			if (found != subscribers.end())
			{
				found->second.erase(std::remove(found->second.begin(), found->second.end(), subscriber), found->second.end());
			}
		}

		void unsubscribeAll(Internal::BaseEventSubscriber* subscriber)
		{
			for (auto kv : subscribers)
			{
				kv.second.erase(std::remove(kv.second.begin(), kv.second.end(), subscriber), kv.second.end());
			}
		}

		template<typename T>
		void emit(const T& event)
		{
			auto found = subscribers.find(std::type_index(typeid(T)));
			if (found != subscribers.end())
			{
				for (auto* base : found->second)
				{
					auto* sub = reinterpret_cast<EventSubscriber<T>*>(base);
					sub->receive(event);
				}
			}
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

#ifdef ECS_TICK_TYPE_VOID
		void tick()
#else
		void tick(ECS_TICK_TYPE data)
#endif
		{
			for (auto* system : systems)
			{
#ifdef ECS_TICK_TYPE_VOID
				system->tick(this);
#else
				system->tick(this, data);
#endif
			}
		}

	private:
		std::vector<Entity*> entities;
		std::vector<EntitySystem*> systems;
		std::unordered_map<std::type_index, std::vector<Internal::BaseEventSubscriber*>> subscribers;
	};
}