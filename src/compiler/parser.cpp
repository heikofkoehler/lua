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

Token Parser::peekNext() const {
    // Peek at the next token without advancing
    // Save current lexer state and get next token
    return lexer_.peekToken();
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
    if (match(TokenType::LOCAL)) {
        return localDeclaration();
    }
    if (match(TokenType::IF)) {
        return ifStatement();
    }
    if (match(TokenType::WHILE)) {
        return whileStatement();
    }
    if (match(TokenType::REPEAT)) {
        return repeatStatement();
    }
    if (match(TokenType::FOR)) {
        return forStatement();
    }
    if (match(TokenType::FUNCTION)) {
        return functionDeclaration();
    }
    if (match(TokenType::RETURN)) {
        return returnStatement();
    }
    if (match(TokenType::BREAK)) {
        return breakStatement();
    }

    // Check for assignment (simple lookahead for IDENTIFIER = )
    if (current_.type == TokenType::IDENTIFIER) {
        // We need to peek ahead to see if this is an assignment
        // For now, let's handle this in a helper
        return assignmentOrExpression();
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

std::unique_ptr<StmtNode> Parser::assignmentOrExpression() {
    int line = current_.line;

    // Parse first expression
    auto firstExpr = expression();

    // Check for multiple assignment: a, b, c = ...
    if (match(TokenType::COMMA)) {
        // Collect all left-hand side variables
        std::vector<std::string> names;

        // First expression must be a variable
        if (auto* varExpr = dynamic_cast<VariableExprNode*>(firstExpr.get())) {
            names.push_back(varExpr->name());
        } else {
            error("Multiple assignment requires variable names on left side");
            return std::make_unique<ExprStmtNode>(std::move(firstExpr), line);
        }

        // Parse remaining variables: b, c
        do {
            if (!check(TokenType::IDENTIFIER)) {
                errorAtCurrent("Expected variable name in assignment list");
                return nullptr;
            }
            names.push_back(current_.lexeme);
            advance();
        } while (match(TokenType::COMMA));

        // Expect '='
        if (!match(TokenType::EQUAL)) {
            error("Expected '=' after variable list");
            return nullptr;
        }

        // Parse value list: 1, 2, 3
        std::vector<std::unique_ptr<ExprNode>> values;
        do {
            values.push_back(expression());
        } while (match(TokenType::COMMA));

        return std::make_unique<MultipleAssignmentStmtNode>(
            std::move(names), std::move(values), line
        );
    }

    // Single assignment (existing code)
    if (match(TokenType::EQUAL)) {
        if (auto* varExpr = dynamic_cast<VariableExprNode*>(firstExpr.get())) {
            auto value = expression();
            return std::make_unique<AssignmentStmtNode>(varExpr->name(), std::move(value), line);
        } else if (auto* indexExpr = dynamic_cast<IndexExprNode*>(firstExpr.get())) {
            auto value = expression();
            return std::make_unique<IndexAssignmentStmtNode>(
                indexExpr->releaseTable(),
                indexExpr->releaseKey(),
                std::move(value),
                line
            );
        } else {
            error("Invalid assignment target");
        }
    }

    // Expression statement
    return std::make_unique<ExprStmtNode>(std::move(firstExpr), line);
}

std::unique_ptr<StmtNode> Parser::localDeclaration() {
    int line = previous_.line;

    // Parse variable list: local a, b, c
    std::vector<std::string> names;

    do {
        if (!check(TokenType::IDENTIFIER)) {
            errorAtCurrent("Expected variable name");
            return nullptr;
        }
        names.push_back(current_.lexeme);
        advance();
    } while (match(TokenType::COMMA));

    // Parse initializer list (if present)
    std::vector<std::unique_ptr<ExprNode>> initializers;
    if (match(TokenType::EQUAL)) {
        do {
            initializers.push_back(expression());
        } while (match(TokenType::COMMA));
    }

    // Single variable: use existing node for backward compatibility
    if (names.size() == 1) {
        auto init = initializers.empty()
            ? std::make_unique<LiteralNode>(Value::nil(), line)
            : std::move(initializers[0]);
        return std::make_unique<LocalDeclStmtNode>(names[0], std::move(init), line);
    }

    // Multiple variables: use new node
    return std::make_unique<MultipleLocalDeclStmtNode>(
        std::move(names), std::move(initializers), line
    );
}

std::unique_ptr<StmtNode> Parser::ifStatement() {
    int line = previous_.line;

    // Parse condition
    auto condition = expression();
    consume(TokenType::THEN, "Expected 'then' after if condition");

    // Parse then branch
    std::vector<std::unique_ptr<StmtNode>> thenBranch;
    while (!check(TokenType::ELSEIF) && !check(TokenType::ELSE) &&
           !check(TokenType::END) && !isAtEnd()) {
        thenBranch.push_back(statement());
    }

    auto ifNode = std::make_unique<IfStmtNode>(std::move(condition), std::move(thenBranch), line);

    // Parse elseif branches
    while (match(TokenType::ELSEIF)) {
        auto elseIfCondition = expression();
        consume(TokenType::THEN, "Expected 'then' after elseif condition");

        std::vector<std::unique_ptr<StmtNode>> elseIfBody;
        while (!check(TokenType::ELSEIF) && !check(TokenType::ELSE) &&
               !check(TokenType::END) && !isAtEnd()) {
            elseIfBody.push_back(statement());
        }

        ifNode->addElseIfBranch(std::move(elseIfCondition), std::move(elseIfBody));
    }

    // Parse else branch
    if (match(TokenType::ELSE)) {
        std::vector<std::unique_ptr<StmtNode>> elseBranch;
        while (!check(TokenType::END) && !isAtEnd()) {
            elseBranch.push_back(statement());
        }
        ifNode->setElseBranch(std::move(elseBranch));
    }

    consume(TokenType::END, "Expected 'end' after if statement");
    return ifNode;
}

std::unique_ptr<StmtNode> Parser::whileStatement() {
    int line = previous_.line;

    // Parse condition
    auto condition = expression();
    consume(TokenType::DO, "Expected 'do' after while condition");

    // Parse body
    std::vector<std::unique_ptr<StmtNode>> body;
    while (!check(TokenType::END) && !isAtEnd()) {
        body.push_back(statement());
    }

    consume(TokenType::END, "Expected 'end' after while body");
    return std::make_unique<WhileStmtNode>(std::move(condition), std::move(body), line);
}

std::unique_ptr<StmtNode> Parser::repeatStatement() {
    int line = previous_.line;

    // Parse body
    std::vector<std::unique_ptr<StmtNode>> body;
    while (!check(TokenType::UNTIL) && !isAtEnd()) {
        body.push_back(statement());
    }

    consume(TokenType::UNTIL, "Expected 'until' after repeat body");

    // Parse condition
    auto condition = expression();

    return std::make_unique<RepeatStmtNode>(std::move(body), std::move(condition), line);
}

std::unique_ptr<StmtNode> Parser::forStatement() {
    int line = previous_.line;

    // Parse loop variable name(s)
    if (!check(TokenType::IDENTIFIER)) {
        errorAtCurrent("Expected variable name after 'for'");
        return nullptr;
    }
    std::string firstVar = current_.lexeme;
    advance();

    // Check if it's numeric (=) or generic (in or ,) for loop
    if (match(TokenType::EQUAL)) {
        // Numeric for loop: for var = start, end, step do ... end

        // Parse start expression
        auto start = expression();

        // Expect ','
        consume(TokenType::COMMA, "Expected ',' after for start value");

        // Parse end expression
        auto end = expression();

        // Parse optional step (defaults to 1)
        std::unique_ptr<ExprNode> step = nullptr;
        if (match(TokenType::COMMA)) {
            step = expression();
        }

        // Expect 'do'
        consume(TokenType::DO, "Expected 'do' after for clauses");

        // Parse body
        std::vector<std::unique_ptr<StmtNode>> body;
        while (!check(TokenType::END) && !isAtEnd()) {
            body.push_back(statement());
        }

        consume(TokenType::END, "Expected 'end' after for body");

        return std::make_unique<ForStmtNode>(firstVar, std::move(start), std::move(end),
                                             std::move(step), std::move(body), line);
    } else {
        // Generic for loop: for var1, var2 in iterator do ... end
        std::vector<std::string> vars;
        vars.push_back(firstVar);
        
        while (match(TokenType::COMMA)) {
            if (!check(TokenType::IDENTIFIER)) {
                errorAtCurrent("Expected variable name after ','");
                return nullptr;
            }
            vars.push_back(current_.lexeme);
            advance();
        }
        
        if (!match(TokenType::IN)) {
             errorAtCurrent("Expected '=' or 'in' after for variable(s)");
             return nullptr;
        }

        // Parse iterator expression
        auto iterator = expression();

        // Expect 'do'
        consume(TokenType::DO, "Expected 'do' after iterator expression");

        // Parse body
        std::vector<std::unique_ptr<StmtNode>> body;
        while (!check(TokenType::END) && !isAtEnd()) {
            body.push_back(statement());
        }

        consume(TokenType::END, "Expected 'end' after for body");

        return std::make_unique<ForInStmtNode>(std::move(vars), std::move(iterator),
                                               std::move(body), line);
    }
}

std::unique_ptr<StmtNode> Parser::functionDeclaration() {
    int line = previous_.line;

    // Parse function name
    if (!check(TokenType::IDENTIFIER)) {
        errorAtCurrent("Expected function name");
        return nullptr;
    }
    std::string name = current_.lexeme;
    advance();

    // Parse parameter list
    consume(TokenType::LEFT_PAREN, "Expected '(' after function name");

    std::vector<std::string> params;
    bool hasVarargs = false;

    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            // Check for varargs (...)
            if (match(TokenType::DOT_DOT_DOT)) {
                hasVarargs = true;
                break;  // ... must be last parameter
            }

            if (!check(TokenType::IDENTIFIER)) {
                errorAtCurrent("Expected parameter name");
                return nullptr;
            }
            params.push_back(current_.lexeme);
            advance();
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RIGHT_PAREN, "Expected ')' after parameters");

    // Parse body
    std::vector<std::unique_ptr<StmtNode>> body;
    while (!check(TokenType::END) && !isAtEnd()) {
        body.push_back(statement());
    }

    consume(TokenType::END, "Expected 'end' after function body");

    return std::make_unique<FunctionDeclNode>(name, std::move(params), std::move(body), hasVarargs, line);
}

std::unique_ptr<StmtNode> Parser::returnStatement() {
    int line = previous_.line;

    // Parse optional return values (comma-separated)
    std::vector<std::unique_ptr<ExprNode>> values;

    // Check if there's an expression to return
    // Return with no values if we see: end, else, elseif, until, or end of file
    if (!check(TokenType::END) && !check(TokenType::ELSE) &&
        !check(TokenType::ELSEIF) && !check(TokenType::UNTIL) && !isAtEnd()) {
        // Parse first expression
        values.push_back(expression());

        // Parse additional comma-separated expressions
        while (match(TokenType::COMMA)) {
            values.push_back(expression());
        }
    }

    return std::make_unique<ReturnStmtNode>(std::move(values), line);
}

std::unique_ptr<StmtNode> Parser::breakStatement() {
    int line = previous_.line;
    return std::make_unique<BreakStmtNode>(line);
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
    auto expr = concat();

    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) ||
           match(TokenType::LESS) || match(TokenType::LESS_EQUAL)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = concat();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::concat() {
    auto expr = term();

    if (match(TokenType::DOT_DOT)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = concat();  // Right-associative
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

    return postfix();
}

std::unique_ptr<ExprNode> Parser::postfix() {
    auto expr = primary();

    // Handle postfix operations: function calls, table indexing, and field access
    while (true) {
        int line = current_.line;

        // Function call: expr(args)
        if (match(TokenType::LEFT_PAREN)) {
            std::vector<std::unique_ptr<ExprNode>> args;

            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    args.push_back(expression());
                } while (match(TokenType::COMMA));
            }

            consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments");
            expr = std::make_unique<CallExprNode>(std::move(expr), std::move(args), line);
        }
        // Table indexing: expr[key]
        else if (match(TokenType::LEFT_BRACKET)) {
            auto key = expression();
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after table key");
            expr = std::make_unique<IndexExprNode>(std::move(expr), std::move(key), line);
        }
        // Field access with dot notation: expr.field
        else if (match(TokenType::DOT)) {
            if (!check(TokenType::IDENTIFIER)) {
                error("Expected field name after '.'");
                return expr;
            }
            advance();
            std::string fieldName = previous_.lexeme;
            auto key = std::make_unique<StringLiteralNode>(fieldName, line);
            expr = std::make_unique<IndexExprNode>(std::move(expr), std::move(key), line);
        }
        else {
            break;
        }
    }

    return expr;
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

    if (match(TokenType::STRING)) {
        // String content (lexer already stripped quotes)
        return std::make_unique<StringLiteralNode>(previous_.lexeme, line);
    }

    // Varargs
    if (match(TokenType::DOT_DOT_DOT)) {
        return std::make_unique<VarargExprNode>(line);
    }

    // Table constructor
    if (match(TokenType::LEFT_BRACE)) {
        std::vector<TableConstructorNode::Entry> entries;

        // Parse entries until we hit '}'
        if (!check(TokenType::RIGHT_BRACE)) {
            do {
                // Check for [key] = value syntax
                if (match(TokenType::LEFT_BRACKET)) {
                    // Computed key: [expr] = value
                    auto keyExpr = expression();
                    consume(TokenType::RIGHT_BRACKET, "Expected ']' after table key");
                    consume(TokenType::EQUAL, "Expected '=' after table key");

                    TableConstructorNode::Entry entry;
                    entry.key = std::move(keyExpr);
                    entry.value = expression();
                    entries.push_back(std::move(entry));
                }
                // Check for key = value syntax (identifier followed by =)
                else if (check(TokenType::IDENTIFIER) && peekNext().type == TokenType::EQUAL) {
                    // Record-style entry: key = value
                    Token keyToken = current_;
                    advance();  // consume identifier
                    consume(TokenType::EQUAL, "Expected '=' after field name");

                    TableConstructorNode::Entry entry;
                    entry.key = std::make_unique<StringLiteralNode>(keyToken.lexeme, keyToken.line);
                    entry.value = expression();
                    entries.push_back(std::move(entry));
                }
                else {
                    // Array-style entry: value (implicit numeric key)
                    TableConstructorNode::Entry entry;
                    entry.key = nullptr;
                    entry.value = expression();
                    entries.push_back(std::move(entry));
                }
            } while (match(TokenType::COMMA) || match(TokenType::SEMICOLON));
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after table constructor");
        return std::make_unique<TableConstructorNode>(std::move(entries), line);
    }

    // Variable reference (function calls handled in postfix())
    if (match(TokenType::IDENTIFIER)) {
        std::string name = previous_.lexeme;
        return std::make_unique<VariableExprNode>(name, line);
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
