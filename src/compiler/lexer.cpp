#include "compiler/lexer.hpp"

// Initialize keywords map
const std::unordered_map<std::string, TokenType> Lexer::keywords_ = {
    {"and",      TokenType::AND},
    {"break",    TokenType::BREAK},
    {"do",       TokenType::DO},
    {"else",     TokenType::ELSE},
    {"elseif",   TokenType::ELSEIF},
    {"end",      TokenType::END},
    {"false",    TokenType::FALSE},
    {"for",      TokenType::FOR},
    {"function", TokenType::FUNCTION},
    {"if",       TokenType::IF},
    {"in",       TokenType::IN},
    {"local",    TokenType::LOCAL},
    {"nil",      TokenType::NIL},
    {"not",      TokenType::NOT},
    {"or",       TokenType::OR},
    {"repeat",   TokenType::REPEAT},
    {"return",   TokenType::RETURN},
    {"then",     TokenType::THEN},
    {"true",     TokenType::TRUE},
    {"until",    TokenType::UNTIL},
    {"while",    TokenType::WHILE},
    {"print",    TokenType::PRINT},
};

Lexer::Lexer(const std::string& source)
    : source_(source), start_(0), current_(0), line_(1) {}

Token Lexer::scanToken() {
    skipWhitespace();

    start_ = current_;

    if (isAtEnd()) {
        return makeToken(TokenType::EOF_TOKEN);
    }

    char c = advance();

    // Identifiers and keywords
    if (isAlpha(c)) {
        return identifier();
    }

    // Numbers
    if (isDigit(c)) {
        return number();
    }

    // Single and multi-character tokens
    switch (c) {
        case '(': return makeToken(TokenType::LEFT_PAREN);
        case ')': return makeToken(TokenType::RIGHT_PAREN);
        case '{': return makeToken(TokenType::LEFT_BRACE);
        case '}': return makeToken(TokenType::RIGHT_BRACE);
        case '[': return makeToken(TokenType::LEFT_BRACKET);
        case ']': return makeToken(TokenType::RIGHT_BRACKET);
        case ',': return makeToken(TokenType::COMMA);
        case ';': return makeToken(TokenType::SEMICOLON);
        case '+': return makeToken(TokenType::PLUS);
        case '-': return makeToken(TokenType::MINUS);
        case '*': return makeToken(TokenType::STAR);
        case '/': return makeToken(TokenType::SLASH);
        case '%': return makeToken(TokenType::PERCENT);
        case '^': return makeToken(TokenType::CARET);

        case '!':
            return makeToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);

        case '=':
            return makeToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);

        case '<':
            return makeToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);

        case '>':
            return makeToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);

        case '~':
            return makeToken(match('=') ? TokenType::TILDE_EQUAL : TokenType::TILDE);

        case '.':
            if (match('.')) {
                return makeToken(TokenType::DOT_DOT);
            } else if (isDigit(peek())) {
                return number();
            } else {
                return makeToken(TokenType::DOT);
            }

        case '"':
        case '\'':
            return string();
    }

    return errorToken("Unexpected character");
}

Token Lexer::peekToken() {
    // Save current lexer state
    size_t savedStart = start_;
    size_t savedCurrent = current_;
    int savedLine = line_;

    // Scan next token
    Token nextToken = scanToken();

    // Restore lexer state
    start_ = savedStart;
    current_ = savedCurrent;
    line_ = savedLine;

    return nextToken;
}

char Lexer::advance() {
    return source_[current_++];
}

char Lexer::peek() const {
    if (isAtEnd()) return '\0';
    return source_[current_];
}

char Lexer::peekNext() const {
    if (current_ + 1 >= source_.length()) return '\0';
    return source_[current_ + 1];
}

bool Lexer::match(char expected) {
    if (isAtEnd()) return false;
    if (source_[current_] != expected) return false;
    current_++;
    return true;
}

Token Lexer::makeToken(TokenType type) const {
    std::string lexeme = source_.substr(start_, current_ - start_);
    return Token(type, lexeme, line_);
}

Token Lexer::errorToken(const std::string& message) const {
    return Token(TokenType::ERROR, message);
}

void Lexer::skipWhitespace() {
    while (true) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\r':
            case '\t':
                advance();
                break;

            case '\n':
                line_++;
                advance();
                break;

            case '-':
                // Lua comments start with --
                if (peekNext() == '-') {
                    // Skip until end of line
                    while (peek() != '\n' && !isAtEnd()) {
                        advance();
                    }
                } else {
                    return;
                }
                break;

            default:
                return;
        }
    }
}

Token Lexer::string() {
    char quote = source_[start_];  // Remember opening quote (' or ")

    while (peek() != quote && !isAtEnd()) {
        if (peek() == '\n') line_++;
        advance();
    }

    if (isAtEnd()) {
        return errorToken("Unterminated string");
    }

    // Closing quote
    advance();

    // Extract string content (without quotes)
    std::string value = source_.substr(start_ + 1, current_ - start_ - 2);
    return Token(TokenType::STRING, value, line_);
}

Token Lexer::number() {
    while (isDigit(peek())) {
        advance();
    }

    // Look for fractional part
    if (peek() == '.' && isDigit(peekNext())) {
        // Consume the '.'
        advance();

        while (isDigit(peek())) {
            advance();
        }
    }

    // Look for exponent
    if (peek() == 'e' || peek() == 'E') {
        advance();
        if (peek() == '+' || peek() == '-') {
            advance();
        }
        while (isDigit(peek())) {
            advance();
        }
    }

    return makeToken(TokenType::NUMBER);
}

Token Lexer::identifier() {
    while (isAlphaNumeric(peek())) {
        advance();
    }

    return makeToken(identifierType());
}

bool Lexer::isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool Lexer::isAlpha(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           c == '_';
}

bool Lexer::isAlphaNumeric(char c) {
    return isAlpha(c) || isDigit(c);
}

TokenType Lexer::identifierType() const {
    std::string text = source_.substr(start_, current_ - start_);
    auto it = keywords_.find(text);
    if (it != keywords_.end()) {
        return it->second;
    }
    return TokenType::IDENTIFIER;
}
