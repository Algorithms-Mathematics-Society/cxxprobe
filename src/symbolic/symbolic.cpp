#include "cxxprobe/symbolic.hpp"

#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>

namespace cxxprobe::symbolic {

namespace {

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

}  // namespace

// Single left-to-right scan with an implicit state machine (Code / line
// comment / block comment / string / char / raw-string). Stripped bytes are
// replaced with spaces (newlines preserved) so the result stays the same
// length and line-aligned with the input — no external lexer, deliberately
// not a full C++ AST (see decision to use regex over libclang).
std::string strip_comments_and_literals(std::string_view source) {
    std::string out{source};
    const std::size_t n = out.size();
    std::size_t i = 0;

    while (i < n) {
        char c = out[i];

        // ── // line comment ──────────────────────────────────────────────
        if (c == '/' && i + 1 < n && out[i + 1] == '/') {
            out[i] = ' ';
            out[i + 1] = ' ';
            i += 2;
            while (i < n && out[i] != '\n') {
                out[i] = ' ';
                ++i;
            }
            continue;
        }

        // ── /* block comment */ (no nesting, matches C++ semantics) ──────
        if (c == '/' && i + 1 < n && out[i + 1] == '*') {
            out[i] = ' ';
            out[i + 1] = ' ';
            i += 2;
            while (i + 1 < n && !(out[i] == '*' && out[i + 1] == '/')) {
                if (out[i] != '\n') {
                    out[i] = ' ';
                }
                ++i;
            }
            if (i + 1 < n) {
                out[i] = ' ';
                out[i + 1] = ' ';
                i += 2;
            } else {
                while (i < n) {
                    if (out[i] != '\n') {
                        out[i] = ' ';
                    }
                    ++i;
                }
            }
            continue;
        }

        // ── string literal, possibly a raw string: (u8|u|U|L)?R"delim(...)delim" ──
        if (c == '"') {
            bool is_raw = false;
            std::size_t prefix_start = i;
            if (i >= 1 && out[i - 1] == 'R') {
                std::size_t r_pos = i - 1;
                std::size_t enc_start = r_pos;
                if (r_pos >= 2 && out.compare(r_pos - 2, 2, "u8") == 0) {
                    enc_start = r_pos - 2;
                } else if (r_pos >= 1 &&
                           (out[r_pos - 1] == 'u' || out[r_pos - 1] == 'U' || out[r_pos - 1] == 'L')) {
                    enc_start = r_pos - 1;
                }
                bool boundary_ok = (enc_start == 0) || !is_ident_char(out[enc_start - 1]);
                if (boundary_ok) {
                    is_raw = true;
                    prefix_start = enc_start;
                }
            }
            (void)prefix_start;  // prefix letters are real code tokens; left untouched

            if (is_raw) {
                std::size_t paren = out.find('(', i + 1);
                if (paren == std::string::npos) {
                    // Malformed raw string — bail out of special handling.
                    out[i] = ' ';
                    ++i;
                    continue;
                }
                std::string delim = out.substr(i + 1, paren - (i + 1));
                std::string terminator = ")" + delim + "\"";
                std::size_t term_pos = out.find(terminator, paren);
                std::size_t content_end = (term_pos == std::string::npos) ? n : term_pos + terminator.size();
                for (std::size_t k = i; k < content_end; ++k) {
                    if (out[k] != '\n') {
                        out[k] = ' ';
                    }
                }
                i = content_end;
                continue;
            }

            out[i] = ' ';
            ++i;
            while (i < n) {
                if (out[i] == '\\' && i + 1 < n) {
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    i += 2;
                    continue;
                }
                bool is_end = (out[i] == '"');
                if (out[i] != '\n') {
                    out[i] = ' ';
                }
                ++i;
                if (is_end) {
                    break;
                }
            }
            continue;
        }

        // ── char literal, distinguished from a C++14 digit separator ─────
        if (c == '\'') {
            bool prev_digit = i > 0 && std::isxdigit(static_cast<unsigned char>(out[i - 1])) != 0;
            bool next_digit = i + 1 < n && std::isxdigit(static_cast<unsigned char>(out[i + 1])) != 0;
            if (prev_digit && next_digit) {
                ++i;  // digit separator, e.g. 1'000'000 — not a literal
                continue;
            }
            out[i] = ' ';
            ++i;
            while (i < n) {
                if (out[i] == '\\' && i + 1 < n) {
                    out[i] = ' ';
                    out[i + 1] = ' ';
                    i += 2;
                    continue;
                }
                bool is_end = (out[i] == '\'');
                if (out[i] != '\n') {
                    out[i] = ' ';
                }
                ++i;
                if (is_end) {
                    break;
                }
            }
            continue;
        }

        ++i;  // ordinary code byte, left untouched
    }

    return out;
}

CheckOutcome evaluate(const cxxprobe::problem::SymbolicCheck& check, std::string_view stripped_source,
                       bool expect_present) {
    CheckOutcome outcome;
    outcome.pattern = check.pattern;
    outcome.regex = check.regex;
    outcome.expect_present = expect_present;
    outcome.message = check.message;

    if (check.regex) {
        std::regex re;
        try {
            re = std::regex{check.pattern};
        } catch (const std::regex_error& ex) {
            throw std::runtime_error{
                std::format("invalid regex pattern '{}': {}", check.pattern, ex.what())};
        }
        outcome.matched = std::regex_search(stripped_source.begin(), stripped_source.end(), re);
    } else {
        outcome.matched = stripped_source.find(check.pattern) != std::string_view::npos;
    }
    outcome.satisfied = (outcome.matched == expect_present);
    return outcome;
}

Report run(const cxxprobe::problem::SymbolicConfig& config, const std::filesystem::path& source_file) {
    std::ifstream ifs{source_file, std::ios::binary};
    if (!ifs) {
        throw std::runtime_error{std::format("cannot open source file: {}", source_file.string())};
    }
    std::string source{std::istreambuf_iterator<char>{ifs}, {}};
    std::string stripped = strip_comments_and_literals(source);

    Report report;
    report.passed = true;
    for (const auto& check : config.must_include) {
        CheckOutcome outcome = evaluate(check, stripped, true);
        report.passed = report.passed && outcome.satisfied;
        report.outcomes.push_back(std::move(outcome));
    }
    for (const auto& check : config.must_not_include) {
        CheckOutcome outcome = evaluate(check, stripped, false);
        report.passed = report.passed && outcome.satisfied;
        report.outcomes.push_back(std::move(outcome));
    }
    return report;
}

}  // namespace cxxprobe::symbolic
