#include "compiler/parser.hpp"

Parser::Parser(Lexer& lexer)
    : lexer_(lexer), current_(TokenType::ERROR, ""), previous_(TokenType::ERROR, ""),
      hadError_(false), panicMode_(false) {
    advance();  // Prime the parser
}

std::unique_ptr<ProgramNode> Parser::parse() {
    auto program = std::make_unique<ProgramNode>();

    while (!isAtEnd()) {
        auto stmt = statement();
        if (stmt) {
            program->addStatement(std::move(stmt));
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
    std::string near = token.near;
    
    if (near.empty()) {
        if (token.type == TokenType::EOF_TOKEN) {
            near = "<eof>";
        } else if (token.type != TokenType::ERROR) {
            near = "'" + token.lexeme + "'";
        }
    } else {
        if (near != "<eof>") {
            near = "'" + near + "'";
        }
    }

    if (!near.empty()) {
        errorMsg += " near " + near;
    }

    std::string source = lexer_.sourceName();
    if (!source.empty() && source[0] == '@') {
        source = source.substr(1);
    } else if (!source.empty() && source[0] == '=') {
        source = source.substr(1);
    }

    throw CompileError(source + ":" + std::to_string(token.line) + ": " + errorMsg, -1);
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
            case TokenType::RETURN:
                return;
            default:
                ; // Continue
        }

        advance();
    }
}
std::unique_ptr<StmtNode> Parser::statement() {
    if (match(TokenType::SEMICOLON)) {
        return nullptr;
    }
    if (match(TokenType::LOCAL)) {
        if (match(TokenType::FUNCTION)) {
            return localFunctionDeclaration();
        }
        return localDeclaration();
    }
    if (match(TokenType::GLOBAL)) {
        return globalDeclaration();
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
    if (match(TokenType::GOTO)) {
        return gotoStatement();
    }
    if (match(TokenType::COLON_COLON)) {
        return labelStatement();
    }
    if (match(TokenType::BREAK)) {
        return breakStatement();
    }
    if (match(TokenType::RETURN)) {
        return returnStatement();
    }
    if (match(TokenType::DO)) {
        int line = previous_.line;
        std::vector<std::unique_ptr<StmtNode>> body;
        while (!check(TokenType::END) && !isAtEnd()) {
            if (auto stmt = statement()) {
                body.push_back(std::move(stmt));
            }
        }
        consume(TokenType::END, "Expected 'end' after 'do' block");
        return std::make_unique<BlockStmtNode>(std::move(body), line);
    }

    // Check for assignment (simple lookahead for IDENTIFIER = )
    if (current_.type == TokenType::IDENTIFIER) {
        // We need to peek ahead to see if this is an assignment
        // For now, let's handle this in a helper
        return assignmentOrExpression();
    }

    return expressionStatement();
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

    // Check for assignment (list of variables followed by =)
    if (match(TokenType::COMMA)) {
        std::vector<std::unique_ptr<ExprNode>> targets;
        targets.push_back(std::move(firstExpr));

        do {
            targets.push_back(expression());
        } while (match(TokenType::COMMA));

        if (match(TokenType::EQUAL)) {
            // Verify all targets are valid
            for (const auto& target : targets) {
                if (dynamic_cast<VariableExprNode*>(target.get()) == nullptr &&
                    dynamic_cast<IndexExprNode*>(target.get()) == nullptr) {
                    error("Invalid assignment target");
                }
            }

            // Parse value list: 1, 2, 3
            std::vector<std::unique_ptr<ExprNode>> values;
            do {
                values.push_back(expression());
            } while (match(TokenType::COMMA));

            return std::make_unique<MultipleAssignmentStmtNode>(
                std::move(targets), std::move(values), line
            );
        } else {
            error("Expected '=' after variable list");
            return nullptr;
        }
    } else if (match(TokenType::EQUAL)) {
        std::vector<std::unique_ptr<ExprNode>> targets;
        targets.push_back(std::move(firstExpr));
        
        // Verify target is valid
        if (dynamic_cast<VariableExprNode*>(targets[0].get()) == nullptr &&
            dynamic_cast<IndexExprNode*>(targets[0].get()) == nullptr) {
            error("Invalid assignment target");
        }

        // Parse value list: 1, 2, 3
        std::vector<std::unique_ptr<ExprNode>> values;
        do {
            values.push_back(expression());
        } while (match(TokenType::COMMA));

        return std::make_unique<MultipleAssignmentStmtNode>(
            std::move(targets), std::move(values), line
        );
    }

    // Expression statement
    return std::make_unique<ExprStmtNode>(std::move(firstExpr), line);
}

Parser::Attribute Parser::attribute() {
    Attribute attr;
    if (match(TokenType::LESS)) {
        consume(TokenType::IDENTIFIER, "Expected attribute name");
        if (previous_.lexeme == "const") {
            attr.isConstant = true;
        } else if (previous_.lexeme == "close") {
            attr.isClose = true;
            attr.isConstant = true; // <close> variables are also constant
        } else {
            errorAt(previous_, "unknown attribute '" + previous_.lexeme + "'");
        }
        consume(TokenType::GREATER, "Expected '>' after attribute");
    }
    return attr;
}

std::unique_ptr<StmtNode> Parser::localDeclaration() {
    int line = previous_.line;

    // Parse variable list: local a <attr>, b <attr>, c
    std::vector<MultipleLocalDeclStmtNode::VarInfo> vars;

    do {
        if (!check(TokenType::IDENTIFIER)) {
            errorAtCurrent("Expected variable name");
            return nullptr;
        }
        std::string name = current_.lexeme;
        advance();
        Attribute attr = attribute();
        vars.push_back({name, attr.isConstant, attr.isClose});
    } while (match(TokenType::COMMA));

    // Parse initializer list (if present)
    std::vector<std::unique_ptr<ExprNode>> initializers;
    if (match(TokenType::EQUAL)) {
        do {
            initializers.push_back(expression());
        } while (match(TokenType::COMMA));
    }

    // Single variable
    if (vars.size() == 1) {
        auto init = initializers.empty()
            ? std::make_unique<LiteralNode>(Value::nil(), line)
            : std::move(initializers[0]);
        return std::make_unique<LocalDeclStmtNode>(
            vars[0].name, std::move(init), line, false, vars[0].isConstant, vars[0].isClose
        );
    }

    // Multiple variables
    return std::make_unique<MultipleLocalDeclStmtNode>(
        std::move(vars), std::move(initializers), line
    );
}

std::unique_ptr<StmtNode> Parser::globalDeclaration() {
    int line = previous_.line;

    // Allow `global function name ...` syntax which simply declares a global function
    if (match(TokenType::FUNCTION)) {
        // Now previous_ is the FUNCTION token; delegate to standard function parser
        return functionDeclaration();
    }

    // Check for global <const> *
    if (match(TokenType::LESS)) {
        consume(TokenType::IDENTIFIER, "Expected attribute name");
        bool isConstant = (previous_.lexeme == "const");
        consume(TokenType::GREATER, "Expected '>' after attribute");
        
        if (match(TokenType::STAR)) {
            // global <attr> *
            return std::make_unique<GlobalDeclStmtNode>("*", isConstant, line);
        } else {
            // This was probably meant for a variable but we don't allow it yet 
            // without a name before the attribute in this simple implementation
            error("Expected '*' or variable name before attribute");
            return nullptr;
        }
    }

    // Parse variable list: global a <attr>, b <attr>, c
    std::vector<MultipleGlobalDeclStmtNode::VarInfo> vars;

    do {
        if (match(TokenType::STAR)) {
            vars.push_back({"*", false});
        } else {
            if (!check(TokenType::IDENTIFIER)) {
                errorAtCurrent("Expected variable name");
                return nullptr;
            }
            std::string name = current_.lexeme;
            advance();
            Attribute attr = attribute();
            vars.push_back({name, attr.isConstant});
        }
    } while (match(TokenType::COMMA));

    if (vars.size() == 1) {
        return std::make_unique<GlobalDeclStmtNode>(vars[0].name, vars[0].isConstant, line);
    }

    return std::make_unique<MultipleGlobalDeclStmtNode>(std::move(vars), line);
}

std::unique_ptr<StmtNode> Parser::localFunctionDeclaration() {
    int line = previous_.line;

    if (!check(TokenType::IDENTIFIER)) {
        errorAtCurrent("Expected function name after 'local function'");
        return nullptr;
    }
    std::string name = current_.lexeme;
    advance();

    FunctionBody fb = parseFunctionBody("after function name");
    auto funcExpr = std::make_unique<FunctionExprNode>(std::move(fb.params), std::move(fb.body), fb.hasVarargs, line);

    return std::make_unique<LocalDeclStmtNode>(name, std::move(funcExpr), line, true);
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
        if (auto s = statement()) thenBranch.push_back(std::move(s));
    }

    auto ifNode = std::make_unique<IfStmtNode>(std::move(condition), std::move(thenBranch), line);

    // Parse elseif branches
    while (match(TokenType::ELSEIF)) {
        auto elseIfCondition = expression();
        consume(TokenType::THEN, "Expected 'then' after elseif condition");

        std::vector<std::unique_ptr<StmtNode>> elseIfBody;
        while (!check(TokenType::ELSEIF) && !check(TokenType::ELSE) &&
               !check(TokenType::END) && !isAtEnd()) {
            if (auto s = statement()) elseIfBody.push_back(std::move(s));
        }

        ifNode->addElseIfBranch(std::move(elseIfCondition), std::move(elseIfBody));
    }

    // Parse else branch
    if (match(TokenType::ELSE)) {
        std::vector<std::unique_ptr<StmtNode>> elseBranch;
        while (!check(TokenType::END) && !isAtEnd()) {
            if (auto s = statement()) elseBranch.push_back(std::move(s));
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
        if (auto s = statement()) body.push_back(std::move(s));
    }

    consume(TokenType::END, "Expected 'end' after while body");

    return std::make_unique<WhileStmtNode>(std::move(condition), std::move(body), line);
}

std::unique_ptr<StmtNode> Parser::repeatStatement() {
    int line = previous_.line;

    // Parse body
    std::vector<std::unique_ptr<StmtNode>> body;
    while (!check(TokenType::UNTIL) && !isAtEnd()) {
        if (auto s = statement()) body.push_back(std::move(s));
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
            if (auto s = statement()) body.push_back(std::move(s));
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
            if (auto s = statement()) body.push_back(std::move(s));
        }

        consume(TokenType::END, "Expected 'end' after for body");

        return std::make_unique<ForInStmtNode>(std::move(vars), std::move(iterator),
                                               std::move(body), line);
    }
}

std::unique_ptr<StmtNode> Parser::functionDeclaration() {
    int line = previous_.line;

    // Parse function name: ID {'.' ID} [':' ID]
    if (!match(TokenType::IDENTIFIER)) {
        errorAtCurrent("Expected function name");
        return nullptr;
    }
    std::string field = previous_.lexeme;
    std::unique_ptr<ExprNode> table = nullptr;

    while (match(TokenType::DOT)) {
        if (!table) {
            table = std::make_unique<VariableExprNode>(field, line);
        } else {
            table = std::make_unique<IndexExprNode>(std::move(table),
                        std::make_unique<StringLiteralNode>(field, line), line);
        }
        consume(TokenType::IDENTIFIER, "Expected field name after '.'");
        field = previous_.lexeme;
    }

    bool isMethod = false;
    if (match(TokenType::COLON)) {
        if (!table) {
            table = std::make_unique<VariableExprNode>(field, line);
        } else {
            table = std::make_unique<IndexExprNode>(std::move(table),
                        std::make_unique<StringLiteralNode>(field, line), line);
        }
        consume(TokenType::IDENTIFIER, "Expected method name after ':'");
        field = previous_.lexeme;
        isMethod = true;
    }

    FunctionBody fb = parseFunctionBody("after function name");
    if (isMethod) {
        fb.params.insert(fb.params.begin(), "self");
    }

    auto funcExpr = std::make_unique<FunctionExprNode>(std::move(fb.params), std::move(fb.body), fb.hasVarargs, line);

    if (table) {
        return std::make_unique<IndexAssignmentStmtNode>(std::move(table),
                    std::make_unique<StringLiteralNode>(field, line),
                    std::move(funcExpr), line);
    } else {
        return std::make_unique<AssignmentStmtNode>(field, std::move(funcExpr), line);
    }
}

Parser::FunctionBody Parser::parseFunctionBody(const std::string& context) {
    // Parse parameter list
    consume(TokenType::LEFT_PAREN, "Expected '(' " + context);

    FunctionBody fb;
    fb.hasVarargs = false;

    if (!check(TokenType::RIGHT_PAREN)) {
        do {
            // Check for varargs (...)
            if (match(TokenType::DOT_DOT_DOT)) {
                fb.hasVarargs = true;
                break;  // ... must be last parameter
            }

            if (!check(TokenType::IDENTIFIER)) {
                errorAtCurrent("Expected parameter name");
                break;
            }
            fb.params.push_back(current_.lexeme);
            advance();
        } while (match(TokenType::COMMA));
    }

    consume(TokenType::RIGHT_PAREN, "Expected ')' after parameters");

    // Parse body
    while (!check(TokenType::END) && !isAtEnd()) {
        if (auto s = statement()) fb.body.push_back(std::move(s));
    }

    consume(TokenType::END, "Expected 'end' after function body");

    return fb;
}

std::unique_ptr<StmtNode> Parser::returnStatement() {
    int line = previous_.line;

    // Parse optional return values (comma-separated)
    std::vector<std::unique_ptr<ExprNode>> values;

    // Check if there's an expression to return
    // Return with no values if we see: end, else, elseif, until, semicolon, or end of file
    if (!check(TokenType::END) && !check(TokenType::ELSE) &&
        !check(TokenType::ELSEIF) && !check(TokenType::UNTIL) && 
        !check(TokenType::SEMICOLON) && !isAtEnd()) {
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

std::unique_ptr<StmtNode> Parser::gotoStatement() {
    int line = previous_.line;
    consume(TokenType::IDENTIFIER, "Expected label name after 'goto'");
    std::string label = previous_.lexeme;
    return std::make_unique<GotoStmtNode>(label, line);
}

std::unique_ptr<StmtNode> Parser::labelStatement() {
    int line = previous_.line;
    consume(TokenType::IDENTIFIER, "Expected label name after '::'");
    std::string label = previous_.lexeme;
    consume(TokenType::COLON_COLON, "Expected '::' after label name");
    return std::make_unique<LabelStmtNode>(label, line);
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
    auto expr = bitwiseOr();

    while (match(TokenType::GREATER) || match(TokenType::GREATER_EQUAL) ||
           match(TokenType::LESS) || match(TokenType::LESS_EQUAL)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = bitwiseOr();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::bitwiseOr() {
    auto expr = bitwiseXor();

    while (match(TokenType::PIPE)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = bitwiseXor();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::bitwiseXor() {
    auto expr = bitwiseAnd();

    while (match(TokenType::TILDE)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = bitwiseAnd();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::bitwiseAnd() {
    auto expr = bitwiseShift();

    while (match(TokenType::AMPERSAND)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = bitwiseShift();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::bitwiseShift() {
    auto expr = concat();

    while (match(TokenType::LESS_LESS) || match(TokenType::GREATER_GREATER)) {
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
    auto expr = unary();

    while (match(TokenType::STAR) || match(TokenType::SLASH) || match(TokenType::SLASH_SLASH) || match(TokenType::PERCENT)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = unary();
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::unary() {
    if (match(TokenType::MINUS) || match(TokenType::NOT) || match(TokenType::TILDE) || match(TokenType::HASH)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto operand = unary();
        return std::make_unique<UnaryNode>(op, std::move(operand), line);
    }

    return power();
}

std::unique_ptr<ExprNode> Parser::power() {
    auto expr = postfix();

    // Right-associative
    if (match(TokenType::CARET)) {
        TokenType op = previous_.type;
        int line = previous_.line;
        auto right = unary();  // Allows a^-b and right-associativity via unary->power
        expr = std::make_unique<BinaryNode>(std::move(expr), op, std::move(right), line);
    }

    return expr;
}

std::unique_ptr<ExprNode> Parser::postfix() {
    auto expr = primary();

    // Handle postfix operations: function calls, table indexing, and field access
    while (true) {
        int line = current_.line;

        // Function call: expr(args), expr{table}, expr"string"
        if (match(TokenType::LEFT_PAREN)) {
            std::vector<std::unique_ptr<ExprNode>> args;

            if (!check(TokenType::RIGHT_PAREN)) {
                do {
                    args.push_back(expression());
                } while (match(TokenType::COMMA));
            }

            consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments");
            expr = std::make_unique<CallExprNode>(std::move(expr), std::move(args), line);
        } else if (check(TokenType::LEFT_BRACE)) {
            // expr{table}
            std::vector<std::unique_ptr<ExprNode>> args;
            args.push_back(primary()); // table constructor is handled by primary()
            expr = std::make_unique<CallExprNode>(std::move(expr), std::move(args), line);
        } else if (match(TokenType::STRING)) {
            // expr"string"
            std::vector<std::unique_ptr<ExprNode>> args;
            args.push_back(std::make_unique<StringLiteralNode>(previous_.lexeme, previous_.line));
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
        // Method call: expr:method(args), expr:method{table}, expr:method"string"
        else if (match(TokenType::COLON)) {
            if (!check(TokenType::IDENTIFIER)) {
                error("Expected method name after ':'");
                return expr;
            }
            advance();
            std::string methodName = previous_.lexeme;
            
            std::vector<std::unique_ptr<ExprNode>> args;
            if (match(TokenType::LEFT_PAREN)) {
                if (!check(TokenType::RIGHT_PAREN)) {
                    do {
                        args.push_back(expression());
                    } while (match(TokenType::COMMA));
                }
                consume(TokenType::RIGHT_PAREN, "Expected ')' after arguments");
            } else if (check(TokenType::LEFT_BRACE)) {
                args.push_back(primary());
            } else if (match(TokenType::STRING)) {
                args.push_back(std::make_unique<StringLiteralNode>(previous_.lexeme, previous_.line));
            } else {
                error("Expected '(' or '{' or string after method name");
                return expr;
            }
            
            expr = std::make_unique<MethodCallExprNode>(std::move(expr), methodName, std::move(args), line);
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
            } while ((match(TokenType::COMMA) || match(TokenType::SEMICOLON)) && !check(TokenType::RIGHT_BRACE));
        }

        consume(TokenType::RIGHT_BRACE, "Expected '}' after table constructor");
        return std::make_unique<TableConstructorNode>(std::move(entries), line);
    }

    // Variable reference (function calls handled in postfix())
    if (match(TokenType::IDENTIFIER)) {
        std::string name = previous_.lexeme;
        return std::make_unique<VariableExprNode>(name, line);
    }

    // Anonymous function
    if (match(TokenType::FUNCTION)) {
        FunctionBody fb = parseFunctionBody("for anonymous function");
        return std::make_unique<FunctionExprNode>(std::move(fb.params), std::move(fb.body), fb.hasVarargs, line);
    }

    // Grouping
    if (match(TokenType::LEFT_PAREN)) {
        auto expr = expression();
        consume(TokenType::RIGHT_PAREN, "Expected ')' after expression");
        return std::make_unique<GroupExprNode>(std::move(expr), line);
    }

    errorAtCurrent("Expected expression");
    return nullptr;
}
