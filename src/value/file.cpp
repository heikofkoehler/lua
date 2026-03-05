#include "value/file.hpp"
#include <sstream>
#include <iostream>

FileObject::FileObject(const std::string& filename, const std::string& mode)
    : GCObject(GCObject::Type::FILE), filename_(filename), mode_(mode), isOpen_(false), isOwned_(true) {

    ownedStream_ = std::make_unique<std::fstream>();
    stream_ = ownedStream_.get();

    std::ios_base::openmode openmode = (std::ios_base::openmode)0;
    
    if (mode.find('b') != std::string::npos) {
        openmode |= std::ios::binary;
    }

    if (mode.find('r') != std::string::npos) {
        openmode |= std::ios::in;
        if (mode.find('+') != std::string::npos) openmode |= std::ios::out;
    } else if (mode.find('w') != std::string::npos) {
        openmode |= std::ios::out | std::ios::trunc;
        if (mode.find('+') != std::string::npos) openmode |= std::ios::in;
    } else if (mode.find('a') != std::string::npos) {
        openmode |= std::ios::out | std::ios::app;
        if (mode.find('+') != std::string::npos) openmode |= std::ios::in;
    }

    if (openmode != 0) {
        ownedStream_->open(filename, openmode);
    }

    isOpen_ = ownedStream_->is_open();
}

FileObject::FileObject(std::iostream* stream, const std::string& name)
    : GCObject(GCObject::Type::FILE), filename_(name), mode_(""), stream_(stream), isOpen_(true), isOwned_(false) {
}

FileObject::FileObject(void* pipe, const std::string& mode)
    : GCObject(GCObject::Type::FILE), filename_("pipe"), mode_(mode), isOpen_(true), isOwned_(false), isPipe_(true), pipe_(pipe) {
}

FileObject::~FileObject() {
    close();
}

bool FileObject::isOpen() const {
    if (isPipe_) return isOpen_ && pipe_ != nullptr;
    return isOpen_ && stream_ && (isOwned_ ? ownedStream_->is_open() : true);
}

bool FileObject::isEOF() const {
    return stream_ && stream_->eof();
}

bool FileObject::write(const std::string& data) {
    if (!isOpen()) return false;

    if (isPipe_) {
        size_t written = fwrite(data.data(), 1, data.length(), (FILE*)pipe_);
        return written == data.length();
    }

    *stream_ << data;
    stream_->flush();

    return stream_->good();
}

std::string FileObject::readAll() {
    if (!isOpen()) return "";

    if (isPipe_) {
        std::string res;
        char buf[4096];
        while (size_t n = fread(buf, 1, sizeof(buf), (FILE*)pipe_)) {
            res.append(buf, n);
        }
        return res;
    }

    std::stringstream buffer;
    buffer << stream_->rdbuf();
    return buffer.str();
}

std::string FileObject::readLine() {
    if (!isOpen()) return "";

    if (isPipe_) {
        std::string res;
        char buf[1024];
        if (fgets(buf, sizeof(buf), (FILE*)pipe_)) {
            res = buf;
            if (!res.empty() && res.back() == '\n') res.pop_back();
        }
        return res;
    }

    std::string line;
    std::getline(*stream_, line);
    return line;
}

void FileObject::close() {
    if (isOpen_) {
        if (isPipe_) {
#ifndef _WIN32
            if (pipe_) pclose((FILE*)pipe_);
#else
            if (pipe_) _pclose((FILE*)pipe_);
#endif
            pipe_ = nullptr;
        } else if (isOwned_ && ownedStream_) {
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
