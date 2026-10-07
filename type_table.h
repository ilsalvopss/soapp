//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_TYPE_TABLE_H
#define SOAPP_TYPE_TABLE_H

#include "builtin_types.h"
#include "type_ids.h"
#include "xml.h"
#include "xsd_types.h"

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace soapp::wsdl {

class TypeTable {
public:
    using Definition = std::variant<
        std::monostate,
        xsd::BuiltinType,
        xsd::SimpleParsedType,
        xsd::ComplexParsedType
    >;

    class Type {
        std::optional<xml::qname> name_;
        TypeRef id_;
        Definition definition_;

        friend class TypeTable;

    public:
        Type(std::optional<xml::qname> name, TypeRef id, Definition definition);

        [[nodiscard]] TypeRef id() const noexcept;

        [[nodiscard]] const std::optional<xml::qname>& name() const noexcept;

        [[nodiscard]] const Definition& definition() const noexcept;

        [[nodiscard]] Definition& definition() noexcept;

        [[nodiscard]] bool defined() const noexcept;
    };

    TypeTable();

    // Reserve a named type before parsing its body.
    [[nodiscard]] TypeRef declare(const xml::qname& name);

    [[nodiscard]] TypeRef add_anonymous();

    void define(TypeRef id, Definition&& definition);

    [[nodiscard]] std::optional<TypeRef> find(const xml::qname& name) const noexcept;

    [[nodiscard]] TypeRef resolve(const xml::qname& name) const;

    [[nodiscard]] const Type& get(TypeRef id) const;

    [[nodiscard]] std::size_t size() const noexcept;

    void for_each(const std::function<void(const Type&)>& callback) const;

private:
    [[nodiscard]] Type& get(TypeRef id);

    [[nodiscard]] TypeRef add_builtin(std::string_view local_name);

    TypeRef next_id_ = 0;
    std::vector<Type> types_;
    std::unordered_map<xml::qname, TypeRef, xml::qname::hash> names_;
};

}

#endif //SOAPP_TYPE_TABLE_H
