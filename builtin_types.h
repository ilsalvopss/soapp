//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_BUILTIN_TYPES_H
#define SOAPP_BUILTIN_TYPES_H

#include <array>
#include <string_view>

namespace soapp::xsd {

class BuiltinType {
public:
    enum class Kind {
        Unimplemented,
        AnyType,
        AnySimpleType,
        String,
        Boolean,
        Decimal,
        Float,
        Double,
        Duration,
        DateTime,
        Time,
        Date,
        Long,
        Int,
        Short,
        Byte,
        NonNegativeInteger,
        UnsignedLong,
        UnsignedInt,
        UnsignedShort,
        UnsignedByte,
        PositiveInteger,
    };

    constexpr explicit BuiltinType(const Kind kind) noexcept : kind_{kind} {}

    [[nodiscard]] constexpr Kind kind() const noexcept {
        return kind_;
    }

private:
    Kind kind_;
};

using BuiltinEntry = std::pair<std::string_view, BuiltinType::Kind>;

// This is not so small anymore. Implementing all these types is a lot of work,
// so for now the unsupported ones are still represented as builtins with the
// Unimplemented kind.
inline constexpr auto builtin_types = std::to_array<BuiltinEntry>({
    {"anyType", BuiltinType::Kind::AnyType},
    {"anySimpleType", BuiltinType::Kind::AnySimpleType},
    {"string", BuiltinType::Kind::String},
    {"normalizedString", BuiltinType::Kind::Unimplemented},
    {"token", BuiltinType::Kind::Unimplemented},
    {"language", BuiltinType::Kind::Unimplemented},
    {"Name", BuiltinType::Kind::Unimplemented},
    {"NCName", BuiltinType::Kind::Unimplemented},
    {"ID", BuiltinType::Kind::Unimplemented},
    {"IDREF", BuiltinType::Kind::Unimplemented},
    {"IDREFS", BuiltinType::Kind::Unimplemented},
    {"ENTITY", BuiltinType::Kind::Unimplemented},
    {"ENTITIES", BuiltinType::Kind::Unimplemented},
    {"NMTOKEN", BuiltinType::Kind::Unimplemented},
    {"NMTOKENS", BuiltinType::Kind::Unimplemented},
    {"boolean", BuiltinType::Kind::Boolean},
    {"base64Binary", BuiltinType::Kind::Unimplemented},
    {"hexBinary", BuiltinType::Kind::Unimplemented},
    {"decimal", BuiltinType::Kind::Decimal},
    {"float", BuiltinType::Kind::Float},
    {"double", BuiltinType::Kind::Double},
    {"duration", BuiltinType::Kind::Duration},
    {"dateTime", BuiltinType::Kind::DateTime},
    {"time", BuiltinType::Kind::Time},
    {"date", BuiltinType::Kind::Date},
    {"gYearMonth", BuiltinType::Kind::Unimplemented},
    {"gYear", BuiltinType::Kind::Unimplemented},
    {"gMonthDay", BuiltinType::Kind::Unimplemented},
    {"gDay", BuiltinType::Kind::Unimplemented},
    {"gMonth", BuiltinType::Kind::Unimplemented},
    {"anyURI", BuiltinType::Kind::Unimplemented},
    {"QName", BuiltinType::Kind::Unimplemented},
    {"NOTATION", BuiltinType::Kind::Unimplemented},
    {"integer", BuiltinType::Kind::Unimplemented},
    {"nonPositiveInteger", BuiltinType::Kind::Unimplemented},
    {"negativeInteger", BuiltinType::Kind::Unimplemented},
    {"long", BuiltinType::Kind::Long},
    {"int", BuiltinType::Kind::Int},
    {"short", BuiltinType::Kind::Short},
    {"byte", BuiltinType::Kind::Byte},
    {"nonNegativeInteger", BuiltinType::Kind::NonNegativeInteger},
    {"unsignedLong", BuiltinType::Kind::UnsignedLong},
    {"unsignedInt", BuiltinType::Kind::UnsignedInt},
    {"unsignedShort", BuiltinType::Kind::UnsignedShort},
    {"unsignedByte", BuiltinType::Kind::UnsignedByte},
    {"positiveInteger", BuiltinType::Kind::PositiveInteger},
});

static inline BuiltinType make_builtin_type(const std::string_view local_name) {
    for (const auto& [name, kind] : builtin_types) {
        if (name == local_name)
            return BuiltinType{ kind };
    }

    return BuiltinType{ BuiltinType::Kind::Unimplemented };
}

}

#endif //SOAPP_BUILTIN_TYPES_H
