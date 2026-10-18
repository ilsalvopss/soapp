//
// Created by Salvo Passaro on 08/10/26.
//

#include "cpp_names.h"

#include "type_table.h"
#include "wsdl.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <functional>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace soapp::cpp {
namespace {

// Helper: converts a string to a valid C++ identifier.
[[nodiscard]] std::string identifier(const std::string_view source) {
    // https://en.cppreference.com/w/cpp/keyword
    constexpr auto keywords = std::to_array<std::string_view>({
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
        "case", "catch", "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept",
        "const", "consteval", "constexpr", "constinit", "const_cast", "continue", "co_await", "co_return",
        "co_yield", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "import",
        "inline", "int", "long", "module", "mutable", "namespace", "new", "noexcept", "not",
        "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public",
        "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union", "unsigned", "using",
        "virtual", "void", "volatile", "wchar_t", "while", "xor", "xor_eq"
    });

    std::string result;
    for (const auto c : source) {
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';

        if (valid && (c != '_' || result.empty() || result.back() != '_'))
            result += c;
        else if (result.empty() || result.back() != '_')
            result += '_';
    }

    if (result.empty())
        result = "X";
    else if (result.front() == '_' || (result.front() >= '0' && result.front() <= '9'))
        result.insert(result.begin(), 'X');

    if (std::ranges::find(keywords, result) != keywords.end())
        result += '_';

    return result;
}

// Helper: converts a string to a valid C++ identifier, capitalizing the first letter.
[[nodiscard]] std::string builtin_identifier(const std::string_view source) {
    auto result = std::string { source };
    if (result.front() >= 'a' && result.front() <= 'z')
        result.front() -= 'a' - 'A';
    return identifier(result);
}

// Reserve a name, adding the first available numeric suffix when necessary.
[[nodiscard]] std::string claim(const std::string& base, std::unordered_set<std::string>& used) {
    if (used.emplace(base).second)
        return base;

    for (std::size_t suffix = 2;; ++suffix) {
        auto candidate = base + (base.back() == '_' ? "" : "_") + std::to_string(suffix);
        if (used.emplace(candidate).second)
            return candidate;
    }
}

// Claim names in source order, reserving the enclosing class name.
[[nodiscard]] std::vector<std::string> claim_member_names(const auto& members, const std::string& owner) {
    std::unordered_set<std::string> used{owner};
    std::vector<std::string> names;
    for (const auto& member : members)
        names.push_back(claim(identifier(member.get_name()), used));

    return names;
}

// Split a string into parts, using any of the characters in `separators` as delimiters.
void explode_into_parts(std::vector<std::string>& parts, std::string_view value, const std::string_view separators) {
    while (!value.empty()) {
        const auto end = value.find_first_of(separators);
        if (end != 0)
            parts.push_back(identifier(value.substr(0, end)));
        if (end == std::string_view::npos)
            break;
        value.remove_prefix(end + 1);
    }
}

// A readable URI to c++ namespace mapping
[[nodiscard]] std::string namespace_path(std::string_view uri) {
    if (uri.empty())
        return "no_namespace";

    std::vector<std::string> parts;

    if (uri.starts_with("http://") || uri.starts_with("https://")) {
        uri.remove_prefix(uri.find("://") + 3);
        const auto end = uri.find_first_of("/?#");
        auto host = uri.substr(0, end);
        if (host.starts_with("www."))
            host.remove_prefix(4);

        std::vector<std::string> labels;
        explode_into_parts(labels, host, ".");

        if (labels.size() > 1)
            labels.pop_back(); // drop TLD

        for (auto it = labels.rbegin(); it != labels.rend(); ++it)
            parts.push_back(*it);

        if (end != std::string_view::npos) {
            uri.remove_prefix(end);
            const auto path_end = uri.find_first_of("?#");
            std::vector<std::string> path;
            explode_into_parts(path, uri.substr(0, path_end), "/");
            if (path.size() > 1 && path.back() == "wsdl")
                path.pop_back();
            parts.insert(parts.end(), path.begin(), path.end());
            if (path_end != std::string_view::npos)
                explode_into_parts(parts, uri.substr(path_end), "?#&=");
        }
    } else {
        explode_into_parts(parts, uri, ":/?#&=");
    }

    if (parts.empty())
        return "no_namespace";

    std::string result = parts.front();
    for (std::size_t i = 1; i < parts.size(); ++i)
        result += "::" + parts[i];

    return result;
}

constexpr std::string_view generated_namespace = "soapp::generated";
constexpr std::string_view builtin_namespace = "soapp::builtin_types";

// A mapping from parent namespace to child identifiers already used in that namespace.
using NsToUsedMap = std::unordered_map<std::string, std::unordered_set<std::string>>;

// Collect child namespace identifiers already occupied in each parent namespace.
// `namespace_paths` is a range of qualified C++ paths under soapp::generated
template<typename NS_T>
requires std::ranges::input_range<const NS_T> &&
         std::convertible_to<std::ranges::range_reference_t<const NS_T>, std::string_view>
[[nodiscard]] NsToUsedMap reserved_namespace_names(const NS_T& namespace_paths) {
    NsToUsedMap used;

    for (std::string_view path : namespace_paths) {
        if (path == builtin_namespace)
            continue;

        path.remove_prefix(generated_namespace.size());
        auto parent = std::string{ generated_namespace };

        while (path.starts_with("::")) {
            path.remove_prefix(2);
            const auto end = path.find("::");
            const auto child = std::string{ path.substr(0, end) };

            used[parent].emplace(child);
            parent += "::" + child;

            if (end == std::string_view::npos)
                break;

            path.remove_prefix(end);
        }
    }

    return used;
}

// Extract a list of all namespaces used in message and port-type declarations
[[nodiscard]] std::vector<std::string_view> declaration_namespaces(const wsdl::CompilationContext& context) {
    std::vector<std::string_view> namespaces;
    for (const auto& name: context.messages | std::views::keys)
        namespaces.push_back(name.ns_uri());
    for (const auto& name: context.port_types | std::views::keys)
        namespaces.push_back(name.ns_uri());
    return namespaces;
}

} // namespace

// Given a WSDL type table and a list of extra namespaces, return a mapping from namespace URI to C++ namespace name.
[[nodiscard]] Names::NamespaceNames Names::namespace_names(const wsdl::TypeTable& types,
                                                           const std::span<const std::string_view> extra_namespaces) {
    NamespaceNames scopes;

    const auto add_namespace = [&](const std::string_view uri) {
        if (uri == xsd::ns_uri)
            return; // The scope of XML schema builtins is fixed, and they are not counted for collisions.

        scopes.emplace(std::string{ uri }, namespace_path(uri));
    };

    types.for_each([&](const auto& type) {
        if (type.name())
            add_namespace(type.name()->ns_uri());
    });

    for (const auto uri : extra_namespaces)
        add_namespace(uri);

    // Allocate paths in URI order so collision suffixes are deterministic.
    std::unordered_set<std::string> used;
    for (auto& scope : scopes | std::views::values)
        scope = std::string{ generated_namespace } + "::" + claim(scope, used);

    scopes.emplace(std::string{ xsd::ns_uri }, std::string{ builtin_namespace });

    return scopes;
}

Names::Names(const wsdl::TypeTable& types, const std::span<const std::string_view> extra_namespaces) :
    namespaces_{ namespace_names(types, extra_namespaces) }, types_(types.size()) {

    assign_scopes(types);
    assign_type_names(types);
    assign_member_names(types);
}

Names::Names(const wsdl::CompilationContext& context) : Names(context.types, declaration_namespaces(context)) {
    assign_declaration_names(context);
}

void Names::assign_scopes(const wsdl::TypeTable& types) {
    types.for_each([&](const auto& type) {
        auto& named = types_[type.id()];

        if (std::holds_alternative<xsd::BuiltinType>(type.definition())) {
            named = { std::string{builtin_namespace}, builtin_identifier(type.name()->local_name()) };
        } else if (type.name()) {
            named.scope = namespace_name(type.name()->ns_uri());
        } else {
            // I'm pretty sure this is wrong..
            // anonymous types should probably be in the same namespace as their parent type
            named.scope = generated_namespace;
        }
    });
}

void Names::assign_type_names(const wsdl::TypeTable& types) {
    auto used = reserved_namespace_names(namespaces_ | std::views::values);

    types.for_each([&](const auto& type) {
        auto& named = types_[type.id()];
        if (std::holds_alternative<xsd::BuiltinType>(type.definition())) {
            used[named.scope].emplace(named.local);
            return;
        }

        const auto base = type.name() ? identifier(type.name()->local_name()) : "AnonymousType_" + std::to_string(type.id());
        named.local = claim(base, used[named.scope]);
    });
}

void Names::assign_member_names(const wsdl::TypeTable& types) {
    enum class State { unseen, visiting, visited };

    std::vector<State> states(types.size(), State::unseen);
    std::vector<std::unordered_set<std::string>> inherited_names(types.size());

    std::function<void(TypeRef)> assign = [&](const TypeRef id) {
        auto& state = states.at(id);
        if (state == State::visited)
            return;
        if (state == State::visiting)
            throw std::runtime_error{ "Cyclic complex inheritance at type " + std::to_string(id)};

        state = State::visiting;

        const auto* complex = std::get_if<xsd::ComplexParsedType>(&types.get(id).definition());
        if (!complex) {
            state = State::visited;
            return;
        }

        std::optional<TypeRef> base;
        const xsd::ComplexParsedType::Sequence* sequence = nullptr;
        std::span<const xsd::ComplexParsedType::Attribute> attributes;
        bool supported = false;

        if (const auto* content = std::get_if<xsd::ComplexParsedType::DirectContent>(&complex->definition())) {
            sequence = content->sequence ? &*content->sequence : nullptr;
            attributes = content->attributes;
            supported = true;
        } else if (const auto* content = std::get_if<xsd::ComplexParsedType::SimpleContent>(&complex->definition())) {
            base = content->base;
            if (content->derivation == xsd::ComplexParsedType::Derivation::extension) {
                attributes = content->attributes;
                supported = true;
            }
        } else if (const auto* content = std::get_if<xsd::ComplexParsedType::ComplexContent>(&complex->definition())) {
            base = content->base;
            if (content->derivation == xsd::ComplexParsedType::Derivation::extension) {
                sequence = content->sequence ? &*content->sequence : nullptr;
                attributes = content->attributes;
                supported = true;
            }
        }

        if (base)
            assign(*base);

        if (!supported) {
            state = State::visited;
            return; // Placeholders have no emitted members, but their base chain must still be acyclic.
        }

        auto& assigned_in_scope = inherited_names[id];
        if (base)
            assigned_in_scope = inherited_names[*base];

        auto& named = types_[id];
        auto used = assigned_in_scope;
        used.emplace(named.local); // Reserve this class's constructor name in its own scope.

        const auto element_count = sequence ? sequence->elements.size() : 0;
        named.elements.resize(element_count);
        named.attributes.resize(attributes.size());

        for (std::size_t i = 0; i < element_count; ++i) {
            const auto& element = sequence->elements[i];
            if (element.occurs.max == 0)
                continue;

            auto& name = named.elements[i];
            name = claim(identifier(element.name), used);
            assigned_in_scope.emplace(name);
        }

        for (std::size_t i = 0; i < attributes.size(); ++i) {
            if (attributes[i].prohibited)
                continue;
            auto& name = named.attributes[i];
            name = claim(identifier(attributes[i].name), used);
            assigned_in_scope.emplace(name);
        }

        state = State::visited;
    };

    types.for_each([&](const auto& type) { assign(type.id()); });
}

void Names::assign_declaration_names(const wsdl::CompilationContext& context) {
    // 1. Reserve already assigned namespace names in their parent namespaces
    auto used = reserved_namespace_names(namespaces_ | std::views::values);

    // 2. Reserve already assigned schema type names in their namespaces
    for (const auto& type : types_) {
        used[type.scope].emplace(type.local);
    }

    // 3. Claim message names, then assign their parts within the message's own scope
    for (const auto& [name, message] : context.messages) {
        auto& [local, members] = messages_[name];
        auto& used_in_scope = used[std::string{ namespace_name(name.ns_uri()) }];
        local = claim(identifier(name.local_name()), used_in_scope);
        members = claim_member_names(message.get_parts(), local);
    }

    // 4. Claim port-type names after messages, then assign their operations
    for (const auto& [name, port_type] : context.port_types) {
        auto& [local, members] = port_types_[name];
        auto& used_in_scope = used[std::string{ namespace_name(name.ns_uri()) }];
        local = claim(identifier(name.local_name()), used_in_scope);
        members = claim_member_names(port_type.get_operations(), local);
    }
}

std::string_view Names::namespace_name(const std::string_view uri) const {
    const auto entry = namespaces_.find(uri);
    if (entry == namespaces_.end())
        throw std::out_of_range{ "No C++ namespace planned for URI: " + std::string{uri} };

    return entry->second;
}

std::string_view Names::type(const TypeRef id) const {
    const auto& name = types_.at(id).local;
    if (name.empty())
        throw std::invalid_argument{ "Type has no C++ name" };

    return name;
}

std::string Names::qualified_type(const TypeRef id) const {
    return std::string{ type_namespace(id) } + "::" + std::string{ type(id) };
}

std::string_view Names::element(const TypeRef owner, const std::size_t index) const {
    return types_.at(owner).elements.at(index);
}

std::string_view Names::attribute(const TypeRef owner, const std::size_t index) const {
    return types_.at(owner).attributes.at(index);
}

std::string Names::qualified_message(const xml::qname& name) const {
    return std::string{namespace_name(name.ns_uri())} + "::" + std::string{message(name)};
}

std::string_view Names::part(const xml::qname& owner, const std::size_t index) const {
    return messages_.at(owner).members.at(index);
}

std::string_view Names::operation(const xml::qname& owner, const std::size_t index) const {
    return port_types_.at(owner).members.at(index);
}

} // namespace soapp::cpp
