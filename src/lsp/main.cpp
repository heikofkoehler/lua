#include "lsp/server.hpp"
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static void printUsage(const char* progname) {
    std::cout << "Usage: " << progname << " [options]\n\n"
              << "Language Server Protocol (LSP) server for Lua 5.5.\n"
              << "Communicates via JSON-RPC 2.0 over standard input and output (stdio).\n\n"
              << "Options:\n"
              << "  --stdio        Run as LSP server over standard I/O (default)\n"
              << "  -v, --version  Display version information\n"
              << "  -h, --help     Display this help message\n";
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--version") {
            std::cout << "Lua Language Server (LuaVM 5.5.0)\n";
            return 0;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--stdio") {
            // default mode
        } else {
            std::cerr << argv[0] << ": unrecognized option '" << arg << "'\n";
            printUsage(argv[0]);
            return 1;
        }
    }

#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);

    lsp::LanguageServer server;
    return server.run(std::cin, std::cout);
}
