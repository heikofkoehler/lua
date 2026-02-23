#ifndef LUA_AST_HPP
#define LUA_AST_HPP

#include "common/common.hpp"
#include "value/value.hpp"
#include "compiler/token.hpp"
#include <memory>
#include <vector>

// Forward declarations
class ASTVisitor;

// Base AST Node
class ASTNode {
public:
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;

    int line() const { return line_; }

protected:
    explicit ASTNode(int line) : line_(line) {}

private:
    int line_;  // Source line number for error reporting
};

// Expression Nodes
class ExprNode : public ASTNode {
protected:
    explicit ExprNode(int line) : ASTNode(line) {}
};

// Literal: numbers, booleans, nil
class LiteralNode : public ExprNode {
public:
    LiteralNode(const Value& value, int line)
        : ExprNode(line), value_(value) {}

    void accept(ASTVisitor& visitor) override;

    const Value& value() const { return value_; }

private:
    Value value_;
};

// String literal (interned during codegen)
class StringLiteralNode : public ExprNode {
public:
    StringLiteralNode(const std::string& content, int line)
        : ExprNode(line), content_(content) {}

    void accept(ASTVisitor& visitor) override;

    const std::string& content() const { return content_; }

private:
    std::string content_;
};

// Unary operation: -x, not x
class UnaryNode : public ExprNode {
public:
    UnaryNode(TokenType op, std::unique_ptr<ExprNode> operand, int line)
        : ExprNode(line), op_(op), operand_(std::move(operand)) {}

    void accept(ASTVisitor& visitor) override;

    TokenType op() const { return op_; }
    ExprNode* operand() const { return operand_.get(); }

private:
    TokenType op_;
    std::unique_ptr<ExprNode> operand_;
};

// Binary operation: a + b, a * b, etc.
class BinaryNode : public ExprNode {
public:
    BinaryNode(std::unique_ptr<ExprNode> left, TokenType op,
               std::unique_ptr<ExprNode> right, int line)
        : ExprNode(line), left_(std::move(left)), op_(op), right_(std::move(right)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* left() const { return left_.get(); }
    TokenType op() const { return op_; }
    ExprNode* right() const { return right_.get(); }

private:
    std::unique_ptr<ExprNode> left_;
    TokenType op_;
    std::unique_ptr<ExprNode> right_;
};

// Variable reference: reading a variable
class VariableExprNode : public ExprNode {
public:
    VariableExprNode(const std::string& name, int line)
        : ExprNode(line), name_(name) {}

    void accept(ASTVisitor& visitor) override;

    const std::string& name() const { return name_; }

private:
    std::string name_;
};

// Vararg expression: ... (variable arguments)
class VarargExprNode : public ExprNode {
public:
    VarargExprNode(int line) : ExprNode(line) {}

    void accept(ASTVisitor& visitor) override;
};

// Function call: func(args)
class CallExprNode : public ExprNode {
public:
    CallExprNode(std::unique_ptr<ExprNode> callee,
                 std::vector<std::unique_ptr<ExprNode>> args,
                 int line)
        : ExprNode(line), callee_(std::move(callee)), args_(std::move(args)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* callee() const { return callee_.get(); }
    const std::vector<std::unique_ptr<ExprNode>>& args() const { return args_; }

private:
    std::unique_ptr<ExprNode> callee_;
    std::vector<std::unique_ptr<ExprNode>> args_;
};

// Table constructor: {}
class TableConstructorNode : public ExprNode {
public:
    // Entry types in table constructor
    struct Entry {
        std::unique_ptr<ExprNode> key;    // nullptr for array-style entries
        std::unique_ptr<ExprNode> value;
    };

    TableConstructorNode(std::vector<Entry> entries, int line)
        : ExprNode(line), entries_(std::move(entries)) {}

    void accept(ASTVisitor& visitor) override;

    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::vector<Entry> entries_;
};

// Index expression: table[key]
class IndexExprNode : public ExprNode {
public:
    IndexExprNode(std::unique_ptr<ExprNode> table,
                  std::unique_ptr<ExprNode> key,
                  int line)
        : ExprNode(line), table_(std::move(table)), key_(std::move(key)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* table() const { return table_.get(); }
    ExprNode* key() const { return key_.get(); }

    // Release ownership (for assignment parsing)
    std::unique_ptr<ExprNode> releaseTable() { return std::move(table_); }
    std::unique_ptr<ExprNode> releaseKey() { return std::move(key_); }

private:
    std::unique_ptr<ExprNode> table_;
    std::unique_ptr<ExprNode> key_;
};

// Statement Nodes
class StmtNode : public ASTNode {
protected:
    explicit StmtNode(int line) : ASTNode(line) {}
};

// Print statement: print(expr)
class PrintStmtNode : public StmtNode {
public:
    PrintStmtNode(std::unique_ptr<ExprNode> expr, int line)
        : StmtNode(line), expr_(std::move(expr)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* expr() const { return expr_.get(); }

private:
    std::unique_ptr<ExprNode> expr_;
};

// Expression statement: evaluate expression and discard result
class ExprStmtNode : public StmtNode {
public:
    ExprStmtNode(std::unique_ptr<ExprNode> expr, int line)
        : StmtNode(line), expr_(std::move(expr)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* expr() const { return expr_.get(); }

private:
    std::unique_ptr<ExprNode> expr_;
};

// Assignment statement: variable = expression
class AssignmentStmtNode : public StmtNode {
public:
    AssignmentStmtNode(const std::string& name, std::unique_ptr<ExprNode> value, int line)
        : StmtNode(line), name_(name), value_(std::move(value)) {}

    void accept(ASTVisitor& visitor) override;

    const std::string& name() const { return name_; }
    ExprNode* value() const { return value_.get(); }

private:
    std::string name_;
    std::unique_ptr<ExprNode> value_;
};

// Index assignment statement: table[key] = value
class IndexAssignmentStmtNode : public StmtNode {
public:
    IndexAssignmentStmtNode(std::unique_ptr<ExprNode> table,
                           std::unique_ptr<ExprNode> key,
                           std::unique_ptr<ExprNode> value,
                           int line)
        : StmtNode(line), table_(std::move(table)), key_(std::move(key)),
          value_(std::move(value)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* table() const { return table_.get(); }
    ExprNode* key() const { return key_.get(); }
    ExprNode* value() const { return value_.get(); }

private:
    std::unique_ptr<ExprNode> table_;
    std::unique_ptr<ExprNode> key_;
    std::unique_ptr<ExprNode> value_;
};

// Local variable declaration: local variable = expression
class LocalDeclStmtNode : public StmtNode {
public:
    LocalDeclStmtNode(const std::string& name, std::unique_ptr<ExprNode> initializer, int line)
        : StmtNode(line), name_(name), initializer_(std::move(initializer)) {}

    void accept(ASTVisitor& visitor) override;

    const std::string& name() const { return name_; }
    ExprNode* initializer() const { return initializer_.get(); }

private:
    std::string name_;
    std::unique_ptr<ExprNode> initializer_;
};

// Multiple local variable declaration: local a, b, c = 1, 2, 3
class MultipleLocalDeclStmtNode : public StmtNode {
public:
    MultipleLocalDeclStmtNode(std::vector<std::string> names,
                             std::vector<std::unique_ptr<ExprNode>> initializers,
                             int line)
        : StmtNode(line), names_(std::move(names)), initializers_(std::move(initializers)) {}

    void accept(ASTVisitor& visitor) override;

    const std::vector<std::string>& names() const { return names_; }
    const std::vector<std::unique_ptr<ExprNode>>& initializers() const { return initializers_; }

private:
    std::vector<std::string> names_;
    std::vector<std::unique_ptr<ExprNode>> initializers_;
};

// Multiple assignment: x, y, z = 1, 2, 3
class MultipleAssignmentStmtNode : public StmtNode {
public:
    MultipleAssignmentStmtNode(std::vector<std::string> names,
                              std::vector<std::unique_ptr<ExprNode>> values,
                              int line)
        : StmtNode(line), names_(std::move(names)), values_(std::move(values)) {}

    void accept(ASTVisitor& visitor) override;

    const std::vector<std::string>& names() const { return names_; }
    const std::vector<std::unique_ptr<ExprNode>>& values() const { return values_; }

private:
    std::vector<std::string> names_;
    std::vector<std::unique_ptr<ExprNode>> values_;
};

// If statement: if-then-elseif-else-end
class IfStmtNode : public StmtNode {
public:
    struct ElseIfBranch {
        std::unique_ptr<ExprNode> condition;
        std::vector<std::unique_ptr<StmtNode>> body;
    };

    IfStmtNode(std::unique_ptr<ExprNode> condition,
               std::vector<std::unique_ptr<StmtNode>> thenBranch,
               int line)
        : StmtNode(line), condition_(std::move(condition)),
          thenBranch_(std::move(thenBranch)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* condition() const { return condition_.get(); }
    const std::vector<std::unique_ptr<StmtNode>>& thenBranch() const { return thenBranch_; }
    const std::vector<ElseIfBranch>& elseIfBranches() const { return elseIfBranches_; }
    const std::vector<std::unique_ptr<StmtNode>>& elseBranch() const { return elseBranch_; }

    void addElseIfBranch(std::unique_ptr<ExprNode> condition,
                         std::vector<std::unique_ptr<StmtNode>> body) {
        elseIfBranches_.push_back({std::move(condition), std::move(body)});
    }

    void setElseBranch(std::vector<std::unique_ptr<StmtNode>> body) {
        elseBranch_ = std::move(body);
    }

private:
    std::unique_ptr<ExprNode> condition_;
    std::vector<std::unique_ptr<StmtNode>> thenBranch_;
    std::vector<ElseIfBranch> elseIfBranches_;
    std::vector<std::unique_ptr<StmtNode>> elseBranch_;
};

// While loop: while-do-end
class WhileStmtNode : public StmtNode {
public:
    WhileStmtNode(std::unique_ptr<ExprNode> condition,
                  std::vector<std::unique_ptr<StmtNode>> body,
                  int line)
        : StmtNode(line), condition_(std::move(condition)), body_(std::move(body)) {}

    void accept(ASTVisitor& visitor) override;

    ExprNode* condition() const { return condition_.get(); }
    const std::vector<std::unique_ptr<StmtNode>>& body() const { return body_; }

private:
    std::unique_ptr<ExprNode> condition_;
    std::vector<std::unique_ptr<StmtNode>> body_;
};

// Repeat-until loop: repeat-until
class RepeatStmtNode : public StmtNode {
public:
    RepeatStmtNode(std::vector<std::unique_ptr<StmtNode>> body,
                   std::unique_ptr<ExprNode> condition,
                   int line)
        : StmtNode(line), body_(std::move(body)), condition_(std::move(condition)) {}

    void accept(ASTVisitor& visitor) override;

    const std::vector<std::unique_ptr<StmtNode>>& body() const { return body_; }
    ExprNode* condition() const { return condition_.get(); }

private:
    std::vector<std::unique_ptr<StmtNode>> body_;
    std::unique_ptr<ExprNode> condition_;
};

// Numeric for loop: for var = start, end, step do body end
class ForStmtNode : public StmtNode {
public:
    ForStmtNode(const std::string& varName,
                std::unique_ptr<ExprNode> start,
                std::unique_ptr<ExprNode> end,
                std::unique_ptr<ExprNode> step,
                std::vector<std::unique_ptr<StmtNode>> body,
                int line)
        : StmtNode(line), varName_(varName), start_(std::move(start)),
          end_(std::move(end)), step_(std::move(step)), body_(std::move(body)) {}

    void accept(ASTVisitor& visitor) override;

    const std::string& varName() const { return varName_; }
    ExprNode* start() const { return start_.get(); }
    ExprNode* end() const { return end_.get(); }
    ExprNode* step() const { return step_.get(); }
    const std::vector<std::unique_ptr<StmtNode>>& body() const { return body_; }

private:
    std::string varName_;
    std::unique_ptr<ExprNode> start_;
    std::unique_ptr<ExprNode> end_;
    std::unique_ptr<ExprNode> step_;  // Can be nullptr (defaults to 1)
    std::vector<std::unique_ptr<StmtNode>> body_;
};

// Generic for loop: for var1, var2 in iterator do body end
class ForInStmtNode : public StmtNode {
public:
    ForInStmtNode(std::vector<std::string> varNames,
                  std::unique_ptr<ExprNode> iterator,
                  std::vector<std::unique_ptr<StmtNode>> body,
                  int line)
        : StmtNode(line), varNames_(std::move(varNames)), iterator_(std::move(iterator)),
          body_(std::move(body)) {}

    void accept(ASTVisitor& visitor) override;

    const std::vector<std::string>& varNames() const { return varNames_; }
    ExprNode* iterator() const { return iterator_.get(); }
    const std::vector<std::unique_ptr<StmtNode>>& body() const { return body_; }

private:
    std::vector<std::string> varNames_;
    std::unique_ptr<ExprNode> iterator_;
    std::vector<std::unique_ptr<StmtNode>> body_;
};

// Function declaration: function name(params) body end
class FunctionDeclNode : public StmtNode {
public:
    FunctionDeclNode(const std::string& name,
                     std::vector<std::string> params,
                     std::vector<std::unique_ptr<StmtNode>> body,
                     bool hasVarargs,
                     int line)
        : StmtNode(line), name_(name), params_(std::move(params)),
          body_(std::move(body)), hasVarargs_(hasVarargs) {}

    void accept(ASTVisitor& visitor) override;

    const std::string& name() const { return name_; }
    const std::vector<std::string>& params() const { return params_; }
    const std::vector<std::unique_ptr<StmtNode>>& body() const { return body_; }
    bool hasVarargs() const { return hasVarargs_; }

private:
    std::string name_;
    std::vector<std::string> params_;
    std::vector<std::unique_ptr<StmtNode>> body_;
    bool hasVarargs_;
};

// Return statement: return expr1, expr2, ...
class ReturnStmtNode : public StmtNode {
public:
    ReturnStmtNode(std::vector<std::unique_ptr<ExprNode>> values, int line)
        : StmtNode(line), values_(std::move(values)) {}

    void accept(ASTVisitor& visitor) override;

    const std::vector<std::unique_ptr<ExprNode>>& values() const { return values_; }

private:
    std::vector<std::unique_ptr<ExprNode>> values_;  // Can be empty for 'return' with no values
};

// Break statement: break
class BreakStmtNode : public StmtNode {
public:
    BreakStmtNode(int line) : StmtNode(line) {}

    void accept(ASTVisitor& visitor) override;
};

// Program: list of statements
class ProgramNode : public ASTNode {
public:
    explicit ProgramNode(int line = 1) : ASTNode(line) {}

    void accept(ASTVisitor& visitor) override;

    void addStatement(std::unique_ptr<StmtNode> stmt) {
        statements_.push_back(std::move(stmt));
    }

    const std::vector<std::unique_ptr<StmtNode>>& statements() const {
        return statements_;
    }

private:
    std::vector<std::unique_ptr<StmtNode>> statements_;
};

// Visitor interface for traversing AST
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visitLiteral(LiteralNode* node) = 0;
    virtual void visitStringLiteral(StringLiteralNode* node) = 0;
    virtual void visitUnary(UnaryNode* node) = 0;
    virtual void visitBinary(BinaryNode* node) = 0;
    virtual void visitVariable(VariableExprNode* node) = 0;
    virtual void visitVararg(VarargExprNode* node) = 0;
    virtual void visitCall(CallExprNode* node) = 0;
    virtual void visitTableConstructor(TableConstructorNode* node) = 0;
    virtual void visitIndexExpr(IndexExprNode* node) = 0;
    virtual void visitPrintStmt(PrintStmtNode* node) = 0;
    virtual void visitExprStmt(ExprStmtNode* node) = 0;
    virtual void visitAssignmentStmt(AssignmentStmtNode* node) = 0;
    virtual void visitIndexAssignmentStmt(IndexAssignmentStmtNode* node) = 0;
    virtual void visitLocalDeclStmt(LocalDeclStmtNode* node) = 0;
    virtual void visitMultipleLocalDeclStmt(MultipleLocalDeclStmtNode* node) = 0;
    virtual void visitMultipleAssignmentStmt(MultipleAssignmentStmtNode* node) = 0;
    virtual void visitIfStmt(IfStmtNode* node) = 0;
    virtual void visitWhileStmt(WhileStmtNode* node) = 0;
    virtual void visitRepeatStmt(RepeatStmtNode* node) = 0;
    virtual void visitForStmt(ForStmtNode* node) = 0;
    virtual void visitForInStmt(ForInStmtNode* node) = 0;
    virtual void visitFunctionDecl(FunctionDeclNode* node) = 0;
    virtual void visitReturn(ReturnStmtNode* node) = 0;
    virtual void visitBreak(BreakStmtNode* node) = 0;
    virtual void visitProgram(ProgramNode* node) = 0;
};

// Accept implementations
inline void LiteralNode::accept(ASTVisitor& visitor) {
    visitor.visitLiteral(this);
}

inline void StringLiteralNode::accept(ASTVisitor& visitor) {
    visitor.visitStringLiteral(this);
}

inline void UnaryNode::accept(ASTVisitor& visitor) {
    visitor.visitUnary(this);
}

inline void BinaryNode::accept(ASTVisitor& visitor) {
    visitor.visitBinary(this);
}

inline void VariableExprNode::accept(ASTVisitor& visitor) {
    visitor.visitVariable(this);
}

inline void VarargExprNode::accept(ASTVisitor& visitor) {
    visitor.visitVararg(this);
}

inline void CallExprNode::accept(ASTVisitor& visitor) {
    visitor.visitCall(this);
}

inline void TableConstructorNode::accept(ASTVisitor& visitor) {
    visitor.visitTableConstructor(this);
}

inline void IndexExprNode::accept(ASTVisitor& visitor) {
    visitor.visitIndexExpr(this);
}

inline void PrintStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitPrintStmt(this);
}

inline void ExprStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitExprStmt(this);
}

inline void AssignmentStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitAssignmentStmt(this);
}

inline void IndexAssignmentStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitIndexAssignmentStmt(this);
}

inline void LocalDeclStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitLocalDeclStmt(this);
}

inline void MultipleLocalDeclStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitMultipleLocalDeclStmt(this);
}

inline void MultipleAssignmentStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitMultipleAssignmentStmt(this);
}

inline void IfStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitIfStmt(this);
}

inline void WhileStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitWhileStmt(this);
}

inline void RepeatStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitRepeatStmt(this);
}

inline void ForStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitForStmt(this);
}

inline void ForInStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitForInStmt(this);
}

inline void FunctionDeclNode::accept(ASTVisitor& visitor) {
    visitor.visitFunctionDecl(this);
}

inline void ReturnStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitReturn(this);
}

inline void BreakStmtNode::accept(ASTVisitor& visitor) {
    visitor.visitBreak(this);
}

inline void ProgramNode::accept(ASTVisitor& visitor) {
    visitor.visitProgram(this);
}

#endif // LUA_AST_HPP
