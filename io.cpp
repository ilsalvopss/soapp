//
// Created by Salvo Passaro on 16/09/26.
//

#include "io.h"

#include <array>
#include <memory>
#include <stdexcept>

namespace soapp::io {

curl::curl() {
    if (const auto code = curl_global_init(CURL_GLOBAL_DEFAULT); code != CURLE_OK)
        throw std::runtime_error{"Failed to initialize libcurl"};
}

std::size_t curl::write_callback(const char* ptr, const std::size_t size, std::size_t nmemb, void* userdata) {
    const auto bytes = size * nmemb;
    auto& out = *static_cast<std::string*>(userdata);
    out.append(ptr, bytes);
    return bytes;
}

template<typename T>
void curl::setopt(CURL* handle, CURLoption option, T value) {
    if (const auto code = curl_easy_setopt(handle, option, value); code != CURLE_OK)
        throw std::runtime_error{curl_easy_strerror(code)};
}

curl::~curl() {
    curl_global_cleanup();
}

curl& curl::the() {
    static curl instance;
    return instance;
}

std::string curl::get(const std::string_view uri) const {
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{curl_easy_init(), curl_easy_cleanup};
    if (!handle)
        throw std::runtime_error{"Failed to initialize CURL easy handle"};

    std::string body;
    std::array<char, CURL_ERROR_SIZE> error_buffer{};
    const std::string uri_string{uri};

    setopt(handle.get(), CURLOPT_URL, uri_string.c_str());
    setopt(handle.get(), CURLOPT_WRITEFUNCTION, &write_callback);
    setopt(handle.get(), CURLOPT_WRITEDATA, &body);
    setopt(handle.get(), CURLOPT_ERRORBUFFER, error_buffer.data());
    setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
    setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 10L);
    setopt(handle.get(), CURLOPT_TIMEOUT, 30L);
    setopt(handle.get(), CURLOPT_PROTOCOLS, CURLPROTO_FILE | CURLPROTO_HTTP | CURLPROTO_HTTPS);
    setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);

    if (const auto code = curl_easy_perform(handle.get()); code != CURLE_OK) {
        const std::string detail = error_buffer[0] ? error_buffer.data() : curl_easy_strerror(code);
        throw std::runtime_error{"Failed to fetch URI " + uri_string + ": " + detail};
    }

    return body;
}

std::string fetch(const std::string_view uri) {
    return curl::the().get(uri);
}

} // namespace soapp::io
