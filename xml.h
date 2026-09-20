//
// Created by Salvo Passaro on 15/09/26.
//

#ifndef SOAPP_XML_H
#define SOAPP_XML_H

#include "libxml/xmlreader.h"
#include "libxml/uri.h"
#include <fmt/chrono.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define xml_time(time)      fmt::format("{:%FT%TZ}", std::chrono::round<std::chrono::seconds>(time))
#define str_bool(value)     value ? "true" : "false"

namespace soapp::xml {

class qname {
public:
    struct hash {
        // https://stackoverflow.com/questions/5889238/why-is-xor-the-default-way-to-combine-hashes
        [[nodiscard]] std::size_t operator()(const qname& name) const noexcept {
            const auto namespace_hash = std::hash<std::string_view>{}(name.ns_uri());
            const auto local_hash = std::hash<std::string_view>{}(name.local_name());
            return namespace_hash ^ (local_hash + 0x9e3779b9 + (namespace_hash << 6) + (namespace_hash >> 2));
        }
    };

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

class zstring_view {
    const char* data_ = nullptr;

public:
    constexpr zstring_view() noexcept = default;

    template <std::size_t N>
    constexpr zstring_view(const char (&s)[N]) noexcept :
    data_{s} {}

    explicit constexpr zstring_view(const char* s) noexcept :
    data_{s} {}

    explicit zstring_view(const std::string& s) noexcept :
    data_{s.c_str()} {}

    [[nodiscard]] constexpr const char* c_str() const noexcept {
        return data_;
    }

    // This conversion is intentionally named: std::string_view does not preserve
    // the null-terminated precondition that this type represents.
    [[nodiscard]] constexpr std::string_view view() const noexcept {
        return data_ ? std::string_view{data_} : std::string_view{};
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return !data_ || *data_ == '\0';
    }
};

namespace detail {
    [[nodiscard]] inline const xmlChar* as_xml(const char* str) noexcept {
        return reinterpret_cast<const xmlChar*>(str);
    }

    [[nodiscard]] inline const xmlChar* as_xml(const std::string& str) noexcept {
        return as_xml(str.c_str());
    }

    [[nodiscard]] inline const xmlChar* as_xml(const zstring_view str) noexcept {
        return as_xml(str.c_str());
    }

    [[nodiscard]] inline zstring_view as_zstring_view(const xmlChar* str) noexcept {
        return zstring_view{reinterpret_cast<const char*>(str)};
    }

    [[nodiscard]] inline std::string_view as_string_view(const xmlChar* str) noexcept {
        return as_zstring_view(str).view();
    }
}

class owned_string {
public:
    owned_string() noexcept = default;
    explicit owned_string(xmlChar* p) noexcept : ptr_{p} {}

    [[nodiscard]] zstring_view zview() const noexcept {
        return detail::as_zstring_view(ptr_.get());
    }

    [[nodiscard]] std::string_view view() const noexcept {
        return zview().view();
    }

    [[nodiscard]] const char* c_str() const noexcept {
        return ptr_ ? reinterpret_cast<const char*>(ptr_.get()) : "";
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    friend std::ostream& operator<<(std::ostream& os, const owned_string& str) {
        return os << str.c_str();
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

class uri {
    std::string value_;
    bool local_;

    // note that this also checks the URI's validity
    // maybe TODO: fix naming
    [[nodiscard]] static bool check_local_scheme(zstring_view value);

    [[nodiscard]] static std::string escape_path(const std::filesystem::path& path);

public:
    // note: remember in the future that a successful construction guarantees that the URI is valid and absolute
    explicit uri(std::string value = {});

    [[nodiscard]] static uri from_path(const std::filesystem::path& path);

    [[nodiscard]] static uri from_path(std::string_view path);

    // note that this is always absolute per our invariant
    [[nodiscard]] uri resolve(zstring_view ref) const;

    [[nodiscard]] std::string_view string() const noexcept;

    [[nodiscard]] const char* c_str() const noexcept;

    [[nodiscard]] bool local() const noexcept;
};


class node_view {
    // let's be constructible only via a document
    friend class document;

    explicit node_view(const xmlNode* node) noexcept : node_{node} {}

public:
    [[nodiscard]] std::string_view name() const noexcept;

    [[nodiscard]] std::string_view ns_uri() const noexcept;

    [[nodiscard]] owned_string text() const;

    [[nodiscard]] bool is(std::string_view local_name, std::string_view namespace_uri) const noexcept;

    [[nodiscard]] std::optional<owned_string> attribute(
        zstring_view local_name, zstring_view namespace_uri = {}) const;

    [[nodiscard]] qname resolve_qname(std::string_view value) const;

    [[nodiscard]] std::vector<node_view> children() const;

    [[nodiscard]] std::vector<node_view> children(
        std::string_view local_name, std::string_view namespace_uri) const;

    [[nodiscard]] std::optional<node_view> child(
        std::string_view local_name, std::string_view namespace_uri) const noexcept;

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

    [[nodiscard]] static document parse(std::string_view source, uri&& base);

    [[nodiscard]] explicit document(zstring_view version, uri&& base);

    [[nodiscard]] std::optional<node_view> root() const;

    [[nodiscard]] uri base() const noexcept;

private:
    using ptr = std::unique_ptr<xmlDoc, deleter>;

    explicit document(ptr doc, uri&& base);

    ptr doc_;
    uri base_;
};

}

#endif //SOAPP_XML_H
