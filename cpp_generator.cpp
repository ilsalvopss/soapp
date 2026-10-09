//
// Created by Salvo Passaro on 08/10/26.
//
#include "cpp_generator.h"

#include "type_table.h"

#include <functional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace soapp::cpp {
namespace {

// given a total of #type_count types, and a set of root types,
// return a vector of all types reachable from the roots in dependency order.
[[nodiscard]] std::vector<TypeRef> dependency_order(
    const std::size_t type_count, const std::span<const TypeRef> roots,
    const std::function<std::vector<TypeRef>(TypeRef)>& dependencies) {
    enum class State { unseen, visiting, visited };

    std::vector<State> states(type_count, State::unseen);
    std::vector<TypeRef> order;

    std::function<void(TypeRef)> visit = [&](const TypeRef id) {
        auto& state = states.at(id);

        if (state == State::visited)
            return;
        if (state == State::visiting)
            throw std::runtime_error{"Cyclic definition dependency at type " + std::to_string(id)};

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

// given a type table and a type id, return the list of type ids that the given type depends on.
[[nodiscard]] std::vector<TypeRef> type_dependencies(const wsdl::TypeTable& types, const TypeRef id) {
    const auto& definition = types.get(id).definition();
    std::vector<TypeRef> dependencies;
    std::string_view role = "base"; // diagnostics :)
    bool simple_only = false;

    if (const auto* simple = std::get_if<xsd::SimpleParsedType>(&definition)) {
        simple_only = true; // simple types can only depend on other simple types (yay!)

        if (const auto* restriction = std::get_if<xsd::SimpleParsedType::Restriction>(&simple->definition())) {
            dependencies.push_back(restriction->base);
            role = "restriction base";
        } else if (const auto* list = std::get_if<xsd::SimpleParsedType::List>(&simple->definition())) {
            dependencies.push_back(list->item_type);
            role = "list item type";
        } else if (const auto* union_ = std::get_if<xsd::SimpleParsedType::Union>(&simple->definition())) {
            dependencies = union_->member_types;
            role = "union member type";
        }
    } else if (const auto* complex = std::get_if<xsd::ComplexParsedType>(&definition)) {
        if (const auto* content = std::get_if<xsd::ComplexParsedType::SimpleContent>(&complex->definition()))
            dependencies.push_back(content->base);
        else if (const auto* content = std::get_if<xsd::ComplexParsedType::ComplexContent>(&complex->definition()))
            dependencies.push_back(content->base);
    }

    // Builtins are already complete; validate and retain every generated dependency.
    std::erase_if(dependencies, [&](const TypeRef dependency) {
        const auto& required = types.get(dependency).definition();

        if (std::holds_alternative<xsd::BuiltinType>(required))
            return true;

        if (simple_only && !std::holds_alternative<xsd::SimpleParsedType>(required))
            throw std::runtime_error{"Simple-type " + std::string{ role } + " is not a simple type"};

        if (std::holds_alternative<std::monostate>(required))
            throw std::runtime_error{"Undefined type dependency: " + std::to_string(dependency)};

        return false;
    });

    return dependencies;
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
        if (members.empty())
            throw std::runtime_error{"Union type has no member types"};

        for (const auto& member : members) {
            base += "::" + names.qualified_type(member) + ", ";
        }

        base = base.substr(0, base.size() - 2); // remove last comma and space
        base += '>';
    }

    writer.line("struct " + std::string{ names.type(id) } + " : " + base + " {};");
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

    const auto ordered_types = dependency_order(types.size(), generated_types,
        [&types](const TypeRef id) {
        return type_dependencies(types, id);
    });

    write_in_namespaces(ordered_types, names, writer, [&](const TypeRef id) {
        if (std::holds_alternative<xsd::SimpleParsedType>(types.get(id).definition()))
            return write_simple_type(types, names, id, writer);

        //TODO: complex
        writer.line("struct " + std::string{names.type(id)} + ';');
    });
}

void write_header(const wsdl::TypeTable& types, const Names& names, std::ostream& out) {
    CodeWriter writer{ out };

    writer.line("#pragma once");
    writer.line("#include \"cpp_builtin_types.h\"");

    write_declarations(types, names, writer);
    // Future service declarations can use the same names and writer here.
}

} // namespace soapp::cpp
