//
// Created by Salvo Passaro on 21/09/26.
//

#include "element_table.h"

#include "xsd_types.h"

#include <fmt/format.h>

#include <stdexcept>

namespace soapp::xsd {

ElementTable::ElementId ElementTable::declare(const xml::qname& name) {
    if (names_.contains(name))
        throw error{
            fmt::format("Duplicate XSD element declaration: {}:{}", name.ns_uri(), name.local_name())
        };

    const auto id = next_id_++;
    names_.emplace(name, id);
    types_.emplace_back(std::nullopt);
    return id;
}

void ElementTable::define(const ElementId id, const TypeRef type) {
    auto& element_type = get(id);
    if (element_type)
        throw error{fmt::format("Element {} is already defined", id)};

    element_type = type;
}

TypeRef ElementTable::resolve(const xml::qname& name) const {
    if (const auto id = find(name)) {
        if (const auto type = get(*id))
            return *type;

        throw error{fmt::format("Element {}:{} is not defined", name.ns_uri(), name.local_name())};
    }

    throw error{
        fmt::format("Unknown XSD element: {}:{}", name.ns_uri(), name.local_name())
    };
}

std::optional<ElementTable::ElementId> ElementTable::find(const xml::qname& name) const noexcept {
    if (const auto it = names_.find(name); it != names_.end())
        return it->second;

    return std::nullopt;
}

std::size_t ElementTable::size() const noexcept {
    return types_.size();
}

std::optional<TypeRef>& ElementTable::get(const ElementId id) {
    if (id >= types_.size())
        throw std::out_of_range{"Invalid XSD ElementId"};

    return types_[id];
}

const std::optional<TypeRef>& ElementTable::get(const ElementId id) const {
    if (id >= types_.size())
        throw std::out_of_range{"Invalid XSD ElementId"};

    return types_[id];
}

} // namespace soapp::xsd
