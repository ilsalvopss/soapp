//
// Created by Salvo Passaro on 15/09/26.
//

#include "xsd_types.h"

#include "io.h"
#include "type_table.h"

#include <fmt/format.h>

#include <charconv>
#include <iostream>
#include <utility>

namespace soapp::xsd {

SimpleParsedType::SimpleParsedType(Definition definition) : definition_{std::move(definition)} {}

const SimpleParsedType::Definition& SimpleParsedType::definition() const noexcept {
    return definition_;
}

std::string SimpleParsedType::print() const {
    if (const auto* restriction = std::get_if<Restriction>(&definition_))
        return fmt::format("SimpleParsedType(restriction of ={})", restriction->base);

    if (const auto* list = std::get_if<List>(&definition_))
        return fmt::format("SimpleParsedType(list of ={})", list->item_type);

    const auto& union_ = std::get<Union>(definition_);
    return fmt::format("SimpleParsedType(union_members={})", union_.member_types.size());
}

ComplexParsedType::ComplexParsedType(Definition definition) : definition_{std::move(definition)} {}

const ComplexParsedType::Definition& ComplexParsedType::definition() const noexcept {
    return definition_;
}

XSDSchema::XSDSchema(xml::document&& doc) : XSDSchema(doc.root().value(), doc.base()) {
    document_ = std::make_unique<xml::document>(std::move(doc));
}

XSDSchema::XSDSchema(const xml::node_view schema, xml::uri&& base) :
    schema_{schema}, target_namespace_{find_target_namespace(schema)}, base_{base} {
    if (!schema_.is("schema", ns_uri))
        throw error{"Expected xs:schema"};

    // As far as I understood, <import> and <include> both allow to "include" other schemas, but:
    // - include is for schemas in the same namespace (targetNamespace), so definitions are merged into this schema
    // - import is for schemas in a different namespace (targetNamespace)

    for (const auto include : schema_.children("include", ns_uri)) {
        const auto schemaLocation_attr = include.attribute("schemaLocation");
        if (!schemaLocation_attr) {
            // § 4.2.1
            // It is not an error for the ·actual value· of the schemaLocation [attribute] to fail
            // to resolve it all, in which case no corresponding inclusion is performed
            continue;
        }

        xml::uri resolved_uri = base_.resolve(schemaLocation_attr->zview());
        if (!base_.local() && resolved_uri.local()) {
            std::cout << "Remote schema wants to import local file... smelly?? Skipping" << std::endl;
            continue;
        }

        const auto schema_content = io::fetch(resolved_uri.string());

        auto doc = xml::document::parse(schema_content, std::move(resolved_uri));
        imported_schemas.emplace_back(std::move(doc));
    }

    for (const auto import : schema_.children("import", ns_uri)) {
        const auto namespace_attr = import.attribute("namespace");
        const auto schemaLocation_attr = import.attribute("schemaLocation");

        if (!schemaLocation_attr)
            throw error{"Missing required xs:import/@schemaLocation"};

        xml::uri resolved_uri = base_.resolve(schemaLocation_attr->zview());
        if (!base_.local() && resolved_uri.local()) {
            std::cout << "Remote schema wants to import local file... smelly?? Skipping" << std::endl;
            continue;
        }

        const auto schema_content = io::fetch(resolved_uri.string());

        auto doc = xml::document::parse(schema_content, std::move(resolved_uri));
        auto imported_schema = XSDSchema{std::move(doc)};
        if (namespace_attr && imported_schema.target_namespace() != namespace_attr->view())
            throw error{"Imported schema targetNamespace does not match xs:import/@namespace"};

        imported_schemas.push_back(std::move(imported_schema));
    }
}

std::string XSDSchema::find_target_namespace(const xml::node_view& schema) {
    if (const auto target_ns = schema.attribute("targetNamespace"))
        return std::string{target_ns->view()};

    return {};
}

xml::qname XSDSchema::declared_name(const std::string_view name) const {
    return xml::qname{ name, target_namespace_ };
}

bool XSDSchema::mark_visited(SchemaContext& context) const {
    // https://en.cppreference.com/cpp/container/unordered_set/emplace

    if (document_)
        return context.visited_documents.emplace(base_.string()).second;

    return true;
}

SimpleParsedType SimpleParsedType::from_node(
    const xml::node_view& simple_type,
    wsdl::TypeTable& type_table) {
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

SimpleParsedType SimpleParsedType::parse_restriction(
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

SimpleParsedType SimpleParsedType::parse_list(
    const xml::node_view& list,
    wsdl::TypeTable& type_table) {
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

SimpleParsedType SimpleParsedType::parse_union(
    const xml::node_view& union_,
    const wsdl::TypeTable& type_table) {
    std::vector<TypeRef> member_types;

    std::cerr << "Warning: xs:union is not supported yet; doing a fake parse" << std::endl;

    return SimpleParsedType{Union{std::move(member_types)}};
}

ComplexParsedType ComplexParsedType::from_node(
    const xml::node_view& complex_type,
    wsdl::TypeTable& type_table) {
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

ComplexParsedType::Occurs ComplexParsedType::parse_occurs(const xml::node_view& element) {
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

TypeRef ComplexParsedType::parse_declaration_type(
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

ComplexParsedType::Element ComplexParsedType::parse_element(
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

ComplexParsedType::Attribute ComplexParsedType::parse_attribute(
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

std::vector<ComplexParsedType::Attribute> ComplexParsedType::parse_attributes(
    const xml::node_view& container,
    wsdl::TypeTable& type_table) {
    std::vector<Attribute> attributes;
    for (const auto attribute : container.children("attribute", ns_uri))
        attributes.push_back(parse_attribute(attribute, type_table));

    return attributes;
}

ComplexParsedType::Sequence ComplexParsedType::parse_sequence(
    const xml::node_view& sequence,
    wsdl::TypeTable& type_table) {

    // TODO: don't ignore xs:choice and xs:all? or should we throw an error? for now, let's just ignore them

    Sequence result;
    for (const auto element : sequence.children("element", ns_uri))
        result.elements.push_back(parse_element(element, type_table));

    return result;
}

ComplexParsedType ComplexParsedType::parse_direct_content(
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

ComplexParsedType ComplexParsedType::parse_simple_content(
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

ComplexParsedType ComplexParsedType::parse_complex_content(
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

void XSDSchema::declare_types(SchemaContext& context) const {
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

void XSDSchema::define_types(SchemaContext& context) const {
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

} // namespace soapp::xsd
