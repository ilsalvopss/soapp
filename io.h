//
// Created by Salvo Passaro on 16/09/26.
//

#ifndef SOAPP_IO_H
#define SOAPP_IO_H

#include <string>
#include <string_view>

#include <curl/curl.h>

namespace soapp::io {

class curl {
    curl();

    static std::size_t write_callback(const char* ptr, std::size_t size, std::size_t nmemb, void* userdata);

    template<typename T>
    static void setopt(CURL* handle, CURLoption option, T value);

public:
    curl(const curl&) = delete;
    curl& operator=(const curl&) = delete;

    ~curl();

    [[nodiscard]] static curl& the();

    // ignore clang-tidy suggestion that this could be static
    // curl global init. is done in the constructor
    [[nodiscard]] std::string get(std::string_view uri) const;
};

[[nodiscard]] std::string fetch(std::string_view uri);

} // namespace soapp::io

#endif //SOAPP_IO_H
