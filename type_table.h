//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_TYPE_TABLE_H
#define SOAPP_TYPE_TABLE_H

#include "builtin_types.h"
#include "xml.h"
#include "xsd_types.h"

#include <fmt/format.h>

#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
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
        Type(std::optional<xml::qname> name, const TypeId id, Definition definition) :
            name_{std::move(name)}, id_{id}, definition_{std::move(definition)} {}

        [[nodiscard]] TypeId id() const noexcept { return id_; }

        [[nodiscard]] const std::optional<xml::qname>& name() const noexcept { return name_; }

        [[nodiscard]] const Definition& definition() const noexcept { return definition_; }

        [[nodiscard]] Definition& definition() noexcept { return definition_; }

        [[nodiscard]] bool defined() const noexcept { return !std::holds_alternative<std::monostate>(definition_); }
    };

    TypeTable() {
        for (const auto name : xsd::builtinTypes)
            (void)add_builtin(name);
    }

    // Reserve a named type before parsing its body.
    [[nodiscard]] TypeId declare(const xml::qname& name) {
        if (names_.contains(name))
            throw std::runtime_error{
                fmt::format("Duplicate XSD type declaration: {}:{}", name.ns_uri(), name.local_name())
            };

        const auto id = next_id_++;
        names_.emplace(name, id);
        types_.emplace_back(name, id, std::monostate{});
        return id;
    }

    [[nodiscard]] TypeId add_anonymous() {
        const auto id = next_id_++;
        types_.emplace_back(std::nullopt, id, std::monostate{});
        return id;
    }

    void define(const TypeId id, Definition&& definition) {
        auto& type = get(id);
        if (type.defined())
            throw std::runtime_error{ fmt::format("Type {} is already defined", id) };

        type.definition_ = std::move(definition);
    }

    [[nodiscard]] std::optional<TypeId> find(const xml::qname& name) const noexcept {
        if (const auto it = names_.find(name); it != names_.end())
            return it->second;

        return std::nullopt;
    }

    [[nodiscard]] TypeId resolve(const xml::qname& name) const {
        if (const auto id = find(name))
            return *id;

        throw std::runtime_error{
            fmt::format("Unknown XSD type: {}:{}", name.ns_uri(), name.local_name())
        };
    }

    [[nodiscard]] const Type& get(const TypeId id) const {
        if (id >= types_.size())
            throw std::out_of_range{"Invalid XSD TypeId"};

        return types_[id];
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return types_.size();
    }

private:
    [[nodiscard]] Type& get(const TypeId id) {
        if (id >= types_.size())
            throw std::out_of_range{"Invalid XSD TypeId"};

        return types_[id];
    }

    [[nodiscard]] TypeId add_builtin(const std::string_view local_name) {
        const auto id = declare(xml::qname{local_name, xsd::ns_uri});
        define(id, xsd::BuiltinType{});
        return id;
    }

    TypeId next_id_ = 0;
    std::vector<Type> types_;
    std::unordered_map<xml::qname, TypeId, xml::qname::hash> names_;
};

}

namespace soapp::xsd {

inline SimpleParsedType SimpleParsedType::from_node(const xml::node_view& simple_type, wsdl::TypeTable& type_table) {
    const auto restriction = simple_type.child("restriction", ns_uri);
    const auto list = simple_type.child("list", ns_uri);
    const auto union_ = simple_type.child("union", ns_uri);

    const auto variety_count = static_cast<unsigned>(restriction.has_value())
                                     + static_cast<unsigned>(list.has_value())
                                     + static_cast<unsigned>(union_.has_value());
    if (variety_count != 1)
        throw error{"xs:simpleType must contain exactly one of xs:restriction, xs:list, or xs:union"};

    if (restriction)
        return parse_restriction(*restriction, type_table);

    if (list)
        return parse_list(*list, type_table);

    if (union_)
        return parse_union(*union_, type_table);

    throw error{"Unsupported xs:simpleType variety"};
}

inline SimpleParsedType SimpleParsedType::parse_restriction(
    const xml::node_view& restriction,
    wsdl::TypeTable& type_table) {
    const auto base = restriction.attribute("base");
    const auto inline_simple_type = restriction.child("simpleType", ns_uri);

    // The spec says there must be exactly one xs:restriction/@base or
    // xs:restriction/xs:simpleType child, not both.
    if (!base && !inline_simple_type)
        throw error{"Missing required xs:restriction/@base or xs:restriction/xs:simpleType"};

    if (base && inline_simple_type)
        throw error{"Both xs:restriction/@base and xs:restriction/xs:simpleType are present; only one is allowed"};

    if (!base) {
        auto inline_type = SimpleParsedType::from_node(*inline_simple_type, type_table);
        const auto base_id = type_table.add_anonymous();

        type_table.define(base_id, std::move(inline_type));

        return SimpleParsedType{ Restriction{base_id} };
    }

    const auto base_name = restriction.resolve_qname(base->view());
    const auto base_id = type_table.resolve(base_name);

    return SimpleParsedType{ Restriction{base_id} };
}

inline SimpleParsedType SimpleParsedType::parse_list(const xml::node_view& list, const wsdl::TypeTable& type_table) {
    TypeRef item_id;

    if (const auto item_type_attr = list.attribute("itemType")) {
        const auto item_name = list.resolve_qname(item_type_attr->view());
        item_id = type_table.resolve(item_name);
    } else if (list.child("simpleType", ns_uri)) {
        throw error{"Inline xs:list/xs:simpleType is not supported yet"};
    } else {
        throw error{"Missing required xs:list/@itemType or xs:list/xs:simpleType"};
    }

    return SimpleParsedType{ List{item_id} };
}

inline SimpleParsedType SimpleParsedType::parse_union(const xml::node_view& union_,const wsdl::TypeTable& type_table) {
    std::vector<TypeRef> member_types;

    std::cerr << "Warning: xs:union is not supported yet; doing a fake parse" << std::endl;

    return SimpleParsedType{Union{std::move(member_types)}};
}

inline void XSDSchema::declare_types(SchemaContext& context) const {
    if (!mark_visited(context))
        return;

    for (const auto& imported_schema : imported_schemas)
        imported_schema.declare_types(context);

    for (const auto simple_type : schema_.children("simpleType", ns_uri)) {
        const auto name = simple_type.attribute("name");
        if (!name)
            continue;

        (void)context.types.declare(xml::qname{ name->view(), target_namespace_ });
    }

    for (const auto complex_type : schema_.children("complexType", ns_uri)) {
        const auto name = complex_type.attribute("name");
        if (!name)
            continue;

        (void)context.types.declare(xml::qname{ name->view(), target_namespace_ });
    }
}

inline void XSDSchema::define_types(SchemaContext& context) const {
    if (!mark_visited(context))
        return;

    for (const auto& imported_schema : imported_schemas)
        imported_schema.define_types(context);

    for (const auto simple_type : schema_.children("simpleType", ns_uri)) {
        const auto name = simple_type.attribute("name");
        TypeRef id;

        if (name)
            id = context.types.resolve(xml::qname{ name->view(), target_namespace_ });
        else
            id = context.types.add_anonymous();

        context.types.define(id, SimpleParsedType::from_node(simple_type, context.types));
    }

    for (const auto complex_type : schema_.children("complexType", ns_uri)) {
        const auto name = complex_type.attribute("name");
        TypeRef id;

        if (name)
            id = context.types.resolve(xml::qname{ name->view(), target_namespace_ });
        else
            id = context.types.add_anonymous();

        context.types.define(id, ComplexParsedType::from_node(complex_type, target_namespace_, context.types));
    }
}

}

#endif //SOAPP_TYPE_TABLE_H
