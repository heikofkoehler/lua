#include "lsp/server.hpp"
#include <iostream>

namespace lsp {

int LanguageServer::run(std::istream& in, std::ostream& out) {
    while (in.good()) {
        int contentLength = -1;
        std::string line;
        bool headerFound = false;

        while (std::getline(in, line)) {
            headerFound = true;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) {
                // Blank line indicates end of header block
                break;
            }
            if (line.rfind("Content-Length:", 0) == 0 || line.rfind("content-length:", 0) == 0) {
                size_t colon = line.find(':');
                std::string lenStr = line.substr(colon + 1);
                while (!lenStr.empty() && lenStr[0] == ' ') lenStr.erase(0, 1);
                try {
                    contentLength = std::stoi(lenStr);
                } catch (...) {
                    contentLength = -1;
                }
            }
        }

        if (!headerFound || !in.good() || contentLength < 0) {
            break;
        }

        std::string payload(contentLength, '\0');
        in.read(&payload[0], contentLength);
        if (in.gcount() != contentLength) {
            break;
        }

        std::string parseError;
        JsonValue msg = JsonValue::parse(payload, &parseError);
        if (!msg.isObject()) {
            continue;
        }

        if (msg.has("id")) {
            JsonValue id = msg.get("id");
            std::string method = msg.get("method").asString();
            JsonValue params = msg.get("params");
            handleRequest(out, id, method, params);
        } else if (msg.has("method")) {
            std::string method = msg.get("method").asString();
            JsonValue params = msg.get("params");
            bool shouldExit = false;
            int exitCode = 0;
            handleNotification(out, method, params, shouldExit, exitCode);
            if (shouldExit) {
                return exitCode;
            }
        }
    }
    return 0;
}

void LanguageServer::handleRequest(std::ostream& out, const JsonValue& id, const std::string& method, const JsonValue& params) {
    if (method == "initialize") {
        JsonValue caps = JsonValue::object({
            {"textDocumentSync", 1}, // Full sync
            {"completionProvider", JsonValue::object({
                {"triggerCharacters", JsonValue::array({".", ":"})},
                {"resolveProvider", false}
            })},
            {"hoverProvider", true},
            {"definitionProvider", true},
            {"documentSymbolProvider", true}
        });
        JsonValue srvInfo = JsonValue::object({
            {"name", "lua-lsp"},
            {"version", "5.5.0"}
        });
        JsonValue result = JsonValue::object({
            {"capabilities", caps},
            {"serverInfo", srvInfo}
        });
        sendResponse(out, id, result);
    } else if (method == "shutdown") {
        isShutdown_ = true;
        sendResponse(out, id, JsonValue());
    } else if (method == "textDocument/documentSymbol") {
        std::string uri = params.get("textDocument").get("uri").asString();
        auto it = documents_.find(uri);
        if (it != documents_.end()) {
            auto symbols = it->second->getDocumentSymbols();
            JsonValue arr = JsonValue::array();
            for (const auto& sym : symbols) {
                arr.push_back(sym.toJson());
            }
            sendResponse(out, id, arr);
        } else {
            sendResponse(out, id, JsonValue::array());
        }
    } else if (method == "textDocument/hover") {
        std::string uri = params.get("textDocument").get("uri").asString();
        Position pos = Position::fromJson(params.get("position"));
        auto it = documents_.find(uri);
        if (it != documents_.end()) {
            auto hover = it->second->getHover(pos);
            if (hover) {
                sendResponse(out, id, hover->toJson());
            } else {
                sendResponse(out, id, JsonValue());
            }
        } else {
            sendResponse(out, id, JsonValue());
        }
    } else if (method == "textDocument/definition") {
        std::string uri = params.get("textDocument").get("uri").asString();
        Position pos = Position::fromJson(params.get("position"));
        auto it = documents_.find(uri);
        if (it != documents_.end()) {
            auto def = it->second->getDefinition(pos);
            if (def) {
                sendResponse(out, id, def->toJson());
            } else {
                sendResponse(out, id, JsonValue());
            }
        } else {
            sendResponse(out, id, JsonValue());
        }
    } else if (method == "textDocument/completion") {
        std::string uri = params.get("textDocument").get("uri").asString();
        Position pos = Position::fromJson(params.get("position"));
        auto it = documents_.find(uri);
        if (it != documents_.end()) {
            auto items = it->second->getCompletions(pos);
            JsonValue itemArr = JsonValue::array();
            for (const auto& item : items) {
                itemArr.push_back(item.toJson());
            }
            JsonValue listObj = JsonValue::object({
                {"isIncomplete", false},
                {"items", itemArr}
            });
            sendResponse(out, id, listObj);
        } else {
            JsonValue listObj = JsonValue::object({
                {"isIncomplete", false},
                {"items", JsonValue::array()}
            });
            sendResponse(out, id, listObj);
        }
    } else {
        sendError(out, id, -32601, "Method not found: " + method);
    }
}

void LanguageServer::handleNotification(std::ostream& out, const std::string& method, const JsonValue& params, bool& shouldExit, int& exitCode) {
    if (method == "initialized") {
        // Handshake confirmed
    } else if (method == "textDocument/didOpen") {
        JsonValue textDoc = params.get("textDocument");
        std::string uri = textDoc.get("uri").asString();
        std::string text = textDoc.get("text").asString();
        int version = textDoc.get("version").asInt();

        documents_[uri] = std::make_unique<DocumentAnalyzer>(uri, text, version);
        publishDiagnostics(out, *documents_[uri]);
    } else if (method == "textDocument/didChange") {
        std::string uri = params.get("textDocument").get("uri").asString();
        int version = params.get("textDocument").get("version").asInt();
        JsonValue contentChanges = params.get("contentChanges");

        if (contentChanges.isArray() && contentChanges.size() > 0) {
            std::string newText = contentChanges[contentChanges.size() - 1].get("text").asString();
            auto it = documents_.find(uri);
            if (it != documents_.end()) {
                it->second->update(newText, version);
                publishDiagnostics(out, *it->second);
            } else {
                documents_[uri] = std::make_unique<DocumentAnalyzer>(uri, newText, version);
                publishDiagnostics(out, *documents_[uri]);
            }
        }
    } else if (method == "textDocument/didClose") {
        std::string uri = params.get("textDocument").get("uri").asString();
        documents_.erase(uri);

        // Clear diagnostics
        JsonValue emptyParams = JsonValue::object({
            {"uri", uri},
            {"diagnostics", JsonValue::array()}
        });
        sendNotification(out, "textDocument/publishDiagnostics", emptyParams);
    } else if (method == "exit") {
        shouldExit = true;
        exitCode = isShutdown_ ? 0 : 1;
    }
}

void LanguageServer::sendResponse(std::ostream& out, const JsonValue& id, const JsonValue& result) {
    JsonValue resp = JsonValue::object({
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    });
    std::string body = resp.serialize();
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out.flush();
}

void LanguageServer::sendError(std::ostream& out, const JsonValue& id, int code, const std::string& message) {
    JsonValue errObj = JsonValue::object({
        {"code", code},
        {"message", message}
    });
    JsonValue resp = JsonValue::object({
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", errObj}
    });
    std::string body = resp.serialize();
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out.flush();
}

void LanguageServer::sendNotification(std::ostream& out, const std::string& method, const JsonValue& params) {
    JsonValue notif = JsonValue::object({
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", params}
    });
    std::string body = notif.serialize();
    out << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    out.flush();
}

void LanguageServer::publishDiagnostics(std::ostream& out, const DocumentAnalyzer& doc) {
    JsonValue diagArr = JsonValue::array();
    for (const auto& d : doc.getDiagnostics()) {
        diagArr.push_back(d.toJson());
    }
    JsonValue params = JsonValue::object({
        {"uri", doc.uri()},
        {"diagnostics", diagArr}
    });
    sendNotification(out, "textDocument/publishDiagnostics", params);
}

} // namespace lsp
