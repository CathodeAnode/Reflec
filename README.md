# Reflec

Reflec is a C++26 entity-component system built with static reflection and C++
modules. Define an ordinary class or struct, and its public data members become
separate components—no registration, macros, or base classes required.

## Build and run the example

You must use **GCC 16 or newer** to build Reflec, along with **CMake 3.30 or
newer** and **Ninja**. Upstream Clang does not yet implement
[C++26 static reflection](https://clang.llvm.org/cxx_status.html#cxx26), which
Reflec requires.

The Reflec CMake target enables reflection with `-freflection` automatically.

```sh
git clone https://github.com/CathodeAnode/Reflec.git
cd Reflec
cmake -S . -B Build -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_BUILD_TYPE=Release
cmake --build Build
./Build/ReflecExample
```

If GCC 16 is installed under a versioned name, replace `g++` with `g++-16` or the
full path to that compiler. Use a fresh build directory when switching compilers.

The [included example](Examples/Main.cpp) creates a player, updates its position
from its velocity, and prints:

```text
Ada: 1, 2, 3
```

## Define an entity and use its components

```cpp
import Reflec;

struct Vector3 {
    float x{}, y{}, z{};
};

class Player {
public:
    Vector3 position;
    Vector3 velocity;
    int health = 100;
};

int main() {
    Reflec::World world;
    auto player = world.Create(Player{{0, 0, 0}, {1, 2, 3}});

    // Access a component by its declaring member.
    world.Get<^^Player::health>(player) -= 10;

    // Run a system over entities that have both components.
    world.Each<^^Player::position, ^^Player::velocity>(
        [](Reflec::Entity, Vector3& position, const Vector3& velocity) {
            position.x += velocity.x;
            position.y += velocity.y;
            position.z += velocity.z;
        }
    );

    world.Destroy(player);
}
```

`^^Player::position` is a compile-time reflection of that member. Components are
identified by their declaring member, so `position` and `velocity` remain separate
even though both are `Vector3`. A member in another class is a different component.

## API at a glance

| Operation | Purpose |
| --- | --- |
| `world.Create(object)` | Create an entity from an object's fields. |
| `world.Get<^^Type::member>(entity)` | Read or modify a component by reference. |
| `world.Has<^^Type::member>(entity)` | Check whether an entity has a component. |
| `world.Each<^^Type::a, ^^Type::b>(callback)` | Visit entities with all requested components. |
| `world.IsAlive(entity)` | Check whether a handle still refers to a live entity in this world. |
| `world.Count()` | Get the number of live entities. |
| `world.Destroy(entity)` | Remove an entity and its components. |

## Usage notes

- `Create` copies fields from lvalues and moves them from rvalues. The supplied
  object does not become a live view of the entity. Strings, containers, and
  move-only values can be components; nested structs remain whole components.
- Entity descriptions need public, mutable value fields and no inheritance.
  References, C arrays, bit fields, unions, and const/volatile fields are not
  supported. Methods and static members do not become components.
- Component references remain valid when other entities are created, but expire
  when their entity or world is destroyed. A const world's `Get` returns a const
  reference. Invalid handles or missing components cause `Get` to throw
  `std::out_of_range`; destroying an invalid handle also throws.
- Systems can modify component values, but must defer entity creation and
  destruction until after `Each` returns. Structural changes inside a callback
  throw `std::logic_error`. World operations are single-threaded.
