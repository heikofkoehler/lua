#ifndef LUA_TOKEN_HPP
#define LUA_TOKEN_HPP

#include "common/common.hpp"
#include <string>

// Token types for Lua lexical analysis
enum class TokenType {
    // Single-character tokens
    LEFT_PAREN, RIGHT_PAREN,
    LEFT_BRACE, RIGHT_BRACE,
    LEFT_BRACKET, RIGHT_BRACKET,
    COMMA, DOT, SEMICOLON,
    PLUS, MINUS, STAR, SLASH, PERCENT, CARET,

    // One or two character tokens
    BANG, BANG_EQUAL,
    EQUAL, EQUAL_EQUAL,
    GREATER, GREATER_EQUAL,
    LESS, LESS_EQUAL,
    TILDE, TILDE_EQUAL,  // ~ and ~= (Lua's not-equal operator)
    DOT_DOT,

    // Literals
    IDENTIFIER, STRING, NUMBER,

    // Keywords
    AND, BREAK, DO, ELSE, ELSEIF,
    END, FALSE, FOR, FUNCTION, IF,
    IN, LOCAL, NIL, NOT, OR,
    REPEAT, RETURN, THEN, TRUE, UNTIL,
    WHILE, PRINT,

    // Special
    EOF_TOKEN, ERROR
};

// Token structure
struct Token {
    TokenType type;
    std::string lexeme;
    int line;

    Token(TokenType type, const std::string& lexeme, int line)
        : type(type), lexeme(lexeme), line(line) {}

    // For error tokens
    Token(TokenType type, const std::string& message)
        : type(type), lexeme(message), line(-1) {}
};

// Get human-readable name for token type
inline const char* tokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::LEFT_PAREN: return "LEFT_PAREN";
        case TokenType::RIGHT_PAREN: return "RIGHT_PAREN";
        case TokenType::LEFT_BRACE: return "LEFT_BRACE";
        case TokenType::RIGHT_BRACE: return "RIGHT_BRACE";
        case TokenType::LEFT_BRACKET: return "LEFT_BRACKET";
        case TokenType::RIGHT_BRACKET: return "RIGHT_BRACKET";
        case TokenType::COMMA: return "COMMA";
        case TokenType::DOT: return "DOT";
        case TokenType::SEMICOLON: return "SEMICOLON";
        case TokenType::PLUS: return "PLUS";
        case TokenType::MINUS: return "MINUS";
        case TokenType::STAR: return "STAR";
        case TokenType::SLASH: return "SLASH";
        case TokenType::PERCENT: return "PERCENT";
        case TokenType::CARET: return "CARET";
        case TokenType::BANG: return "BANG";
        case TokenType::BANG_EQUAL: return "BANG_EQUAL";
        case TokenType::EQUAL: return "EQUAL";
        case TokenType::EQUAL_EQUAL: return "EQUAL_EQUAL";
        case TokenType::GREATER: return "GREATER";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";
        case TokenType::LESS: return "LESS";
        case TokenType::LESS_EQUAL: return "LESS_EQUAL";
        case TokenType::TILDE: return "TILDE";
        case TokenType::TILDE_EQUAL: return "TILDE_EQUAL";
        case TokenType::DOT_DOT: return "DOT_DOT";
        case TokenType::IDENTIFIER: return "IDENTIFIER";
        case TokenType::STRING: return "STRING";
        case TokenType::NUMBER: return "NUMBER";
        case TokenType::AND: return "AND";
        case TokenType::BREAK: return "BREAK";
        case TokenType::DO: return "DO";
        case TokenType::ELSE: return "ELSE";
        case TokenType::ELSEIF: return "ELSEIF";
        case TokenType::END: return "END";
        case TokenType::FALSE: return "FALSE";
        case TokenType::FOR: return "FOR";
        case TokenType::FUNCTION: return "FUNCTION";
        case TokenType::IF: return "IF";
        case TokenType::IN: return "IN";
        case TokenType::LOCAL: return "LOCAL";
        case TokenType::NIL: return "NIL";
        case TokenType::NOT: return "NOT";
        case TokenType::OR: return "OR";
        case TokenType::REPEAT: return "REPEAT";
        case TokenType::RETURN: return "RETURN";
        case TokenType::THEN: return "THEN";
        case TokenType::TRUE: return "TRUE";
        case TokenType::UNTIL: return "UNTIL";
        case TokenType::WHILE: return "WHILE";
        case TokenType::PRINT: return "PRINT";
        case TokenType::EOF_TOKEN: return "EOF";
        case TokenType::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

#endif // LUA_TOKEN_HPP
