#ifndef SOAPP_CPP_BUILTIN_TYPES_H
#define SOAPP_CPP_BUILTIN_TYPES_H

#include <cstdint>
#include <string>

namespace soapp::builtin_types {

template<typename T>
class Type {
    T value;

public:
    Type() = default;
    explicit Type(const T& v) : value(v) {}
    explicit Type(T&& v) : value(std::move(v)) {}

    [[nodiscard]] const T& get() const { return value; }
    [[nodiscard]] T operator*() { return value; }
    [[nodiscard]] const T& operator*() const { return value; }

    void set(const T& v) { value = v; }
    void set(T&& v) { value = std::move(v); }
    Type& operator=(const T& v) { value = v; return *this; }
    Type& operator=(T&& v) { value = std::move(v); return *this; }
};

struct Unimplemented {};

struct String : Type<std::string> {};
struct Boolean : Type<bool> {};
struct Float : Type<float> {};
struct Double : Type<double> {};
struct Long : Type<std::int64_t> {};
struct Int : Type<std::int32_t> {};
struct Short : Type<std::int16_t> {};
struct Byte : Type<std::int8_t> {};
struct UnsignedLong : Type<std::uint64_t> {};
struct UnsignedInt : Type<std::uint32_t> {};
struct UnsignedShort : Type<std::uint16_t> {};
struct UnsignedByte : Type<std::uint8_t> {};

struct AnyType : Unimplemented {};
struct AnySimpleType : Unimplemented {};
struct NormalizedString : Unimplemented {};
struct Token : Unimplemented {};
struct Language : Unimplemented {};
struct Name : Unimplemented {};
struct NCName : Unimplemented {};
struct ID : Unimplemented {};
struct IDREF : Unimplemented {};
struct IDREFS : Unimplemented {};
struct ENTITY : Unimplemented {};
struct ENTITIES : Unimplemented {};
struct NMTOKEN : Unimplemented {};
struct NMTOKENS : Unimplemented {};
struct Base64Binary : Unimplemented {};
struct HexBinary : Unimplemented {};
struct Decimal : Unimplemented {};
struct Duration : Unimplemented {};
struct DateTime : Unimplemented {};
struct Time : Unimplemented {};
struct Date : Unimplemented {};
struct GYearMonth : Unimplemented {};
struct GYear : Unimplemented {};
struct GMonthDay : Unimplemented {};
struct GDay : Unimplemented {};
struct GMonth : Unimplemented {};
struct AnyURI : Unimplemented {};
struct QName : Unimplemented {};
struct NOTATION : Unimplemented {};
struct Integer : Unimplemented {};
struct NonPositiveInteger : Unimplemented {};
struct NegativeInteger : Unimplemented {};
struct NonNegativeInteger : Unimplemented {};
struct PositiveInteger : Unimplemented {};

} // namespace soapp::builtin_types

#endif // SOAPP_CPP_BUILTIN_TYPES_H
