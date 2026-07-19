#include "cxxprobe/symbolic.hpp"

#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <stdexcept>

namespace cxxprobe::symbolic {

namespace {

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Blanks out a // line comment starting at i (out[i] == out[i+1] == '/').
// The terminating newline, if any, is left untouched. Returns the index
// just past the comment (at the newline, or at n).
std::size_t skip_line_comment(std::string& out, std::size_t i, std::size_t n) {
    out[i] = ' ';
    out[i + 1] = ' ';
    i += 2;
    while (i < n && out[i] != '\n') {
        out[i] = ' ';
        ++i;
    }
    return i;
}

// Blanks out a /* block comment */ starting at i. C++ block comments don't
// nest — the first "*/" ends it. Embedded newlines are preserved.
std::size_t skip_block_comment(std::string& out, std::size_t i, std::size_t n) {
    out[i] = ' ';
    out[i + 1] = ' ';
    i += 2;
    while (i + 1 < n && (out[i] != '*' || out[i + 1] != '/')) {
        if (out[i] != '\n') {
            out[i] = ' ';
        }
        ++i;
    }
    if (i + 1 >= n) {
        // Unterminated comment — blank to end of input.
        for (; i < n; ++i) {
            if (out[i] != '\n') {
                out[i] = ' ';
            }
        }
        return i;
    }
    out[i] = ' ';
    out[i + 1] = ' ';
    return i + 2;
}

// A raw string is (u8|u|U|L)?R"delim(...)delim" — out[quote_pos] == '"'.
// Returns the start of the whole prefix (encoding + R) if a raw string
// begins immediately before quote_pos, or std::nullopt otherwise (plain
// string literal, or 'R' belongs to a longer identifier).
std::optional<std::size_t> detect_raw_string_prefix(std::string_view out, std::size_t quote_pos) {
    if (quote_pos < 1 || out[quote_pos - 1] != 'R') {
        return std::nullopt;
    }
    std::size_t r_pos = quote_pos - 1;
    std::size_t enc_start = r_pos;
    if (r_pos >= 2 && out.compare(r_pos - 2, 2, "u8") == 0) {
        enc_start = r_pos - 2;
    } else if (r_pos >= 1 && (out[r_pos - 1] == 'u' || out[r_pos - 1] == 'U' || out[r_pos - 1] == 'L')) {
        enc_start = r_pos - 1;
    }
    bool boundary_ok = (enc_start == 0) || !is_ident_char(out[enc_start - 1]);
    return boundary_ok ? std::make_optional(enc_start) : std::nullopt;
}

// Blanks out R"delim(...)delim" starting at quote_pos (out[quote_pos] ==
// '"'). Prefix letters (R, and any encoding prefix) are left untouched —
// only the quote through the closing terminator is blanked. Returns the
// index just past the literal.
std::size_t skip_raw_string_literal(std::string& out, std::size_t quote_pos, std::size_t n) {
    std::size_t paren = out.find('(', quote_pos + 1);
    if (paren == std::string::npos) {
        // Malformed raw string — bail out of special handling.
        out[quote_pos] = ' ';
        return quote_pos + 1;
    }
    std::string delim = out.substr(quote_pos + 1, paren - (quote_pos + 1));
    std::string terminator = ")" + delim + "\"";
    std::size_t term_pos = out.find(terminator, paren);
    std::size_t content_end = (term_pos == std::string::npos) ? n : term_pos + terminator.size();
    for (std::size_t k = quote_pos; k < content_end; ++k) {
        if (out[k] != '\n') {
            out[k] = ' ';
        }
    }
    return content_end;
}

// Blanks out a plain string or char literal starting at i (out[i] ==
// quote_char), honoring backslash escapes. Shared between '"' and '\''
// since the scan logic is identical modulo the quote character. Returns the
// index just past the closing quote (or n, if unterminated).
std::size_t skip_quoted_literal(std::string& out, std::size_t i, std::size_t n, char quote_char) {
    out[i] = ' ';
    ++i;
    while (i < n) {
        if (out[i] == '\\' && i + 1 < n) {
            out[i] = ' ';
            out[i + 1] = ' ';
            i += 2;
            continue;
        }
        bool is_end = (out[i] == quote_char);
        if (out[i] != '\n') {
            out[i] = ' ';
        }
        ++i;
        if (is_end) {
            break;
        }
    }
    return i;
}

// A '\'' between two hex digits is a C++14 digit separator (e.g.
// 1'000'000), not the start of a char literal.
bool is_digit_separator(std::string_view out, std::size_t i, std::size_t n) {
    bool prev_digit = i > 0 && std::isxdigit(static_cast<unsigned char>(out[i - 1])) != 0;
    bool next_digit = i + 1 < n && std::isxdigit(static_cast<unsigned char>(out[i + 1])) != 0;
    return prev_digit && next_digit;
}

}  // namespace

// Single left-to-right scan (Code / line comment / block comment / string /
// char / raw-string). Stripped bytes are replaced with spaces (newlines
// preserved) so the result stays the same length and line-aligned with the
// input — no external lexer, deliberately not a full C++ AST (see decision
// to use regex over libclang).
std::string strip_comments_and_literals(std::string_view source) {
    std::string out{source};
    const std::size_t n = out.size();
    std::size_t i = 0;

    while (i < n) {
        char c = out[i];

        if (c == '/' && i + 1 < n && out[i + 1] == '/') {
            i = skip_line_comment(out, i, n);
        } else if (c == '/' && i + 1 < n && out[i + 1] == '*') {
            i = skip_block_comment(out, i, n);
        } else if (c == '"') {
            if (auto prefix_start = detect_raw_string_prefix(out, i)) {
                (void)*prefix_start;  // prefix letters are real code tokens; left untouched
                i = skip_raw_string_literal(out, i, n);
            } else {
                i = skip_quoted_literal(out, i, n, '"');
            }
        } else if (c == '\'') {
            i = is_digit_separator(out, i, n) ? i + 1 : skip_quoted_literal(out, i, n, '\'');
        } else {
            ++i;  // ordinary code byte, left untouched
        }
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
        outcome.matched = stripped_source.contains(check.pattern);
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
