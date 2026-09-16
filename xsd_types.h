//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_XSD_TYPES_H
#define SOAPP_XSD_TYPES_H

#include "xml.h"
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

struct ParsedAttribute {
    xml::qname name;
    TypeRef type;
};

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
            std::cout << "Found xs:list simpleType; not implemented yet" << std::endl;
        }

        if (const auto union_ = simple_type.child("union", ns_uri)) {
            std::cout << "Found xs:union simpleType; not implemented yet" << std::endl;
        }

        throw error{"Unsupported xs:simpleType variety;"};
    }

    static SimpleParsedType parse_atomic(SimpleParsedType& type, const xml::node_view& restriction) {
        const auto base = restriction.attribute("base");

        if (!base)
            throw error{"Missing required xs:restriction/@base"};

        type.base = TypeRef{ restriction.resolve_qname(**base) };

        return type;
    }

    [[nodiscard]] std::string print() const {
        return fmt::format("SimpleParsedType(name={}, base={}, variety={})",
                           name ? name->local_name() : "<anonymous>",
                           base.local_name(),
                           variety == Variety::atomic_ ? "atomic" : "unknown");
    }
};

struct ComplexParsedType {
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
    std::vector<ParsedAttribute> attributes;
};

class XSDSchema {
public:
    explicit XSDSchema(const xml::node_view schema) :
    schema_{schema}, target_namespace_{find_target_namespace(schema)} {
        if (!schema_.is("schema", ns_uri))
            throw error{"Expected xs:schema"};

        // TODO: resolve imports and includes here if necessary

        // From the spec:
        // "...since it is unreasonable to expect a single type system grammar can be used to describe
        // all abstract types present and future, WSDL allows type systems to be added via extensibility elements.
        // An extensibility element may appear under the types element to identify the type definition system
        // being used and to provide an XML container element for the type definitions.
        // The role of this element can be compared to that of the schema element of the XML Schema language."

        // Maybe in the future...
    }

    [[nodiscard]] const std::string& target_namespace() const noexcept {
        return target_namespace_;
    }

    [[nodiscard]] std::vector<SimpleParsedType> parse_simple() const {
        std::vector<SimpleParsedType> types;

        for (const auto simple_type : schema_.children("simpleType", ns_uri))
            types.push_back(SimpleParsedType::from_node(simple_type, target_namespace_));

        return types;
    }

private:
    [[nodiscard]] static std::string find_target_namespace(const xml::node_view& schema) {
        if (const auto target_ns = schema.attribute("targetNamespace"))
            return std::string{target_ns->view()};

        return {};
    }

    [[nodiscard]] inline xml::qname declared_name(const std::string_view name) const {
        return xml::qname{name, target_namespace_};
    }

    const xml::node_view schema_;
    const std::string target_namespace_;
};

}

#endif //SOAPP_XSD_TYPES_H
