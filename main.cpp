#include <iostream>
#include <fstream>
#include "wsdl.h"

int main() {
    std::ifstream t("../devicemgmt.wsdl");

    std::string str((std::istreambuf_iterator<char>(t)),
                    std::istreambuf_iterator<char>());

    auto w = soapp::wsdl::WSDL11(std::move(str));

}