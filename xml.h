//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_XML_H
#define SOAPP_XML_H

#include "libxml/xmlreader.h"
#include <fmt/chrono.h>

#define xml_time(time)      fmt::format("{:%FT%TZ}", std::chrono::round<std::chrono::seconds>(time))
#define str_bool(value)     value ? "true" : "false"

namespace soapp::xml {

namespace detail {
    [[nodiscard]] inline const xmlChar* as_xml(const char* str) noexcept {
        return reinterpret_cast<const xmlChar*>(str);
    }

    [[nodiscard]] inline const xmlChar* as_xml(const std::string& str) noexcept {
        return as_xml(str.c_str());
    }

    [[nodiscard]] inline std::string_view as_string_view(const xmlChar* str) noexcept {
        if (!str)
            return {};

        return reinterpret_cast<const char*>(str);
    }
}

class error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class node_view {
    // let's be constructible only via a document
    friend class document;

    explicit node_view(const xmlNode* node) noexcept : node_{node} {}

public:
    [[nodiscard]] std::string_view name() const noexcept {
        return detail::as_string_view(node_->name);
    }

private:
    const xmlNode* node_;
};

class document {
    struct deleter {
        void operator()(xmlDoc* doc) const noexcept {
            xmlFreeDoc(doc);
        }
    };

public:
    // move only
    document(document&&) noexcept = default;
    document& operator=(document&&) noexcept = default;

    document(const document&) = delete;
    document& operator=(const document&) = delete;

    [[nodiscard]] static document parse(const std::string_view source) {
        const std::unique_ptr<xmlParserCtxt, decltype(&xmlFreeParserCtxt)>
        ctxt = { xmlNewParserCtxt(), xmlFreeParserCtxt };

        if (!ctxt)
            throw error{"!OOM!"};

        // TODO? xmlCtxtSetErrorHandler

        auto* raw_doc = xmlCtxtReadMemory(
            ctxt.get(),
            source.data(),
            static_cast<int>(source.size()),
            nullptr,
            nullptr,
            XML_PARSE_NO_XXE
        );

        if (!raw_doc) {
            if (const auto* err = xmlCtxtGetLastError(ctxt.get()); err && err->message)
                throw error{err->message};

            throw error{"XML parsing failed"};
        }

        return document{ptr{raw_doc}};
    }

    [[nodiscard]] explicit document(const std::string_view version) {
        // libxml2 wants a null-terminated string
        const std::string version_string{version};

        doc_.reset(xmlNewDoc(detail::as_xml(version_string)));

        if (!doc_)
            throw error{"!OOM!"};
    }

    [[nodiscard]] std::optional<node_view> root() const;

private:
    using ptr = std::unique_ptr<xmlDoc, deleter>;

    explicit document(ptr);

    ptr doc_;
};

}

#endif //SOAPP_XML_H
