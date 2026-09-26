#ifndef LUA_LSP_TYPES_HPP
#define LUA_LSP_TYPES_HPP

#include "lsp/json.hpp"
#include <string>
#include <vector>

namespace lsp {

struct Position {
    int line = 0;       // 0-indexed
    int character = 0;  // 0-indexed character offset

    JsonValue toJson() const {
        return JsonValue::object({
            {"line", line},
            {"character", character}
        });
    }

    static Position fromJson(const JsonValue& j) {
        Position p;
        if (j.isObject()) {
            p.line = j.get("line").asInt();
            p.character = j.get("character").asInt();
        }
        return p;
    }
};

struct Range {
    Position start;
    Position end;

    JsonValue toJson() const {
        return JsonValue::object({
            {"start", start.toJson()},
            {"end", end.toJson()}
        });
    }

    static Range fromJson(const JsonValue& j) {
        Range r;
        if (j.isObject()) {
            r.start = Position::fromJson(j.get("start"));
            r.end = Position::fromJson(j.get("end"));
        }
        return r;
    }
};

struct Location {
    std::string uri;
    Range range;

    JsonValue toJson() const {
        return JsonValue::object({
            {"uri", uri},
            {"range", range.toJson()}
        });
    }
};

enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

struct Diagnostic {
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string source = "lua-lsp";

    JsonValue toJson() const {
        return JsonValue::object({
            {"range", range.toJson()},
            {"severity", static_cast<int>(severity)},
            {"message", message},
            {"source", source}
        });
    }
};

enum class SymbolKind {
    File = 1,
    Module = 2,
    Namespace = 3,
    Package = 4,
    Class = 5,
    Method = 6,
    Property = 7,
    Field = 8,
    Constructor = 9,
    Enum = 10,
    Interface = 11,
    Function = 12,
    Variable = 13,
    Constant = 14,
    String = 15,
    Number = 16,
    Boolean = 17,
    Array = 18,
    Object = 19,
    Key = 20,
    Null = 21,
    EnumMember = 22,
    Struct = 23,
    Event = 24,
    Operator = 25,
    TypeParameter = 26
};

struct DocumentSymbol {
    std::string name;
    std::string detail;
    SymbolKind kind;
    Range range;
    Range selectionRange;
    std::vector<DocumentSymbol> children;

    JsonValue toJson() const {
        JsonValue childrenArr = JsonValue::array();
        for (const auto& child : children) {
            childrenArr.push_back(child.toJson());
        }

        JsonValue obj = JsonValue::object({
            {"name", name},
            {"kind", static_cast<int>(kind)},
            {"range", range.toJson()},
            {"selectionRange", selectionRange.toJson()}
        });
        if (!detail.empty()) {
            obj["detail"] = detail;
        }
        if (!children.empty()) {
            obj["children"] = childrenArr;
        }
        return obj;
    }
};

enum class CompletionItemKind {
    Text = 1,
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Unit = 11,
    Value = 12,
    Enum = 13,
    Keyword = 14,
    Snippet = 15,
    Color = 16,
    File = 17,
    Reference = 18,
    Folder = 19,
    EnumMember = 20,
    Constant = 21,
    Struct = 22,
    Event = 23,
    Operator = 24,
    TypeParameter = 25
};

struct CompletionItem {
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Text;
    std::string detail;
    std::string documentation;
    std::string insertText;

    JsonValue toJson() const {
        JsonValue obj = JsonValue::object({
            {"label", label},
            {"kind", static_cast<int>(kind)}
        });
        if (!detail.empty()) obj["detail"] = detail;
        if (!documentation.empty()) {
            obj["documentation"] = JsonValue::object({
                {"kind", "markdown"},
                {"value", documentation}
            });
        }
        if (!insertText.empty()) obj["insertText"] = insertText;
        return obj;
    }
};

struct Hover {
    std::string contents; // Markdown content
    Range range;
    bool hasRange = false;

    JsonValue toJson() const {
        JsonValue obj = JsonValue::object({
            {"contents", JsonValue::object({
                {"kind", "markdown"},
                {"value", contents}
            })}
        });
        if (hasRange) {
            obj["range"] = range.toJson();
        }
        return obj;
    }
};

} // namespace lsp

#endif // LUA_LSP_TYPES_HPP
