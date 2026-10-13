//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_XSD_TYPES_H
#define SOAPP_XSD_TYPES_H

#include "type_ids.h"
#include "xml.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
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

class XSDSchema;
class ElementTable;

struct SchemaContext {
    wsdl::TypeTable& types;
    ElementTable& elements;
    std::unordered_set<std::string> visited_documents;
};

// Parsed simple-type definition, with dependencies represented by TypeRef(s)
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

    explicit SimpleParsedType(Definition definition);

public:
    [[nodiscard]] static SimpleParsedType from_node(const xml::node_view& simple_type, wsdl::TypeTable& type_table);

    static SimpleParsedType parse_restriction(const xml::node_view& restriction, wsdl::TypeTable& type_table);

    static SimpleParsedType parse_list(const xml::node_view& list, wsdl::TypeTable& type_table);

    static SimpleParsedType parse_union(const xml::node_view& union_, wsdl::TypeTable& type_table);

    [[nodiscard]] const Definition& definition() const noexcept;

    // given a type table, return the set of types on which this type depends
    [[nodiscard]] std::vector<TypeRef> dependencies(const wsdl::TypeTable& type_table) const;

    [[nodiscard]] std::string print() const;
};

// https://www.w3.org/TR/xmlschema-1/#Complex_Type_Definitions
class ComplexParsedType {
public:
    friend class XSDSchema;

    enum class Derivation {
        none,
        extension,
        restriction
    };

    struct Occurs {
        std::size_t min = 1;
        std::optional<std::size_t> max = 1; // nullopt means unbounded
    };

    struct Element {
        std::string name; // XML local name of the declared or referenced element.
        TypeRef type;
        Occurs occurs;
    };

    struct Attribute {
        std::string name;
        TypeRef type;
        bool required = false;
        bool prohibited = false;
    };

    struct Sequence {
        std::vector<Element> elements;
    };

    struct DirectContent {
        std::optional<Sequence> sequence;
        std::vector<Attribute> attributes;
    };

    // Simple-valued content derived from a base type, with attribute declarations.
    struct SimpleContent {
        TypeRef base;
        Derivation derivation;
        std::vector<Attribute> attributes;
    };

    // Complex content derived from a base type, with element and attribute declarations.
    struct ComplexContent {
        TypeRef base;
        Derivation derivation;
        std::optional<Sequence> sequence;
        std::vector<Attribute> attributes;
    };

    using Definition = std::variant<DirectContent, SimpleContent, ComplexContent>;

private:
    Definition definition_;

    explicit ComplexParsedType(Definition definition);

public:
    static ComplexParsedType from_node(const xml::node_view& complex_type, SchemaContext& context);

    [[nodiscard]] const Definition& definition() const noexcept;

    // given a type table, return the set of types on which this type depends
    [[nodiscard]] std::vector<TypeRef> dependencies(const wsdl::TypeTable& type_table) const;

private:
    static Occurs parse_occurs(const xml::node_view& element);

    // Resolves a declaration's type and parses its inline definition, if present.
    static TypeRef parse_declaration_type(
        const xml::node_view& declaration,
        bool allow_complex,
        SchemaContext& context);

    // Resolves a named or default type, or reserves an ID for an inline type.
    static TypeRef declaration_type(const xml::node_view& declaration, bool allow_complex, wsdl::TypeTable& type_table);

    // Defines a declaration's inline type at the supplied type ID, if present.
    static void define_inline_type(const xml::node_view& declaration, TypeRef id, SchemaContext& context);

    static Element parse_element(const xml::node_view& element, SchemaContext& context);

    static Attribute parse_attribute(const xml::node_view& attribute, SchemaContext& context);

    static std::vector<Attribute> parse_attributes(const xml::node_view& container, SchemaContext& context);

    static Sequence parse_sequence(const xml::node_view& sequence, SchemaContext& context);

    static ComplexParsedType parse_direct_content(const xml::node_view& complex_type, SchemaContext& context);

    static ComplexParsedType parse_simple_content(const xml::node_view& simple_content, SchemaContext& context);

    static ComplexParsedType parse_complex_content(const xml::node_view& complex_content, SchemaContext& context);
};

class XSDSchema {
public:
    XSDSchema(const XSDSchema&) = delete;
    XSDSchema& operator=(const XSDSchema&) = delete;

    XSDSchema(XSDSchema&&) noexcept = default;
    XSDSchema& operator=(XSDSchema&&) noexcept = default;

    explicit XSDSchema(xml::document&& doc);

    explicit XSDSchema(xml::node_view schema, xml::uri&& base);

    [[nodiscard]] const std::string& target_namespace() const noexcept { return target_namespace_; }

    // Declare named types and (named) global elements
    void declare_types(SchemaContext& context) const;

    // Resolve the types for each declared global element, or reserve an ID for an inline type.
    void bind_elements(SchemaContext& context) const;

    void define_types(SchemaContext& context) const;

private:
    // (Construction helper) Find the target namespace of this schema, if any.
    [[nodiscard]] static std::string find_target_namespace(const xml::node_view& schema);

    // Given a local name, return the qualified name in this schema's target namespace.
    [[nodiscard]] xml::qname declared_name(std::string_view name) const;

    // mark this document's base URI as visited in the schema context, returning true if it was not already visited
    [[nodiscard]] bool mark_visited(SchemaContext& context) const;

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
