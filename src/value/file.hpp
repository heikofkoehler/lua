#ifndef LUA_FILE_HPP
#define LUA_FILE_HPP

#include "vm/gc.hpp"
#include "common/common.hpp"
#include "value/value.hpp"
#include <fstream>
#include <string>
#include <memory>

// FileObject: Represents an open file handle
// Wraps C++ fstream for file I/O operations

class FileObject : public GCObject {
public:
    // Open file with mode ("r" = read, "w" = write, "a" = append)
    FileObject(const std::string& filename, const std::string& mode);
    
    // Wrap existing stream (e.g. std::cout, std::cin)
    FileObject(std::iostream* stream, const std::string& name);
    
    ~FileObject();

    // Disable copy, allow move
    FileObject(const FileObject&) = delete;
    FileObject& operator=(const FileObject&) = delete;
    FileObject(FileObject&&) = default;
    FileObject& operator=(FileObject&&) = default;

    // Check if file is open
    bool isOpen() const;

    // Check if EOF reached
    bool isEOF() const;

    // Write string to file
    bool write(const std::string& data);

    // Read entire file contents
    std::string readAll();

    // Read one line
    std::string readLine();

    // Close file
    void close();

    // Get filename for debugging
    const std::string& filename() const { return filename_; }

    // GC interface: files don't reference other objects
    void markReferences() override {}

private:
    std::string filename_;
    std::string mode_;
    std::unique_ptr<std::fstream> ownedStream_;
    std::iostream* stream_;
    bool isOpen_;
    bool isOwned_;
};

#endif // LUA_FILE_HPP
