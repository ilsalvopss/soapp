//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_WSDL_H
#define SOAPP_WSDL_H

#include "element_table.h"
#include "message_table.h"
#include "xml.h"
#include "xsd_types.h"
#include "type_table.h"

#include <string>
#include <vector>

namespace soapp::wsdl {
// https://www.w3.org/TR/2001/NOTE-wsdl-20010315
// NOTE: there is also version 2.0 but onvif uses 1.1
// https://stackoverflow.com/questions/66155090/how-to-find-wsdl-version

struct CompilationContext {
    TypeTable types;
    xsd::ElementTable elements;
    MessageTable messages;
};

class WSDL11 : protected xml::document {
    static constexpr std::string_view ns_uri = "http://schemas.xmlsoap.org/wsdl/";

    const xml::node_view definitions; // fast track to root node of the WSDL document

    std::string target_namespace_;
    std::vector<xsd::XSDSchema> imported_schemas; // XSD schemas imported via <import> elements
    std::vector<xsd::XSDSchema> local_schemas;
    std::vector<WSDL11> imported_wsdl; // WSDL documents imported via <import> elements

    // private helper to find the definitions node in a WSDL document
    [[nodiscard]] static xml::node_view get_definitions(const xml::document& doc);

    [[nodiscard]] xml::qname declared_name(std::string_view name) const;

    void declare_schemas(xsd::SchemaContext& context) const;

    void define_schemas(xsd::SchemaContext& context) const;

    void parse_messages(CompilationContext& context) const;

public:
    class error : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    explicit WSDL11(std::string_view source, xml::uri&& base);

    // A WSDL document is simply a set of definitions.
    // There is a definitions element at the root, and definitions inside.
    explicit WSDL11(xml::document&& doc);

    void parse_types(CompilationContext& context) const;
};

}

#endif //SOAPP_WSDL_H
