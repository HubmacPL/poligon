#include "poligon/state_dump.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "poligon/fatal_error.h"

namespace poligon {

namespace {

void AppendEscaped(std::string& out, const char* text) {
    out += '"';
    for (const char* c = text; *c != '\0'; ++c) {
        const unsigned char byte = static_cast<unsigned char>(*c);
        if (byte == '"' || byte == '\\') {
            out += '\\';
            out += *c;
        } else if (byte < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04x", byte);
            out += buffer;
        } else {
            out += *c;
        }
    }
    out += '"';
}

void AppendFloating(std::string& out, double value, int significant_digits) {
    // JSON has no representation for these, so they are written as strings.
    if (std::isnan(value)) {
        out += "\"nan\"";
        return;
    }
    if (std::isinf(value)) {
        out += value > 0 ? "\"inf\"" : "\"-inf\"";
        return;
    }
    char buffer[40];
    std::snprintf(buffer, sizeof(buffer), "%.*g", significant_digits, value);
    out += buffer;
}

void AppendSigned(std::string& out, std::int64_t value) {
    out += std::to_string(value);
}

void AppendUnsigned(std::string& out, std::uint64_t value) {
    out += std::to_string(value);
}

template <typename T>
T ReadAs(const void* pointer) {
    T value;
    std::memcpy(&value, pointer, sizeof(T));
    return value;
}

// Text form of an id: "Position" for a component, "(ChildOf,parent)" for a pair.
std::string GetIdText(const ecs_world_t* world, ecs_id_t id) {
    char* text = ecs_id_str(world, id);
    std::string result = text != nullptr ? text : "";
    ecs_os_free(text);
    return result;
}

void AppendPrimitive(std::string& out, const ecs_world_t* world, ecs_primitive_kind_t kind, const void* pointer,
                     const std::string& component_name) {
    switch (kind) {
    case EcsBool:
        out += ReadAs<bool>(pointer) ? "true" : "false";
        return;
    case EcsChar:
    case EcsI8:
        AppendSigned(out, ReadAs<std::int8_t>(pointer));
        return;
    case EcsByte:
    case EcsU8:
        AppendUnsigned(out, ReadAs<std::uint8_t>(pointer));
        return;
    case EcsU16:
        AppendUnsigned(out, ReadAs<std::uint16_t>(pointer));
        return;
    case EcsU32:
        AppendUnsigned(out, ReadAs<std::uint32_t>(pointer));
        return;
    case EcsU64:
    case EcsUPtr:
        AppendUnsigned(out, ReadAs<std::uint64_t>(pointer));
        return;
    case EcsI16:
        AppendSigned(out, ReadAs<std::int16_t>(pointer));
        return;
    case EcsI32:
        AppendSigned(out, ReadAs<std::int32_t>(pointer));
        return;
    case EcsI64:
    case EcsIPtr:
        AppendSigned(out, ReadAs<std::int64_t>(pointer));
        return;
    case EcsF32:
        AppendFloating(out, ReadAs<float>(pointer), 9);
        return;
    case EcsF64:
        AppendFloating(out, ReadAs<double>(pointer), 17);
        return;
    case EcsEntity:
    case EcsId:
        // Entities are written as their full 64-bit id (index and generation).
        AppendUnsigned(out, ReadAs<std::uint64_t>(pointer));
        return;
    case EcsString:
        break;
    }
    (void)world;
    const std::string message =
        "DumpWorldStateToJson: component " + component_name + " has a member type the dump does not support yet";
    FatalError(message.c_str());
}

// Writes one value of `type` on a single line. Returns false when the type has no reflection data.
bool AppendValue(std::string& out, const ecs_world_t* world, ecs_entity_t type, const void* pointer,
                 const std::string& component_name) {
    if (const EcsPrimitive* primitive = ecs_get(world, type, EcsPrimitive)) {
        AppendPrimitive(out, world, primitive->kind, pointer, component_name);
        return true;
    }

    if (const EcsEnum* enumeration = ecs_get(world, type, EcsEnum)) {
        return AppendValue(out, world, enumeration->underlying_type, pointer, component_name);
    }

    if (const EcsStruct* structure = ecs_get(world, type, EcsStruct)) {
        const int member_count = ecs_vec_count(&structure->members);
        const ecs_member_t* members = ecs_vec_first_t(&structure->members, ecs_member_t);
        out += '{';
        for (int i = 0; i < member_count; ++i) {
            const ecs_member_t& member = members[i];
            if (i > 0) {
                out += ", ";
            }
            AppendEscaped(out, member.name);
            out += ": ";

            const char* member_pointer = static_cast<const char*>(pointer) + member.offset;
            bool supported = true;
            if (member.count > 1) {
                const ecs_type_info_t* element_info = ecs_get_type_info(world, member.type);
                out += '[';
                for (int element = 0; element < member.count && supported; ++element) {
                    if (element > 0) {
                        out += ", ";
                    }
                    supported = AppendValue(out, world, member.type, member_pointer + element * element_info->size,
                                            component_name);
                }
                out += ']';
            } else {
                supported = AppendValue(out, world, member.type, member_pointer, component_name);
            }
            if (!supported) {
                const std::string message = "DumpWorldStateToJson: member \"" + std::string(member.name) +
                                            "\" of component " + component_name +
                                            " has a type without registered members";
                FatalError(message.c_str());
            }
        }
        out += '}';
        return true;
    }

    if (ecs_has(world, type, EcsType)) {
        // Reflection exists, but of a kind the dump cannot write (array type, vector, opaque).
        const std::string message =
            "DumpWorldStateToJson: component " + component_name + " has a member type the dump does not support yet";
        FatalError(message.c_str());
    }
    return false;
}

// Definitions are part of the program, not of the game state: components (and their members),
// systems, observers, queries, modules and everything flecs creates for itself.
bool IsDefinition(const ecs_world_t* world, ecs_entity_t entity) {
    if (ecs_has_pair(world, entity, ecs_id(EcsPoly), EcsWildcard)) {
        return true;  // system, observer or query
    }
    for (ecs_entity_t current = entity; current != 0; current = ecs_get_parent(world, current)) {
        if (current == EcsFlecs || ecs_has_id(world, current, EcsModule) ||
            ecs_has(world, current, EcsComponent)) {
            return true;
        }
    }
    return false;
}

// True for entities flecs creates for itself (everything inside its own "flecs" scope).
bool IsFlecsInternal(const ecs_world_t* world, ecs_entity_t entity) {
    for (ecs_entity_t current = entity; current != 0; current = ecs_get_parent(world, current)) {
        if (current == EcsFlecs) {
            return true;
        }
    }
    return false;
}

void AddUnreflected(std::vector<std::string>& unreflected, const std::string& name) {
    for (const std::string& existing : unreflected) {
        if (existing == name) {
            return;
        }
    }
    unreflected.push_back(name);
}

// Writes `"name": value` for one component of an entity.
void AppendComponent(std::string& out, const ecs_world_t* world, ecs_entity_t entity, ecs_id_t id,
                     std::vector<std::string>& unreflected) {
    const std::string name = GetIdText(world, id);
    AppendEscaped(out, name.c_str());
    out += ": ";

    const ecs_entity_t type = ecs_get_typeid(world, id);
    const void* value = type != 0 ? ecs_get_id(world, entity, id) : nullptr;
    if (value == nullptr) {
        out += "true";  // a tag or a pair without data
        return;
    }
    if (!AppendValue(out, world, type, value, name)) {
        const ecs_type_info_t* info = ecs_get_type_info(world, type);
        out += "{\"unreflected_bytes\": " + std::to_string(info != nullptr ? info->size : 0) + "}";
        AddUnreflected(unreflected, name);
    }
}

} // namespace

std::string DumpWorldStateToJson(const flecs::world& cpp_world) {
    const ecs_world_t* world = cpp_world.c_ptr();
    std::vector<std::string> unreflected;
    std::string entities_text;
    std::string singletons_text;
    int entity_count = 0;
    int singleton_count = 0;

    const ecs_entities_t all = ecs_get_entities(world);
    for (int index = 0; index < all.alive_count; ++index) {
        const ecs_entity_t entity = all.ids[index];

        if (IsDefinition(world, entity)) {
            // A singleton is a component stored on its own component entity.
            if (ecs_has(world, entity, EcsComponent) && ecs_has_id(world, entity, entity) &&
                !IsFlecsInternal(world, entity)) {
                singletons_text += singleton_count > 0 ? ",\n    " : "\n    ";
                AppendComponent(singletons_text, world, entity, entity, unreflected);
                ++singleton_count;
            }
            continue;
        }

        entities_text += entity_count > 0 ? ",\n    {" : "\n    {";
        // The low 32 bits of an entity id are its index, the high 32 bits its generation:
        // how many times that index has been reused after a deletion.
        entities_text += "\n      \"id\": " + std::to_string(static_cast<std::uint32_t>(entity));
        entities_text += ",\n      \"generation\": " + std::to_string(static_cast<std::uint32_t>(entity >> 32));
        if (const char* name = ecs_get_name(world, entity)) {
            entities_text += ",\n      \"name\": ";
            AppendEscaped(entities_text, name);
        }
        entities_text += ",\n      \"components\": {";

        int component_count = 0;
        const ecs_type_t* type = ecs_get_type(world, entity);
        for (int i = 0; type != nullptr && i < type->count; ++i) {
            const ecs_id_t id = type->array[i];
            // The name is already written above.
            if (ecs_id_is_pair(id) && ECS_PAIR_FIRST(id) == static_cast<std::uint32_t>(ecs_id(EcsIdentifier))) {
                continue;
            }
            entities_text += component_count > 0 ? ",\n        " : "\n        ";
            AppendComponent(entities_text, world, entity, id, unreflected);
            ++component_count;
        }
        entities_text += component_count > 0 ? "\n      }" : "}";
        entities_text += "\n    }";
        ++entity_count;
    }

    std::string out = "{\n  \"entity_count\": " + std::to_string(entity_count) + ",\n  \"entities\": [";
    out += entities_text;
    out += entity_count > 0 ? "\n  ],\n" : "],\n";
    out += "  \"singletons\": {";
    out += singletons_text;
    out += singleton_count > 0 ? "\n  },\n" : "},\n";
    out += "  \"unreflected_components\": [";
    for (size_t i = 0; i < unreflected.size(); ++i) {
        out += i > 0 ? ", " : "";
        AppendEscaped(out, unreflected[i].c_str());
    }
    out += "]\n}\n";
    return out;
}

bool SaveWorldStateToJson(const flecs::world& world, const char* path) {
    const std::string json = DumpWorldStateToJson(world);
    std::FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") != 0 || file == nullptr) {
        return false;
    }
    const size_t written = std::fwrite(json.data(), 1, json.size(), file);
    const bool closed = std::fclose(file) == 0;
    return written == json.size() && closed;
}

std::uint64_t HashWorldState(const flecs::world& world) {
    const std::string json = DumpWorldStateToJson(world);
    std::uint64_t hash = 14695981039346656037ull;
    for (const char c : json) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ull;
    }
    return hash;
}

StateHashLog::~StateHashLog() {
    if (file_ != nullptr) {
        std::fclose(file_);
    }
}

bool StateHashLog::Open(const char* path, std::int64_t interval) {
    if (interval < 1) {
        FatalError("StateHashLog::Open: interval must be at least 1");
    }
    if (file_ != nullptr) {
        FatalError("StateHashLog::Open: the log is already open");
    }
    if (fopen_s(&file_, path, "wb") != 0 || file_ == nullptr) {
        file_ = nullptr;
        return false;
    }
    interval_ = interval;
    last_written_ = -1;
    failed_ = false;
    return true;
}

void StateHashLog::WriteLine(const flecs::world& world, std::int64_t ticks_completed) {
    if (std::fprintf(file_, "%lld %016llx\n", static_cast<long long>(ticks_completed),
                     static_cast<unsigned long long>(HashWorldState(world))) < 0) {
        failed_ = true;
    }
    last_written_ = ticks_completed;
}

void StateHashLog::AfterTick(const flecs::world& world, std::int64_t ticks_completed) {
    if (file_ != nullptr && ticks_completed % interval_ == 0) {
        WriteLine(world, ticks_completed);
    }
}

bool StateHashLog::Close(const flecs::world& world, std::int64_t ticks_completed) {
    if (file_ == nullptr) {
        return true;
    }
    if (last_written_ != ticks_completed) {
        WriteLine(world, ticks_completed);
    }
    if (std::fclose(file_) != 0) {
        failed_ = true;
    }
    file_ = nullptr;
    return !failed_;
}

} // namespace poligon
