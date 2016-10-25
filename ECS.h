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

//////////////////////////////////////////////////////////////////////////
// CODE //
//////////////////////////////////////////////////////////////////////////

namespace ECS
{
	typedef float DefaultTickData;

	class World;

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
		friend class World;

		const static size_t InvalidEntityId = 0;

		// Do not create entities yourself, use World::create().
		Entity(class World* world, size_t id)
			: world(world), id(id)
		{
		}

		// Do not delete entities yourself, use World::destroy().
		~Entity()
		{
			removeAll();
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
			for (auto pair : components)
			{
				delete pair.second;
			}

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

			view(get<Types>()...); // variadic template expansion is fun
			return true;
		}

		/**
		 * Get this entity's id. Entity ids aren't too useful at the moment, but can be used to tell the difference between entities when debugging.
		 */
		size_t getEntityId() const
		{
			return id;
		}

		bool isPendingDestroy() const
		{
			return bPendingDestroy;
		}

	private:
		std::unordered_map<std::type_index, Internal::BaseComponentContainer*> components;
		World* world;

		size_t id;
		bool bPendingDestroy = false;
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
		class EntityIterator
		{
		public:
			EntityIterator(class World* world, size_t index, bool bIsEnd, bool bIncludePendingDestroy);

			size_t getIndex() const
			{
				return index;
			}

			bool isEnd() const;

			bool includePendingDestroy() const
			{
				return bIncludePendingDestroy;
			}

			World* getWorld() const
			{
				return world;
			}

			Entity* get() const;

			Entity* operator*() const
			{
				return get();
			}

			bool operator==(const EntityIterator& other) const
			{
				if (world != other.world)
					return false;

				if (isEnd())
					return other.isEnd();

				return index == other.index;
			}

			bool operator!=(const EntityIterator& other) const
			{
				if (world != other.world)
					return true;

				if (isEnd())
					return !other.isEnd();

				return index != other.index;
			}

			EntityIterator& operator++();

		private:
			bool bIsEnd = false;
			size_t index;
			class ECS::World* world;
			bool bIncludePendingDestroy;
		};

		class EntityView
		{
		public:
			EntityView(const EntityIterator& first, const EntityIterator& last)
				: firstItr(first), lastItr(last)
			{
				if (firstItr.get() == nullptr || (firstItr.get()->isPendingDestroy() && !firstItr.includePendingDestroy()))
				{
					++firstItr;
				}
			}

			EntityIterator begin()
			{
				return firstItr;
			}

			EntityIterator end()
			{
				return lastItr;
			}

		private:
			EntityIterator firstItr;
			EntityIterator lastItr;
		};

		template<typename... Types>
		class EntityComponentIterator
		{
		public:
			EntityComponentIterator(class World* world, size_t index, bool bIsEnd, bool bIncludePendingDestroy)
				: bIsEnd(bIsEnd), index(index), world(world), bIncludePendingDestroy(bIncludePendingDestroy)
			{
				if (index >= world->getCount())
					this->bIsEnd = true;
			}

			size_t getIndex() const
			{
				return index;
			}

			bool isEnd() const
			{
				return bIsEnd || index >= world->getCount();
			}

			bool includePendingDestroy() const
			{
				return bIncludePendingDestroy;
			}

			World* getWorld() const
			{
				return world;
			}

			Entity* get() const
			{
				if (isEnd())
					return nullptr;

				return world->getByIndex(index);
			}

			Entity* operator*() const
			{
				return get();
			}

			bool operator==(const EntityComponentIterator<Types...>& other) const
			{
				if (world != other.world)
					return false;

				if (isEnd())
					return other.isEnd();

				return index == other.index;
			}

			bool operator!=(const EntityComponentIterator<Types...>& other) const
			{
				if (world != other.world)
					return true;

				if (isEnd())
					return !other.isEnd();

				return index != other.index;
			}

			EntityComponentIterator<Types...>& operator++()
			{
				++index;
				while (index < world->getCount() && (get() == nullptr || !get()->has<Types...>() || (get()->isPendingDestroy() && !bIncludePendingDestroy)))
				{
					++index;
				}

				if (index >= world->getCount())
					bIsEnd = true;

				return *this;
			}

		private:
			bool bIsEnd = false;
			size_t index;
			class ECS::World* world;
			bool bIncludePendingDestroy;
		};

		template<typename... Types>
		class EntityComponentView
		{
		public:
			EntityComponentView(const EntityComponentIterator<Types...>& first, const EntityComponentIterator<Types...>& last)
				: firstItr(first), lastItr(last)
			{
				if (firstItr.get() == nullptr || (firstItr.get()->isPendingDestroy() && !firstItr.includePendingDestroy())
					|| !firstItr.get()->has<Types...>())
				{
					++firstItr;
				}
			}

			EntityComponentIterator<Types...> begin()
			{
				return firstItr;
			}

			EntityComponentIterator<Types...> end()
			{
				return lastItr;
			}

		private:
			EntityComponentIterator<Types...> firstItr;
			EntityComponentIterator<Types...> lastItr;
		};
	}

	/**
	 * The world creates, destroys, and manages entities. The lifetime of entities and _registered_ systems are handled by the world
	 * (don't delete a system without unregistering it from the world first!), while event subscribers have their own lifetimes
	 * (the world doesn't delete them automatically when the world is deleted).
	 *
	 * This is just a common interface, the actual world implementation is ECS::Internal::WorldImpl. Don't use it directly.
	 * Create worlds using ECS::World::createWorld();
	 */
	class World
	{
	public:
		/**
		 * Use this function to construct the world with a custom allocator.
		 */
		template<typename Allocator = std::allocator<Entity>>
		static World* createWorld(Allocator alloc)
		{
			using WorldTemplate = Internal::WorldImpl<Allocator>;
			using WorldAllocator = std::allocator_traits<Allocator>::template rebind_alloc<WorldTemplate>;
			
			WorldAllocator worldAlloc(alloc);
			WorldTemplate* world = std::allocator_traits<WorldAllocator>::allocate(worldAlloc, 1);
			std::allocator_traits<WorldAllocator>::construct(worldAlloc, world, alloc);

			return world;
		}

		/**
		 * Use this function to construct the world with the default allocator.
		 */
		static World* createWorld()
		{
			return createWorld(std::allocator<Entity>());
		}

		virtual void destroyWorld() = 0;

		/**
		 * Destroying the world will emit OnEntityDestroyed events and call EntitySystem::unconfigure() as appropriate.
		 *
		 * Use World::destroyWorld to destroy and deallocate the world, do not manually delete the world!
		 */
		virtual ~World() {}

		/**
		 * Create a new entity. This will emit the OnEntityCreated event.
		 */
		virtual Entity* create() = 0;

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
		 *
		 * A warning: Do not set immediate to true if you are currently iterating through entities!
		 */
		virtual void destroy(Entity* ent, bool immediate = false) = 0;

		/**
		 * Delete all entities in the pending destroy queue. Returns true if any entities were cleaned up,
		 * false if there were no entities to clean up.
		 */
		virtual bool cleanup() = 0;

		/**
		 * Reset the world, destroying all entities. Entity ids will be reset as well.
		 */
		virtual void reset() = 0;

		/**
		 * Register a system. The world will manage the memory of the system unless you unregister the system.
		 */
		virtual void registerSystem(EntitySystem* system) = 0;

		/**
		 * Unregister a system.
		 */
		virtual void unregisterSystem(EntitySystem* system) = 0;

		/**
		 * Subscribe to an event.
		 */
		template<typename T>
		void subscribe(EventSubscriber<T>* subscriber)
		{
			subscribeInternal(subscriber, typeid(T));
		}

		/**
		 * Unsubscribe from an event.
		 */
		template<typename T>
		void unsubscribe(EventSubscriber<T>* subscriber)
		{
			unsubscribeInternal(subscriber, typeid(T));
		}

		/**
		 * Unsubscribe from all events. Don't be afraid of the void pointer, just pass in your subscriber as normal.
		 */
		virtual void unsubscribeAll(void* subscriber) = 0;

		/**
		 * Emit an event. This will do nothing if there are no subscribers for the event type.
		 */
		template<typename T>
		void emit(const T& event)
		{
			emitInternal(typeid(T), [&, this](Internal::BaseEventSubscriber* base) {
				auto* sub = reinterpret_cast<EventSubscriber<T>*>(base);
				sub->receive(this, event);
			});
		}

		/**
		 * Run a function on each entity with a specific set of components. This is useful for implementing an EntitySystem.
		 *
		 * If you want to include entities that are pending destruction, set includePendingDestroy to true.
		 */
		template<typename... Types>
		void each(std::function<void(Entity*, ComponentHandle<Types>...)> viewFunc, bool bIncludePendingDestroy = false)
		{
			for (auto* ent : each<Types...>(bIncludePendingDestroy))
			{
				viewFunc(ent, ent->get<Types>()...);
			}
		}

		/**
		* Run a function on all entities.
		*/
		void all(std::function<void(Entity*)> viewFunc, bool bIncludePendingDestroy = false)
		{
			for (auto* ent : all(bIncludePendingDestroy))
			{
				viewFunc(ent);
			}
		}

		/**
		 * Get a view for entities with a specific set of components. The list of entities is calculated on the fly, so this method itself
		 * has little overhead. This is mostly useful with a range based for loop.
		 */
		template<typename... Types>
		Internal::EntityComponentView<Types...> each(bool bIncludePendingDestroy = false)
		{
			Internal::EntityComponentIterator<Types...> first(this, 0, false, bIncludePendingDestroy);
			Internal::EntityComponentIterator<Types...> last(this, getCount(), true, bIncludePendingDestroy);
			return Internal::EntityComponentView<Types...>(first, last);
		}

		Internal::EntityView all(bool bIncludePendingDestroy = false)
		{
			Internal::EntityIterator first(this, 0, false, bIncludePendingDestroy);
			Internal::EntityIterator last(this, getCount(), true, bIncludePendingDestroy);
			return Internal::EntityView(first, last);
		}

		virtual size_t getCount() const = 0;

		virtual Entity* getByIndex(size_t idx) const = 0;

		/**
		 * Get an entity by an id. This is a slow process.
		 */
		virtual Entity* getById(size_t id) const = 0;

		/**
		 * Tick the world. See the definition for ECS_TICK_TYPE at the top of this file for more information on
		 * passing data through tick().
		 */
#ifdef ECS_TICK_TYPE_VOID
		virtual void tick() = 0;
#else
		virtual void tick(ECS_TICK_TYPE data) = 0;
#endif

	protected:
		virtual void subscribeInternal(Internal::BaseEventSubscriber* subscriber, const std::type_info& typeInfo) = 0;
		virtual void unsubscribeInternal(Internal::BaseEventSubscriber* subscriber, const std::type_info& typeInfo) = 0;

		// This is a workaround, need to find a better way to do this later.
		virtual void emitInternal(const std::type_info& typeInfo, std::function<void(Internal::BaseEventSubscriber*)> func) = 0;

		static bool isEntityPendingDestroy(Entity* ent)
		{
			return ent->bPendingDestroy;
		}

		static void setEntityPendingDestroy(Entity* ent, bool bPendingDestroy)
		{
			ent->bPendingDestroy = bPendingDestroy;
		}
	};

	namespace Internal
	{
		EntityIterator::EntityIterator(class World* world, size_t index, bool bIsEnd, bool bIncludePendingDestroy)
			: bIsEnd(bIsEnd), index(index), world(world), bIncludePendingDestroy(bIncludePendingDestroy)
		{
			if (index >= world->getCount())
				this->bIsEnd = true;
		}

		bool EntityIterator::isEnd() const
		{
			return bIsEnd || index >= world->getCount();
		}

		Entity* EntityIterator::get() const
		{
			if (isEnd())
				return nullptr;

			return world->getByIndex(index);
		}

		EntityIterator& EntityIterator::operator++()
		{
			++index;
			while (index < world->getCount() && (get() == nullptr || (get()->isPendingDestroy() && !bIncludePendingDestroy)))
			{
				++index;
			}

			if (index >= world->getCount())
				bIsEnd = true;

			return *this;
		}

		/**
		 * Templated implementation for the world. This exists so we can have a common interface to the world (ECS::World) without
		 * having to also template entities, systems, etc. The default template uses standard allocators.
		 */
		template<typename Allocator = std::allocator<Entity>>
		class WorldImpl : public ECS::World
		{
		public:
			using WorldAllocator = std::allocator_traits<Allocator>::template rebind_alloc<WorldImpl<Allocator>>;
			using EntityAllocator = std::allocator_traits<Allocator>::template rebind_alloc<Entity>;
			using SystemAllocator = std::allocator_traits<Allocator>::template rebind_alloc<EntitySystem>;
			using EntityPtrAllocator = std::allocator_traits<Allocator>::template rebind_alloc<Entity*>;
			using SystemPtrAllocator = std::allocator_traits<Allocator>::template rebind_alloc<EntitySystem*>;
			using SubscriberPtrAllocator = std::allocator_traits<Allocator>::template rebind_alloc<BaseEventSubscriber*>;
			using SubscriberPairAllocator = std::allocator_traits<Allocator>::template rebind_alloc<std::pair<const std::type_index, std::vector<BaseEventSubscriber*, SubscriberPtrAllocator>>>;

			WorldImpl(Allocator alloc)
				: entAlloc(alloc), systemAlloc(alloc),
				entities({}, EntityPtrAllocator(alloc)),
				systems({}, SystemPtrAllocator(alloc)),
				subscribers({}, 0, std::hash<std::type_index>(), std::equal_to<std::type_index>(), SubscriberPairAllocator(alloc))
			{
			}

			virtual ~WorldImpl()
			{
				for (auto* ent : entities)
				{
					if (!isEntityPendingDestroy(ent))
					{
						setEntityPendingDestroy(ent, true);
						emit<Events::OnEntityDestroyed>({ ent });
					}

					std::allocator_traits<EntityAllocator>::destroy(entAlloc, ent);
					std::allocator_traits<EntityAllocator>::deallocate(entAlloc, ent, 1);
				}

				for (auto* system : systems)
				{
					system->unconfigure(this);
					std::allocator_traits<SystemAllocator>::destroy(systemAlloc, system);
					std::allocator_traits<SystemAllocator>::deallocate(systemAlloc, system, 1);
				}
			}

			virtual void destroyWorld() override
			{
				WorldAllocator alloc(entAlloc);
				std::allocator_traits<WorldAllocator>::destroy(alloc, this);
				std::allocator_traits<WorldAllocator>::deallocate(alloc, this, 1);
			}

			virtual Entity* create() override
			{
				++lastEntityId;
				Entity* ent = std::allocator_traits<EntityAllocator>::allocate(entAlloc, 1);
				std::allocator_traits<EntityAllocator>::construct(entAlloc, ent, this, lastEntityId);
				entities.push_back(ent);

				emit<Events::OnEntityCreated>({ ent });

				return ent;
			}

			virtual void destroy(Entity* ent, bool immediate = false) override
			{
				if (ent == nullptr)
					return;

				if (ent->isPendingDestroy())
				{
					if (immediate)
					{
						entities.erase(std::remove(entities.begin(), entities.end(), ent), entities.end());
						std::allocator_traits<EntityAllocator>::destroy(entAlloc, ent);
						std::allocator_traits<EntityAllocator>::deallocate(entAlloc, ent, 1);
					}

					return;
				}

				setEntityPendingDestroy(ent, true);

				emit<Events::OnEntityDestroyed>({ ent });

				if (immediate)
				{
					entities.erase(std::remove(entities.begin(), entities.end(), ent), entities.end());
					std::allocator_traits<EntityAllocator>::destroy(entAlloc, ent);
					std::allocator_traits<EntityAllocator>::deallocate(entAlloc, ent, 1);
				}
			}

			virtual bool cleanup() override
			{
				size_t count = 0;
				entities.erase(std::remove_if(entities.begin(), entities.end(), [&, this](auto* ent) {
					if (ent->isPendingDestroy())
					{
						std::allocator_traits<EntityAllocator>::destroy(entAlloc, ent);
						std::allocator_traits<EntityAllocator>::deallocate(entAlloc, ent, 1);
						++count;
						return true;
					}

					return false;
				}), entities.end());

				return count > 0;
			}

			virtual void reset() override
			{
				for (auto* ent : entities)
				{
					if (!isEntityPendingDestroy(ent))
					{
						setEntityPendingDestroy(ent, true);
						emit<Events::OnEntityDestroyed>({ ent });
					}
					std::allocator_traits<EntityAllocator>::destroy(entAlloc, ent);
					std::allocator_traits<EntityAllocator>::deallocate(entAlloc, ent, 1);
				}

				entities.clear();
				lastEntityId = 0;
			}

			virtual void registerSystem(EntitySystem* system) override
			{
				systems.push_back(system);
				system->configure(this);
			}

			void unregisterSystem(EntitySystem* system) override
			{
				systems.erase(std::remove(systems.begin(), systems.end(), system), systems.end());
				system->unconfigure(this);
			}

			virtual void unsubscribeAll(void* subscriber) override
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

			virtual size_t getCount() const override
			{
				return entities.size();
			}

			virtual Entity* getByIndex(size_t idx) const override
			{
				if (idx >= getCount())
					return nullptr;

				return entities[idx];
			}

			virtual Entity* getById(size_t id) const override
			{
				if (id == Entity::InvalidEntityId || id > lastEntityId)
					return nullptr;

				// We should likely store entities in a map of id -> entity so that this is faster.
				for (auto* ent : entities)
				{
					if (ent->getEntityId() == id)
						return ent;
				}

				return nullptr;
			}

#ifdef ECS_TICK_TYPE_VOID
			virtual void tick() override
#else
			virtual void tick(ECS_TICK_TYPE data) override
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

			EntityAllocator& getPrimaryAllocator()
			{
				return entAlloc;
			}

		protected:
			virtual void subscribeInternal(BaseEventSubscriber* subscriber, const std::type_info& typeInfo) override
			{
				auto found = subscribers.find(std::type_index(typeInfo));
				if (found == subscribers.end())
				{
					std::vector<BaseEventSubscriber*, SubscriberPtrAllocator> subList;
					subList.push_back(subscriber);

					subscribers.insert({ std::type_index(typeInfo), subList });
				}
				else
				{
					found->second.push_back(subscriber);
				}
			}

			virtual void unsubscribeInternal(Internal::BaseEventSubscriber* subscriber, const std::type_info& typeInfo) override
			{
				auto found = subscribers.find(std::type_index(typeInfo));
				if (found != subscribers.end())
				{
					found->second.erase(std::remove(found->second.begin(), found->second.end(), subscriber), found->second.end());
					if (found->second.size() == 0)
					{
						subscribers.erase(found);
					}
				}
			}

			virtual void emitInternal(const std::type_info& typeInfo, std::function<void(Internal::BaseEventSubscriber*)> func) override
			{
				auto found = subscribers.find(std::type_index(typeInfo));
				if (found != subscribers.end())
				{
					for (auto* base : found->second)
					{
						func(base);
					}
				}
			}

		private:
			EntityAllocator entAlloc;
			SystemAllocator systemAlloc;

			std::vector<Entity*, EntityPtrAllocator> entities;
			std::vector<EntitySystem*, SystemPtrAllocator> systems;
			std::unordered_map<std::type_index,
				std::vector<Internal::BaseEventSubscriber*>,
				std::hash<std::type_index>,
				std::equal_to<std::type_index>,
				SubscriberPairAllocator> subscribers;

			size_t lastEntityId = 0;
		};
	}
}
