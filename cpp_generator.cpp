//
// Created by Salvo Passaro on 08/10/26.
//
#include "cpp_generator.h"
#include "type_table.h"
#include "wsdl.h"

#include <cassert>
#include <functional>
#include <optional>
#include <ostream>
#include <ranges>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include <vector>

namespace soapp::cpp {
namespace {

constexpr std::string_view generated_namespace = "soapp::generated";

struct CyclicDependency : std::runtime_error {
    TypeRef type;

    explicit CyclicDependency(const TypeRef id) :
        std::runtime_error{ "Cyclic definition dependency at type " + std::to_string(id) }, type{id} {}
};

// given a total of #type_count types, and a set of root types,
// return a vector of all types reachable from the roots in dependency order.
// `dependencies(type)` should return the dependencies of `type`.
[[nodiscard]] std::vector<TypeRef> dependency_order(const std::size_t type_count, const std::span<const TypeRef> roots,
                                                    const std::function<std::vector<TypeRef>(TypeRef)>& dependencies) {
    enum class State { unseen, visiting, visited };

    std::vector<State> states(type_count, State::unseen);
    std::vector<TypeRef> order;

    std::function<void(TypeRef)> visit = [&](const TypeRef id) {
        auto& state = states.at(id);

        if (state == State::visited)
            return;
        if (state == State::visiting)
            throw CyclicDependency{ id };

        state = State::visiting;

        for (const auto dependency : dependencies(id))
            visit(dependency);

        state = State::visited;

        order.push_back(id);
    };

    for (const auto root : roots)
        visit(root);

    return order;
}

// given a type table, a namer, and a type id for a SimpleType, write the C++ declaration for that type.
void write_simple_type(const wsdl::TypeTable& types, const Names& names, const TypeRef id, CodeWriter& writer) {
    const auto& simple = std::get<xsd::SimpleParsedType>(types.get(id).definition());
    std::string base = "::soapp::builtin_types::Unimplemented";

    if (const auto* restriction = std::get_if<xsd::SimpleParsedType::Restriction>(&simple.definition()))
        base = "::" + names.qualified_type(restriction->base);
    else if (const auto* list = std::get_if<xsd::SimpleParsedType::List>(&simple.definition()))
        base = "::soapp::builtin_types::SimpleList<::" + names.qualified_type(list->item_type) + ">";
    else if (std::holds_alternative<xsd::SimpleParsedType::Union>(simple.definition())) {
        base = "::soapp::builtin_types::SimpleUnion<";

        const auto& members = std::get<xsd::SimpleParsedType::Union>(simple.definition()).member_types;
        assert(!members.empty());

        for (const auto& member : members) {
            base += "::" + names.qualified_type(member) + ", ";
        }

        base = base.substr(0, base.size() - 2); // remove last comma and space
        base += '>';
    }

    writer.line("struct " + std::string{ names.type(id) } + " : " + base + " {};");
}

void write_field(const TypeRef type, const std::string_view name, const std::string_view wrapper,
                 const Names& names, CodeWriter& writer) {
    auto type_name = "::" + names.qualified_type(type);

    if (!wrapper.empty())
        type_name = std::string{ wrapper } + '<' + type_name + '>';

    writer.line(type_name + ' ' + std::string{ name } + "{};");
}

void write_sequence(const xsd::ComplexParsedType::Sequence& sequence, const Names& names,
                    const TypeRef owner, CodeWriter& writer) {
    for (std::size_t i = 0; i < sequence.elements.size(); ++i) {
        const auto& element = sequence.elements[i];

        if (element.occurs.max == 0)
            continue;

        const auto wrapper =
            element.occurs.max != 1 ? "::std::vector" : element.occurs.min == 0 ? "::std::optional" : "";

        write_field(element.type, names.element(owner, i), wrapper, names, writer);
    }
}

void write_attributes(const std::span<const xsd::ComplexParsedType::Attribute> attributes, const Names& names,
                      const TypeRef owner, CodeWriter& writer) {
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const auto& attribute = attributes[i];

        if (attribute.prohibited)
            continue;

        const auto wrapper =
            attribute.required ? "" : "::std::optional";

        write_field(attribute.type, names.attribute(owner, i), wrapper, names, writer);
    }
}

void write_complex_placeholder(const Names& names, const TypeRef id, const std::string_view reason, CodeWriter& writer) {
    writer.line("// TODO: " + std::string{ reason });
    writer.line("struct " + std::string{ names.type(id) } + " : ::soapp::builtin_types::Unimplemented {};");
}

void write_complex_struct(const std::optional<TypeRef> base,
                          const std::optional<xsd::ComplexParsedType::Sequence>& sequence,
                          const std::span<const xsd::ComplexParsedType::Attribute> attributes,
                          const Names& names, const TypeRef id, CodeWriter& writer) {
    auto declaration = "struct " + std::string{ names.type(id) };
    if (base)
        declaration += " : ::" + names.qualified_type(*base);

    writer.line(declaration + " {");
    {
        auto indentation = writer.indented();

        if (sequence)
            write_sequence(*sequence, names, id, writer);

        write_attributes(attributes, names, id, writer);
    }
    writer.line("};");
}

void write_complex_content(const xsd::ComplexParsedType::DirectContent& content, const Names& names,
                           const TypeRef id, CodeWriter& writer) {
    write_complex_struct(std::nullopt, content.sequence, content.attributes, names, id, writer);
}

void write_complex_content(const xsd::ComplexParsedType::SimpleContent& content,
                           const Names& names, const TypeRef id, CodeWriter& writer) {
    if (content.derivation != xsd::ComplexParsedType::Derivation::extension) {
        write_complex_placeholder(names, id, "unsupported yet", writer);
        return;
    }

    write_complex_struct(content.base, std::nullopt, content.attributes, names, id, writer);
}

void write_complex_content(const xsd::ComplexParsedType::ComplexContent& content,
                           const Names& names, const TypeRef id, CodeWriter& writer) {
    if (content.derivation != xsd::ComplexParsedType::Derivation::extension) {
        write_complex_placeholder(names, id, "unsupported yet", writer);
        return;
    }

    write_complex_struct(content.base, content.sequence, content.attributes, names, id, writer);
}

void write_complex_type(const wsdl::TypeTable& types, const Names& names, const TypeRef id, CodeWriter& writer,
                        const bool just_placeholder) {
    if (just_placeholder) {
        write_complex_placeholder(names, id, "recursive complex layout", writer);
        return;
    }

    const auto& complex = std::get<xsd::ComplexParsedType>(types.get(id).definition());
    std::visit([&](const auto& content) {
        write_complex_content(content, names, id, writer);
    }, complex.definition());
}

// Group adjacent declarations without changing their dependency order.
template<typename Declarations, typename ScopeFor, typename Write>
void write_namespace_groups(const Declarations& declarations, const ScopeFor& scope_for,
                            CodeWriter& writer, const Write& write) {
    const auto last = declarations.end();
    for (auto it = declarations.begin(); it != last;) {
        const auto scope = scope_for(*it);
        writer.line();

        if (scope == generated_namespace) {
            while (it != last && scope_for(*it) == scope)
                write(*it++);
            continue;
        }

        const auto relative_scope = scope.substr(generated_namespace.size() + 2);
        writer.line("namespace " + std::string{ relative_scope } + " {");
        {
            auto indentation = writer.indented();
            while (it != last && scope_for(*it) == scope)
                write(*it++);
        }
        writer.line("} // namespace " + std::string{ relative_scope });
    }
}

struct TypeDeclarations {
    std::vector<TypeRef> order;               // Types in dependency order, with recursive types omitted.
    std::unordered_set<TypeRef> placeholder;  // Types that are recursive and need a placeholder declaration.
};

// Generate a dependency ordered type declaration plan, and a set of placeholder types.
[[nodiscard]] TypeDeclarations type_declarations(const wsdl::TypeTable& types) {
    std::vector<TypeRef> generated_types;

    types.for_each([&](const auto& type) {
        if (std::holds_alternative<xsd::SimpleParsedType>(type.definition()) ||
            std::holds_alternative<xsd::ComplexParsedType>(type.definition()))
            generated_types.push_back(type.id());
    });

    TypeDeclarations declarations;

    for (;;) {
        try {
            declarations.order = dependency_order(types.size(), generated_types, [&](const TypeRef id) {
                return declarations.placeholder.contains(id) ? std::vector<TypeRef>{ } : types.get(id).dependencies(types);
            }); break;
        } catch (const CyclicDependency& cycle) {
            if (!std::holds_alternative<xsd::ComplexParsedType>(types.get(cycle.type).definition()))
                throw; // Simple-type cycles remain errors.

            // Remember offending types so that we can emit a placeholder for them, and try again.
            // This is a bit violent, I know... but for now it only breaks self-nested Subcodes in SOAP Faults
            // Not supporting that yet is fine with me
            declarations.placeholder.emplace(cycle.type);
        }
    }

    return declarations;
}

void write_message(const wsdl::Message& message, const Names& names, CodeWriter& writer) {
    const auto& name = message.get_name();
    const auto& parts = message.get_parts();

    if (parts.size() <= 1) {
        const auto type = parts.empty() ? "void" : "::" + names.qualified_type(parts.front().get_type());
        writer.line("using " + std::string{ names.message(name) } + " = " + type + ";");

        return;
    }

    writer.line("struct " + std::string{ names.message(name) } + " {");
    {
        auto indentation = writer.indented();
        for (std::size_t i = 0; i < parts.size(); ++i)
            write_field(parts[i].get_type(), names.part(name, i), "", names, writer);
    }
    writer.line("};");
}

void write_operation(const wsdl::PortType::Operation& operation, const xml::qname& owner,
                     const std::size_t index, const wsdl::MessageTable& messages,
                     const Names& names, CodeWriter& writer) {
    std::string result = "void";
    if (const auto& output = operation.get_output_message(); output && !messages.at(*output).get_parts().empty())
        result = "::" + names.qualified_message(*output);

    std::string parameter;
    const auto& input = operation.get_input_message();
    if (!messages.at(input).get_parts().empty())
        parameter = "const ::" + names.qualified_message(input) + "& request";

    writer.line(result + ' ' + std::string{ names.operation(owner, index) } + '(' + parameter + ");");
}

void write_port_type(const wsdl::PortType& port_type, const wsdl::MessageTable& messages,
                     const Names& names, CodeWriter& writer) {
    const auto& name = port_type.get_name();
    const auto& operations = port_type.get_operations();

    writer.line("class " + std::string{ names.port_type(name) } + " {");
    writer.line("public:");
    {
        auto indentation = writer.indented();
        for (std::size_t i = 0; i < operations.size(); ++i)
            write_operation(operations[i], name, i, messages, names, writer);
    }
    writer.line("};");
}

} // namespace

void write_header(const wsdl::CompilationContext& context, const Names& names, std::ostream& out) {
    CodeWriter writer{ out };

    writer.line("#pragma once");
    writer.line("#include \"cpp_builtin_types.h\"");
    writer.line("#include <optional>");
    writer.line("#include <vector>");

    const auto [ordered, recursive] = type_declarations(context.types);
    if (ordered.empty() && context.messages.empty() && context.port_types.empty())
        return;

    writer.line();
    writer.line("namespace " + std::string{ generated_namespace } + " {");
    {
        auto indentation = writer.indented();

        write_namespace_groups(
            ordered,
            [&](const TypeRef id) { return names.type_namespace(id); },
            writer,
            [&](const TypeRef id) {
                if (std::holds_alternative<xsd::SimpleParsedType>(context.types.get(id).definition()))
                    write_simple_type(context.types, names, id, writer);
                else
                    write_complex_type(context.types, names, id, writer, recursive.contains(id));
            }
        );

        write_namespace_groups(
            context.messages | std::views::values,
            [&](const auto& decl) { return names.namespace_name(decl.get_name().ns_uri()); },
            writer,
            [&](const auto& message) { write_message(message, names, writer); }
        );

        write_namespace_groups(
            context.port_types | std::views::values,
            [&](const auto& decl) { return names.namespace_name(decl.get_name().ns_uri()); },
            writer,
            [&](const auto& port_type) { write_port_type(port_type, context.messages, names, writer); }
        );
    }

    writer.line("} // namespace " + std::string{ generated_namespace });
}

} // namespace soapp::cpp
