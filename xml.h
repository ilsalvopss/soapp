//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_XML_H
#define SOAPP_XML_H

#include "libxml/xmlreader.h"
#include <fmt/chrono.h>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#define xml_time(time)      fmt::format("{:%FT%TZ}", std::chrono::round<std::chrono::seconds>(time))
#define str_bool(value)     value ? "true" : "false"

namespace soapp::xml {

class qname {
public:
    qname() = default;

    qname(const std::string_view local_name, const std::string_view namespace_uri) :
    local_name_{local_name}, ns_uri_{namespace_uri} {}

    [[nodiscard]] std::string_view local_name() const noexcept {
        return local_name_;
    }

    [[nodiscard]] std::string_view ns_uri() const noexcept {
        return ns_uri_;
    }

    bool operator==(const qname& other) const noexcept {
        return local_name_ == other.local_name_ && ns_uri_ == other.ns_uri_;
    }

private:
    std::string local_name_;
    std::string ns_uri_;
};

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

class owned_string {
public:
    explicit owned_string(xmlChar* p) noexcept : ptr_{p} {}

    [[nodiscard]] std::string_view view() const noexcept {
        return detail::as_string_view(ptr_.get());
    }

    [[nodiscard]] std::string_view operator*() const noexcept {
        return view();
    }

    [[nodiscard]] const char* c_str() const noexcept {
        return reinterpret_cast<const char*>(ptr_.get());
    }

private:
    struct deleter {
        void operator()(xmlChar* p) const noexcept {
            xmlFree(p);
        }
    };

    std::unique_ptr<xmlChar, deleter> ptr_;
};

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

    [[nodiscard]] std::string_view ns_uri() const noexcept {
        return detail::as_string_view(node_->ns ? node_->ns->href : nullptr);
    }

    [[nodiscard]] owned_string text() const {
        auto* content = xmlNodeGetContent(node_);

        if (!content)
            throw error{"!OOM!"};

        return owned_string {content};
    }

    [[nodiscard]] bool is(const std::string_view local_name, const std::string_view namespace_uri) const noexcept {
        return name() == local_name &&
               ns_uri() == namespace_uri;
    }

    [[nodiscard]] std::optional<owned_string> attribute(
        const std::string_view local_name, const std::string_view namespace_uri = {}) const {
        xmlChar* value = nullptr;

        const std::string local_name_s{local_name};

        const auto r =
            xmlNodeGetAttrValue(node_,
                detail::as_xml(local_name_s),
                namespace_uri.empty() ? nullptr : detail::as_xml(std::string{namespace_uri}),
                &value);

        if (r == -1)
            throw error{"!OOM!"};
        if (r == 1)
            return std::nullopt;

        return owned_string {value};
    }

    [[nodiscard]] qname resolve_qname(const std::string_view value) const {
        const auto separator = value.find(':');

        if (separator == std::string_view::npos)
            return qname{value, {}};

        const auto prefix = value.substr(0, separator);
        const auto local_name = value.substr(separator + 1);

        const std::string prefix_string{prefix};
        const auto* ns = xmlSearchNs(node_->doc, const_cast<xmlNode*>(node_), detail::as_xml(prefix_string));

        if (!ns)
            throw error{fmt::format("Unknown XML namespace prefix: {}", prefix)};

        return qname{local_name, detail::as_string_view(ns->href)};
    }

    [[nodiscard]] std::vector<node_view> children() const {
        std::vector<node_view> nodes;

        for (const auto* child = node_->children; child; child = child->next) {
            if (child->type == XML_ELEMENT_NODE)
                nodes.push_back(node_view{child});
        }

        return nodes;
    }

    [[nodiscard]] std::vector<node_view> children(
        const std::string_view local_name, const std::string_view namespace_uri) const {
        std::vector<node_view> nodes;

        for (auto child : children()) {
            if (child.is(local_name, namespace_uri))
                nodes.push_back(child);
        }

        return nodes;
    }

    [[nodiscard]] std::optional<node_view> child(
        const std::string_view local_name, const std::string_view namespace_uri) const noexcept {
        for (auto* child = node_->children; child; child = child->next) {
            if (child->type != XML_ELEMENT_NODE)
                continue;

            if (node_view view{child}; view.is(local_name, namespace_uri))
                return view;
        }

        return std::nullopt;
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

    [[nodiscard]] std::optional<node_view> root() const {
        const auto* root = xmlDocGetRootElement(doc_.get());

        if (!root)
            return std::nullopt;

        return node_view{root};
    }

private:
    using ptr = std::unique_ptr<xmlDoc, deleter>;

    explicit document(ptr doc) : doc_{std::move(doc)} {
        if (!doc_)
            throw error{"null XML document"};
    }

    ptr doc_;
};

}

#endif //SOAPP_XML_H
