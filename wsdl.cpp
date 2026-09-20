//
// Created by Salvo Passaro on 15/09/26.
//

#include "wsdl.h"

#include "io.h"

#include <iostream>
#include <utility>

namespace soapp::wsdl {

xml::node_view WSDL11::get_definitions(const xml::document& doc) {
    const auto root = doc.root();
    if (!root || root->name() != "definitions" || root->ns_uri() != ns_uri)
        throw error{"Root node is not definitions"};

    return *root;
}

const TypeTable& WSDL11::type_table() const noexcept {
    return types_;
}

WSDL11::WSDL11(const std::string_view source, xml::uri&& base) :
    WSDL11(xml::document::parse(source, std::move(base))) {}

// A WSDL document is simply a set of definitions.
// There is a definitions element at the root, and definitions inside.
WSDL11::WSDL11(xml::document&& doc) :
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

void WSDL11::parse_types() {
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

} // namespace soapp::wsdl
