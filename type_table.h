//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_TYPE_TABLE_H
#define SOAPP_TYPE_TABLE_H

#include "builtin_types.h"
#include "xml.h"
#include "xsd_types.h"

#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace soapp::wsdl {

class TypeTable {
public:
    using TypeId = xsd::TypeRef;

    using Definition = std::variant<
        std::monostate,
        xsd::BuiltinType,
        xsd::SimpleParsedType,
        xsd::ComplexParsedType
    >;

    class Type {
        std::optional<xml::qname> name_;
        TypeId id_;
        Definition definition_;

        friend class TypeTable;

    public:
        Type(std::optional<xml::qname> name, TypeId id, Definition definition);

        [[nodiscard]] TypeId id() const noexcept;

        [[nodiscard]] const std::optional<xml::qname>& name() const noexcept;

        [[nodiscard]] const Definition& definition() const noexcept;

        [[nodiscard]] Definition& definition() noexcept;

        [[nodiscard]] bool defined() const noexcept;
    };

    TypeTable();

    // Reserve a named type before parsing its body.
    [[nodiscard]] TypeId declare(const xml::qname& name);

    [[nodiscard]] TypeId add_anonymous();

    void define(TypeId id, Definition&& definition);

    [[nodiscard]] std::optional<TypeId> find(const xml::qname& name) const noexcept;

    [[nodiscard]] TypeId resolve(const xml::qname& name) const;

    [[nodiscard]] const Type& get(TypeId id) const;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    [[nodiscard]] Type& get(TypeId id);

    [[nodiscard]] TypeId add_builtin(std::string_view local_name);

    TypeId next_id_ = 0;
    std::vector<Type> types_;
    std::unordered_map<xml::qname, TypeId, xml::qname::hash> names_;
};

}

#endif //SOAPP_TYPE_TABLE_H
