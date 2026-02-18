#ifndef LUA_LEXER_HPP
#define LUA_LEXER_HPP

#include "common/common.hpp"
#include "compiler/token.hpp"
#include <string>
#include <unordered_map>

// Lexer: Tokenizes Lua source code
// Converts raw text into a stream of tokens for the parser

class Lexer {
public:
    explicit Lexer(const std::string& source);

    // Scan and return the next token
    Token scanToken();

    // Check if at end of source
    bool isAtEnd() const { return current_ >= source_.length(); }

    // Get current line number
    int line() const { return line_; }

private:
    std::string source_;
    size_t start_;      // Start of current lexeme
    size_t current_;    // Current position in source
    int line_;          // Current line number

    // Keywords map
    static const std::unordered_map<std::string, TokenType> keywords_;

    // Character handling
    char advance();
    char peek() const;
    char peekNext() const;
    bool match(char expected);

    // Token creation
    Token makeToken(TokenType type) const;
    Token errorToken(const std::string& message) const;

    // Skip whitespace and comments
    void skipWhitespace();

    // Lexeme scanners
    Token string();
    Token number();
    Token identifier();

    // Helper predicates
    static bool isDigit(char c);
    static bool isAlpha(char c);
    static bool isAlphaNumeric(char c);

    // Keyword lookup
    TokenType identifierType() const;
};

#endif // LUA_LEXER_HPP
