#include <flecs.h>
#include <iostream>
struct ProbeValue { int value; };
int main() {
    flecs::world world;
    auto entity = world.entity().set<ProbeValue>({42});
    const auto* value = entity.try_get<ProbeValue>();
    if (!value || value->value != 42) { std::cerr << "Flecs component round trip failed\n"; return 1; }
    entity.destruct();
    if (entity.is_alive()) { std::cerr << "Flecs entity still alive after destruction\n"; return 2; }
    std::cout << "Flecs isolated world passed\n";
}
