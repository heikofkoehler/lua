#ifndef LUA_LSP_SERVER_HPP
#define LUA_LSP_SERVER_HPP

#include "lsp/analysis.hpp"
#include <iostream>
#include <unordered_map>
#include <memory>

namespace lsp {

class LanguageServer {
public:
    LanguageServer() = default;

    int run(std::istream& in, std::ostream& out);

private:
    std::unordered_map<std::string, std::unique_ptr<DocumentAnalyzer>> documents_;
    bool isShutdown_ = false;

    void handleRequest(std::ostream& out, const JsonValue& id, const std::string& method, const JsonValue& params);
    void handleNotification(std::ostream& out, const std::string& method, const JsonValue& params, bool& shouldExit, int& exitCode);

    void sendResponse(std::ostream& out, const JsonValue& id, const JsonValue& result);
    void sendError(std::ostream& out, const JsonValue& id, int code, const std::string& message);
    void sendNotification(std::ostream& out, const std::string& method, const JsonValue& params);

    void publishDiagnostics(std::ostream& out, const DocumentAnalyzer& doc);
};

} // namespace lsp

#endif // LUA_LSP_SERVER_HPP
