//
// Created by Salvo Passaro on 20/10/26.
//
#include "cpp_generator.h"
#include "wsdl.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view usage =
    "Usage: wsdl2pp [-o <header>] <input.wsdl>\n"
    "Generate a C++ header from a WSDL 1.1 file.\n"
    "\n"
    "Options:\n"
    "  -o <header>  Write to a file (default: standard output).\n"
    "  -h, --help   Show this help.\n";

} // namespace

int main(const int argc, char* argv[]) {
    if (argc == 1) {
        std::cerr << usage;
        return 1;
    }

    try {
        std::filesystem::path input_path;
        std::filesystem::path output_path;

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg{ argv[i] };

            if (arg == "-h" || arg == "--help") {
                std::cout << usage;
                return 0;
            } if (arg == "-o") {
                if (++i == argc || std::string_view{ argv[i] }.empty())
                    throw std::runtime_error{ "Expected a filename after -o" };

                output_path = argv[i];
            } else if (arg.starts_with('-')) {
                throw std::runtime_error{ "Unknown option: " + std::string{ arg } };
            } else {
                input_path = argv[i];
            }
        }

        if (input_path.empty())
            throw std::runtime_error{ "Expected an input WSDL file (see --help)" };

        std::ifstream input{ input_path, std::ios::binary };
        if (!input)
            throw std::runtime_error{ "Cannot open input file: " + input_path.string() };

        const std::string source { std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{} };
        const soapp::wsdl::WSDL11 wsdl{ source, soapp::xml::uri::from_path(input_path) };

        soapp::wsdl::CompilationContext context;

        wsdl.parse_types(context);
        wsdl.parse_messages(context);
        wsdl.parse_port_types(context);

        const soapp::cpp::Names names{ context };

        std::ostringstream header;
        soapp::cpp::write_header(context, names, header);

        if (output_path.empty()) {
            std::cout << header.view() << std::flush;
        } else {
            std::ofstream output{ output_path, std::ios::binary };
            if (!output)
                throw std::runtime_error{ "Cannot open output file: " + output_path.string() };
            output << header.view();
            output.close();
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "wsdl2pp: " << error.what() << '\n';
        return 1;
    }
}
