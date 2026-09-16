//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_WSDL_H
#define SOAPP_WSDL_H

#include "xml.h"
#include "xsd_types.h"

namespace soapp::wsdl {
// https://www.w3.org/TR/2001/NOTE-wsdl-20010315
// NOTE: there is also version 2.0 but onvif uses 1.1
// https://stackoverflow.com/questions/66155090/how-to-find-wsdl-version

class WSDL11 : protected xml::document {
    static constexpr std::string_view ns_uri = "http://schemas.xmlsoap.org/wsdl/";

    const xml::node_view definitions; // fast track to root node of the WSDL document

    // private helper to find the definitions node in a WSDL document
    [[nodiscard]] static xml::node_view get_definitions(const xml::document& doc) {
        const auto root = doc.root();
        if (!root || root->name() != "definitions" || root->ns_uri() != ns_uri)
            throw error{"Root node is not definitions"};

        return *root;
    }

public:
    class error : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    explicit WSDL11(const std::string_view source) : WSDL11(xml::document::parse(source)) {}

    // A WSDL document is simply a set of definitions.
    // There is a definitions element at the root, and definitions inside.
    explicit WSDL11(xml::document&& doc) :
    xml::document(std::move(doc)), definitions(get_definitions(*this)) {
        // check for <import>s and handle them if necessary
        // to my understanding that is just: parse this other wsdl and merge the definitions into this one..

        if (auto types = definitions.child("types", ns_uri)) {
            // here we can ONLY have <xs:schema>s

            for (const auto schema : types->children()) {
                xsd::XSDSchema xsd{schema};

                for (const auto& simple_type : xsd.parse_simple()) {
                    fmt::print("Parsed simple type: {}\n", simple_type.print());
                }
            }
        }
    }
};

}

#endif //SOAPP_WSDL_H
