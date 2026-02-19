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

    // Parse the left-hand side expression (could be variable, function call, or table index)
    auto expr = expression();

    // Check if it's an assignment
    if (match(TokenType::EQUAL)) {
        // Check if the expression is assignable
        if (auto* varExpr = dynamic_cast<VariableExprNode*>(expr.get())) {
            // Variable assignment: x = value
            std::string varName = varExpr->name();
            auto value = expression();
            return std::make_unique<AssignmentStmtNode>(varName, std::move(value), line);
        } else if (auto* indexExpr = dynamic_cast<IndexExprNode*>(expr.get())) {
            // Index assignment: t[key] = value
            // We need to extract the table and key from indexExpr, then release ownership
            // Since we need to move them, we'll create new nodes
            auto value = expression();

            // Create IndexAssignmentStmtNode by moving table and key from indexExpr
            return std::make_unique<IndexAssignmentStmtNode>(
                indexExpr->releaseTable(),
                indexExpr->releaseKey(),
                std::move(value),
                line
            );
        } else {
            error("Invalid assignment target");
            return std::make_unique<ExprStmtNode>(std::move(expr), line);
        }
    }

    // Not an assignment, just an expression statement
    return std::make_unique<ExprStmtNode>(std::move(expr), line);
}

std::unique_ptr<StmtNode> Parser::localDeclaration() {
    int line = previous_.line;

    if (!check(TokenType::IDENTIFIER)) {
        errorAtCurrent("Expected variable name after 'local'");
        return nullptr;
    }

    std::string varName = current_.lexeme;
    advance();

    std::unique_ptr<ExprNode> initializer = nullptr;
    if (match(TokenType::EQUAL)) {
        initializer = expression();
    } else {
        // No initializer, defaults to nil
        initializer = std::make_unique<LiteralNode>(Value::nil(), line);
    }

    return std::make_unique<LocalDeclStmtNode>(varName, std::move(initializer), line);
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

    // Parse loop variable name
    if (!check(TokenType::IDENTIFIER)) {
        errorAtCurrent("Expected variable name after 'for'");
        return nullptr;
    }
    std::string varName = current_.lexeme;
    advance();

    // Check if it's numeric (=) or generic (in) for loop
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

        return std::make_unique<ForStmtNode>(varName, std::move(start), std::move(end),
                                             std::move(step), std::move(body), line);
    } else if (match(TokenType::IN)) {
        // Generic for loop: for var in iterator do ... end

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

        return std::make_unique<ForInStmtNode>(varName, std::move(iterator),
                                               std::move(body), line);
    } else {
        errorAtCurrent("Expected '=' or 'in' after for variable");
        return nullptr;
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
    if (!check(TokenType::RIGHT_PAREN)) {
        do {
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

    return std::make_unique<FunctionDeclNode>(name, std::move(params), std::move(body), line);
}

std::unique_ptr<StmtNode> Parser::returnStatement() {
    int line = previous_.line;

    // Parse optional return value
    std::unique_ptr<ExprNode> value = nullptr;

    // Check if there's an expression to return
    // Return with no value if we see: end, else, elseif, until, or end of file
    if (!check(TokenType::END) && !check(TokenType::ELSE) &&
        !check(TokenType::ELSEIF) && !check(TokenType::UNTIL) && !isAtEnd()) {
        value = expression();
    }

    return std::make_unique<ReturnStmtNode>(std::move(value), line);
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

    return postfix();
}

std::unique_ptr<ExprNode> Parser::postfix() {
    auto expr = primary();

    // Handle postfix operations: function calls and table indexing
    while (true) {
        int line = current_.line;

        // Function call: expr(args)
        if (match(TokenType::LEFT_PAREN)) {
            // Only support calls on variable names for now
            auto* varExpr = dynamic_cast<VariableExprNode*>(expr.get());
            if (!varExpr) {
                error("Can only call functions by name");
                return expr;
            }

            std::string name = varExpr->name();
            std::vector<std::unique_ptr<ExprNode>> args;

            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    args.push_back(expression());
                } while (match(TokenType::COMMA));
            }

            consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments");
            expr = std::make_unique<CallExprNode>(name, std::move(args), line);
        }
        // Table indexing: expr[key]
        else if (match(TokenType::LEFT_BRACKET)) {
            auto key = expression();
            consume(TokenType::RIGHT_BRACKET, "Expected ']' after table key");
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

    // Table constructor
    if (match(TokenType::LEFT_BRACE)) {
        consume(TokenType::RIGHT_BRACE, "Expected '}' after table constructor");
        return std::make_unique<TableConstructorNode>(line);
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
