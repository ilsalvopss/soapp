//
// Created by Salvo Passaro on 15/09/26.
//

#include "xml.h"

#include <utility>

namespace soapp::xml {

bool uri::check_local_scheme(const zstring_view value) {
    const std::unique_ptr<xmlURI, decltype(&xmlFreeURI)> parsed{ xmlParseURI(value.c_str()), &xmlFreeURI };

    if (!parsed)
        throw error{ "Invalid URI" };

    if (!parsed->scheme)
        throw error{ "URI must be absolute" };

    return xmlStrcasecmp(BAD_CAST parsed->scheme, BAD_CAST "file") == 0;
}

std::string uri::escape_path(const std::filesystem::path& path) {
    const std::string value = path.generic_string(); // seems to be the one....

    const owned_string escaped{ xmlURIEscapeStr(detail::as_xml(value), BAD_CAST "/:") };
    if (!escaped)
        throw error{"Failed to escape filesystem path: " + value};

    return std::string{ escaped.view() };
}

uri::uri(std::string value) :
    value_{std::move(value)}, local_{check_local_scheme(zstring_view { value_ })} {}

uri uri::from_path(const std::filesystem::path& path) {
    if (path.empty())
        throw error{"Filesystem path must not be empty"};

    const auto absolute = std::filesystem::absolute(path);
    const std::string escaped = escape_path(absolute);

    return uri{"file://" + escaped};
}

uri uri::from_path(const std::string_view path) {
    return from_path(std::filesystem::path { std::string { path } });
}

uri uri::resolve(const zstring_view ref) const {
    const owned_string resolved { xmlBuildURI(detail::as_xml(ref), detail::as_xml(value_)) };
    if (!resolved)
        throw error { "Failed to resolve URI " + std::string{ref.view()} + " against base " + value_ };

    return uri { std::string { resolved.view() } };
}

std::string_view uri::string() const noexcept {
    return value_;
}

const char* uri::c_str() const noexcept {
    return value_.c_str();
}

bool uri::local() const noexcept {
    return local_;
}

std::string_view node_view::name() const noexcept {
    return detail::as_string_view(node_->name);
}

std::string_view node_view::ns_uri() const noexcept {
    return detail::as_string_view(node_->ns ? node_->ns->href : nullptr);
}

owned_string node_view::text() const {
    auto* content = xmlNodeGetContent(node_);

    if (!content)
        throw error{"!OOM!"};

    return owned_string {content};
}

bool node_view::is(const std::string_view local_name, const std::string_view namespace_uri) const noexcept {
    return name() == local_name &&
           ns_uri() == namespace_uri;
}

std::optional<owned_string> node_view::attribute(
    const zstring_view local_name, const zstring_view namespace_uri) const {
    xmlChar* value = nullptr;

    const auto r =
        xmlNodeGetAttrValue(node_,
            detail::as_xml(local_name),
            namespace_uri.empty() ? nullptr : detail::as_xml(namespace_uri),
            &value);

    if (r == -1)
        throw error{"!OOM!"};
    if (r == 1)
        return std::nullopt;

    return owned_string {value};
}

qname node_view::resolve_qname(const std::string_view value) const {
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

std::vector<node_view> node_view::children() const {
    std::vector<node_view> nodes;

    for (const auto* child = node_->children; child; child = child->next) {
        if (child->type == XML_ELEMENT_NODE)
            nodes.push_back(node_view{child});
    }

    return nodes;
}

std::vector<node_view> node_view::children(
    const std::string_view local_name, const std::string_view namespace_uri) const {
    std::vector<node_view> nodes;

    for (auto child : children()) {
        if (child.is(local_name, namespace_uri))
            nodes.push_back(child);
    }

    return nodes;
}

std::optional<node_view> node_view::child(
    const std::string_view local_name, const std::string_view namespace_uri) const noexcept {
    for (auto* child = node_->children; child; child = child->next) {
        if (child->type != XML_ELEMENT_NODE)
            continue;

        if (node_view view{child}; view.is(local_name, namespace_uri))
            return view;
    }

    return std::nullopt;
}

document document::parse(const std::string_view source, uri&& base) {
    const std::unique_ptr<xmlParserCtxt, decltype(&xmlFreeParserCtxt)>
    ctxt = { xmlNewParserCtxt(), xmlFreeParserCtxt };

    if (!ctxt)
        throw error{"!OOM!"};

    // TODO? xmlCtxtSetErrorHandler

    auto* raw_doc = xmlCtxtReadMemory(
        ctxt.get(),
        source.data(),
        static_cast<int>(source.size()),
        base.string().empty() ? nullptr : base.c_str(),
        nullptr,
        XML_PARSE_NO_XXE
    );

    if (!raw_doc) {
        if (const auto* err = xmlCtxtGetLastError(ctxt.get()); err && err->message)
            throw error{err->message};

        throw error{"XML parsing failed"};
    }

    return document{ptr{raw_doc}, std::move( base )};
}

document::document(const zstring_view version, uri&& base) : base_{std::move(base)} {
    doc_.reset(xmlNewDoc(detail::as_xml(version)));

    if (!doc_)
        throw error{"!OOM!"};
}

std::optional<node_view> document::root() const {
    const auto* root = xmlDocGetRootElement(doc_.get());

    if (!root)
        return std::nullopt;

    return node_view{root};
}

uri document::base() const noexcept {
    return base_;
}

document::document(ptr doc, uri&& base) : doc_{std::move(doc)}, base_{std::move(base)} {
    if (!doc_)
        throw error{"null XML document"};
}

} // namespace soapp::xml
