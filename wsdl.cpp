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

void WSDL11::bind_schema_elements(xsd::SchemaContext& context) const {
    for (const auto& wsdl : imported_wsdl)
        wsdl.bind_schema_elements(context);

    for (const auto& schema : imported_schemas)
        schema.bind_elements(context);

    for (const auto& schema : local_schemas)
        schema.bind_elements(context);
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
            throw error{ "Missing required wsdl:message/@name" };

        const auto message_name = declared_name(name->view());
        if (context.messages.contains(message_name))
            throw error { "Duplicate wsdl:message declaration: " + std::string{ message_name.local_name() } };

        std::vector<Message::Part> parts;
        for (const auto part : message_node.children("part", ns_uri)) {
            const auto part_name = part.attribute("name");
            const auto element = part.attribute("element");
            const auto type = part.attribute("type");

            if (!part_name)
                throw error{ "Missing required wsdl:part/@name" };

            if (element.has_value() == type.has_value())
                throw error{ "wsdl:part must contain exactly one of @element or @type" };

            if (element) {
                const auto element_name = part.resolve_qname(element->view());
                const auto resolved_type = context.elements.resolve(element_name);

                parts.emplace_back(
                    std::string{ part_name->view() },
                    resolved_type,
                    element_name
                );
            } else {
                const auto resolved_type = context.types.resolve(part.resolve_qname(type->view()));

                parts.emplace_back(std::string{ part_name->view() }, resolved_type);
            }
        }

        context.messages.emplace(message_name, Message{ message_name, std::move(parts) });
    }
}

void WSDL11::parse_port_types(CompilationContext& context) const {
    for (const auto& wsdl : imported_wsdl)
        wsdl.parse_port_types(context);

    const auto resolve_message = [&](const xml::node_view& node) {
        const auto message = node.attribute("message");
        if (!message)
            throw error{ "Missing required wsdl:" + std::string{ node.name() } + "/@message" };

        auto name = node.resolve_qname(message->view());
        if (!context.messages.contains(name))
            throw error{ "Unknown wsdl:message: " + std::string{name.ns_uri()} + ":" + std::string{name.local_name()} };

        return name;
    };

    for (const auto port_type_node : definitions.children("portType", ns_uri)) {
        const auto name = port_type_node.attribute("name");
        if (!name)
            throw error{ "Missing required wsdl:portType/@name" };

        const auto port_type_name = declared_name(name->view());
        if (context.port_types.contains(port_type_name))
            throw error{ "Duplicate wsdl:portType declaration: " + std::string{port_type_name.local_name()} };

        std::vector<PortType::Operation> operations;
        for (const auto operation_node : port_type_node.children("operation", ns_uri)) {
            const auto operation_name = operation_node.attribute("name");
            if (!operation_name)
                throw error{ "Missing required wsdl:operation/@name" };

            std::optional<xml::node_view> input;
            std::optional<xml::node_view> output;
            for (const auto child : operation_node.children()) {
                if (child.is("input", ns_uri)) {
                    if (input)
                        throw error{ "wsdl:operation contains multiple wsdl:input elements" };
                    if (output) // input after output! look at the grammar in § 2.4.3 Solicit-response Operation
                        throw error{ "Solicit-response operations are not supported" };
                    input = child;
                } else if (child.is("output", ns_uri)) {
                    if (output)
                        throw error{ "wsdl:operation contains multiple wsdl:output elements" };
                    output = child;
                }
            }

            if (!input)
                throw error{ "wsdl:operation must contain a wsdl:input in our current implementation, sorry" };

            auto input_message = resolve_message(*input);
            std::optional<xml::qname> output_message;
            if (output)
                output_message = resolve_message(*output);

            operations.emplace_back(std::string{operation_name->view()}, std::move(input_message),
                                    std::move(output_message));
        }

        context.port_types.emplace(port_type_name, PortType{port_type_name, std::move(operations)});
    }
}

void WSDL11::parse_types(CompilationContext& context) const {
    // declaration phase!
    xsd::SchemaContext declaration_context{ context.types, context.elements };
    declare_schemas(declaration_context);

    xsd::SchemaContext binding_context{ context.types, context.elements };
    bind_schema_elements(binding_context);

    xsd::SchemaContext definition_context{ context.types, context.elements };
    define_schemas(definition_context);
}

} // namespace soapp::wsdl
