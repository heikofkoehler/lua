#include "value/file.hpp"
#include <sstream>
#include <iostream>
#include <cerrno>

FileObject::FileObject(const std::string& filename, const std::string& mode)
    : GCObject(GCObject::Type::FILE), filename_(filename), mode_(mode),
      cfile_(nullptr), isOpen_(false), isPipe_(false), isStandard_(false) {
    cfile_ = std::fopen(filename.c_str(), mode.c_str());
    isOpen_ = (cfile_ != nullptr);
}

FileObject::FileObject(std::iostream* /*stream*/, const std::string& name)
    : GCObject(GCObject::Type::FILE), filename_(name), mode_(""),
      cfile_(nullptr), isOpen_(false), isPipe_(false), isStandard_(false) {
}

FileObject::FileObject(FILE* file, const std::string& mode, bool isPipe, bool isStandard)
    : GCObject(GCObject::Type::FILE),
      filename_(isPipe ? "pipe" : (isStandard ? "stdstream" : "file")),
      mode_(mode), cfile_(file), isOpen_(file != nullptr), isPipe_(isPipe), isStandard_(isStandard) {
}

FileObject::~FileObject() {
    close();
}

bool FileObject::write(const std::string& data) {
    if (!isOpen()) return false;
    if (data.empty()) return true;
    size_t written = std::fwrite(data.data(), 1, data.size(), cfile_);
    return written == data.size();
}

std::string FileObject::readAll() {
    if (!isOpen()) return "";
    std::string res;
    char buf[4096];
    while (size_t n = std::fread(buf, 1, sizeof(buf), cfile_)) {
        res.append(buf, n);
    }
    return res;
}

std::string FileObject::read(size_t bytes) {
    if (!isOpen() || bytes == 0) return "";
    std::string result(bytes, '\0');
    size_t n = std::fread(&result[0], 1, bytes, cfile_);
    result.resize(n);
    return result;
}

std::string FileObject::readLine(bool withNewline) {
    if (!isOpen()) return "";
    std::string line;
    int c;
    while ((c = std::fgetc(cfile_)) != EOF) {
        if (c == '\n') {
            if (withNewline) line += '\n';
            break;
        }
        line += static_cast<char>(c);
    }
    return line;
}

int FileObject::peek() {
    if (!isOpen()) return EOF;
    int c = std::fgetc(cfile_);
    if (c != EOF) std::ungetc(c, cfile_);
    return c;
}

int FileObject::getChar() {
    if (!isOpen()) return EOF;
    return std::fgetc(cfile_);
}

void FileObject::ungetChar(int c) {
    if (isOpen() && c != EOF) std::ungetc(c, cfile_);
}

int FileObject::close() {
    if (!isOpen_) return -2; // Already closed
    if (isStandard_) return -1; // Cannot close standard file

    int res = 0;
    if (cfile_) {
        if (devFullCookie_) {
            devFullCookie_->closing = true;
        }
        if (isPipe_) {
#ifndef _WIN32
            res = pclose(cfile_);
#else
            res = _pclose(cfile_);
#endif
        } else {
            res = std::fclose(cfile_);
        }
        cfile_ = nullptr;
        devFullCookie_ = nullptr;
    }
    isOpen_ = false;
    return res;
}

bool FileObject::seek(const std::string& whence, int64_t offset, int64_t& newPosition) {
    if (!isOpen()) return false;
    int w = SEEK_CUR;
    if (whence == "set") w = SEEK_SET;
    else if (whence == "cur") w = SEEK_CUR;
    else if (whence == "end") w = SEEK_END;
    else return false;

#if defined(__APPLE__) || defined(__linux__)
    if (fseeko(cfile_, static_cast<off_t>(offset), w) != 0) return false;
    newPosition = ftello(cfile_);
#elif defined(_MSC_VER)
    if (_fseeki64(cfile_, offset, w) != 0) return false;
    newPosition = _ftelli64(cfile_);
#else
    if (std::fseek(cfile_, static_cast<long>(offset), w) != 0) return false;
    newPosition = std::ftell(cfile_);
#endif
    return newPosition != -1;
}

bool FileObject::flush() {
    if (!isOpen()) return false;
    return std::fflush(cfile_) == 0;
}

bool FileObject::setvbuf(const std::string& mode, size_t size) {
    if (!isOpen()) return false;
    int m = _IOFBF;
    if (mode == "no") m = _IONBF;
    else if (mode == "full") m = _IOFBF;
    else if (mode == "line") m = _IOLBF;
    else return false;
    return ::setvbuf(cfile_, nullptr, m, size > 0 ? size : BUFSIZ) == 0;
}
