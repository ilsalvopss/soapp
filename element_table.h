//
// Created by Salvo Passaro on 21/09/26.
//

#ifndef SOAPP_ELEMENT_TABLE_H
#define SOAPP_ELEMENT_TABLE_H

#include "type_ids.h"
#include "xml.h"

#include <optional>
#include <unordered_map>
#include <vector>

namespace soapp::xsd {

class ElementTable {
public:
    using ElementId = std::uint32_t;

    ElementTable() = default;

    [[nodiscard]] ElementId declare(const xml::qname& name);

    void define(ElementId id, TypeRef type);

    [[nodiscard]] std::optional<ElementId> find(const xml::qname& name) const noexcept;

    [[nodiscard]] TypeRef resolve(const xml::qname& name) const;

    [[nodiscard]] std::size_t size() const noexcept;

private:
    [[nodiscard]] std::optional<TypeRef>& get(ElementId id);

    [[nodiscard]] const std::optional<TypeRef>& get(ElementId id) const;

    ElementId next_id_ = 0;
    std::vector<std::optional<TypeRef>> types_;
    std::unordered_map<xml::qname, ElementId, xml::qname::hash> names_;
};

}

#endif //SOAPP_ELEMENT_TABLE_H
