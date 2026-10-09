#include <filesystem>
#include <fstream>
#include "wsdl.h"
#include <iostream>
#include "cpp_generator.h"

int main() {
    const auto path = std::filesystem::absolute("../tests/devicemgmt.wsdl");
    std::ifstream t{path};

    std::string str((std::istreambuf_iterator<char>(t)),
                    std::istreambuf_iterator<char>());

    auto base = soapp::xml::uri::from_path(path);
    auto w = soapp::wsdl::WSDL11{str, std::move(base)};
    soapp::wsdl::CompilationContext context;

    w.parse_types(context);

    const soapp::cpp::Names names{ context.types };
    soapp::cpp::write_header(context.types, names, std::cout);
}
