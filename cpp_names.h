//
// Created by Salvo Passaro on 08/10/26.
//

#ifndef SOAPP_CPP_NAMES_H
#define SOAPP_CPP_NAMES_H

#include "type_ids.h"

#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace soapp::wsdl {
class TypeTable;
}

namespace soapp::cpp {

class Names {
public:
    explicit Names(const wsdl::TypeTable& types, std::span<const std::string_view> extra_namespaces = {});

    [[nodiscard]] std::string_view namespace_name(std::string_view uri) const;

    // Builtin names refer to cpp_builtin_types.h; other type names are generated.
    // type() is local to type_namespace(); qualified_type() includes the scope.
    [[nodiscard]] std::string_view type(TypeRef id) const;
    [[nodiscard]] std::string_view type_namespace(TypeRef id) const;
    [[nodiscard]] std::string qualified_type(TypeRef id) const;

private:
    using NamespaceNames = std::map<std::string, std::string, std::less<>>;

    struct TypeName {
        std::string scope; // namespace name, e.g. "soapp::xsd"
        std::string local; // local name, e.g. "String"
    };

    NamespaceNames namespaces_;     // maps namespace URI to C++ namespace name
    std::vector<TypeName> types_;

    static NamespaceNames namespace_names(
        const wsdl::TypeTable& types, std::span<const std::string_view> extra_namespaces);

    void assign_scopes(const wsdl::TypeTable& types);
    void assign_type_names(const wsdl::TypeTable& types);
};

} // namespace soapp::cpp

#endif // SOAPP_CPP_NAMES_H
