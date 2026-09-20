//
// Created by Salvo Passaro on 18/09/26.
//

#include "type_table.h"

#include <fmt/format.h>

#include <stdexcept>
#include <utility>

namespace soapp::wsdl {

TypeTable::Type::Type(std::optional<xml::qname> name, const TypeId id, Definition definition) :
    name_{std::move(name)}, id_{id}, definition_{std::move(definition)} {}

TypeTable::TypeId TypeTable::Type::id() const noexcept {
    return id_;
}

const std::optional<xml::qname>& TypeTable::Type::name() const noexcept {
    return name_;
}

const TypeTable::Definition& TypeTable::Type::definition() const noexcept {
    return definition_;
}

TypeTable::Definition& TypeTable::Type::definition() noexcept {
    return definition_;
}

bool TypeTable::Type::defined() const noexcept {
    return !std::holds_alternative<std::monostate>(definition_);
}

TypeTable::TypeTable() {
    for (const auto name : xsd::builtinTypes)
        (void)add_builtin(name);

    (void)declare(xml::qname{"unimplementedType", xsd::ns_uri});
}

TypeTable::TypeId TypeTable::declare(const xml::qname& name) {
    if (names_.contains(name))
        throw std::runtime_error{
            fmt::format("Duplicate XSD type declaration: {}:{}", name.ns_uri(), name.local_name())
        };

    const auto id = next_id_++;
    names_.emplace(name, id);
    types_.emplace_back(name, id, std::monostate{});
    return id;
}

TypeTable::TypeId TypeTable::add_anonymous() {
    const auto id = next_id_++;
    types_.emplace_back(std::nullopt, id, std::monostate{});
    return id;
}

void TypeTable::define(const TypeId id, Definition&& definition) {
    auto& type = get(id);
    if (type.defined())
        throw std::runtime_error{ fmt::format("Type {} is already defined", id) };

    type.definition_ = std::move(definition);
}

std::optional<TypeTable::TypeId> TypeTable::find(const xml::qname& name) const noexcept {
    if (const auto it = names_.find(name); it != names_.end())
        return it->second;

    return std::nullopt;
}

TypeTable::TypeId TypeTable::resolve(const xml::qname& name) const {
    if (const auto id = find(name))
        return *id;

    throw std::runtime_error{
        fmt::format("Unknown XSD type: {}:{}", name.ns_uri(), name.local_name())
    };
}

const TypeTable::Type& TypeTable::get(const TypeId id) const {
    if (id >= types_.size())
        throw std::out_of_range{"Invalid XSD TypeId"};

    return types_[id];
}

std::size_t TypeTable::size() const noexcept {
    return types_.size();
}

TypeTable::Type& TypeTable::get(const TypeId id) {
    if (id >= types_.size())
        throw std::out_of_range{"Invalid XSD TypeId"};

    return types_[id];
}

TypeTable::TypeId TypeTable::add_builtin(const std::string_view local_name) {
    const auto id = declare(xml::qname{local_name, xsd::ns_uri});
    define(id, xsd::BuiltinType{});
    return id;
}

} // namespace soapp::wsdl
