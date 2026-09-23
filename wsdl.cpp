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

xml::qname WSDL11::declared_name(const std::string_view name) const {
    return xml::qname{name, target_namespace_};
}

WSDL11::WSDL11(const std::string_view source, xml::uri&& base) :
    WSDL11(xml::document::parse(source, std::move(base))) {}

// A WSDL document is simply a set of definitions.
// There is a definitions element at the root, and definitions inside.
WSDL11::WSDL11(xml::document&& doc) :
    xml::document(std::move(doc)), definitions(get_definitions(*this)) {
    if (const auto target_namespace = definitions.attribute("targetNamespace"))
        target_namespace_ = std::string{target_namespace->view()};

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

void WSDL11::declare_schemas(xsd::SchemaContext& context) const {
    for (const auto& wsdl : imported_wsdl)
        wsdl.declare_schemas(context);

    for (const auto& schema : imported_schemas)
        schema.declare_types(context);

    for (const auto& schema : local_schemas)
        schema.declare_types(context);
}

void WSDL11::define_schemas(xsd::SchemaContext& context) const {
    for (const auto& wsdl : imported_wsdl)
        wsdl.define_schemas(context);

    for (const auto& schema : imported_schemas)
        schema.define_types(context);

    for (const auto& schema : local_schemas)
        schema.define_types(context);
}

void WSDL11::parse_messages(CompilationContext& context) const {
    for (const auto& wsdl : imported_wsdl)
        wsdl.parse_messages(context);

    for (const auto message_node : definitions.children("message", ns_uri)) {
        const auto name = message_node.attribute("name");
        if (!name)
            throw error{"Missing required wsdl:message/@name"};

        const auto message_name = declared_name(name->view());
        if (context.messages.contains(message_name))
            throw error{
                "Duplicate wsdl:message declaration: " + std::string{message_name.local_name()}
            };

        Message message{message_name, {}};

        for (const auto part : message_node.children("part", ns_uri)) {
            const auto part_name = part.attribute("name");
            const auto element = part.attribute("element");
            const auto type = part.attribute("type");

            if (!part_name)
                throw error{"Missing required wsdl:part/@name"};

            if (element && type || !element && !type)
                throw error{"wsdl:part must contain exactly one of @element or @type"};

            if (element) {
                const auto element_name = part.resolve_qname(element->view());

                message.parts.push_back(MessagePart {
                    .name = std::string{part_name->view()},
                    .type = context.elements.resolve(element_name),
                    .element = element_name
                });
            } else {
                const auto type_name = part.resolve_qname(type->view());

                message.parts.push_back(MessagePart {
                    .name = std::string{part_name->view()},
                    .type = context.types.resolve(type_name),
                    .element = std::nullopt
                });
            }
        }

        context.messages.emplace(message_name, std::move(message));
    }
}

void WSDL11::parse_types(CompilationContext& context) const {
    // declaration phase!
    xsd::SchemaContext declaration_context{context.types, context.elements};
    declare_schemas(declaration_context);

    std::cout << "Declared " << context.types.size() << " types" << std::endl;

    xsd::SchemaContext definition_context{context.types, context.elements};
    define_schemas(definition_context);

    std::cout << "Defined " << context.types.size() << " types" << std::endl;

    parse_messages(context);
}

} // namespace soapp::wsdl
