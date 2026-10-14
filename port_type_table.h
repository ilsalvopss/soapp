//
// Created by Salvo Passaro on 13/10/26.
//

#ifndef SOAPP_PORT_TYPE_TABLE_H
#define SOAPP_PORT_TYPE_TABLE_H

#include "xml.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace soapp::wsdl {

// https://www.w3.org/TR/2001/NOTE-wsdl-20010315#_porttypes
class PortType {
public:
    // WSDL has four transmission primitives (aka. operations) that an endpoint can support:
    // 1. One-way. The endpoint receives a message.
    // 2. Request-response. The endpoint receives a message, and sends a correlated message.
    // 3. Solicit-response. The endpoint sends a message, and receives a correlated message. [not supported yet]
    // 4. Notification. The endpoint sends a message. [not supported yet]
    class Operation {
        std::string name;                         // unique within the port type
        xml::qname input_message;                 // always present, (1 and 2)
        std::optional<xml::qname> output_message; // present for (2) only

    public:
        Operation(std::string name, xml::qname input_message, std::optional<xml::qname> output_message = std::nullopt) :
            name{ std::move(name) }, input_message{ std::move(input_message) },
            output_message{ std::move(output_message) } {}

        [[nodiscard]] const std::string& get_name() const noexcept { return name; }
        [[nodiscard]] const xml::qname& get_input_message() const noexcept { return input_message; }
        [[nodiscard]] const std::optional<xml::qname>& get_output_message() const noexcept { return output_message; }
    };

    PortType(xml::qname name, std::vector<Operation> operations) :
        name{ std::move(name) }, operations{ std::move(operations) } {}

    [[nodiscard]] const xml::qname& get_name() const noexcept { return name; }
    [[nodiscard]] const std::vector<Operation>& get_operations() const noexcept { return operations; }

private:
    xml::qname name;                    // unique within the target ns (i guess..)
    std::vector<Operation> operations;
};

using PortTypeTable = std::unordered_map<xml::qname, PortType, xml::qname::hash>;

} // namespace soapp::wsdl

#endif //SOAPP_PORT_TYPE_TABLE_H
