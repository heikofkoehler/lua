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
    {"global",   TokenType::GLOBAL},
    {"goto",     TokenType::GOTO},
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
        case '[':
            if (peek() == '[' || peek() == '=') {
                return longString();
            }
            return makeToken(TokenType::LEFT_BRACKET);
        case ']': return makeToken(TokenType::RIGHT_BRACKET);
        case ',': return makeToken(TokenType::COMMA);
        case ';': return makeToken(TokenType::SEMICOLON);
        case ':': 
            return makeToken(match(':') ? TokenType::COLON_COLON : TokenType::COLON);
        case '#': return makeToken(TokenType::HASH);
        case '+': return makeToken(TokenType::PLUS);
        case '-': return makeToken(TokenType::MINUS);
        case '*': return makeToken(TokenType::STAR);
        case '/':
            return makeToken(match('/') ? TokenType::SLASH_SLASH : TokenType::SLASH);
        case '%': return makeToken(TokenType::PERCENT);
        case '^': return makeToken(TokenType::CARET);
        case '&': return makeToken(TokenType::AMPERSAND);
        case '|': return makeToken(TokenType::PIPE);

        case '!':
            return makeToken(match('=') ? TokenType::BANG_EQUAL : TokenType::BANG);

        case '=':
            return makeToken(match('=') ? TokenType::EQUAL_EQUAL : TokenType::EQUAL);

        case '<':
            if (match('<')) return makeToken(TokenType::LESS_LESS);
            return makeToken(match('=') ? TokenType::LESS_EQUAL : TokenType::LESS);

        case '>':
            if (match('>')) return makeToken(TokenType::GREATER_GREATER);
            return makeToken(match('=') ? TokenType::GREATER_EQUAL : TokenType::GREATER);

        case '~':
            return makeToken(match('=') ? TokenType::TILDE_EQUAL : TokenType::TILDE);

        case '.':
            if (match('.')) {
                // Check for third dot (...)
                if (match('.')) {
                    return makeToken(TokenType::DOT_DOT_DOT);
                }
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

    return errorToken("Unexpected character '" + std::string(1, c) + "'");
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

Token Lexer::errorToken(const std::string& message, const std::string& near) const {
    if (!near.empty()) {
        return Token(TokenType::ERROR, message, line_, near);
    }
    std::string lexeme = source_.substr(start_, current_ - start_);
    return Token(TokenType::ERROR, message, line_, lexeme);
}

void Lexer::skipWhitespace() {
    // Special-case: ignore a Unix shebang at start of file
    // Many scripts begin with "#!./lua" which is not valid Lua syntax,
    // so we treat the first line as a comment if it starts with '#!'.
    if (current_ == 0 && peek() == '#' && peekNext() == '!') {
        // consume until end of line or EOF
        while (!isAtEnd() && peek() != '\n') advance();
        // newline will be handled below and bump line count
    }

    while (true) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\t':
            case '\v':
            case '\f':
                advance();
                break;

            case '\n':
                line_++;
                advance();
                if (peek() == '\r') advance();
                break;

            case '\r':
                line_++;
                advance();
                if (peek() == '\n') advance();
                break;

            case '-':
                // Lua comments start with --
                if (peekNext() == '-') {
                    advance(); // skip first -
                    advance(); // skip second -
                    if (peek() == '[') {
                        // Potential long comment
                        size_t savedCurrent = current_;
                        int savedLine = line_;
                        advance(); // skip [
                        int level = 0;
                        while (peek() == '=') {
                            level++;
                            advance();
                        }
                        if (peek() == '[') {
                            advance(); // skip [
                            // It IS a long comment. Skip until matching ]==]
                            while (!isAtEnd()) {
                                if (peek() == ']') {
                                    advance();
                                    int closingLevel = 0;
                                    while (peek() == '=') {
                                        closingLevel++;
                                        advance();
                                    }
                                    if (peek() == ']' && closingLevel == level) {
                                        advance();
                                        break; // continue the while(true) loop
                                    }
                                } else {
                                    if (advance() == '\n') line_++;
                                }
                            }
                        } else {
                            // Not a long comment, skip until end of line
                            current_ = savedCurrent;
                            line_ = savedLine;
                            while (peek() != '\n' && !isAtEnd()) {
                                advance();
                            }
                        }
                    } else {
                        // Skip until end of line
                        while (peek() != '\n' && !isAtEnd()) {
                            advance();
                        }
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
    std::string value;

    while (peek() != quote && !isAtEnd()) {
        char c = peek();

        if (c == '\\') {
            advance();  // consume backslash
            if (isAtEnd()) break;
            char esc = advance();
            switch (esc) {
                case 'a':  value += '\a'; break;
                case 'b':  value += '\b'; break;
                case 'f':  value += '\f'; break;
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case 'v':  value += '\v'; break;
                case '\\': value += '\\'; break;
                case '\'': value += '\''; break;
                case '"':  value += '"';  break;
                case 'z': {
                    // Skip following whitespace
                    while (!isAtEnd()) {
                        char c = peek();
                        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f') {
                            if (c == '\n') line_++;
                            else if (c == '\r' && peekNext() == '\n') {
                                advance(); // skip \r
                            } else if (c == '\r') line_++;
                            advance();
                        } else {
                            break;
                        }
                    }
                    continue; 
                }
                case '\n': line_++; value += '\n'; break;
                case '\r':
                    if (peek() == '\n') advance();
                    line_++;
                    value += '\n';
                    break;
                case 'u': {
                    // Unicode escape \u{XXX}
                    if (advance() != '{') return errorToken("invalid escape sequence");
                    unsigned long code = 0;
                    int digits = 0;
                    while (isxdigit(peek())) {
                        char h = advance();
                        code = code * 16 + (isdigit(h) ? h - '0' :
                                            tolower(h) - 'a' + 10);
                        digits++;
                    }
                    if (digits == 0) return errorToken("invalid escape sequence");
                    if (advance() != '}') return errorToken("invalid escape sequence");
                    
                    // Convert code point to UTF-8
                    if (code <= 0x7F) {
                        value += static_cast<char>(code);
                    } else if (code <= 0x7FF) {
                        value += static_cast<char>(0xC0 | (code >> 6));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code <= 0xFFFF) {
                        value += static_cast<char>(0xE0 | (code >> 12));
                        value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code <= 0x1FFFFF) {
                        value += static_cast<char>(0xF0 | (code >> 18));
                        value += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code <= 0x3FFFFFF) {
                        value += static_cast<char>(0xF8 | (code >> 24));
                        value += static_cast<char>(0x80 | ((code >> 18) & 0x3F));
                        value += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code <= 0x7FFFFFFF) {
                        value += static_cast<char>(0xFC | (code >> 30));
                        value += static_cast<char>(0x80 | ((code >> 24) & 0x3F));
                        value += static_cast<char>(0x80 | ((code >> 18) & 0x3F));
                        value += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        value += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        value += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        return errorToken("Unicode escape too large");
                    }
                    break;
                }
                case 'x': {
                    // Hex escape \xXX (exactly 2 digits)
                    if (isAtEnd()) return errorToken("hexadecimal escape requires 2 digits");
                    if (!isxdigit(peek())) return errorToken("invalid escape sequence");
                    char h1 = advance();
                    if (isAtEnd()) return errorToken("hexadecimal escape requires 2 digits");
                    if (!isxdigit(peek())) return errorToken("invalid escape sequence");
                    char h2 = advance();
                    
                    int hex = (isdigit(h1) ? h1 - '0' : tolower(h1) - 'a' + 10) * 16 +
                              (isdigit(h2) ? h2 - '0' : tolower(h2) - 'a' + 10);
                    value += static_cast<char>(hex);
                    break;
                }
                default:
                    if (isDigit(esc)) {
                        // Decimal escape \ddd (up to 3 digits, 0-255)
                        int dec = esc - '0';
                        if (isDigit(peek())) dec = dec * 10 + (advance() - '0');
                        if (isDigit(peek())) dec = dec * 10 + (advance() - '0');
                        if (dec > 255) return errorToken("decimal escape too large");
                        value += static_cast<char>(dec);
                    } else {
                        return errorToken("invalid escape sequence");
                    }
                    break;
            }
        } else {
            if (c == '\n') line_++;
            value += advance();
        }
    }

    if (isAtEnd()) {
        return errorToken("unfinished string");
    }

    // Closing quote
    advance();

    return Token(TokenType::STRING, value, line_);
}

Token Lexer::longString() {
    int level = 0;
    while (peek() == '=') {
        level++;
        advance();
    }

    if (peek() != '[') {
        // Not a long string after all (maybe [==3])
        // But in standard Lua, [ followed by = is only for long strings.
        // For now, let's treat it as an error or just return the brackets.
        return errorToken("Expected '[' for long string");
    }

    advance(); // Consume the second '['

    // If first character is a newline, skip it
    if (peek() == '\n') {
        current_++;
    } else if (peek() == '\r') {
        current_++;
        if (peek() == '\n') current_++;
    }

    std::string value;
    while (!isAtEnd()) {
        if (peek() == ']') {
            advance(); // consume ']'
            int closingLevel = 0;
            while (peek() == '=') {
                closingLevel++;
                advance();
            }
            if (peek() == ']' && closingLevel == level) {
                advance(); // consume ']'
                return Token(TokenType::STRING, value, line_);
            }
            // Not a match, add the ']' and '=' back to the string
            value += ']';
            for (int i = 0; i < closingLevel; i++) value += '=';
            // Continue scanning from after the '='
        } else if (peek() == '\n') {
            line_++;
            advance();
            if (peek() == '\r') advance();
            value += '\n';
        } else if (peek() == '\r') {
            line_++;
            advance();
            if (peek() == '\n') advance();
            value += '\n';
        } else {
            value += advance();
        }
    }

    if (isAtEnd()) {
        return errorToken("unfinished long string near <eof>");
    }

    return errorToken("unfinished long string");
}

Token Lexer::number() {
    if (source_[start_] == '0' && (peek() == 'x' || peek() == 'X')) {
        advance(); // skip x/X
        while (isxdigit(peek())) {
            advance();
        }
        if (peek() == '.') {
            advance();
            while (isxdigit(peek())) {
                advance();
            }
        }
        if (peek() == 'p' || peek() == 'P') {
            advance();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            while (isDigit(peek())) {
                advance();
            }
        }
    } else {
        while (isDigit(peek())) {
            advance();
        }

        // Look for decimal point
        if (peek() == '.') {
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
