//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_XSD_TYPES_H
#define SOAPP_XSD_TYPES_H

#include "xml.h"
#include "io.h"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace soapp::xsd {

// https://www.w3.org/TR/xmlschema-1/
// https://www.w3.org/TR/xmlschema-2/

static inline constexpr std::string_view ns_uri = "http://www.w3.org/2001/XMLSchema";

class error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using TypeRef = xml::qname;

// https://www.w3.org/TR/xmlschema-1/#Simple_Type_Definitions
class SimpleParsedType {
    enum class Variety {
        atomic_,
        union_,
        list_
    };

    // Empty for anonymous inline simple types.
    std::optional<xml::qname> name;

    TypeRef base;
    Variety variety = Variety::atomic_;

public:
    [[nodiscard]] static SimpleParsedType from_node(const xml::node_view& simple_type, const std::string_view target_ns) {
        SimpleParsedType type;

        if (const auto name = simple_type.attribute("name"))
            type.name = xml::qname{**name, target_ns};

        if (const auto restriction = simple_type.child("restriction", ns_uri))
            return parse_atomic(type, *restriction);

        if (const auto list = simple_type.child("list", ns_uri)) {
            std::cerr << "Found xs:list simpleType; not implemented yet" << std::endl;
            return type;
        }

        if (const auto union_ = simple_type.child("union", ns_uri)) {
            std::cerr << "Found xs:union simpleType; not implemented yet" << std::endl;
            return type;
        }

        throw error{"Unsupported xs:simpleType variety;"};
    }

    static SimpleParsedType parse_atomic(SimpleParsedType& type, const xml::node_view& restriction) {
        const auto base = restriction.attribute("base");

        // TODO: support inline simpleType definitions as base types
        // The spec says there must be exactly one xs:restriction/@base or xs:restriction/xs:simpleType child. not both

        if (!base)
            throw error{"Missing required xs:restriction/@base"};

        type.base = TypeRef{ restriction.resolve_qname(**base) };

        return type;
    }

    [[nodiscard]] std::string print() const {
        return fmt::format("SimpleParsedType(name={}, ns={}, base={}, variety={})",
                           name ? name->local_name() : "<anonymous>",
                           name ? name->ns_uri() : "<>",
                           std::string(base.ns_uri()) + ":" + std::string(base.local_name()),
                           variety == Variety::atomic_ ? "atomic" : "unknown");
    }
};

class ComplexParsedType {
    enum class Derivation {
        none,
        extension,
        restriction
    };

    // Not parsed yet; kept as the shape to grow into.
    std::optional<xml::qname> name;
    std::optional<TypeRef> base;
    Derivation derivation = Derivation::none;

    //std::vector<ParsedElement> elements;

public:
    [[nodiscard]] std::string print() const {
        return fmt::format("ComplexParsedType(name={}, ns={}, base={}, derivation={})",
                           name ? name->local_name() : "<anonymous>",
                           name ? name->ns_uri() : "<>",
                           base ? std::string(base->ns_uri()) + ":" + std::string(base->local_name()) : "<none>",
                           derivation == Derivation::extension ? "extension" :
                           derivation == Derivation::restriction ? "restriction" : "none");
    }

    [[nodiscard]] static ComplexParsedType from_node(const xml::node_view& complex_type, const std::string_view target_ns) {
        ComplexParsedType type;

        if (const auto name = complex_type.attribute("name"))
            type.name = xml::qname{**name, target_ns};

        // CHEATING EH !

        return type;
    }
};

class XSDSchema {
public:
    XSDSchema(const XSDSchema&) = delete;
    XSDSchema& operator=(const XSDSchema&) = delete;

    XSDSchema(XSDSchema&&) noexcept = default;
    XSDSchema& operator=(XSDSchema&&) noexcept = default;

    explicit XSDSchema(xml::document&& doc) : XSDSchema(doc.root().value(), doc.base()) {
        document_ = std::make_unique<xml::document>(std::move(doc));
    }

    explicit XSDSchema(const xml::node_view schema, xml::uri&& base) :
    schema_{schema}, target_namespace_{find_target_namespace(schema)}, base_{base} {
        if (!schema_.is("schema", ns_uri))
            throw error{"Expected xs:schema"};

        // As far as I understood, <import> and <include> both allow to "include" other schemas, but:
        // - include is for schemas in the same namespace (targetNamespace), so definitions are merged into this schema
        // - import is for schemas in a different namespace (targetNamespace)

        for (const auto include : schema_.children("include", ns_uri)) {
            auto schemaLocation_attr = include.attribute("schemaLocation");

            if (!schemaLocation_attr) {
                // § 4.2.1
                // It is not an error for the ·actual value· of the schemaLocation [attribute] to fail
                // to resolve it all, in which case no corresponding inclusion is performed
                continue;
            }

            std::cout << "Found xs:include: schemaLocation=" << **schemaLocation_attr << std::endl;

            xml::uri resolved_uri = base_.resolve(**schemaLocation_attr);
            if (!base_.local() && resolved_uri.local()) {
                std::cout << "Remote schema wants to import local file... smelly?? Skipping" << std::endl;
                continue;
            }

            std::cout << "Resolved xs:include schemaLocation to: " << resolved_uri.string() << std::endl;

            auto schema_content = io::fetch(resolved_uri.string());

            auto doc = xml::document::parse(schema_content, std::move(resolved_uri));
            imported_schemas.emplace_back(std::move(doc));
        }

        for (const auto import : schema_.children("import", ns_uri)) {
            auto namespace_attr = import.attribute("namespace");
            auto schemaLocation_attr = import.attribute("schemaLocation");

            if (!schemaLocation_attr)
                throw error{"Missing required xs:import/@schemaLocation"};

            std::cout << "Found xs:import: namespace=" << (namespace_attr ? **namespace_attr : "")
                      << ", schemaLocation=" << **schemaLocation_attr << std::endl;

            xml::uri resolved_uri = base_.resolve(**schemaLocation_attr);
            if (!base_.local() && resolved_uri.local()) {
                std::cout << "Remote schema wants to import local file... smelly?? Skipping" << std::endl;
                continue;
            }

            std::cout << "Resolved xs:import schemaLocation to: " << resolved_uri.string() << std::endl;

            auto schema_content = io::fetch(resolved_uri.string());

            auto doc = xml::document::parse(schema_content, std::move(resolved_uri));
            auto imported_schema = XSDSchema{std::move(doc)};
            if (namespace_attr && imported_schema.target_namespace() != **namespace_attr)
                throw error{"Imported schema targetNamespace does not match xs:import/@namespace"};

            imported_schemas.push_back(std::move(imported_schema));
        }
    }

    [[nodiscard]] const std::string& target_namespace() const noexcept {
        return target_namespace_;
    }

    [[nodiscard]] std::vector<SimpleParsedType> parse_simple() const {
        std::vector<SimpleParsedType> types;

        for (const auto simple_type : schema_.children("simpleType", ns_uri))
            types.push_back(SimpleParsedType::from_node(simple_type, target_namespace_));

        for (const auto& imported_schema : imported_schemas) {
            const auto imported_types = imported_schema.parse_simple();
            types.insert(types.end(), imported_types.begin(), imported_types.end());
        }

        return types;
    }

    [[nodiscard]] std::vector<ComplexParsedType> parse_complex() const {
        std::vector<ComplexParsedType> types;

        for (const auto complex_type : schema_.children("complexType", ns_uri))
            types.push_back(ComplexParsedType::from_node(complex_type, target_namespace_));

        for (const auto& imported_schema : imported_schemas) {
            const auto imported_types = imported_schema.parse_complex();
            types.insert(types.end(), imported_types.begin(), imported_types.end());
        }

        return types;
    }

private:
    // (Construction helper) Find the target namespace of this schema, if any.
    [[nodiscard]] static std::string find_target_namespace(const xml::node_view& schema) {
        if (const auto target_ns = schema.attribute("targetNamespace"))
            return std::string{target_ns->view()};

        return {};
    }

    // Given a local name, return the qualified name in this schema's target namespace.
    [[nodiscard]] inline xml::qname declared_name(const std::string_view name) const {
        return xml::qname{ name, target_namespace_ };
    }

    // if this backs a document, keep it alive so that the schema node is valid
    std::unique_ptr<xml::document> document_;

    xml::node_view schema_;
    std::string target_namespace_;
    xml::uri base_;
    std::vector<XSDSchema> imported_schemas;
};

}

#endif //SOAPP_XSD_TYPES_H
