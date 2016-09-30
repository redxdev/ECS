#include <iostream>

#include "Entity.h"

using namespace ECS;

struct Position
{
	Position(float x, float y) : x(x), y(y) {}
	Position() {}

	float x;
	float y;
};

struct Rotation
{
	Rotation(float angle) : angle(angle) {}
	Rotation() {}

	float angle;
};

struct SomeComponent
{
	SomeComponent() {}
};

class TestSystem : public EntitySystem, public EventSubscriber<Events::OnEntityCreated>
{
public:
	virtual ~TestSystem() {}

	virtual void configure(class World* world) override
	{
		world->subscribe<Events::OnEntityCreated>(this);
	}

	virtual void tick(class World* world, float deltaTime) override
	{
		world->each<Position, Rotation>([&](Entity* ent, auto pos, auto rot) {
			pos->x += deltaTime;
			pos->y += deltaTime;
			rot->angle += deltaTime * 2;
		});
	}

	virtual void receive(class World* world, const Events::OnEntityCreated& event) override
	{
		std::cout << "An entity was created!" << std::endl;
	}
};

int main(int argc, char** argv)
{
	std::cout << "EntityComponentSystem Test" << std::endl
		<< "==========================" << std::endl;

	World world;

	world.registerSystem(new TestSystem());

	Entity* ent = world.create();
	auto pos = ent->assign<Position>(0.f, 0.f);
	auto rot = ent->assign<Rotation>(0.f);

	std::cout << "Initial values: position(" << pos->x << ", " << pos->y << "), rotation(" << rot->angle << ")" << std::endl;

	world.tick(10.f);

	std::cout << "After tick(10): position(" << pos->x << ", " << pos->y << "), rotation(" << rot->angle << ")" << std::endl;

	std::cout << "Creating more entities..." << std::endl;

	for (int i = 0; i < 10; ++i)
	{
		ent = world.create();
		ent->assign<SomeComponent>();
	}

	int count = 0;
	std::cout << "Counting entities with SomeComponent..." << std::endl;
	// range based for loop
	for (auto ent : world.each<SomeComponent>())
	{
		++count;
		std::cout << "Found entity #" << ent->getEntityId() << std::endl;
	}
	std::cout << count << " entities have SomeComponent!" << std::endl;

	std::cout << "Press any key to exit..." << std::endl;
	std::getchar();

	return 0;
}