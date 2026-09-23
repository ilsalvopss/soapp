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

struct MessagePart {
    std::string name;
    xsd::TypeRef type;
    std::optional<xml::qname> element;
};

struct Message {
    xml::qname name;
    std::vector<MessagePart> parts;
};

using MessageTable = std::unordered_map<xml::qname, Message, xml::qname::hash>;

}

#endif //SOAPP_MESSAGE_TABLE_H
