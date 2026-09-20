//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_XSD_TYPES_H
#define SOAPP_XSD_TYPES_H

#include "xml.h"
#include "io.h"

#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace soapp::wsdl {
class TypeTable;
}

namespace soapp::xsd {

// https://www.w3.org/TR/xmlschema-1/
// https://www.w3.org/TR/xmlschema-2/

static inline constexpr std::string_view ns_uri = "http://www.w3.org/2001/XMLSchema";

class error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A resolved reference into the TypeTable.
using TypeRef = std::uint32_t;

class XSDSchema;

struct SchemaContext {
    wsdl::TypeTable& types;
    std::unordered_set<std::string> visited_documents;
};

// https://www.w3.org/TR/xmlschema-1/#Simple_Type_Definitions
class SimpleParsedType {
public:
    struct Restriction {
        TypeRef base;
    };

    struct List {
        TypeRef item_type;
    };

    struct Union {
        std::vector<TypeRef> member_types;
    };

    using Definition = std::variant<Restriction, List, Union>;

private:
    Definition definition_;

    explicit SimpleParsedType(Definition definition) : definition_{std::move(definition)} {}

public:
    [[nodiscard]] static SimpleParsedType from_node(
        const xml::node_view& simple_type,
        const wsdl::TypeTable& type_table);

    static SimpleParsedType parse_restriction(
        const xml::node_view& restriction,
        const wsdl::TypeTable& type_table);

    static SimpleParsedType parse_list(
        const xml::node_view& list,
        const wsdl::TypeTable& type_table);

    static SimpleParsedType parse_union(
        const xml::node_view& union_,
        const wsdl::TypeTable& type_table);

    [[nodiscard]] const Definition& definition() const noexcept {
        return definition_;
    }

    [[nodiscard]] std::string print() const {
        if (const auto* restriction = std::get_if<Restriction>(&definition_))
            return fmt::format("SimpleParsedType(restriction of ={})", restriction->base);

        if (const auto* list = std::get_if<List>(&definition_))
            return fmt::format("SimpleParsedType(list of ={})", list->item_type);

        const auto& union_ = std::get<Union>(definition_);
        return fmt::format("SimpleParsedType(union_members={})", union_.member_types.size());
    }
};

class ComplexParsedType {

public:
    static ComplexParsedType from_node(
        const xml::node_view& complex_type,
        const std::string& target_namespace,
        const wsdl::TypeTable& type_table) {}

    [[nodiscard]] std::string print() const {
        return fmt::format("ComplexParsedType()");
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

    [[nodiscard]] const std::string& target_namespace() const noexcept {
        return target_namespace_;
    }

    void declare_types(SchemaContext& context) const;

    void define_types(SchemaContext& context) const;

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

    [[nodiscard]] bool mark_visited(SchemaContext& context) const {
        // https://en.cppreference.com/cpp/container/unordered_set/emplace

        if (document_)
            return context.visited_documents.emplace(base_.string()).second;
        
        return true;
    }

    // if this backs a document (i.e. no other document embeds this scheme, such as in wsdl's <types>),
    // keep it alive so that the schema node is safely alive
    // there is a recurring argument in my mind if this is actually a sign that
    // schemas should actually inherit from document; however I guess this is a reasonable compromise for now
    std::unique_ptr<xml::document> document_;

    xml::node_view schema_;                  // the schema node itself, which is the root of this schema
    std::string target_namespace_;           // the target namespace of this schema, if any
    xml::uri base_;                          // the base URI of this schema (<import>s somewhat need it)
    std::vector<XSDSchema> imported_schemas; // XSD schemas imported via <import> and <include> elements
};

}

#endif //SOAPP_XSD_TYPES_H
