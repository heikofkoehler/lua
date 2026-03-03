Token Lexer::string() {
    char quote = source_[start_];  // Remember opening quote (' or ")
    std::string value;

    while (peek() != quote && !isAtEnd()) {
        char c = peek();

        if (c == '') {
            advance();  // consume backslash
            if (isAtEnd()) break;
            char esc = advance();
            switch (esc) {
                case 'a':  value += '\a'; break;
                case 'b':  value += '\b'; break;
                case 'f':  value += '\f'; break;
                case 'n':  value += '
'; break;
                case 'r':  value += ''; break;
                case 't':  value += '	'; break;
                case 'v':  value += '\v'; break;
                case '': value += ''; break;
                case ''': value += '''; break;
                case '"':  value += '"';  break;
                case 'z': {
                    // Skip following whitespace
                    while (!isAtEnd()) {
                        char c = peek();
                        if (c == ' ' || c == '	' || c == '' || c == '
' || c == '\v' || c == '\f') {
                            if (c == '
') line_++;
                            else if (c == '' && peekNext() == '
') {
                                advance(); // skip 
                            } else if (c == '') line_++;
                            advance();
                        } else {
                            break;
                        }
                    }
                    continue; 
                }
                case '
': line_++; value += '
'; break;
                case '':
                    if (peek() == '
') advance();
                    line_++;
                    value += '
';
                    break;
                case 'u': {
                    // Unicode escape \u{XXX}
                    if (advance() != '{') return errorToken("invalid escape sequence", "\u");
                    unsigned long code = 0;
                    int digits = 0;
                    while (isxdigit(peek())) {
                        char h = advance();
                        code = code * 16 + (isdigit(h) ? h - '0' :
                                            tolower(h) - 'a' + 10);
                        digits++;
                    }
                    if (digits == 0) return errorToken("invalid escape sequence", "\u{");
                    if (advance() != '}') return errorToken("invalid escape sequence", "\u{...");
                    
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
                        return errorToken("Unicode escape too large", "\u{...");
                    }
                    break;
                }
                case 'x': {
                    // Hex escape \xXX (exactly 2 digits)
                    if (isAtEnd()) return errorToken("hexadecimal escape requires 2 digits");
                    if (!isxdigit(peek())) return errorToken("invalid escape sequence", "\x" + std::string(1, peek()));
                    char h1 = advance();
                    if (isAtEnd()) return errorToken("hexadecimal escape requires 2 digits");
                    if (!isxdigit(peek())) return errorToken("invalid escape sequence", "\x" + std::string(1, h1) + std::string(1, peek()));
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
                        return errorToken("invalid escape sequence", "" + std::string(1, esc));
                    }
                    break;
            }
        } else {
            if (c == '
') line_++;
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