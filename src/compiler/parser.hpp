#ifndef LUA_PARSER_HPP
#define LUA_PARSER_HPP

#include "common/common.hpp"
#include "compiler/token.hpp"
#include "compiler/lexer.hpp"
#include "compiler/ast.hpp"
#include <memory>

// Parser: Converts token stream into Abstract Syntax Tree
// Uses recursive descent parsing with operator precedence

class Parser {
public:
    explicit Parser(Lexer& lexer);

    // Parse entire program
    std::unique_ptr<ProgramNode> parse();

private:
    Lexer& lexer_;
    Token current_;
    Token previous_;
    bool hadError_;
    bool panicMode_;

    // Token management
    void advance();
    void consume(TokenType type, const std::string& message);
    bool check(TokenType type) const;
    Token peekNext() const;  // Look ahead one token
    bool match(TokenType type);
    bool isAtEnd() const;

    // Error handling
    void error(const std::string& message);
    void errorAtCurrent(const std::string& message);
    void errorAt(const Token& token, const std::string& message);
    void synchronize();

    // Parsing methods (in order of precedence, lowest to highest)
    std::unique_ptr<StmtNode> statement();
    std::unique_ptr<StmtNode> printStatement();
    std::unique_ptr<StmtNode> expressionStatement();
    std::unique_ptr<StmtNode> assignmentOrExpression();
    std::unique_ptr<StmtNode> localDeclaration();
    std::unique_ptr<StmtNode> ifStatement();
    std::unique_ptr<StmtNode> whileStatement();
    std::unique_ptr<StmtNode> repeatStatement();
    std::unique_ptr<StmtNode> forStatement();
    std::unique_ptr<StmtNode> functionDeclaration();
    std::unique_ptr<StmtNode> returnStatement();
    std::unique_ptr<StmtNode> breakStatement();

    std::unique_ptr<ExprNode> expression();
    std::unique_ptr<ExprNode> logicalOr();
    std::unique_ptr<ExprNode> logicalAnd();
    std::unique_ptr<ExprNode> equality();
    std::unique_ptr<ExprNode> comparison();
    std::unique_ptr<ExprNode> concat();
    std::unique_ptr<ExprNode> term();
    std::unique_ptr<ExprNode> factor();
    std::unique_ptr<ExprNode> power();
    std::unique_ptr<ExprNode> unary();
    std::unique_ptr<ExprNode> postfix();
    std::unique_ptr<ExprNode> primary();
};

#endif // LUA_PARSER_HPP
