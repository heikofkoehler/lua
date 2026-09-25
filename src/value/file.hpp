#ifndef LUA_FILE_HPP
#define LUA_FILE_HPP

#include "vm/gc.hpp"
#include "common/common.hpp"
#include "value/value.hpp"
#include <cstdio>
#include <string>
#include <iostream>

struct DevFullCookie {
    bool closing = false;
};

typedef struct lua_State lua_State;
using lua_CFunction = int (*)(lua_State* L);

#ifndef LUAL_STREAM_DEFINED
#define LUAL_STREAM_DEFINED
typedef struct luaL_Stream {
    FILE* f;
    lua_CFunction closef;
} luaL_Stream;
#endif

// FileObject: Represents an open file handle wrapping C FILE*
class FileObject : public GCObject {
public:
    // Open file from path with mode ("r", "w", "a", "r+", "w+", "a+", "rb", "wb", "ab", "r+b", "w+b", "a+b")
    FileObject(const std::string& filename, const std::string& mode);
    
    // Wrap existing stream (for compatibility)
    FileObject(std::iostream* stream, const std::string& name);

    // Open pipe or wrap stream (stdin, stdout, stderr)
    FileObject(FILE* file, const std::string& mode, bool isPipe = false, bool isStandard = false);
    
    ~FileObject();

    FileObject(const FileObject&) = delete;
    FileObject& operator=(const FileObject&) = delete;
    FileObject(FileObject&&) = default;
    FileObject& operator=(FileObject&&) = default;

    bool isOpen() const { return isOpen_ && cfile_ != nullptr; }
    bool isEOF() const { return cfile_ ? feof(cfile_) : true; }
    bool isStandard() const { return isStandard_; }
    bool isPipe() const { return isPipe_; }
    FILE* cfile() const { return cfile_; }

    // Write string to file
    bool write(const std::string& data);

    // Read entire file contents
    std::string readAll();

    // Read specific number of bytes
    std::string read(size_t bytes);

    // Read one line
    std::string readLine(bool withNewline = false);

    // Peek next character
    int peek();

    // Read next character
    int getChar();

    // Unget character
    void ungetChar(int c);

    // Close file: returns 0 on success, -1 on standard file, -2 on already closed, or exit code for pipe
    int close();

    // Seek in file
    bool seek(const std::string& whence, int64_t offset, int64_t& newPosition);

    // Flush file
    bool flush();

    // Set buffer
    bool setvbuf(const std::string& mode, size_t size);

    // Get filename for debugging
    const std::string& filename() const { return filename_; }

    DevFullCookie* devFullCookie() const { return devFullCookie_; }
    void setDevFullCookie(DevFullCookie* cookie) { devFullCookie_ = cookie; }

    luaL_Stream* stream() {
        stream_.f = isOpen() ? cfile_ : nullptr;
        stream_.closef = isOpen() ? &cCloseHelper : nullptr;
        return &stream_;
    }
    static int cCloseHelper(lua_State* L);

    // GC interface: files don't reference other objects
    void markReferences() override {}

    size_t size() const override {
        return sizeof(FileObject) + filename_.capacity() + mode_.capacity();
    }

private:
    std::string filename_;
    std::string mode_;
    FILE* cfile_ = nullptr;
    DevFullCookie* devFullCookie_ = nullptr;
    luaL_Stream stream_{nullptr, nullptr};
    bool isOpen_ = false;
    bool isPipe_ = false;
    bool isStandard_ = false;
};

#endif // LUA_FILE_HPP
