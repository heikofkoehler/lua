#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
#include "compiler/lexer.hpp"
#include "compiler/parser.hpp"
#include "compiler/codegen.hpp"
#include "value/function.hpp"

#define LUAC_VERSION "Lua 5.5.0  Copyright (C) 1994-2024 Lua.org, PUC-Rio"

static void printUsage(const char* progname) {
    std::cerr << "usage: " << progname << " [options] [filenames]\n"
              << "Available options are:\n"
              << "  -l       list (disassemble)\n"
              << "  -o name  output to file 'name' (default is \"luac.out\")\n"
              << "  -p       parse only\n"
              << "  -s       strip debug information\n"
              << "  -v       show version information\n"
              << "  --       stop handling options\n"
              << "  -        stop handling options and process stdin\n";
}

// Read whole stream, skipping BOM and shebang on line 1
static std::string readSource(std::istream& in) {
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    size_t startPos = 0;
    // Skip UTF-8 BOM
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        startPos = 3;
    }

    // Skip shebang (#...) on first line
    if (startPos < content.size() && content[startPos] == '#') {
        size_t nextLine = content.find('\n', startPos);
        if (nextLine != std::string::npos) {
            startPos = nextLine + 1;
        } else {
            startPos = content.size();
        }
    }

    if (startPos > 0) {
        return content.substr(startPos);
    }
    return content;
}

// Compile a single file/stream to FunctionObject
static std::unique_ptr<FunctionObject> loadChunk(const std::string& filename, const char* progname) {
    bool isStdin = (filename == "-");
    std::string sourceName = isStdin ? "=stdin" : ("@" + filename);

    if (isStdin) {
        std::string source = readSource(std::cin);
        if (source.size() >= 4 && std::memcmp(source.data(), "\x1bLua", 4) == 0) {
            std::istringstream iss(source, std::ios::binary);
            try {
                return FunctionObject::deserialize(iss, "");
            } catch (const std::exception& e) {
                std::cerr << progname << ": cannot load bytecode from stdin: " << e.what() << "\n";
                return nullptr;
            }
        }

        try {
            Lexer lexer(source);
            lexer.setSourceName(sourceName);
            Parser parser(lexer);
            auto program = parser.parse();
            if (!program) return nullptr;

            CodeGenerator codegen;
            return codegen.generate(program.get(), sourceName);
        } catch (const std::exception& e) {
            std::cerr << progname << ": " << e.what() << "\n";
            return nullptr;
        }
    }

    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << progname << ": cannot open " << filename << ": No such file or directory\n";
        return nullptr;
    }

    // Peek signature
    char sig[4];
    file.read(sig, 4);
    bool isBytecode = (file.gcount() == 4 && std::memcmp(sig, "\x1bLua", 4) == 0);
    file.clear();
    file.seekg(0);

    if (isBytecode) {
        try {
            return FunctionObject::deserialize(file, "");
        } catch (const std::exception& e) {
            std::cerr << progname << ": cannot load bytecode from " << filename << ": " << e.what() << "\n";
            return nullptr;
        }
    }

    std::string source = readSource(file);
    try {
        Lexer lexer(source);
        lexer.setSourceName(sourceName);
        Parser parser(lexer);
        auto program = parser.parse();
        if (!program) return nullptr;

        CodeGenerator codegen;
        return codegen.generate(program.get(), sourceName);
    } catch (const std::exception& e) {
        std::cerr << progname << ": " << e.what() << "\n";
        return nullptr;
    }
}

// Combine multiple chunks into one master chunk (matching standard PUC Lua luac behavior)
static std::unique_ptr<FunctionObject> combineChunks(std::vector<std::unique_ptr<FunctionObject>>& chunks) {
    if (chunks.size() == 1) {
        return std::move(chunks[0]);
    }

    auto masterChunk = std::make_unique<Chunk>();
    for (auto& chunk : chunks) {
        size_t upvalueCount = chunk->upvalueCount();
        size_t funcIndex = masterChunk->addFunction(chunk.release());
        Value funcValue = Value::function(funcIndex);
        size_t constantIndex = masterChunk->addConstant(funcValue);

        if (constantIndex <= 255) {
            masterChunk->write(static_cast<uint8_t>(OpCode::OP_CLOSURE), 0);
            masterChunk->write(static_cast<uint8_t>(constantIndex), 0);
        } else {
            masterChunk->write(static_cast<uint8_t>(OpCode::OP_CLOSURE_LONG), 0);
            masterChunk->write(static_cast<uint8_t>(constantIndex & 0xFF), 0);
            masterChunk->write(static_cast<uint8_t>((constantIndex >> 8) & 0xFF), 0);
            masterChunk->write(static_cast<uint8_t>((constantIndex >> 16) & 0xFF), 0);
        }

        // Emit upvalue descriptors: inherit upvalues (e.g. _ENV at index 0) from master function
        for (size_t u = 0; u < upvalueCount; ++u) {
            masterChunk->write(0, 0); // isLocal = 0 (inherited from enclosing upvalue)
            masterChunk->write(static_cast<uint8_t>(u), 0); // index in enclosing function
        }

        // Call sub-chunk with 0 arguments and 0 expected returns (expectedRetCount = 1 means 0 results)
        masterChunk->write(static_cast<uint8_t>(OpCode::OP_CALL), 0);
        masterChunk->write(0, 0); // nargs = 0
        masterChunk->write(1, 0); // expectedRetCount = 1 (0 results kept)
    }
    masterChunk->write(static_cast<uint8_t>(OpCode::OP_RETURN), 0);

    auto master = std::make_unique<FunctionObject>("=(luac)", 0, std::move(masterChunk), 1, true);
    master->addUpvalueName("_ENV");
    return master;
}

int main(int argc, char** argv) {
    const char* progname = "luac";
    if (argc > 0 && argv[0] && argv[0][0]) {
        progname = argv[0];
    }

    bool list = false;
    bool parseOnly = false;
    bool strip = false;
    bool showVersion = false;
    std::string outputFile = "luac.out";
    bool outputSpecified = false;
    std::vector<std::string> inputFiles;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--") {
            // Stop parsing flags, remaining args are input files
            for (int j = i + 1; j < argc; ++j) {
                inputFiles.push_back(argv[j]);
            }
            break;
        } else if (arg == "-") {
            inputFiles.push_back("-");
        } else if (arg.rfind("-", 0) == 0 && arg.size() > 1) {
            for (size_t k = 1; k < arg.size(); ++k) {
                char opt = arg[k];
                if (opt == 'l') {
                    list = true;
                } else if (opt == 'p') {
                    parseOnly = true;
                } else if (opt == 's') {
                    strip = true;
                } else if (opt == 'v') {
                    showVersion = true;
                } else if (opt == 'o') {
                    if (k + 1 < arg.size()) {
                        outputFile = arg.substr(k + 1);
                        outputSpecified = true;
                        break;
                    } else if (i + 1 < argc) {
                        outputFile = argv[++i];
                        outputSpecified = true;
                        break;
                    } else {
                        std::cerr << progname << ": '-o' needs argument\n";
                        printUsage(progname);
                        return 1;
                    }
                } else {
                    std::cerr << progname << ": unrecognized option '" << arg << "'\n";
                    printUsage(progname);
                    return 1;
                }
            }
        } else {
            inputFiles.push_back(arg);
        }
    }

    if (showVersion) {
        std::cout << LUAC_VERSION << "\n";
        if (inputFiles.empty()) {
            return 0;
        }
    }

    if (inputFiles.empty()) {
        std::cerr << progname << ": no input files given\n";
        printUsage(progname);
        return 1;
    }

    std::vector<std::unique_ptr<FunctionObject>> compiledChunks;
    for (const auto& file : inputFiles) {
        auto func = loadChunk(file, progname);
        if (!func) {
            return 1;
        }
        compiledChunks.push_back(std::move(func));
    }

    auto master = combineChunks(compiledChunks);
    if (!master) {
        return 1;
    }

    if (list) {
        master->disassemble();
    }

    // Determine whether to write output:
    // In standard luac:
    // If -p is passed, never write output.
    // If -l is passed without explicit -o, do not produce luac.out unless -o was specified.
    bool shouldWrite = !parseOnly && (outputSpecified || !list);
    if (shouldWrite) {
        std::ofstream out(outputFile, std::ios::binary);
        if (!out.is_open()) {
            std::cerr << progname << ": cannot open " << outputFile << ": Permission denied or invalid path\n";
            return 1;
        }
        master->serialize(out, "", strip);
        if (!out.good()) {
            std::cerr << progname << ": error writing output file: " << outputFile << "\n";
            return 1;
        }
    }

    return 0;
}
