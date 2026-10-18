//
// Created by Salvo Passaro on 18/10/26.
//

#ifndef SOAPP_SOAP_H
#define SOAPP_SOAP_H

#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace soapp::soap {

// Runtime shared by generated SOAP clients.
class endpoint {
public:
    explicit endpoint(std::string url) : url_{std::move(url)} {}

    [[nodiscard]] std::string_view get_url() const noexcept { return url_; }
    void set_url(std::string url) { url_ = std::move(url); }

protected:
    // TODO: serialize the request, exchange SOAP envelopes, handle faults, and deserialize the response.
    template<typename Response, typename Request>
    Response invoke(const Request&) {
        std::cout << "Invoking SOAP operation with request message" << std::endl;

        return Response{};
    }

    // Operations with an empty input message have no request argument.
    template<typename Response>
    Response invoke() {
        std::cout << "Invoking SOAP operation with no request message" << std::endl;

        return Response{};
    }

private:
    std::string url_;
};

} // namespace soapp::soap

#endif // SOAPP_SOAP_H
