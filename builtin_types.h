//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_BUILTIN_TYPES_H
#define SOAPP_BUILTIN_TYPES_H

#include <array>
#include <string_view>

namespace soapp::xsd {

class BuiltinType {};

// This is intentionally the small, commonly encountered subset for now.  It
// can grow as the code generator acquires mappings for more XSD datatypes.
inline constexpr std::array<std::string_view, 11> builtinTypes = {
    "string",
    "boolean",
    "decimal",
    "float",
    "double",
    "integer",
    "int",
    "long",
    "short",
    "byte",
    "dateTime",
};

}

#endif //SOAPP_BUILTIN_TYPES_H
