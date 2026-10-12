//
// Created by Salvo Passaro on 08/10/26.
//

#include "cpp_names.h"

#include "type_table.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
[[nodiscard]] std::string claim(std::string base, std::unordered_set<std::string>& used) {
    if (used.emplace(base).second)
        return base;

    for (std::size_t suffix = 2;; ++suffix) {
        auto candidate = base + (base.back() == '_' ? "" : "_") + std::to_string(suffix);
        if (used.emplace(candidate).second)
            return candidate;
    }
}

[[nodiscard]] std::vector<std::string> assign_local_names(const std::vector<std::string>& bases,
                                                          std::unordered_set<std::string> used) {
    std::unordered_map<std::string, std::size_t> counts;
    std::vector<std::string> names(bases.size());

    for (const auto& base : bases) {
        if (!base.empty())
            ++counts[base];
    }

    for (std::size_t i = 0; i < bases.size(); ++i) {
        if (!bases[i].empty() && counts[bases[i]] == 1 && used.emplace(bases[i]).second)
            names[i] = bases[i];
    }

    for (std::size_t i = 0; i < bases.size(); ++i) {
        if (!bases[i].empty() && names[i].empty())
            names[i] = claim(bases[i], used);
    }

    return names;
}

void append_parts(std::vector<std::string>& parts, std::string_view value, const std::string_view separators) {
    while (!value.empty()) {
        const auto end = value.find_first_of(separators);
        if (end != 0)
            parts.push_back(identifier(value.substr(0, end)));
        if (end == std::string_view::npos)
            break;
        value.remove_prefix(end + 1);
    }
}

// A readable URI mapping: dropping www, the TLD and trailing wsdl may cause collisions.
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
        append_parts(labels, host, ".");

        if (labels.size() > 1)
            labels.pop_back(); // drop TLD

        for (auto it = labels.rbegin(); it != labels.rend(); ++it)
            parts.push_back(*it);

        if (end != std::string_view::npos) {
            uri.remove_prefix(end);
            const auto path_end = uri.find_first_of("?#");
            std::vector<std::string> path;
            append_parts(path, uri.substr(0, path_end), "/");
            if (path.size() > 1 && path.back() == "wsdl")
                path.pop_back();
            parts.insert(parts.end(), path.begin(), path.end());
            if (path_end != std::string_view::npos)
                append_parts(parts, uri.substr(path_end), "?#&=");
        }
    } else {
        append_parts(parts, uri, ":/?#&=");
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
using UsedNames = std::unordered_map<std::string, std::unordered_set<std::string>>;

// A type and a child namespace cannot use the same identifier in their parent scope.
void reserve_child_namespaces(std::string_view path, UsedNames& used) {
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

} // namespace

[[nodiscard]] Names::NamespaceNames Names::namespace_names(
    const wsdl::TypeTable& types, const std::span<const std::string_view> extra_namespaces) {
    NamespaceNames scopes;
    std::unordered_map<std::string, std::size_t> counts;

    const auto add_namespace = [&](const std::string_view uri) {
        if (uri == xsd::ns_uri)
            return; // The scope of XML schema builtins is fixed, and they are not counted for collisions.

        // Different ns uris may map to the same C++ namespace path; let's notice if this happens
        const auto [entry, inserted] = scopes.emplace(std::string { uri }, namespace_path(uri));
        if (inserted)
            ++counts[entry->second];
    };

    types.for_each([&](const auto& type) {
        if (type.name())
            add_namespace(type.name()->ns_uri());
    });

    for (const auto uri : extra_namespaces)
        add_namespace(uri);

    // Protect naturally unique paths before suffixes are assigned to colliding paths.
    // std::map keeps the choice of which URI gets a suffix deterministic.
    std::unordered_set<std::string> used;
    for (const auto& scope : scopes | std::views::values)
        if (counts[scope] == 1)
            used.emplace(scope);
    for (auto& scope : scopes | std::views::values)
        if (counts[scope] > 1)
            scope = claim(scope, used);

    for (auto& scope : scopes | std::views::values)
        scope = std::string{ generated_namespace } + "::" + scope;

    scopes.emplace(std::string{ xsd::ns_uri }, std::string{ builtin_namespace });

    return scopes;
}

Names::Names(const wsdl::TypeTable& types, const std::span<const std::string_view> extra_namespaces) :
    namespaces_{ namespace_names(types, extra_namespaces) }, types_(types.size()) {

    assign_scopes(types);
    assign_type_names(types);
    assign_member_names(types);
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
    std::vector<std::string> bases(types.size());
    std::unordered_map<std::string, std::size_t> counts;
    UsedNames used;

    for (const auto& scope : namespaces_ | std::views::values)
        if (scope != builtin_namespace)
            reserve_child_namespaces(scope, used);

    types.for_each([&](const auto& type) {
        if (std::holds_alternative<xsd::BuiltinType>(type.definition())) {
            const auto& name = types_[type.id()];
            used[name.scope].emplace(name.local);
            return;
        }

        const auto id = type.id();
        const auto& scope = types_[id].scope;

        bases[id] = type.name() ? identifier(type.name()->local_name()) : "AnonymousType_" + std::to_string(id);
        ++counts[scope + "::" + bases[id]];
    });

    // Case 1: A type name is unique in its namespace, and no child namespace has the same name.
    types.for_each([&](const auto& type) {
        const auto id = type.id();
        const auto& scope = types_[id].scope;

        if (!bases[id].empty() && counts[scope + "::" + bases[id]] == 1 && !used[scope].contains(bases[id])) {
            types_[id].local = bases[id];
            used[scope].emplace(bases[id]);
        }
    });

    // Case 2: Resolve remaining collisions for named declarations before anonymous types.
    types.for_each([&](const auto& type) {
        const auto id = type.id();

        if (!bases[id].empty() && types_[id].local.empty() && type.name())
            types_[id].local = claim(bases[id], used[types_[id].scope]);
    });

    // Case 3: Resolve remaining collisions for anonymous types.
    types.for_each([&](const auto& type) {
        const auto id = type.id();

        if (!bases[id].empty() && types_[id].local.empty())
            types_[id].local = claim(bases[id], used[types_[id].scope]);
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

        const auto element_count = sequence ? sequence->elements.size() : 0;
        std::vector<std::string> bases(element_count + attributes.size());

        for (std::size_t i = 0; i < element_count; ++i) {
            const auto& element = sequence->elements[i];
            if (element.occurs.max != 0)
                bases[i] = identifier(element.name);
        }

        for (std::size_t i = 0; i < attributes.size(); ++i) {
            if (attributes[i].prohibited)
                continue;
            bases[element_count + i] = identifier(attributes[i].name);
        }

        auto& named = types_[id];
        auto used = assigned_in_scope;
        used.emplace(named.local); // Reserve this class's constructor name in its own scope.
        const auto members = assign_local_names(bases, std::move(used));

        named.elements.assign(members.begin(), members.begin() + element_count);
        named.attributes.assign(members.begin() + element_count, members.end());
        for (const auto& member : members)
            if (!member.empty())
                assigned_in_scope.emplace(member);

        state = State::visited;
    };

    types.for_each([&](const auto& type) { assign(type.id()); });
}

std::string_view Names::namespace_name(const std::string_view uri) const {
    const auto entry = namespaces_.find(uri);
    if (entry == namespaces_.end())
        throw std::out_of_range{"No C++ namespace planned for URI: " + std::string{uri}};

    return entry->second;
}

std::string_view Names::type(const TypeRef id) const {
    const auto& name = types_.at(id).local;
    if (name.empty())
        throw std::invalid_argument{"Type has no C++ name"};

    return name;
}

std::string_view Names::type_namespace(const TypeRef id) const {
    return types_.at(id).scope;
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

} // namespace soapp::cpp
