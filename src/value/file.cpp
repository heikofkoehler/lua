#include "value/file.hpp"
#include <sstream>
#include <iostream>

FileObject::FileObject(const std::string& filename, const std::string& mode)
    : GCObject(GCObject::Type::FILE), filename_(filename), mode_(mode), isOpen_(false), isOwned_(true) {

    ownedStream_ = std::make_unique<std::fstream>();
    stream_ = ownedStream_.get();

    // Parse mode and open file
    if (mode == "r") {
        ownedStream_->open(filename, std::ios::in);
    } else if (mode == "w") {
        ownedStream_->open(filename, std::ios::out | std::ios::trunc);
    } else if (mode == "a") {
        ownedStream_->open(filename, std::ios::out | std::ios::app);
    } else if (mode == "r+") {
        ownedStream_->open(filename, std::ios::in | std::ios::out);
    } else if (mode == "w+") {
        ownedStream_->open(filename, std::ios::in | std::ios::out | std::ios::trunc);
    } else if (mode == "a+") {
        ownedStream_->open(filename, std::ios::in | std::ios::out | std::ios::app);
    }

    isOpen_ = ownedStream_->is_open();
}

FileObject::FileObject(std::iostream* stream, const std::string& name)
    : GCObject(GCObject::Type::FILE), filename_(name), mode_(""), stream_(stream), isOpen_(true), isOwned_(false) {
}

FileObject::~FileObject() {
    close();
}

bool FileObject::isOpen() const {
    return isOpen_ && stream_ && (isOwned_ ? ownedStream_->is_open() : true);
}

bool FileObject::isEOF() const {
    return stream_ && stream_->eof();
}

bool FileObject::write(const std::string& data) {
    if (!isOpen()) return false;

    *stream_ << data;
    stream_->flush();

    return stream_->good();
}

std::string FileObject::readAll() {
    if (!isOpen()) return "";

    std::stringstream buffer;
    buffer << stream_->rdbuf();
    return buffer.str();
}

std::string FileObject::readLine() {
    if (!isOpen()) return "";

    std::string line;
    std::getline(*stream_, line);
    return line;
}

void FileObject::close() {
    if (isOpen_) {
        if (isOwned_ && ownedStream_) {
            ownedStream_->close();
        }
        isOpen_ = false;
    }
}

bool FileObject::seek(const std::string& whence, int64_t offset, int64_t& newPosition) {
    if (!isOpen()) return false;
    
    std::ios_base::seekdir dir;
    if (whence == "set") dir = std::ios_base::beg;
    else if (whence == "cur") dir = std::ios_base::cur;
    else if (whence == "end") dir = std::ios_base::end;
    else return false;

    // Clear EOF flag before seeking
    stream_->clear();

    stream_->seekg(offset, dir);
    stream_->seekp(offset, dir);
    
    if (stream_->fail()) return false;
    
    newPosition = stream_->tellg();
    if (newPosition == -1) newPosition = stream_->tellp();
    
    return newPosition != -1;
}

bool FileObject::flush() {
    if (!isOpen()) return false;
    stream_->flush();
    return stream_->good();
}
