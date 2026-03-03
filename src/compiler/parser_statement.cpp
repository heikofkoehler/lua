std::unique_ptr<StmtNode> Parser::statement() {
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
            body.push_back(statement());
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