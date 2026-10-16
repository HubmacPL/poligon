#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "poligon/ecs/ecs.h"

namespace poligon {

// Writes the complete game state of a world as JSON text.
//
// Included: every living entity that is not a definition (component, system, module or
// anything inside one), with its id, generation, name and all components, plus the values of
// all singleton components.
//
// Numbers are exact: float is printed with 9 significant digits and double with 17, which is
// enough to tell any two different values apart. Two dumps are byte-identical exactly when the
// two states are identical.
//
// A component whose members were not registered cannot be read. It is written as
// {"unreflected_bytes": N} and its name is listed under "unreflected_components", so a test
// can require that list to be empty.
//
// Supported member types: bool, char, integers, float, double, flecs::entity, enums, nested
// structs and fixed-size arrays of those. Anything else (strings, vectors) ends the process
// through FatalError naming the component; extend the dump when such a member is needed.
std::string DumpWorldStateToJson(const flecs::world& world);

// Same, written to a file. Returns false if the file cannot be written.
bool SaveWorldStateToJson(const flecs::world& world, const char* path);

// 64-bit FNV-1a hash of DumpWorldStateToJson. Equal hashes mean equal states (up to hash collisions).
std::uint64_t HashWorldState(const flecs::world& world);

// Writes the state hash to a text file every `interval` ticks, one line per entry:
//   <ticks completed> <hash as 16 hex digits>
// Two runs that should be identical can then be compared line by line, and the first
// differing line tells in which stretch of ticks they diverged (tools/state_compare does this).
class StateHashLog {
public:
    StateHashLog() = default;
    ~StateHashLog();
    StateHashLog(const StateHashLog&) = delete;
    StateHashLog& operator=(const StateHashLog&) = delete;

    // interval must be at least 1. Returns false if the file cannot be created.
    bool Open(const char* path, std::int64_t interval);
    // Call after every tick with the number of ticks completed so far (1 after the first tick).
    // Does nothing when the log is not open.
    void AfterTick(const flecs::world& world, std::int64_t ticks_completed);
    // Writes the final state (unless AfterTick has just written it) and closes the file.
    // Returns false if anything could not be written. Does nothing when the log is not open.
    bool Close(const flecs::world& world, std::int64_t ticks_completed);

private:
    void WriteLine(const flecs::world& world, std::int64_t ticks_completed);

    std::FILE* file_ = nullptr;
    std::int64_t interval_ = 1;
    std::int64_t last_written_ = -1;
    bool failed_ = false;
};

} // namespace poligon
