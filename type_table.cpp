//
// Created by Salvo Passaro on 18/09/26.
//

#include "type_table.h"

#include <ranges>
#include <fmt/format.h>

#include <stdexcept>
#include <utility>

namespace soapp::wsdl {

TypeTable::Type::Type(std::optional<xml::qname> name, const TypeRef id, Definition definition) :
    name_{std::move(name)}, id_{id}, definition_{std::move(definition)} {}

TypeRef TypeTable::Type::id() const noexcept {
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
    for (const auto& name: xsd::builtin_types | std::views::keys)
        (void)add_builtin(name);

    (void)declare(xml::qname{"unimplementedType", xsd::ns_uri});
}

TypeRef TypeTable::declare(const xml::qname& name) {
    if (names_.contains(name))
        throw std::runtime_error{
            fmt::format("Duplicate XSD type declaration: {}:{}", name.ns_uri(), name.local_name())
        };

    const auto id = next_id_++;
    names_.emplace(name, id);
    types_.emplace_back(name, id, std::monostate{});
    return id;
}

TypeRef TypeTable::add_anonymous() {
    const auto id = next_id_++;
    types_.emplace_back(std::nullopt, id, std::monostate{});
    return id;
}

void TypeTable::define(const TypeRef id, Definition&& definition) {
    auto& type = get(id);
    if (type.defined())
        throw std::runtime_error{ fmt::format("Type {} is already defined", id) };

    type.definition_ = std::move(definition);
}

std::optional<TypeRef> TypeTable::find(const xml::qname& name) const noexcept {
    if (const auto it = names_.find(name); it != names_.end())
        return it->second;

    return std::nullopt;
}

void TypeTable::for_each(const std::function<void(const Type&)>& callback) const {
    for (const auto& type : types_)
        callback(type);
}

TypeRef TypeTable::resolve(const xml::qname& name) const {
    if (const auto id = find(name))
        return *id;

    throw std::runtime_error{
        fmt::format("Unknown XSD type: {}:{}", name.ns_uri(), name.local_name())
    };
}

const TypeTable::Type& TypeTable::get(const TypeRef id) const {
    if (id >= types_.size())
        throw std::out_of_range{"Invalid TypeRef"};

    return types_[id];
}

std::size_t TypeTable::size() const noexcept {
    return types_.size();
}

TypeTable::Type& TypeTable::get(const TypeRef id) {
    if (id >= types_.size())
        throw std::out_of_range{"Invalid TypeRef"};

    return types_[id];
}

TypeRef TypeTable::add_builtin(const std::string_view local_name) {
    const auto id = declare(xml::qname{ local_name, xsd::ns_uri });
    define(id, xsd::make_builtin_type(local_name));
    return id;
}

} // namespace soapp::wsdl
