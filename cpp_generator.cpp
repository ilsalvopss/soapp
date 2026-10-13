//
// Created by Salvo Passaro on 08/10/26.
//
#include "cpp_generator.h"
#include "type_table.h"

#include <cassert>
#include <functional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <unordered_set>
#include <variant>
#include <vector>

namespace soapp::cpp {
namespace {

struct CyclicDependency : std::runtime_error {
    TypeRef type;

    explicit CyclicDependency(const TypeRef id) :
        std::runtime_error{"Cyclic definition dependency at type " + std::to_string(id)}, type{id} {}
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

        const auto wrapper = // FIXME: use custom helper types eg. ::soapp::builtin_types::Multiplicity<...
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

        const auto wrapper = // FIXME: use custom helper types eg. ::soapp::builtin_types::NotRequired<...
            attribute.required ? "" : "::std::optional";

        write_field(attribute.type, names.attribute(owner, i), wrapper, names, writer);
    }
}

void write_complex_type(const wsdl::TypeTable& types, const Names& names, const TypeRef id, CodeWriter& writer,
                        const bool just_placeholder) {

    const auto& complex = std::get<xsd::ComplexParsedType>(types.get(id).definition());

    const auto* content = std::get_if<xsd::ComplexParsedType::DirectContent>(&complex.definition());
    const auto* simple = std::get_if<xsd::ComplexParsedType::SimpleContent>(&complex.definition());
    const auto* derived = std::get_if<xsd::ComplexParsedType::ComplexContent>(&complex.definition());
    const bool simple_extension = simple && simple->derivation == xsd::ComplexParsedType::Derivation::extension;
    const bool complex_extension = derived && derived->derivation == xsd::ComplexParsedType::Derivation::extension;

    if (just_placeholder || (!content && !simple_extension && !complex_extension)) {
        // Just emit a placeholder for recursive types, or for complex content that we don't yet support.
        writer.line(just_placeholder ? "// TODO: recursive complex layout" : "// TODO: unsupported yet");
        writer.line("struct " + std::string{ names.type(id) } + " : ::soapp::builtin_types::Unimplemented {};");
        return;
    }

    auto declaration = "struct " + std::string{ names.type(id) };
    if (simple_extension)
        declaration += " : ::" + names.qualified_type(simple->base);
    else if (complex_extension)
        declaration += " : ::" + names.qualified_type(derived->base);

    writer.line(declaration + " {");
    {
        auto indentation = writer.indented();
        if (content) {
            if (content->sequence)
                write_sequence(*content->sequence, names, id, writer);

            write_attributes(content->attributes, names, id, writer);
        } else if (simple_extension) {
            write_attributes(simple->attributes, names, id, writer);
        } else {
            if (derived->sequence)
                write_sequence(*derived->sequence, names, id, writer);

            write_attributes(derived->attributes, names, id, writer);
        }
    }
    writer.line("};");
}

// Group adjacent declarations without changing their dependency order.
void write_in_namespaces(
    const std::span<const TypeRef> ids, const Names& names, CodeWriter& writer,
    const std::function<void(TypeRef)>& write_type) {
    for (std::size_t first = 0; first < ids.size();) {
        const auto scope = std::string{names.type_namespace(ids[first])};
        writer.line();
        writer.line("namespace " + scope + " {");
        {
            auto indentation = writer.indented();
            while (first < ids.size() && names.type_namespace(ids[first]) == scope)
                write_type(ids[first++]);
        }
        writer.line("} // namespace " + scope);
    }
}

} // namespace

void write_declarations(const wsdl::TypeTable& types, const Names& names, CodeWriter& writer) {
    std::vector<TypeRef> generated_types;

    types.for_each([&](const auto& type) {
        if (std::holds_alternative<xsd::SimpleParsedType>(type.definition()) ||
            std::holds_alternative<xsd::ComplexParsedType>(type.definition()))
            generated_types.push_back(type.id());
    });

    std::unordered_set<TypeRef> resucrive_types;
    std::vector<TypeRef> ordered_types;

    for (;;) {
        try {
            ordered_types = dependency_order(types.size(), generated_types, [&](const TypeRef id) {
                return resucrive_types.contains(id) ? std::vector<TypeRef>{ } : types.get(id).dependencies(types);
            }); break;
        } catch (const CyclicDependency& cycle) {
            if (!std::holds_alternative<xsd::ComplexParsedType>(types.get(cycle.type).definition()))
                throw; // Simple-type cycles remain errors.

            // Remember offending types so that we can emit a placeholder for them, and try again.
            // This is a bit violent, I know... but for now it only breaks self-nested Subcodes in SOAP Faults
            // Not supporting that yet is fine with me
            resucrive_types.emplace(cycle.type);
        }
    }

    write_in_namespaces(ordered_types, names, writer, [&](const TypeRef id) {
        if (std::holds_alternative<xsd::SimpleParsedType>(types.get(id).definition()))
            return write_simple_type(types, names, id, writer);

        write_complex_type(types, names, id, writer, resucrive_types.contains(id));
    });
}

void write_header(const wsdl::TypeTable& types, const Names& names, std::ostream& out) {
    CodeWriter writer{ out };

    writer.line("#pragma once");
    writer.line("#include \"cpp_builtin_types.h\"");
    writer.line("#include <optional>");
    writer.line("#include <vector>");

    write_declarations(types, names, writer);
    // Future service declarations can use the same names and writer here.
}

} // namespace soapp::cpp
