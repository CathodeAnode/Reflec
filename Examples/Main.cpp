#include <iostream>
#include <string>
import Reflec;

struct Vector3 {
    float x{}, y{}, z{};
};

class Player {
public:
    std::string name;
    Vector3 position;
    Vector3 velocity;
    int health = 100;
};

int main() {
    Reflec::World world;
    auto player = world.Create(Player{
        "Ada",
        {0, 0, 0},
        {1, 2, 3}
    });
    world.Each<^^Player::position, ^^Player::velocity>(
        [](Reflec::Entity entity, Vector3& position, const Vector3& velocity) {
            (void)entity;
            position.x += velocity.x;
            position.y += velocity.y;
            position.z += velocity.z;
        }
    );
    auto& position = world.Get<^^Player::position>(player);
    std::cout << world.Get<^^Player::name>(player) << ": "
              << position.x << ", " << position.y << ", " << position.z << '\n';
    world.Destroy(player);
}
