//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_TYPE_TABLE_H
#define SOAPP_TYPE_TABLE_H

#include "builtin_types.h"
#include "xml.h"
#include "xsd_types.h"

#include <fmt/format.h>
#include <charconv>
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

        (void)declare(xml::qname{"unimplementedType", xsd::ns_uri});
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

inline SimpleParsedType SimpleParsedType::parse_list(const xml::node_view& list, wsdl::TypeTable& type_table) {
    TypeRef item_id;

    if (const auto item_type_attr = list.attribute("itemType")) {
        const auto item_name = list.resolve_qname(item_type_attr->view());
        item_id = type_table.resolve(item_name);
    } else if (const auto inline_simple_type = list.child("simpleType", ns_uri)) {
        auto inline_type = SimpleParsedType::from_node(*inline_simple_type, type_table);

        item_id = type_table.add_anonymous();
        type_table.define(item_id, std::move(inline_type));
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

inline ComplexParsedType ComplexParsedType::from_node(const xml::node_view& complex_type, wsdl::TypeTable& type_table) {
    const auto simple_content = complex_type.child("simpleContent", ns_uri);
    const auto complex_content = complex_type.child("complexContent", ns_uri);

    if (simple_content && complex_content)
        throw error{"xs:complexType contains both xs:simpleContent and xs:complexContent"};

    if (simple_content)
        return parse_simple_content(*simple_content, type_table);

    if (complex_content)
        return parse_complex_content(*complex_content, type_table);

    return parse_direct_content(complex_type, type_table);
}

inline ComplexParsedType::Occurs ComplexParsedType::parse_occurs(const xml::node_view& element) {
    Occurs occurs;

    auto parse_non_negative = [](const xml::owned_string& value) {
        std::size_t result = 0;
        const auto text = value.view();
        const auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), result);

        if (status != std::errc{} || end != text.data() + text.size())
            throw error{fmt::format("Invalid non-negative value '{}'", text)};

        return result;
    };

    if (const auto min = element.attribute("minOccurs"))
        occurs.min = parse_non_negative(*min);

    if (const auto max = element.attribute("maxOccurs")) {
        if (max->view() == "unbounded")
            occurs.max = std::nullopt;
        else
            occurs.max = parse_non_negative(*max);
    }

    if (occurs.max && occurs.min > *occurs.max)
        throw error{"xs:element minOccurs greater than maxOccurs"};

    return occurs;
}

inline TypeRef ComplexParsedType::parse_declaration_type(
    const xml::node_view& declaration,
    const bool allow_complex,
    wsdl::TypeTable& type_table) {
    const auto type_attr = declaration.attribute("type");
    const auto simple_type = declaration.child("simpleType", ns_uri);
    const auto complex_type = declaration.child("complexType", ns_uri);

    const auto inline_count = static_cast<unsigned>(simple_type.has_value())
                                    + static_cast<unsigned>(complex_type.has_value());

    if (type_attr && inline_count != 0)
        throw error{"An XSD declaration cannot have both @type and an inline type"};

    if (inline_count > 1)
        throw error{"An XSD declaration cannot contain both xs:simpleType and xs:complexType"};

    if (type_attr)
        return type_table.resolve(declaration.resolve_qname(type_attr->view()));

    if (simple_type) {
        auto definition = SimpleParsedType::from_node(*simple_type, type_table);
        const auto id = type_table.add_anonymous();
        type_table.define(id, std::move(definition));
        return id;
    }

    if (complex_type) {
        if (!allow_complex)
            throw error{"unexpected inline xs:complexType found"};

        auto definition = ComplexParsedType::from_node(*complex_type, type_table);
        const auto id = type_table.add_anonymous();
        type_table.define(id, std::move(definition));
        return id;
    }

    const auto default_type = allow_complex ? "anyType" : "anySimpleType";
    return type_table.resolve(xml::qname{ default_type, ns_uri });
}

inline ComplexParsedType::Element ComplexParsedType::parse_element(
    const xml::node_view& element,
    wsdl::TypeTable& type_table) {
    const auto name = element.attribute("name");

    if (!name) {
        // TODO: handle anonymous elements properly; for now, we just return a placeholder type
        std::cerr << "Warning: anonymous xs:element found; doing a fake parse" << std::endl;

        return Element{
            .name = std::string{"<anonymous_element>"},
            .type = type_table.resolve(xml::qname{"unimplementedType", ns_uri}),
            .occurs = parse_occurs(element)
        };
    }

    return Element{
        .name = std::string{name->view()},
        .type = parse_declaration_type(element, true, type_table),
        .occurs = parse_occurs(element)
    };
}

inline ComplexParsedType::Attribute ComplexParsedType::parse_attribute(
    const xml::node_view& attribute,
    wsdl::TypeTable& type_table) {
    const auto name = attribute.attribute("name");

    if (!name) {
        // TODO: handle anonymous attributes properly; for now, we just return a placeholder type
        std::cerr << "Warning: anonymous xs:attribute found; doing a fake parse" << std::endl;

        return Attribute{
            .name = std::string{"<anonymous_attribute>"},
            .type = type_table.resolve(xml::qname{"unimplementedType", ns_uri}),
            .required = false
        };
    }

    const auto use = attribute.attribute("use");
    if (use && use->view() != "optional" && use->view() != "required" && use->view() != "prohibited")
        throw error{fmt::format("Invalid xs:attribute @use value '{}'", use->view())};

    return Attribute{
        .name = std::string{name->view()},
        .type = parse_declaration_type(attribute, false, type_table),
        .required = use && use->view() == "required"
    };
}

inline std::vector<ComplexParsedType::Attribute> ComplexParsedType::parse_attributes(
    const xml::node_view& container,
    wsdl::TypeTable& type_table) {
    std::vector<Attribute> attributes;
    for (const auto attribute : container.children("attribute", ns_uri))
        attributes.push_back(parse_attribute(attribute, type_table));

    return attributes;
}

inline ComplexParsedType::Sequence ComplexParsedType::parse_sequence(
    const xml::node_view& sequence,
    wsdl::TypeTable& type_table) {

    // TODO: don't ignore xs:choice and xs:all? or should we throw an error? for now, let's just ignore them

    Sequence result;
    for (const auto element : sequence.children("element", ns_uri))
        result.elements.push_back(parse_element(element, type_table));

    return result;
}

inline ComplexParsedType ComplexParsedType::parse_direct_content(
    const xml::node_view& complex_type,
    wsdl::TypeTable& type_table) {
    if (complex_type.child("choice", ns_uri) || complex_type.child("all", ns_uri)) {
        std::cerr << "Warning: xs:choice and xs:all are not supported yet; doing a fake parse" << std::endl;

        return ComplexParsedType { DirectContent{
                .sequence = std::nullopt,
                .attributes = parse_attributes(complex_type, type_table)
            }
        };
    }

    const auto sequences = complex_type.children("sequence", ns_uri);
    if (sequences.size() > 1)
        throw error{"xs:complexType contains multiple xs:sequence elements"};

    std::optional<Sequence> sequence;
    if (!sequences.empty())
        sequence = parse_sequence(sequences.front(), type_table);

    return ComplexParsedType{DirectContent{
        .sequence = std::move(sequence),
        .attributes = parse_attributes(complex_type, type_table)
    }};
}

inline ComplexParsedType ComplexParsedType::parse_simple_content(
    const xml::node_view& simple_content,
    wsdl::TypeTable& type_table) {
    const auto extension = simple_content.child("extension", ns_uri);
    const auto restriction = simple_content.child("restriction", ns_uri);

    if (extension && restriction)
        throw error{"xs:simpleContent contains both xs:extension and xs:restriction"};

    const auto derivation = extension ? extension : restriction;
    if (!derivation)
        throw error{"Missing xs:extension or xs:restriction in xs:simpleContent"};

    const auto base = derivation->attribute("base");
    if (!base)
        throw error{"Missing required @base on xs:simpleContent derivation"};

    return ComplexParsedType{SimpleContent{
        .base = type_table.resolve(derivation->resolve_qname(base->view())),
        .derivation = extension ? Derivation::extension : Derivation::restriction,
        .attributes = parse_attributes(*derivation, type_table)
    }};
}

inline ComplexParsedType ComplexParsedType::parse_complex_content(
    const xml::node_view& complex_content,
    wsdl::TypeTable& type_table) {
    const auto extension = complex_content.child("extension", ns_uri);
    const auto restriction = complex_content.child("restriction", ns_uri);

    if (extension && restriction)
        throw error{"xs:complexContent contains both xs:extension and xs:restriction"};

    const auto derivation = extension ? extension : restriction;
    if (!derivation)
        throw error{"Missing xs:extension or xs:restriction in xs:complexContent"};

    const auto base = derivation->attribute("base");
    if (!base)
        throw error{"Missing required @base on xs:complexContent derivation"};

    if (derivation->child("choice", ns_uri) || derivation->child("all", ns_uri))
        throw error{"xs:choice and xs:all are not supported yet"};

    const auto sequences = derivation->children("sequence", ns_uri);
    if (sequences.size() > 1)
        throw error{"XSD derivation contains multiple xs:sequence elements"};

    std::optional<Sequence> sequence;
    if (!sequences.empty())
        sequence = parse_sequence(sequences.front(), type_table);

    return ComplexParsedType{ComplexContent {
        .base = type_table.resolve(derivation->resolve_qname(base->view())),
        .derivation = extension ? Derivation::extension : Derivation::restriction,
        .sequence = std::move(sequence),
        .attributes = parse_attributes(*derivation, type_table)
    } };
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

        context.types.define(id, ComplexParsedType::from_node(complex_type, context.types));
    }
}

}

#endif //SOAPP_TYPE_TABLE_H
