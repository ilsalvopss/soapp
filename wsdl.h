//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_WSDL_H
#define SOAPP_WSDL_H

#include "io.h"
#include "xml.h"
#include "xsd_types.h"
#include "type_table.h"

#include <iostream>

namespace soapp::wsdl {
// https://www.w3.org/TR/2001/NOTE-wsdl-20010315
// NOTE: there is also version 2.0 but onvif uses 1.1
// https://stackoverflow.com/questions/66155090/how-to-find-wsdl-version

class WSDL11 : protected xml::document {
    static constexpr std::string_view ns_uri = "http://schemas.xmlsoap.org/wsdl/";

    const xml::node_view definitions; // fast track to root node of the WSDL document

    TypeTable types_;
    std::vector<xsd::XSDSchema> imported_schemas; // XSD schemas imported via <import> elements
    std::vector<xsd::XSDSchema> local_schemas;
    std::vector<WSDL11> imported_wsdl; // WSDL documents imported via <import> elements

    // private helper to find the definitions node in a WSDL document
    [[nodiscard]] static xml::node_view get_definitions(const xml::document& doc) {
        const auto root = doc.root();
        if (!root || root->name() != "definitions" || root->ns_uri() != ns_uri)
            throw error{"Root node is not definitions"};

        return *root;
    }

public:
    class error : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    [[nodiscard]] const TypeTable& type_table() const noexcept {
        return types_;
    }

    explicit WSDL11(const std::string_view source, xml::uri&& base) :
    WSDL11(xml::document::parse(source, std::move(base))) {}

    // A WSDL document is simply a set of definitions.
    // There is a definitions element at the root, and definitions inside.
    explicit WSDL11(xml::document&& doc) :
    xml::document(std::move(doc)), definitions(get_definitions(*this)) {
        // check for <import>s and handle them if necessary
        // wsdl spec is really strange here... the imported file could be another WSDL or a schema (XSD) file
        // and maybe even anything else

        for (const auto import : definitions.children("import", ns_uri)) {
            auto location_attr = import.attribute("location");
            auto namespace_attr = import.attribute("namespace");

            if (!location_attr || !namespace_attr) {
                std::cout << "Found wsdl:import without location/ns attribute; skipping" << std::endl;
                continue;
            }

            std::cout << "Found wsdl:import: location=" << *location_attr
                      << ", namespace=" << *namespace_attr << std::endl;

            auto resolved_uri = base().resolve(location_attr->zview());
            if (!base().local() && resolved_uri.local()) {
                std::cout << "Remote WSDL wants to import local file... smelly?? Skipping" << std::endl;
                continue;
            }

            auto data = io::fetch(resolved_uri.string());
            auto imported_doc = xml::document::parse(data, std::move(resolved_uri));

            auto root = imported_doc.root();

            if (!root)
                throw error{"Imported document has no root node"};

            if (root->name() == "definitions")
                imported_wsdl.emplace_back(std::move(imported_doc));
            else if (root->name() == "schema")
                imported_schemas.emplace_back(std::move(imported_doc));
        }

        if (const auto types = definitions.child("types", ns_uri)) {
            // here we can have <xs:schema>s

            for (const auto schema : types->children()) {
                xsd::XSDSchema xsd{ schema, base() };

                local_schemas.emplace_back(std::move(xsd));
            }
        }
    }

    void parse_types() {
        // declaration phase!
        xsd::SchemaContext declaration_context{types_};

        for (const auto& schema : imported_schemas)
            schema.declare_types(declaration_context);

        for (const auto& wsdl : imported_wsdl)
            for (const auto& schema : wsdl.imported_schemas)
                schema.declare_types(declaration_context);

        for (const auto& schema : local_schemas)
            schema.declare_types(declaration_context);

        std::cout << "Declared " << types_.size() << " types" << std::endl;

        xsd::SchemaContext definition_context{types_};

        for (const auto& schema : imported_schemas)
            schema.define_types(definition_context);

        for (const auto& wsdl : imported_wsdl)
            for (const auto& schema : wsdl.imported_schemas)
                schema.define_types(definition_context);

        for (const auto& schema : local_schemas)
            schema.define_types(definition_context);

        std::cout << "Defined " << types_.size() << " types" << std::endl;
    }
};

}

#endif //SOAPP_WSDL_H
