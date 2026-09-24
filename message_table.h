//
// Created by Salvo Passaro on 21/09/26.
//

#ifndef SOAPP_MESSAGE_TABLE_H
#define SOAPP_MESSAGE_TABLE_H

#include "type_ids.h"
#include "xml.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace soapp::wsdl {

class Message {
public:
    class Part {
        std::string name;
        TypeRef type;
        std::optional<xml::qname> element;

        friend class Message;

    public:
        Part(std::string name, TypeRef type, std::optional<xml::qname> element = std::nullopt) :
        name{std::move(name)}, type{type}, element{std::move(element)} {}

        [[nodiscard]] const std::string& get_name() const noexcept { return name; }
        [[nodiscard]] TypeRef get_type() const noexcept { return type; }
        [[nodiscard]] const std::optional<xml::qname>& get_element() const noexcept { return element; }
    };

    Message(xml::qname name, std::vector<Part> parts) : name{std::move(name)}, parts{std::move(parts)} {}

    [[nodiscard]] const xml::qname& get_name() const noexcept { return name; }
    [[nodiscard]] const std::vector<Part>& get_parts() const noexcept { return parts; }
private:
    xml::qname name;
    std::vector<Part> parts;
};

using MessageTable = std::unordered_map<xml::qname, Message, xml::qname::hash>;

}

#endif //SOAPP_MESSAGE_TABLE_H
