#include "value/file.hpp"
#include <sstream>

FileObject::FileObject(const std::string& filename, const std::string& mode)
    : filename_(filename), mode_(mode), isOpen_(false) {

    stream_ = std::make_unique<std::fstream>();

    // Parse mode and open file
    if (mode == "r") {
        stream_->open(filename, std::ios::in);
    } else if (mode == "w") {
        stream_->open(filename, std::ios::out | std::ios::trunc);
    } else if (mode == "a") {
        stream_->open(filename, std::ios::out | std::ios::app);
    } else if (mode == "r+") {
        stream_->open(filename, std::ios::in | std::ios::out);
    } else if (mode == "w+") {
        stream_->open(filename, std::ios::in | std::ios::out | std::ios::trunc);
    } else if (mode == "a+") {
        stream_->open(filename, std::ios::in | std::ios::out | std::ios::app);
    }

    isOpen_ = stream_->is_open();
}

FileObject::~FileObject() {
    close();
}

bool FileObject::isOpen() const {
    return isOpen_ && stream_ && stream_->is_open();
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
    if (isOpen_ && stream_) {
        stream_->close();
        isOpen_ = false;
    }
}
