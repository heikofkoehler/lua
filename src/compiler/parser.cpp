#include "compiler/parser.hpp"

Parser::Parser(Lexer& lexer)
    : lexer_(lexer), current_(TokenType::ERROR, ""), previous_(TokenType::ERROR, ""),
      hadError_(false), panicMode_(false) {
    advance();  // Prime the parser
}

std::unique_ptr<ProgramNode> Parser::parse() {
    auto program = std::make_unique<ProgramNode>();

    while (!isAtEnd()) {
        try {
            auto stmt = statement();
            if (stmt) {
                program->addStatement(std::move(stmt));
            }
        } catch (const CompileError& e) {
            hadError_ = true;
            Log::error(e.what());
            synchronize();
        }
    }

    if (hadError_) {
        return nullptr;
    }

    return program;
}

void Parser::advance() {
    previous_ = current_;

    while (true) {
        current_ = lexer_.scanToken();
        if (current_.type != TokenType::ERROR) break;

        errorAtCurrent(current_.lexeme);
    }
}

void Parser::consume(TokenType type, const std::string& message) {
    if (current_.type == type) {
        advance();
        return;
    }

    errorAtCurrent(message);
}

bool Parser::check(TokenType type) const {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

bool Parser::isAtEnd() const {
    return current_.type == TokenType::EOF_TOKEN;
}

void Parser::error(const std::string& message) {
    errorAt(previous_, message);
}

void Parser::errorAtCurrent(const std::string& message) {
    errorAt(current_, message);
}

void Parser::errorAt(const Token& token, const std::string& message) {
    if (panicMode_) return;
    panicMode_ = true;
    hadError_ = true;

    std::string errorMsg = message;
    if (token.type == TokenType::EOF_TOKEN) {
        errorMsg = "at end: " + message;
    } else if (token.type != TokenType::ERROR) {
        errorMsg = "at '" + token.lexeme + "': " + message;
    }

    throw CompileError(errorMsg, token.line);
}

void Parser::synchronize() {
    panicMode_ = false;

    while (!isAtEnd()) {
        if (previous_.type == TokenType::SEMICOLON) return;

        switch (current_.type) {
            case TokenType::FUNCTION:
            case TokenType::LOCAL:
            case TokenType::FOR:
            case TokenType::IF:
            case TokenType::WHILE:
            case TokenType::PRINT:
            case TokenType::RETURN:
                return;
            default:
                ; // Continue
        }

        advance();
    }
}

std::unique_ptr<StmtNode> Parser::statement() {
    if (match(TokenType::PRINT)) {
        return printStatement();
    }

    return expressionStatement();
}

std::unique_ptr<StmtNode> Parser::printStatement() {
    int line = previous_.line;
    consume(TokenType::LEFT_PAREN, "Expected '(' after 'print'");
    auto expr = expression();
    consume(TokenType::RIGHT_PAREN, "Expected ')' after expression");

    return std::make_unique<PrintStmtNode>(std::move(expr), line);
}

std::unique_ptr<StmtNode> Parser::expressionStatement() {
    int line = current_.line;
    auto expr = expression();
    return std::make_unique<ExprStmtNode>(std::move(expr), line);
}

std::unique_ptr<ExprNode> Parser::expression() {
    return logicalOr();
}

std::unique_ptr<ExprNode> Parser::logicalOr() {
    auto expr = logicalAnd();

    while (match(TokenType::OR)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = logicalAnd();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::logicalAnd() {
    auto expr = equality();

    while (match(TokenType::AND)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = equality();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::equality() {
    auto expr = comparison();

    while (match(TokenType::EQUAL_EQUAL) || match(TokenType::BANG_EQUAL) || match(TokenType::TILDE_EQUAL)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = comparison();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::comparison() {
    auto expr = term();

    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) ||
           match(TokenType::LESS) || match(TokenType::LESS_EQUAL)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = term();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::term() {
    auto expr = factor();

    while (match(TokenType::PLUS) || match(TokenType::MINUS)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = factor();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::factor() {
    auto expr = power();

    while (match(TokenType::STAR) || match(TokenType::SLASH) || match(TokenType::PERCENT)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = power();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::power() {
    auto expr = unary();

    // Right-associative
    if (match(TokenType::CARET)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = power();  // Recursive call for right-associativity
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::unary() {
    if (match(TokenType::MINUS) || match(TokenType::NOT)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto operand = unary();
        return std::make_unique<UnaryNode>(op, std::move(operand), line);
    }

    return primary();
}

std::unique_ptr<ExprNode> Parser::primary() {
    int line = current_.line;

    // Literals
    if (match(TokenType::FALSE)) {
        return std::make_unique<LiteralNode>(Value::boolean(false), line);
    }

    if (match(TokenType::TRUE)) {
        return std::make_unique<LiteralNode>(Value::boolean(true), line);
    }

    if (match(TokenType::NIL)) {
        return std::make_unique<LiteralNode>(Value::nil(), line);
    }

    if (match(TokenType::NUMBER)) {
        double value = std::stod(previous_.lexeme);
        return std::make_unique<LiteralNode>(Value::number(value), line);
    }

    // Grouping
    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();
        consume(TokenType::RIGHT_PAREN, "Expected ')' after expression");
        return expr;
    }

    errorAtCurrent("Expected expression");
    return nullptr;
}
