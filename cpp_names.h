//
// Created by Salvo Passaro on 08/10/26.
//

#ifndef SOAPP_CPP_NAMES_H
#define SOAPP_CPP_NAMES_H

#include "type_ids.h"
#include "xml.h"

#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace soapp::wsdl {
class TypeTable;
struct CompilationContext;
}

namespace soapp::cpp {

class Names {
public:
    explicit Names(const wsdl::TypeTable& types, std::span<const std::string_view> extra_namespaces = {});
    explicit Names(const wsdl::CompilationContext& context);

    [[nodiscard]] std::string_view namespace_name(std::string_view uri) const;

    // Builtin names refer to cpp_builtin_types.h; other type names are generated.
    // type() is local to type_namespace(); qualified_type() includes the scope.
    [[nodiscard]] std::string_view type(TypeRef id) const;
    [[nodiscard]] std::string_view type_namespace(const TypeRef id) const { return types_.at(id).scope; };
    [[nodiscard]] std::string qualified_type(TypeRef id) const;

    // Indices match the IR's sequence and attribute vectors; omitted fields have empty names.
    [[nodiscard]] std::string_view element(TypeRef owner, std::size_t index) const;
    [[nodiscard]] std::string_view attribute(TypeRef owner, std::size_t index) const;

    [[nodiscard]] std::string_view message(const xml::qname& name) const { return messages_.at(name).local; }
    [[nodiscard]] std::string qualified_message(const xml::qname& name) const;
    [[nodiscard]] std::string_view part(const xml::qname& owner, std::size_t index) const;

    [[nodiscard]] std::string_view port_type(const xml::qname& name) const { return port_types_.at(name).local; }
    [[nodiscard]] std::string_view operation(const xml::qname& owner, std::size_t index) const;

private:
    using NamespaceNames = std::map<std::string, std::string, std::less<>>;

    struct TypeName {
        std::string scope; // namespace name, e.g. "soapp::xsd"
        std::string local; // local name, e.g. "String"
        std::vector<std::string> elements;
        std::vector<std::string> attributes;
    };

    struct DeclarationName {
        std::string local;
        std::vector<std::string> members; // Message parts or port-type operations, in IR order.
    };

    using DeclarationNames = std::unordered_map<xml::qname, DeclarationName, xml::qname::hash>;

    NamespaceNames namespaces_;     // maps namespace URI to C++ namespace name
    std::vector<TypeName> types_;
    DeclarationNames messages_;
    DeclarationNames port_types_;

    static NamespaceNames namespace_names(
        const wsdl::TypeTable& types, std::span<const std::string_view> extra_namespaces);

    void assign_scopes(const wsdl::TypeTable& types);
    void assign_type_names(const wsdl::TypeTable& types);
    void assign_member_names(const wsdl::TypeTable& types);
    void assign_declaration_names(const wsdl::CompilationContext& context);
};

} // namespace soapp::cpp

#endif // SOAPP_CPP_NAMES_H
