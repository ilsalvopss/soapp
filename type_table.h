//
// Created by Salvo Passaro on 18/09/26.
//

#ifndef SOAPP_TYPE_TABLE_H
#define SOAPP_TYPE_TABLE_H

#include "builtin_types.h"
#include "xml.h"
#include "xsd_types.h"

#include <fmt/format.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace soapp::wsdl {

class TypeTable {
public:
    using TypeId = std::uint32_t;

    using Definition = std::variant<
        std::monostate,
        xsd::BuiltinType,
        xsd::SimpleParsedType,
        xsd::ComplexParsedType
    >;

    class Type {
        std::optional<xml::qname> name_;
        TypeId id_;
        Definition definition_;

        friend class TypeTable;

    public:
        Type(std::optional<xml::qname> name, const TypeId id, Definition definition) :
            name_{std::move(name)}, id_{id}, definition_{std::move(definition)} {}

        [[nodiscard]] TypeId id() const noexcept {
            return id_;
        }

        [[nodiscard]] const std::optional<xml::qname>& name() const noexcept {
            return name_;
        }

        [[nodiscard]] const Definition& definition() const noexcept {
            return definition_;
        }

        [[nodiscard]] Definition& definition() noexcept {
            return definition_;
        }

        [[nodiscard]] bool defined() const noexcept {
            return !std::holds_alternative<std::monostate>(definition_);
        }
    };

    TypeTable() {
        for (const auto name : xsd::builtinTypes)
            (void)add_builtin(name);
    }

    // Reserve a named type before parsing its body.
    [[nodiscard]] TypeId declare(const xml::qname& name) {
        if (names_.contains(name))
            throw std::runtime_error{
                fmt::format("Duplicate XSD type declaration: {}:{}", name.ns_uri(), name.local_name())
            };

        const auto id = next_id_++;
        names_.emplace(name, id);
        types_.emplace_back(name, id, std::monostate{});
        return id;
    }

    [[nodiscard]] TypeId add_anonymous() {
        const auto id = next_id_++;
        types_.emplace_back(std::nullopt, id, std::monostate{});
        return id;
    }

    void define(const TypeId id, Definition definition) {
        auto& type = get(id);
        if (type.defined())
            throw std::runtime_error{ fmt::format("Type {} is already defined", id) };

        type.definition_ = std::move(definition);
    }

    [[nodiscard]] std::optional<TypeId> find(const xml::qname& name) const noexcept {
        if (const auto it = names_.find(name); it != names_.end())
            return it->second;

        return std::nullopt;
    }

    [[nodiscard]] TypeId resolve(const xml::qname& name) const {
        if (const auto id = find(name))
            return *id;

        throw std::runtime_error{
            fmt::format("Unknown XSD type: {}:{}", name.ns_uri(), name.local_name())};
    }

    [[nodiscard]] const Type& get(const TypeId id) const {
        if (id >= types_.size())
            throw std::out_of_range{"Invalid XSD TypeId"};

        return types_[id];
    }

private:
    [[nodiscard]] Type& get(const TypeId id) {
        if (id >= types_.size())
            throw std::out_of_range{"Invalid XSD TypeId"};

        return types_[id];
    }

    [[nodiscard]] TypeId add_builtin(const std::string_view local_name) {
        const auto id = declare(xml::qname{local_name, xsd::ns_uri});
        define(id, xsd::BuiltinType{});
        return id;
    }

    TypeId next_id_ = 0;
    std::vector<Type> types_;
    std::unordered_map<xml::qname, TypeId, xml::qname::hash> names_;
};

}

#endif //SOAPP_TYPE_TABLE_H
