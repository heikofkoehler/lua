#include "compiler/chunk.hpp"
#include "value/function.hpp"
#include "value/string.hpp"
#include <iostream>
#include <iomanip>

Chunk::~Chunk() {
    // Clean up owned function objects
    for (auto* func : functions_) {
        delete func;
    }
    // Clean up owned string objects
    for (auto* str : strings_) {
        delete str;
    }
}

void Chunk::write(uint8_t byte, int line) {
    code_.push_back(byte);
    lines_.push_back(line);
}

size_t Chunk::addConstant(const Value& value) {
    constants_.push_back(value);
    return constants_.size() - 1;
}

size_t Chunk::addIdentifier(const std::string& name) {
    identifiers_.push_back(name);
    return identifiers_.size() - 1;
}

const std::string& Chunk::getIdentifier(size_t index) const {
    return identifiers_[index];
}

size_t Chunk::addFunction(FunctionObject* func) {
    functions_.push_back(func);
    return functions_.size() - 1;
}

FunctionObject* Chunk::getFunction(size_t index) const {
    if (index >= functions_.size()) {
        return nullptr;
    }
    return functions_[index];
}

size_t Chunk::addString(const std::string& str) {
    // Check if string already exists (interning)
    auto it = stringIndices_.find(str);
    if (it != stringIndices_.end()) {
        return it->second;
    }

    // String doesn't exist, create and intern it
    StringObject* strObj = new StringObject(str);
    size_t index = strings_.size();
    strings_.push_back(strObj);
    stringIndices_[str] = index;
    return index;
}

StringObject* Chunk::getString(size_t index) const {
    if (index >= strings_.size()) {
        return nullptr;
    }
    return strings_[index];
}

int Chunk::getLine(size_t offset) const {
    if (offset >= lines_.size()) {
        return -1;
    }
    return lines_[offset];
}

void Chunk::disassemble(const std::string& name) const {
    std::cout << "== " << name << " ==" << std::endl;

    for (size_t offset = 0; offset < code_.size();) {
        offset = disassembleInstruction(offset);
    }
}

size_t Chunk::disassembleInstruction(size_t offset) const {
    std::cout << std::setfill('0') << std::setw(4) << offset << " ";

    // Print line number
    if (offset > 0 && lines_[offset] == lines_[offset - 1]) {
        std::cout << "   | ";
    } else {
        std::cout << std::setw(4) << lines_[offset] << " ";
    }

    uint8_t instruction = code_[offset];
    OpCode op = static_cast<OpCode>(instruction);

    switch (op) {
        case OpCode::OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", offset);

        case OpCode::OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OpCode::OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OpCode::OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);

        case OpCode::OP_GET_GLOBAL:
            return byteInstruction("OP_GET_GLOBAL", offset);
        case OpCode::OP_SET_GLOBAL:
            return byteInstruction("OP_SET_GLOBAL", offset);
        case OpCode::OP_GET_LOCAL:
            return byteInstruction("OP_GET_LOCAL", offset);
        case OpCode::OP_SET_LOCAL:
            return byteInstruction("OP_SET_LOCAL", offset);

        case OpCode::OP_ADD:
            return simpleInstruction("OP_ADD", offset);
        case OpCode::OP_SUB:
            return simpleInstruction("OP_SUB", offset);
        case OpCode::OP_MUL:
            return simpleInstruction("OP_MUL", offset);
        case OpCode::OP_DIV:
            return simpleInstruction("OP_DIV", offset);
        case OpCode::OP_IDIV:
            return simpleInstruction("OP_IDIV", offset);
        case OpCode::OP_MOD:
            return simpleInstruction("OP_MOD", offset);
        case OpCode::OP_POW:
            return simpleInstruction("OP_POW", offset);
        case OpCode::OP_BAND:
            return simpleInstruction("OP_BAND", offset);
        case OpCode::OP_BOR:
            return simpleInstruction("OP_BOR", offset);
        case OpCode::OP_BXOR:
            return simpleInstruction("OP_BXOR", offset);
        case OpCode::OP_SHL:
            return simpleInstruction("OP_SHL", offset);
        case OpCode::OP_SHR:
            return simpleInstruction("OP_SHR", offset);
        case OpCode::OP_CONCAT:
            return simpleInstruction("OP_CONCAT", offset);

        case OpCode::OP_NEG:
            return simpleInstruction("OP_NEG", offset);
        case OpCode::OP_NOT:
            return simpleInstruction("OP_NOT", offset);
        case OpCode::OP_BNOT:
            return simpleInstruction("OP_BNOT", offset);

        case OpCode::OP_EQUAL:
            return simpleInstruction("OP_EQUAL", offset);
        case OpCode::OP_LESS:
            return simpleInstruction("OP_LESS", offset);
        case OpCode::OP_LESS_EQUAL:
            return simpleInstruction("OP_LESS_EQUAL", offset);
        case OpCode::OP_GREATER:
            return simpleInstruction("OP_GREATER", offset);
        case OpCode::OP_GREATER_EQUAL:
            return simpleInstruction("OP_GREATER_EQUAL", offset);

        case OpCode::OP_GET_UPVALUE:
            return byteInstruction("OP_GET_UPVALUE", offset);
        case OpCode::OP_SET_UPVALUE:
            return byteInstruction("OP_SET_UPVALUE", offset);
        case OpCode::OP_CLOSE_UPVALUE:
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);

        case OpCode::OP_PRINT:
            return simpleInstruction("OP_PRINT", offset);
        case OpCode::OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OpCode::OP_DUP:
            return simpleInstruction("OP_DUP", offset);
        case OpCode::OP_SWAP:
            return simpleInstruction("OP_SWAP", offset);
        case OpCode::OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, offset);
        case OpCode::OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, offset);
        case OpCode::OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, offset);

        case OpCode::OP_CLOSURE:
            return constantInstruction("OP_CLOSURE", offset);
        case OpCode::OP_CALL:
            return callInstruction("OP_CALL", offset);
        case OpCode::OP_CALL_MULTI:
            return callInstruction("OP_CALL_MULTI", offset);
        case OpCode::OP_RETURN_VALUE:
            return byteInstruction("OP_RETURN_VALUE", offset);

        case OpCode::OP_NEW_TABLE:
            return simpleInstruction("OP_NEW_TABLE", offset);
        case OpCode::OP_GET_TABLE:
            return simpleInstruction("OP_GET_TABLE", offset);
        case OpCode::OP_SET_TABLE:
            return simpleInstruction("OP_SET_TABLE", offset);
        case OpCode::OP_SET_TABLE_MULTI:
            return simpleInstruction("OP_SET_TABLE_MULTI", offset);

        case OpCode::OP_IO_OPEN:
            return simpleInstruction("OP_IO_OPEN", offset);
        case OpCode::OP_IO_WRITE:
            return simpleInstruction("OP_IO_WRITE", offset);
        case OpCode::OP_IO_READ:
            return simpleInstruction("OP_IO_READ", offset);
        case OpCode::OP_IO_CLOSE:
            return simpleInstruction("OP_IO_CLOSE", offset);

        case OpCode::OP_GET_VARARG:
            return byteInstruction("OP_GET_VARARG", offset);

        case OpCode::OP_YIELD:
            return yieldInstruction("OP_YIELD", offset);

        case OpCode::OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);

        default:
            std::cout << "Unknown opcode " << static_cast<int>(instruction) << std::endl;
            return offset + 1;
    }
}

size_t Chunk::simpleInstruction(const char* name, size_t offset) const {
    std::cout << name << std::endl;
    return offset + 1;
}

size_t Chunk::constantInstruction(const char* name, size_t offset) const {
    uint8_t constantIndex = code_[offset + 1];
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setw(4) << static_cast<int>(constantIndex)
              << " '";
    constants_[constantIndex].print(std::cout);
    std::cout << "'" << std::endl;
    return offset + 2;
}

size_t Chunk::jumpInstruction(const char* name, int sign, size_t offset) const {
    uint16_t jump = static_cast<uint16_t>(code_[offset + 1] | (code_[offset + 2] << 8));
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setw(4) << offset
              << " -> " << (offset + 3 + sign * jump) << std::endl;
    return offset + 3;
}

size_t Chunk::byteInstruction(const char* name, size_t offset) const {
    uint8_t slot = code_[offset + 1];
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setw(4) << static_cast<int>(slot);

    // If it's a global variable instruction, show the name
    if (std::string(name).find("GLOBAL") != std::string::npos && slot < identifiers_.size()) {
        std::cout << " '" << identifiers_[slot] << "'";
    }

    std::cout << std::endl;
    return offset + 2;
}

size_t Chunk::callInstruction(const char* name, size_t offset) const {
    uint8_t argCount = code_[offset + 1];
    uint8_t retCount = code_[offset + 2];
    std::cout << std::left << std::setw(16) << name
              << std::right << " args=" << static_cast<int>(argCount)
              << " returns=" << static_cast<int>(retCount) << std::endl;
    return offset + 3;
}

size_t Chunk::yieldInstruction(const char* name, size_t offset) const {
    uint8_t count = code_[offset + 1];
    uint8_t retCount = code_[offset + 2];
    std::cout << std::left << std::setw(16) << name
              << std::right << " count=" << static_cast<int>(count)
              << " returns=" << static_cast<int>(retCount) << std::endl;
    return offset + 3;
}

void Chunk::serialize(std::ostream& os) const {
    // Magic number
    os.write("LUA\x01", 4);
    
    // Bytecode
    uint32_t codeSize = static_cast<uint32_t>(code_.size());
    os.write(reinterpret_cast<const char*>(&codeSize), sizeof(codeSize));
    os.write(reinterpret_cast<const char*>(code_.data()), codeSize);
    
    // Lines
    uint32_t linesSize = static_cast<uint32_t>(lines_.size());
    os.write(reinterpret_cast<const char*>(&linesSize), sizeof(linesSize));
    os.write(reinterpret_cast<const char*>(lines_.data()), linesSize * sizeof(int));
    
    // Identifiers
    uint32_t idCount = static_cast<uint32_t>(identifiers_.size());
    os.write(reinterpret_cast<const char*>(&idCount), sizeof(idCount));
    for (const auto& id : identifiers_) {
        uint32_t len = static_cast<uint32_t>(id.length());
        os.write(reinterpret_cast<const char*>(&len), sizeof(len));
        os.write(id.c_str(), len);
    }
    
    // Constants (this will recursively serialize functions)
    uint32_t constCount = static_cast<uint32_t>(constants_.size());
    os.write(reinterpret_cast<const char*>(&constCount), sizeof(constCount));
    for (const auto& constant : constants_) {
        constant.serialize(os, this);
    }
}

std::unique_ptr<Chunk> Chunk::deserialize(std::istream& is) {
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, "LUA\x01", 4) != 0) {
        throw std::runtime_error("Invalid bytecode format");
    }
    
    auto chunk = std::make_unique<Chunk>();
    
    // Bytecode
    uint32_t codeSize;
    is.read(reinterpret_cast<char*>(&codeSize), sizeof(codeSize));
    chunk->code_.resize(codeSize);
    is.read(reinterpret_cast<char*>(chunk->code_.data()), codeSize);
    
    // Lines
    uint32_t linesSize;
    is.read(reinterpret_cast<char*>(&linesSize), sizeof(linesSize));
    chunk->lines_.resize(linesSize);
    is.read(reinterpret_cast<char*>(chunk->lines_.data()), linesSize * sizeof(int));
    
    // Identifiers
    uint32_t idCount;
    is.read(reinterpret_cast<char*>(&idCount), sizeof(idCount));
    for (uint32_t i = 0; i < idCount; i++) {
        uint32_t len;
        is.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string id(len, '\0');
        is.read(&id[0], len);
        chunk->identifiers_.push_back(id);
    }
    
    // Constants
    uint32_t constCount;
    is.read(reinterpret_cast<char*>(&constCount), sizeof(constCount));
    for (uint32_t i = 0; i < constCount; i++) {
        chunk->constants_.push_back(Value::deserialize(is, chunk.get()));
    }
    
    return chunk;
}

void FunctionObject::serialize(std::ostream& os) const {
    uint32_t nameLen = static_cast<uint32_t>(name_.length());
    os.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    os.write(name_.c_str(), nameLen);
    
    os.write(reinterpret_cast<const char*>(&arity_), sizeof(arity_));
    os.write(reinterpret_cast<const char*>(&upvalueCount_), sizeof(upvalueCount_));
    uint8_t varargs = hasVarargs_ ? 1 : 0;
    os.write(reinterpret_cast<const char*>(&varargs), sizeof(varargs));
    
    chunk_->serialize(os);
}

std::unique_ptr<FunctionObject> FunctionObject::deserialize(std::istream& is) {
    uint32_t nameLen;
    is.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
    std::string name(nameLen, '\0');
    is.read(&name[0], nameLen);
    
    int arity, upvalueCount;
    is.read(reinterpret_cast<char*>(&arity), sizeof(arity));
    is.read(reinterpret_cast<char*>(&upvalueCount), sizeof(upvalueCount));
    uint8_t varargs;
    is.read(reinterpret_cast<char*>(&varargs), sizeof(varargs));
    
    auto chunk = Chunk::deserialize(is);
    return std::make_unique<FunctionObject>(name, arity, std::move(chunk), upvalueCount, varargs != 0);
}
