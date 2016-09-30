# ECS

This is a simple C++ header-only type-safe entity component system library. It makes heavy use of C++11
constructs, so make sure you have an up to date compiler. It isn't meant to do absolutely everything,
so please feel free to modify it when using. There's a VS2015 solution provided, but it should
compile on any standard compiler with C++11 support.

## Tutorial

This ECS library is based on the [Evolve Your Hierarchy](http://cowboyprogramming.com/2007/01/05/evolve-your-heirachy/) article. If you haven't read it, please do or else things won't make much sense. This is a data-driven entity component system library, and to know how to work with it you need to know what that entails (this is _not_ the same as Unity's components so don't expect it to be).

### Your first components

Components in ECS can be any data type, but generally they'll be a struct containing some plain old data.
For now, let's define two components:

    struct Position
    {
        Position(float x, float y) : x(x), y(y) {}
        Position() : x(0.f), y(0.f) {}
    
        float x;
        float y;
    }
    
    struct Rotation
    {
        Rotation(float angle) : angle(angle) {}
        Rotation() : angle(0) {}
        float angle;
    }

This isn't the most realistic example - normally you'd just have a single transform component for a game but this should
help illustrate some functionality later. Also note that we don't have to do anything special for these structs to
act as components, though there is the requirement for at least a default constructor.

### Create a system

Now we need some logic to act on that data. Let's make a simple gravity system:

    class GravitySystem : public EntitySystem
    {
    public:
        GravitySystem(float amount)
        {
            gravityAmount = amount;
        }
        
        virtual ~GravitySystem() {}
        
        virtual void tick(World* world, float deltaTime) override
        {
            world->each<Position>([&](Entity* ent, ComponentHandle<Position> position) {
                position->y += gravityAmount * deltaTime;
            });
        }
        
    private:
        float gravityAmount;
    }

This is a pretty standard class definition. We subclass `EntitySystem` and implement the `tick()` method. The world
provides the `each` method, which takes a list of component types and runs a given function (in this case a
lambda) on every entity that has those components. Note that the lambda is passed a `ComponentHandle`, and not the
component itself.

### Create the world

Next, inside a `main()` function somewhere, you can add the following code to create the world, setup the system, and
create an entity:

    World world;
    world.registerSystem(new GravitySystem(-9.8f));
    
    Entity* ent = world.create();
    ent->assign<Position>(0.f, 0.f); // assign() takes arguments and passes them to the constructor
    ent->assign<Rotation>(35.f);

Now you can call the tick function on the world in order to tick all systems that have been registered with the world:

    world.tick(deltaTime);

### Working with components

You may retrieve a component handle (for example, to print out the position of your entity) with `get`:

    ComponentHandle<Position> pos = ent->get<Position>();
    std::cout << "My position is " << pos->x << ", " << pos->y << std::endl;

If an entity doesn't have a component and you try to retrieve that type from it, `get` will return an invalid
component handle:

    ComponentHandle<Position> pos = otherEnt->get<Position>(); // assume otherEnt doesn't have a Position component
    pos.isValid(); // returns false, note the . instead of the ->

### Events

For communication between systems (and with other objects outside of ECS) there is an event system. Events can be any
type of object, and you can subscribe to specific types of events by subclassing `EventSubscriber` and calling
`subscribe` on the world:

    struct MyEvent
    {
        int foo;
        float bar;
    }
    
    class MyEventSubscriber : public EventSubscriber<MyEvent>
    {
    public:
        virtual ~MyEventSubscriber() {}
        
        virtual void receive(const MyEvent& event) override
        {
            std::cout << "MyEvent was emitted!" << std::endl;
        }
    }
    
    // ...
    
    MyEventSubscriber* mySubscriber = new MyEventSubscriber();
    world.subscribe<MyEvent>(mySubscriber);
    
Then, to emit an event:

    world.emit<MyEvent>({ 123, 45.67f }); // you can use initializer syntax if you want, this sets foo = 123 and bar = 45.67f

Make sure you call `unsubscribe` or `unsubscribeAll` on your subscriber before deleting it, or else emitting the event
may cause a crash or other undesired behavior.

### Systems and events

Often, your event subscribers will also be systems. Systems have `configure` and `unconfigure` functions that are called
when they are added to/removed from the world and which you may use to subscribe and unsubscribe from events:

    class MySystem : public EntitySystem, public EventSubscriber<MyEvent>
    {
        // ...
        
        virtual void configure(World* world) override
        {
            world->subscribe<MyEvent>(this);
        }
        
        virtual void unconfigure(World* world) override
        {
            world->unsubscribeAll(this);
            // You may also unsubscribe from specific events with world->unsubscribe<MyEvent>(this), but
            // when unconfigure is called you usually want to unsubscribe from all events.
        }
        
        // ...
    }

### Built-in events

There are a handful of built-in events. Here is the list:

  * `OnEntityCreated` - called when an entity has been created.
  * `OnEntityDestroyed` - called when an entity is being destroyed (including when a world is beind deleted).
  * `OnComponentAssigned` - called when a component is assigned to an entity. This might mean the component is new to the entity, or there's just a new assignment of the component to that entity overwriting an old one.

There is no `OnComponentRemoved` event as of this time.