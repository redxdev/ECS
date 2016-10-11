/*
Copyright (c) 2016 Sam Bloomberg

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// TODO: Make it safe to destroy entities during an each/all.

#pragma once

#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <functional>
#include <vector>
#include <algorithm>
#include <stdint.h>

//////////////////////////////////////////////////////////////////////////
// SETTINGS //
//////////////////////////////////////////////////////////////////////////


// Define what you want to pass to the tick() function by defining ECS_TICK_TYPE before including this header,
// or leave it as default (float).
// This is really messy to do but the alternative is some sort of slow custom event setup for ticks, which is silly.

// Add this before including this header if you don't want to pass anything to tick()
//#define ECS_TICK_TYPE_VOID
#ifndef ECS_TICK_TYPE
#define ECS_TICK_TYPE ECS::DefaultTickData
#endif

// Define ECS_TICK_NO_CLEANUP if you don't want the world to automatically cleanup dead entities
// at the beginning of each tick. This will require you to call cleanup() manually to prevent memory
// leaks.
//#define ECS_TICK_NO_CLEANUP


namespace ECS
{

	typedef float DefaultTickData;

	// Do not use anything in the Internal namespace yourself.
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

	/**
	 * Think of this as a pointer to a component. Whenever you get a component from the world or an entity,
	 * it'll be wrapped in a ComponentHandle.
	 */
	template<typename T>
	class ComponentHandle
	{
	public:
		ComponentHandle()
			: component(nullptr)
		{
		}

		ComponentHandle(T* component)
			: component(component)
		{
		}

		T* operator->() const
		{
			return component;
		}

		operator bool() const
		{
			return isValid();
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

	/**
	 * A container for components. Entities do not have any logic of their own, except of that which to manage
	 * components. Components themselves are generally structs that contain data with which EntitySystems can
	 * act upon, but technically any data type may be used as a component, though only one of each data type
	 * may be on a single Entity at a time.
	 */
	class Entity
	{
	public:
		const static uint32_t InvalidEntityId = 0;

		// Do not create entities yourself, use World::create().
		Entity(class World* world, uint32_t id)
			: world(world), id(id)
		{
		}

		// Do not delete entities yourself, use World::destroy().
		~Entity()
		{
			for (auto kv : components)
			{
				delete kv.second;
			}
		}

		/**
		 * Get the world associated with this entity.
		 */
		class World* getWorld() const
		{
			return world;
		}

		/**
		 * Does this entity have a component?
		 */
		template<typename T>
		bool has() const
		{
			auto index = std::type_index(typeid(T));
			return components.find(index) != components.end();
		}

		/**
		 * Does this entity have this list of components? The order of components does not matter.
		 */
		template<typename T, typename V, typename... Types>
		bool has() const
		{
			return has<T>() && has<V, Types...>();
		}

		/**
		 * Assign a new component (or replace an old one). All components must have a default constructor, though they
		 * may have additional constructors. You may pass arguments to this function the same way you would to a constructor.
		 *
		 * It is recommended that components be simple types (not const, not references, not pointers). If you need to store
		 * any of the above, wrap it in a struct.
		 */
		template<typename T, typename... Args>
		ComponentHandle<T> assign(Args... args)
		{
			auto found = components.find(std::type_index(typeid(T)));
			if (found != components.end())
			{
				Internal::ComponentContainer<T>* container = reinterpret_cast<Internal::ComponentContainer<T>*>(found->second);
				container->data = T(args...);

				auto handle = ComponentHandle<T>(&container->data);
				world->emit<Events::OnComponentAssigned<T>>({ this, handle });
				return handle;
			}
			else
			{
				Internal::ComponentContainer<T>* container = new Internal::ComponentContainer<T>();
				container->data = T(args...);
				components.insert({ std::type_index(typeid(T)), container });

				auto handle = ComponentHandle<T>(&container->data);
				world->emit<Events::OnComponentAssigned<T>>({ this, handle });
				return handle;
			}
		}

		/**
		 * Remove a component of a specific type.
		 */
		template<typename T>
		bool remove()
		{
			return components.erase(std::type_index(typeid(T))) > 0;
		}

		/**
		 * Remove all components from this entity.
		 */
		void removeAll()
		{
			components.clear();
		}

		/**
		 * Get a component from this entity.
		 */
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

		/**
		 * Call a function with components from this entity as arguments. This will return true if this entity has
		 * all specified components attached, and false if otherwise.
		 */
		template<typename... Types>
		bool with(std::function<void(ComponentHandle<Types>...)> view)
		{
			if (!has<Types...>())
				return false;

			view(get<Types>()...); // variadic template expansion wtf
			return true;
		}

		/**
		 * Get this entity's id. Entity ids aren't too useful at the moment, but can be used to tell the difference between entities when debugging.
		 */
		uint32_t getEntityId() const
		{
			return id;
		}

	private:
		std::unordered_map<std::type_index, Internal::BaseComponentContainer*> components;
		World* world;

		uint32_t id;
	};

	/**
	 * A system that acts on entities. Generally, this will act on a subset of entities using World::each().
	 *
	 * Systems often will respond to events by subclassing EventSubscriber. You may use configure() to subscribe to events,
	 * but remember to unsubscribe in unconfigure().
	 */
	class EntitySystem
	{
	public:
		virtual ~EntitySystem() {}

		/**
		 * Called when this system is added to a world.
		 */
		virtual void configure(class World* world)
		{
		}

		/**
		 * Called when this system is being removed from a world.
		 */
		virtual void unconfigure(class World* world)
		{
		}

		/**
		 * Called when World::tick() is called. See ECS_TICK_TYPE at the top of this file for more
		 * information about passing data to tick.
		 */
#ifdef ECS_TICK_TYPE_VOID
		virtual void tick(class World* world)
#else
		virtual void tick(class World* world, ECS_TICK_TYPE data)
#endif
		{
		}
	};

	/**
	 * Subclass this as EventSubscriber<EventType> and then call World::subscribe() in order to subscribe to events. Make sure
	 * to call World::unsubscribe() or World::unsubscribeAll() when your subscriber is deleted!
	 */
	template<typename T>
	class EventSubscriber : public Internal::BaseEventSubscriber
	{
	public:
		virtual ~EventSubscriber() {}

		/**
		 * Called when an event is emitted by the world.
		 */
		virtual void receive(class World* world, const T& event) = 0;
	};

	namespace Events
	{
		// Called when a new entity is created.
		struct OnEntityCreated
		{
			Entity* entity;
		};

		// Called when an entity is about to be destroyed.
		struct OnEntityDestroyed
		{
			Entity* entity;
		};

		// Called when a component is assigned (not necessarily created).
		template<typename T>
		struct OnComponentAssigned
		{
			Entity* entity;
			ComponentHandle<T> component;
		};
	}

	namespace Internal
	{
		template<typename... Types>
		class EntityIterator
		{
		public:
			EntityIterator(const std::vector<Entity*>::iterator& itr, const std::vector<Entity*>::iterator& end)
				: itr(itr), end(end)
			{
			}

			std::vector<Entity*>::iterator& getRawIterator()
			{
				return itr;
			}

			Entity* operator*() const
			{
				return *itr;
			}

			bool operator==(const EntityIterator& other) const
			{
				return itr == other.itr;
			}

			bool operator!=(const EntityIterator& other) const
			{
				return itr != other.itr;
			}

			EntityIterator& operator++()
			{
				++itr;
				while (itr != end && !(*itr)->has<Types...>())
				{
					++itr;
				}

				return *this;
			}

		private:
			std::vector<Entity*>::iterator itr;
			std::vector<Entity*>::iterator end;
		};

		template<typename... Types>
		class EntityView
		{
		public:
			EntityView(const std::vector<Entity*>::iterator& first, const std::vector<Entity*>::iterator& last)
				: first(first), last(last)
			{
				while (this->first != this->last && !(*this->first)->has<Types...>())
				{
					++this->first;
				}
			}

			EntityIterator<Types...> begin()
			{
				return EntityIterator<Types...>(first, last);
			}

			EntityIterator<Types...> end()
			{
				return EntityIterator<Types...>(last, last);
			}

		private:
			std::vector<Entity*>::iterator first;
			std::vector<Entity*>::iterator last;
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
		/**
		 * Destroying the world will emit OnEntityDestroyed events and call EntitySystem::unconfigure() as appropriate.
		 */
		~World()
		{
			for (auto* ent : entities)
			{
				emit<Events::OnEntityDestroyed>({ ent });
				delete ent;
			}

			for (auto* system : systems)
			{
				system->unconfigure(this);
				delete system;
			}
		}

		/**
		 * Create a new entity. This will emit the OnEntityCreated event.
		 */
		Entity* create()
		{
			++lastEntityId;
			Entity* ent = new Entity(this, lastEntityId);
			entities.push_back(ent);

			emit<Events::OnEntityCreated>({ ent });

			return ent;
		}

		/**
		 * Destroy an entity. This will emit the OnEntityDestroy event.
		 *
		 * If immediate is false (recommended), then the entity won't be immediately
		 * deleted but instead will be removed at the beginning of the next tick() or
		 * when cleanup() is called. OnEntityDestroyed will still be called immediately.
		 *
		 * This function is safe to call multiple times on a single entity. Note that calling
		 * this once with immediate = false and then calling it with immediate = true will
		 * remove the entity from the pending destroy queue and will immediately destroy it
		 * _without_ emitting a second OnEntityDestroyed event.
		 */
		void destroy(Entity* ent, bool immediate = false)
		{
			if (ent == nullptr)
				return;

			if (isPendingDestroy(ent))
			{
				if (immediate)
				{
					deadEntities.erase(std::remove(deadEntities.begin(), deadEntities.end(), ent), deadEntities.end());
					delete ent; // OnEntityDestroyed was already emitted, just delete it.
				}
				
				return;
			}

			entities.erase(std::remove(entities.begin(), entities.end(), ent), entities.end());

			emit<Events::OnEntityDestroyed>({ ent });

			if (!immediate)
			{
				deadEntities.push_back(ent);
			}

			if (immediate)
			{
				delete ent;
			}
		}

		/**
		 * Delete all entities in the pending destroy queue. Returns true if any entities were cleaned up,
		 * false if there were no entities to clean up.
		 */
		bool cleanup()
		{
			if (deadEntities.empty())
				return false;

			for (auto* ent : deadEntities)
			{
				delete ent;
			}

			deadEntities.clear();

			return true;
		}

		/**
		 * Reset the world, destroying all entities. Entity ids will be reset as well.
		 */
		void reset()
		{
			for (auto ent : entities)
			{
				emit<Events::OnEntityDestroyed>({ ent });
				delete ent;
			}

			entities.clear();
			lastEntityId = 0;
		}

		/**
		 * Register a system. The world will manage the memory of the system unless you unregister the system.
		 */
		void registerSystem(EntitySystem* system)
		{
			systems.push_back(system);
			system->configure(this);
		}

		/**
		 * Unregister a system.
		 */
		void unregisterSystem(EntitySystem* system)
		{
			systems.erase(std::remove(systems.begin(), systems.end(), system), systems.end());
			system->unconfigure(this);
		}

		/**
		 * Subscribe to an event.
		 */
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

		/**
		 * Unsubscribe from an event.
		 */
		template<typename T>
		void unsubscribe(EventSubscriber<T>* subscriber)
		{
			auto found = subscribers.find(std::type_index(typeid(T)));
			if (found != subscribers.end())
			{
				found->second.erase(std::remove(found->second.begin(), found->second.end(), subscriber), found->second.end());
				if (found->second.size() == 0)
				{
					subscribers.erase(found);
				}
			}
		}

		/**
		 * Unsubscribe from all events. Don't be afraid of the void pointer, just pass in your subscriber as normal.
		 */
		void unsubscribeAll(void* subscriber)
		{
			for (auto kv : subscribers)
			{
				kv.second.erase(std::remove(kv.second.begin(), kv.second.end(), subscriber), kv.second.end());
				if (kv.second.size() == 0)
				{
					subscribers.erase(kv.first);
				}
			}
		}

		/**
		 * Emit an event. This will do nothing if there are no subscribers for the event type.
		 */
		template<typename T>
		void emit(const T& event)
		{
			auto found = subscribers.find(std::type_index(typeid(T)));
			if (found != subscribers.end())
			{
				for (auto* base : found->second)
				{
					auto* sub = reinterpret_cast<EventSubscriber<T>*>(base);
					sub->receive(this, event);
				}
			}
		}

		/**
		 * Run a function on each entity with a specific set of components. This is useful for implementing an EntitySystem.
		 */
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

		/**
		* Run a function on all entities.
		*/
		void all(std::function<void(Entity*)> view)
		{
			for (auto* ent : entities)
			{
				view(ent);
			}
		}

		/**
		 * Get a view for entities with a specific set of components. The list of entities is calculated on the fly, so this method itself
		 * has little overhead. This is mostly useful with a range based for loop.
		 */
		template<typename... Types>
		Internal::EntityView<Types...> each()
		{
			return Internal::EntityView<Types...>(entities.begin(), entities.end());
		}

		/**
		 * Get the list of entities.
		 */
		const std::vector<Entity*> getEntities() const
		{
			return entities;
		}

		/**
		 * Get an entity by an id. This is a slow process.
		 */
		Entity* getEntityById(uint32_t id) const
		{
			if (id == Entity::InvalidEntityId || id > lastEntityId)
				return nullptr;

			// We should likely store entities in a map of id -> entity so that this is faster.
			for (auto ent : entities)
			{
				if (ent->getEntityId() == id)
					return ent;
			}

			return nullptr;
		}

		bool isPendingDestroy(Entity* ent) const
		{
			return std::find(deadEntities.begin(), deadEntities.end(), ent) != deadEntities.end();
		}

		/**
		 * Tick the world. See the definition for ECS_TICK_TYPE at the top of this file for more information on
		 * passing data through tick().
		 */
#ifdef ECS_TICK_TYPE_VOID
		void tick()
#else
		void tick(ECS_TICK_TYPE data)
#endif
		{
#ifndef ECS_TICK_NO_CLEANUP
			cleanup();
#endif

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
		std::vector<Entity*> deadEntities; // todo: replace with unordered_set when we have a has implementation for Entity
		std::vector<EntitySystem*> systems;
		std::unordered_map<std::type_index, std::vector<Internal::BaseEventSubscriber*>> subscribers;

		uint32_t lastEntityId = 0;
	};
}