#ifndef LUA_LSP_ANALYSIS_HPP
#define LUA_LSP_ANALYSIS_HPP

#include "lsp/lsp_types.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>

namespace lsp {

struct StdLibDoc {
    std::string name;
    std::string signature;
    std::string description;
    CompletionItemKind kind = CompletionItemKind::Function;
};

struct ScopeSymbol {
    std::string name;
    std::string detail;
    std::string doc;
    SymbolKind kind;
    Range range;
    Range selectionRange;
    int lineDefined = 0;
};

struct Scope {
    int id = 0;
    int startLine = 0; // 0-indexed
    int endLine = 0;   // 0-indexed
    std::shared_ptr<Scope> parent = nullptr;
    std::vector<std::shared_ptr<Scope>> children;
    std::vector<ScopeSymbol> symbols;

    bool containsLine(int line) const {
        return line >= startLine && line <= endLine;
    }
};

class DocumentAnalyzer {
public:
    DocumentAnalyzer(std::string uri, std::string text, int version = 0);

    void update(std::string text, int version);

    const std::string& uri() const { return uri_; }
    const std::string& text() const { return text_; }
    int version() const { return version_; }

    const std::vector<Diagnostic>& getDiagnostics() const { return diagnostics_; }
    std::vector<DocumentSymbol> getDocumentSymbols() const;
    std::optional<Hover> getHover(Position pos) const;
    std::optional<Location> getDefinition(Position pos) const;
    std::vector<CompletionItem> getCompletions(Position pos) const;

    Position offsetToPosition(size_t offset) const;
    size_t positionToOffset(Position pos) const;
    std::string getLine(int line) const;
    int lineCount() const;

    std::string extractPrecedingComment(int line) const;

    static const std::map<std::string, StdLibDoc>& getStdLibDocs();
    static const std::map<std::string, std::string>& getKeywordDocs();

    friend class LspSymbolVisitor;

private:
    std::string uri_;
    std::string text_;
    int version_ = 0;
    std::vector<size_t> lineStarts_;
    std::vector<Diagnostic> diagnostics_;
    std::vector<DocumentSymbol> symbols_;
    std::shared_ptr<Scope> rootScope_;

    void rebuildLineStarts();
    void analyze();
    std::string extractIdentifierAt(Position pos, Range* outRange = nullptr) const;
    std::string extractExpressionAt(Position pos) const;
    std::string extractPrefixBefore(Position pos) const;

    const ScopeSymbol* findSymbolInScope(const std::string& name, Position pos) const;
    static void initStdLib();
};

} // namespace lsp

#endif // LUA_LSP_ANALYSIS_HPP
