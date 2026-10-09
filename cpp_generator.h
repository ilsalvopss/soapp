//
// Created by Salvo Passaro on 08/10/26.
//
#ifndef SOAPP_CPP_GENERATOR_H
#define SOAPP_CPP_GENERATOR_H

#include "cpp_names.h"

#include <iosfwd>
#include <ostream>
#include <string_view>

namespace soapp::wsdl {
class TypeTable;
}

namespace soapp::cpp {

// Writes lines with indentation scoped to the lifetime of an Indent object.
class CodeWriter {
public:
    explicit CodeWriter(std::ostream& out) noexcept : out_{out} {}

    class Indent {
        CodeWriter& writer_;

        friend class CodeWriter;
        explicit Indent(CodeWriter& writer) noexcept : writer_{writer} { ++writer_.depth_; }

    public:
        Indent(const Indent&) = delete;
        Indent& operator=(const Indent&) = delete;
        ~Indent() { --writer_.depth_; }
    };

    void line(const std::string_view text = {}) const {
        if (!text.empty()) {
            for (std::size_t level = 0; level < depth_; ++level)
                out_ << "    ";
            out_ << text;
        }
        out_ << '\n';
    }

    [[nodiscard]] Indent indented() noexcept { return Indent{*this}; }

private:
    std::ostream& out_;
    std::size_t depth_ = 0;
};

// Emit both type kinds in dependency order, sharing names with other emitters.
void write_declarations(const wsdl::TypeTable& types, const Names& names, CodeWriter& writer);

// Assemble a complete header: its prologue belongs here, rather than to each emitter.
void write_header(const wsdl::TypeTable& types, const Names& names, std::ostream& out);

} // namespace soapp::cpp

#endif //SOAPP_CPP_GENERATOR_H
