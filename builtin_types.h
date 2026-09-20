//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_BUILTIN_TYPES_H
#define SOAPP_BUILTIN_TYPES_H

#include <array>
#include <string_view>

namespace soapp::xsd {

class BuiltinType {};

// This is not so small anymore.
// Implementing all these types is a lot of work,
// so for now we just declare them as built-in types and don't implement them.
inline constexpr auto builtinTypes = std::to_array<std::string_view>({
    "anyType",
    "anySimpleType",
    "string",
    "normalizedString",
    "token",
    "language",
    "Name",
    "NCName",
    "ID",
    "IDREF",
    "IDREFS",
    "ENTITY",
    "ENTITIES",
    "NMTOKEN",
    "NMTOKENS",
    "boolean",
    "base64Binary",
    "hexBinary",
    "decimal",
    "float",
    "double",
    "duration",
    "dateTime",
    "time",
    "date",
    "gYearMonth",
    "gYear",
    "gMonthDay",
    "gDay",
    "gMonth",
    "anyURI",
    "QName",
    "NOTATION",
    "integer",
    "nonPositiveInteger",
    "negativeInteger",
    "int",
    "long",
    "short",
    "byte",
    "nonNegativeInteger",
    "unsignedLong",
    "unsignedInt",
    "unsignedShort",
    "unsignedByte",
    "positiveInteger",
});

}

#endif //SOAPP_BUILTIN_TYPES_H
