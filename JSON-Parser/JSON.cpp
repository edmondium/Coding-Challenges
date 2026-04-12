#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <expected>
#include <ranges>
#include <algorithm>
#include <iterator>
#include <cstdio>

enum struct JSONError { InvalidToken, InvalidStructure, DepthExceeded, InvalidNumber, NotAContainer };
enum struct TokenType { LBrace, RBrace, LBracket, RBracket, Colon, Comma, String, Number, Bool, Null, Space };

struct Token {
    TokenType type;
    char value[256];
};

auto tokenize(std::string_view input) -> std::expected<std::vector<Token>, JSONError> {
    std::vector<Token> tokens;
    for (size_t i = 0; i < input.size();) {
        char c = input[i];
        if (std::isspace(static_cast<unsigned char>(c))) {
            tokens.push_back({TokenType::Space, ""});
            i++;
            continue;
        }
        if (c == '{') { tokens.push_back({TokenType::LBrace, "{"}); i++; continue; }
        if (c == '}') { tokens.push_back({TokenType::RBrace, "}"}); i++; continue; }
        if (c == '[') { tokens.push_back({TokenType::LBracket, "["}); i++; continue; }
        if (c == ']') { tokens.push_back({TokenType::RBracket, "]"}); i++; continue; }
        if (c == ':') { tokens.push_back({TokenType::Colon, ":"}); i++; continue; }
        if (c == ',') { tokens.push_back({TokenType::Comma, ","}); i++; continue; }
        
        if (c == '"') {
            std::string s; i++;
            while (i < input.size() && input[i] != '"') {
                if (static_cast<unsigned char>(input[i]) < 0x20) return std::unexpected(JSONError::InvalidToken);
                if (input[i] == '\\') {
                    if (++i >= input.size() || !std::string_view("\"\\/bfnrtu").contains(input[i])) 
                        return std::unexpected(JSONError::InvalidToken);
                    s += '\\'; s += input[i];
                    if (input[i] == 'u') {
                        for (int j = 0; j < 4; ++j) {
                            if (++i >= input.size() || !std::isxdigit(static_cast<unsigned char>(input[i]))) return std::unexpected(JSONError::InvalidToken);
                            s += input[i];
                        }
                    }
                    i++;
                } else s += input[i++];
            }
            if (i >= input.size()) return std::unexpected(JSONError::InvalidToken);
            Token t{TokenType::String}; std::snprintf(t.value, sizeof(t.value), "%s", s.c_str());
            tokens.push_back(t); i++; continue;
        }
        
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            std::string num;
            while (i < input.size() && (std::isdigit(static_cast<unsigned char>(input[i])) || std::string_view(".eE+-").contains(input[i])))
                num += input[i++];
            Token t{TokenType::Number}; std::snprintf(t.value, sizeof(t.value), "%s", num.c_str());
            tokens.push_back(t); continue;
        }

        auto try_lit = [&](std::string_view lit, TokenType type) {
            if (input.substr(i).starts_with(lit)) {
                tokens.push_back({type, ""}); i += lit.size(); return true;
            }
            return false;
        };
        if (try_lit("true", TokenType::Bool) || try_lit("false", TokenType::Bool) || try_lit("null", TokenType::Null)) continue;

        return std::unexpected(JSONError::InvalidToken);
    }
    return tokens;
}

auto validate_numbers(std::vector<Token> tokens) -> std::expected<std::vector<Token>, JSONError> {
    const Token* ptr = tokens.data();
    const size_t t_size = tokens.size();
    bool valid = true;

    #pragma acc parallel loop copyin(ptr[0:t_size]) reduction(&:valid)
    for (size_t i = 0; i < t_size; ++i) {
        if (ptr[i].type == TokenType::Number) {
            const char* v = ptr[i].value;
            size_t j = (v[0] == '-') ? 1 : 0;
            if (v[j] == '\0' || !std::isdigit(static_cast<unsigned char>(v[j]))) valid = false;
            else {
                if (v[j] == '0' && std::isdigit(static_cast<unsigned char>(v[j+1]))) valid = false;
                while (std::isdigit(static_cast<unsigned char>(v[j]))) j++;
                if (v[j] == '.') {
                    j++;
                    if (!std::isdigit(static_cast<unsigned char>(v[j]))) valid = false;
                    while (std::isdigit(static_cast<unsigned char>(v[j]))) j++;
                }
                if (v[j] == 'e' || v[j] == 'E') {
                    j++;
                    if (v[j] == '+' || v[j] == '-') j++;
                    if (!std::isdigit(static_cast<unsigned char>(v[j]))) valid = false;
                    while (std::isdigit(static_cast<unsigned char>(v[j]))) j++;
                }
                if (v[j] != '\0') valid = false;
            }
        }
    }
    if (!valid) return std::unexpected(JSONError::InvalidNumber);
    return tokens;
}

struct ParseResult { size_t next_pos; };
using Result = std::expected<ParseResult, JSONError>;

auto parse_val(const std::vector<Token>& tokens, size_t pos, int depth) -> Result;

auto skip(const std::vector<Token>& tokens, size_t pos) -> size_t {
    return (pos < tokens.size() && tokens[pos].type == TokenType::Space) ? skip(tokens, pos + 1) : pos;
}

auto parse_obj(const std::vector<Token>& tokens, size_t pos, int depth) -> Result {
    pos = skip(tokens, pos + 1);
    if (pos < tokens.size() && tokens[pos].type == TokenType::RBrace) return ParseResult{pos + 1};
    while (true) {
        pos = skip(tokens, pos);
        if (pos >= tokens.size() || tokens[pos].type != TokenType::String) return std::unexpected(JSONError::InvalidStructure);
        pos = skip(tokens, pos + 1);
        if (pos >= tokens.size() || tokens[pos].type != TokenType::Colon) return std::unexpected(JSONError::InvalidStructure);
        auto res = parse_val(tokens, pos + 1, depth);
        if (!res) return res;
        pos = skip(tokens, res->next_pos);
        if (pos < tokens.size() && tokens[pos].type == TokenType::Comma) { pos++; continue; }
        if (pos < tokens.size() && tokens[pos].type == TokenType::RBrace) return ParseResult{pos + 1};
        return std::unexpected(JSONError::InvalidStructure);
    }
}

auto parse_arr(const std::vector<Token>& tokens, size_t pos, int depth) -> Result {
    pos = skip(tokens, pos + 1);
    if (pos < tokens.size() && tokens[pos].type == TokenType::RBracket) return ParseResult{pos + 1};
    while (true) {
        auto res = parse_val(tokens, pos, depth);
        if (!res) return res;
        pos = skip(tokens, res->next_pos);
        if (pos < tokens.size() && tokens[pos].type == TokenType::Comma) { pos++; continue; }
        if (pos < tokens.size() && tokens[pos].type == TokenType::RBracket) return ParseResult{pos + 1};
        return std::unexpected(JSONError::InvalidStructure);
    }
}

auto parse_val(const std::vector<Token>& tokens, size_t pos, int depth) -> Result {
    if (depth > 19) return std::unexpected(JSONError::DepthExceeded);
    pos = skip(tokens, pos);
    if (pos >= tokens.size()) return std::unexpected(JSONError::InvalidStructure);
    auto t = tokens[pos].type;
    if (t == TokenType::LBrace) return parse_obj(tokens, pos, depth + 1);
    if (t == TokenType::LBracket) return parse_arr(tokens, pos, depth + 1);
    if (t == TokenType::String || t == TokenType::Number || t == TokenType::Bool || t == TokenType::Null) return ParseResult{pos + 1};
    return std::unexpected(JSONError::InvalidStructure);
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    std::ifstream file(argv[1]);
    if (!file) return 1;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto final_check = tokenize(content)
        .and_then(validate_numbers)
        .and_then([](const std::vector<Token>& tokens) -> Result {
            size_t start = skip(tokens, 0);
            if (start >= tokens.size() || (tokens[start].type != TokenType::LBrace && tokens[start].type != TokenType::LBracket))
                return std::unexpected(JSONError::NotAContainer);
            return parse_val(tokens, start, 0).and_then([&](ParseResult r) -> Result {
                if (skip(tokens, r.next_pos) != tokens.size()) return std::unexpected(JSONError::InvalidStructure);
                return r;
            });
        });

    if (final_check) std::cout << "valid" << std::endl;
    else std::cout << "invalid" << std::endl;

    return final_check ? 0 : 1;
}