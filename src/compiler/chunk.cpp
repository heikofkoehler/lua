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

void Chunk::pop() {
    if (!code_.empty()) {
        code_.pop_back();
        lines_.pop_back();
    }
}

size_t Chunk::addConstant(const Value& value) {
    auto it = constantMap_.find(value.bits());
    if (it != constantMap_.end()) {
        return it->second;
    }
    size_t index = constants_.size();
    constants_.push_back(value);
    constantMap_[value.bits()] = index;
    return index;
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

size_t Chunk::addInt64(int64_t val) {
    for (size_t i = 0; i < int64s_.size(); ++i) {
        if (int64s_[i] == val) return i;
    }
    int64s_.push_back(val);
    return int64s_.size() - 1;
}

int64_t Chunk::getInt64(size_t index) const {
    if (index >= int64s_.size()) {
        return 0;
    }
    return int64s_[index];
}

int Chunk::getLine(size_t offset) const {
    if (hasRCode_ && !rlines_.empty()) {
        if (offset >= rlines_.size()) {
            return -1;
        }
        return rlines_[offset];
    }
    if (offset >= lines_.size()) {
        return -1;
    }
    return lines_[offset];
}

void Chunk::disassemble(const std::string& name) const {
    std::cout << "--- Chunk: " << name << " ---" << std::endl;
    if (!constants_.empty()) {
        std::cout << "Constants (" << constants_.size() << "):" << std::endl;
        for (size_t i = 0; i < constants_.size(); i++) {
            std::cout << "  [" << std::setw(3) << i << "] ";
            constants_[i].print(std::cout);
            std::cout << std::endl;
        }
    }

    if (!identifiers_.empty()) {
        std::cout << "Identifiers (" << identifiers_.size() << "):" << std::endl;
        for (size_t i = 0; i < identifiers_.size(); i++) {
            std::cout << "  [" << std::setw(3) << i << "] " << identifiers_[i] << std::endl;
        }
    }

    std::cout << "Bytecode (" << code_.size() << " bytes):" << std::endl;
    for (size_t offset = 0; offset < code_.size();) {
        offset = disassembleInstruction(offset);
    }

    // Recursively disassemble sub-functions
    for (auto* func : functions_) {
        func->disassemble();
    }
}

void FunctionObject::disassemble() const {
    std::cout << "----------------------------------------------------------------" << std::endl;
    std::cout << "Function:  " << name_ << std::endl;
    std::cout << "Params:    " << arity_ << (hasVarargs_ ? "+" : "") << std::endl;
    std::cout << "Upvalues:  " << upvalueCount_ << std::endl;
    
    if (!localVars_.empty()) {
        std::cout << "Locals (" << localVars_.size() << "):" << std::endl;
        for (const auto& l : localVars_) {
            std::cout << "  [" << std::right << std::setfill(' ') << std::setw(3) << l.slot << "] " 
                      << std::left << std::setw(16) << l.name 
                      << " " << std::right << std::setfill('0') << std::setw(4) << l.startPC 
                      << "-" << std::setw(4) << l.endPC << std::endl;
        }
        std::cout << std::setfill(' ');
    }

    chunk_->disassemble(name_);
}

size_t Chunk::disassembleInstruction(size_t offset) const {
    std::cout << std::right << std::setfill('0') << std::setw(4) << offset << " ";

    // Print line number
    if (offset > 0 && lines_[offset] == lines_[offset - 1]) {
        std::cout << "   | ";
    } else {
        std::cout << std::right << std::setfill(' ') << std::setw(4) << lines_[offset] << " ";
    }
    std::cout << std::setfill(' ');

    uint8_t instruction = code_[offset];
    OpCode op = static_cast<OpCode>(instruction);

    switch (op) {
        case OpCode::OP_CONSTANT:
            return constantInstruction("OP_CONSTANT", offset);
        case OpCode::OP_CONSTANT_LONG: {
            uint32_t index = code_[offset + 1];
            index |= (code_[offset + 2] << 8);
            index |= (code_[offset + 3] << 16);
            std::cout << std::left << std::setw(16) << "OP_CONSTANT_LONG" << " " 
                      << std::right << std::setfill(' ') << std::setw(4) << index << " '";
            std::cout << constants_[index];
            std::cout << "'" << std::endl;
            return offset + 4;
        }

        case OpCode::OP_NIL:
            return simpleInstruction("OP_NIL", offset);
        case OpCode::OP_TRUE:
            return simpleInstruction("OP_TRUE", offset);
        case OpCode::OP_FALSE:
            return simpleInstruction("OP_FALSE", offset);

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
        case OpCode::OP_LEN:
            return simpleInstruction("OP_LEN", offset);

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
        case OpCode::OP_GET_TABUP:
            return twoByteInstruction("OP_GET_TABUP", offset);
        case OpCode::OP_SET_TABUP:
            return twoByteInstruction("OP_SET_TABUP", offset);
        case OpCode::OP_GET_TABUP_LONG:
        case OpCode::OP_SET_TABUP_LONG: {
            uint8_t upIndex = code_[offset + 1];
            uint32_t constIndex = code_[offset + 2] | (code_[offset + 3] << 8) | (code_[offset + 4] << 16);
            std::cout << std::left << std::setw(16) << (op == OpCode::OP_GET_TABUP_LONG ? "OP_GET_TABUP_LONG" : "OP_SET_TABUP_LONG")
                      << " up:" << (int)upIndex << " const:" << constIndex << "\n";
            return offset + 5;
        }
        case OpCode::OP_CLOSE_UPVALUE:
            return simpleInstruction("OP_CLOSE_UPVALUE", offset);
        case OpCode::OP_CLOSE:
            return byteInstruction("OP_CLOSE", offset);
        case OpCode::OP_TBC: {
            uint8_t slot = code_[offset + 1];
            uint8_t nameIdx = code_[offset + 2];
            std::cout << std::left << std::setw(16) << "OP_TBC" << " slot:" << (int)slot << " name:" << (int)nameIdx << "\n";
            return offset + 3;
        }

        case OpCode::OP_POP:
            return simpleInstruction("OP_POP", offset);
        case OpCode::OP_DUP:
            return simpleInstruction("OP_DUP", offset);
        case OpCode::OP_SWAP:
            return simpleInstruction("OP_SWAP", offset);
        case OpCode::OP_ROTATE:
            return byteInstruction("OP_ROTATE", offset);
        case OpCode::OP_JUMP:
            return jumpInstruction("OP_JUMP", 1, offset);
        case OpCode::OP_JUMP_IF_FALSE:
            return jumpInstruction("OP_JUMP_IF_FALSE", 1, offset);
        case OpCode::OP_LOOP:
            return jumpInstruction("OP_LOOP", -1, offset);
        case OpCode::OP_FORPREP: {
            uint8_t base = code_[offset + 1];
            uint16_t jmp = code_[offset + 2] | (code_[offset + 3] << 8);
            std::cout << std::left << std::setw(16) << "OP_FORPREP" << " "
                      << std::right << std::setfill(' ') << std::setw(4) << static_cast<int>(base)
                      << " -> " << (offset + 4 + jmp) << std::endl;
            return offset + 4;
        }
        case OpCode::OP_FORLOOP: {
            uint8_t base = code_[offset + 1];
            uint16_t jmp = code_[offset + 2] | (code_[offset + 3] << 8);
            std::cout << std::left << std::setw(16) << "OP_FORLOOP" << " "
                      << std::right << std::setfill(' ') << std::setw(4) << static_cast<int>(base)
                      << " -> " << (offset + 4 - jmp) << std::endl;
            return offset + 4;
        }

        case OpCode::OP_CLOSURE: {
            uint8_t constant = code_[offset + 1];
            std::cout << std::left << std::setw(16) << "OP_CLOSURE" << " "
                      << std::right << std::setfill(' ') << std::setw(4) << static_cast<int>(constant) << " '";
            std::cout << constants_[constant] << "'" << std::endl;
            
            FunctionObject* func = getFunction(constants_[constant].asFunctionIndex());
            size_t currentOffset = offset + 2;
            for (int i = 0; i < func->upvalueCount(); i++) {
                uint8_t isLocal = code_[currentOffset++];
                uint8_t index = code_[currentOffset++];
                std::cout << std::right << std::setfill('0') << std::setw(4) << currentOffset - 2 << "    |                     "
                          << std::left << std::setfill(' ') << (isLocal ? "local" : "upvalue") << " " << static_cast<int>(index) << std::endl;
            }
            return currentOffset;
        }

        case OpCode::OP_CLOSURE_LONG: {
            uint32_t constant = code_[offset + 1];
            constant |= (code_[offset + 2] << 8);
            constant |= (code_[offset + 3] << 16);
            std::cout << std::left << std::setw(16) << "OP_CLOSURE_LONG" << " "
                      << std::right << std::setfill(' ') << std::setw(4) << constant << " '";
            std::cout << constants_[constant] << "'" << std::endl;
            
            FunctionObject* func = getFunction(constants_[constant].asFunctionIndex());
            size_t currentOffset = offset + 4;
            for (int i = 0; i < func->upvalueCount(); i++) {
                uint8_t isLocal = code_[currentOffset++];
                uint8_t index = code_[currentOffset++];
                std::cout << std::right << std::setfill('0') << std::setw(4) << currentOffset - 2 << "    |                     "
                          << std::left << std::setfill(' ') << (isLocal ? "local" : "upvalue") << " " << static_cast<int>(index) << std::endl;
            }
            return currentOffset;
        }

        case OpCode::OP_CALL:
            return callInstruction("OP_CALL", offset);
        case OpCode::OP_CALL_MULTI:
            return callInstruction("OP_CALL_MULTI", offset);
        case OpCode::OP_TAILCALL:
            return byteInstruction("OP_TAILCALL", offset);
        case OpCode::OP_TAILCALL_MULTI:
            return byteInstruction("OP_TAILCALL_MULTI", offset);
        case OpCode::OP_RETURN_VALUE:
            return byteInstruction("OP_RETURN_VALUE", offset);
        case OpCode::OP_RETURN_VALUE_MULTI:
            return byteInstruction("OP_RETURN_VALUE_MULTI", offset);

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
        case OpCode::OP_PACK_VARARG_TABLE:
            return simpleInstruction("OP_PACK_VARARG_TABLE", offset);
        case OpCode::OP_GET_VARARG_ITEM:
            return simpleInstruction("OP_GET_VARARG_ITEM", offset);
        case OpCode::OP_GET_VARARG_COUNT:
            return simpleInstruction("OP_GET_VARARG_COUNT", offset);
        case OpCode::OP_DEF_GLOBAL:
            return twoByteInstruction("OP_DEF_GLOBAL", offset);
        case OpCode::OP_DEF_GLOBAL_LONG: {
            uint8_t upIndex = code_[offset + 1];
            uint32_t constIndex = code_[offset + 2] | (code_[offset + 3] << 8) | (code_[offset + 4] << 16);
            std::cout << "OP_DEF_GLOBAL_LONG  upvalue: " << static_cast<int>(upIndex)
                      << "  const: " << constIndex << " '";
            if (constIndex < constants_.size()) {
                constants_[constIndex].print(std::cout);
            }
            std::cout << "'" << std::endl;
            return offset + 5;
        }
        case OpCode::OP_DEF_GLOBAL_TABLE:
            return simpleInstruction("OP_DEF_GLOBAL_TABLE", offset);

        case OpCode::OP_YIELD_MULTI:
            return yieldInstruction("OP_YIELD_MULTI", offset);

        case OpCode::OP_RETURN:
            return simpleInstruction("OP_RETURN", offset);

        default:
            std::cout << "Unknown opcode " << static_cast<int>(instruction) << std::endl;
            return offset + 1;
    }
}

size_t Chunk::instructionLength(size_t offset) const {
    if (offset >= code_.size()) return 0;
    OpCode op = static_cast<OpCode>(code_[offset]);
    switch (op) {
        case OpCode::OP_CONSTANT:
            return 2;
        case OpCode::OP_CONSTANT_LONG:
            return 4;
        case OpCode::OP_NIL:
        case OpCode::OP_TRUE:
        case OpCode::OP_FALSE:
            return 1;
        case OpCode::OP_GET_LOCAL:
        case OpCode::OP_SET_LOCAL:
        case OpCode::OP_GET_UPVALUE:
        case OpCode::OP_SET_UPVALUE:
        case OpCode::OP_CLOSE:
        case OpCode::OP_ROTATE:
        case OpCode::OP_TAILCALL:
        case OpCode::OP_TAILCALL_MULTI:
        case OpCode::OP_RETURN_VALUE:
        case OpCode::OP_RETURN_VALUE_MULTI:
        case OpCode::OP_GET_VARARG:
            return 2;
        case OpCode::OP_GET_TABUP:
        case OpCode::OP_SET_TABUP:
        case OpCode::OP_TBC:
        case OpCode::OP_JUMP:
        case OpCode::OP_JUMP_IF_FALSE:
        case OpCode::OP_LOOP:
        case OpCode::OP_CALL:
        case OpCode::OP_CALL_MULTI:
        case OpCode::OP_DEF_GLOBAL:
        case OpCode::OP_YIELD_MULTI:
            return 3;
        case OpCode::OP_FORPREP:
        case OpCode::OP_FORLOOP:
            return 4;
        case OpCode::OP_GET_TABUP_LONG:
        case OpCode::OP_SET_TABUP_LONG:
        case OpCode::OP_DEF_GLOBAL_LONG:
            return 5;
        case OpCode::OP_CLOSURE: {
            if (offset + 1 >= code_.size()) return 2;
            uint8_t c = code_[offset + 1];
            if (c < constants_.size() && constants_[c].isFunction()) {
                const FunctionObject* func = getFunction(constants_[c].asFunctionIndex());
                if (func) return 2 + func->upvalueCount() * 2;
            }
            return 2;
        }
        case OpCode::OP_CLOSURE_LONG: {
            if (offset + 3 >= code_.size()) return 4;
            uint32_t c = code_[offset + 1] | (code_[offset + 2] << 8) | (code_[offset + 3] << 16);
            if (c < constants_.size() && constants_[c].isFunction()) {
                const FunctionObject* func = getFunction(constants_[c].asFunctionIndex());
                if (func) return 4 + func->upvalueCount() * 2;
            }
            return 4;
        }
        default:
            return 1;
    }
}

size_t Chunk::simpleInstruction(const char* name, size_t offset) const {
    std::cout << name << std::endl;
    return offset + 1;
}

size_t Chunk::constantInstruction(const char* name, size_t offset) const {
    uint8_t constantIndex = code_[offset + 1];
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setfill(' ') << std::setw(4) << static_cast<int>(constantIndex)
              << " '";
    constants_[constantIndex].print(std::cout);
    std::cout << "'" << std::endl;
    return offset + 2;
}

size_t Chunk::jumpInstruction(const char* name, int sign, size_t offset) const {
    uint16_t jump = static_cast<uint16_t>(code_[offset + 1] | (code_[offset + 2] << 8));
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setfill(' ') << std::setw(4) << offset
              << " -> " << (offset + 3 + sign * jump) << std::endl;
    return offset + 3;
}

size_t Chunk::byteInstruction(const char* name, size_t offset) const {
    uint8_t slot = code_[offset + 1];
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setfill(' ') << std::setw(4) << static_cast<int>(slot);

    // If it's a global variable instruction, show the name
    if (std::string(name).find("GLOBAL") != std::string::npos && slot < identifiers_.size()) {
        std::cout << " '" << identifiers_[slot] << "'";
    }

    std::cout << std::endl;
    return offset + 2;
}

size_t Chunk::twoByteInstruction(const char* name, size_t offset) const {
    uint8_t byte1 = code_[offset + 1];
    uint8_t byte2 = code_[offset + 2];
    std::cout << std::left << std::setw(16) << name
              << std::right << std::setfill(' ') << std::setw(4) << static_cast<int>(byte1)
              << " " << std::setw(4) << static_cast<int>(byte2) << std::endl;
    return offset + 3;
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

void Chunk::serialize(std::ostream& os, const std::string& parentSource, bool strip) const {
    // Source Name: omit if strip or same as parentSource
    if (strip || (!parentSource.empty() && sourceName_ == parentSource)) {
        uint32_t nameLen = 0;
        os.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    } else {
        uint32_t nameLen = static_cast<uint32_t>(sourceName_.length());
        os.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
        os.write(sourceName_.c_str(), nameLen);
    }

    // Bytecode
    uint32_t codeSize = static_cast<uint32_t>(code_.size());
    os.write(reinterpret_cast<const char*>(&codeSize), sizeof(codeSize));
    os.write(reinterpret_cast<const char*>(code_.data()), codeSize);
    
    // Lines
    if (strip) {
        uint32_t linesSize = 0;
        os.write(reinterpret_cast<const char*>(&linesSize), sizeof(linesSize));
    } else {
        uint32_t linesSize = static_cast<uint32_t>(lines_.size());
        os.write(reinterpret_cast<const char*>(&linesSize), sizeof(linesSize));
        os.write(reinterpret_cast<const char*>(lines_.data()), linesSize * sizeof(int));
    }
    
    // Identifiers
    uint32_t idCount = static_cast<uint32_t>(identifiers_.size());
    os.write(reinterpret_cast<const char*>(&idCount), sizeof(idCount));
    for (const auto& id : identifiers_) {
        uint32_t len = static_cast<uint32_t>(id.length());
        os.write(reinterpret_cast<const char*>(&len), sizeof(len));
        os.write(id.c_str(), len);
    }
    
    // Constants (this will recursively serialize functions)
    // For register-compiled functions, use stackConstants_ (stack VM constant pool)
    // since the serialized bytecode is the stack VM bytecode (for string.dump/load).
    const std::vector<Value>& consts = stackConstants_.empty() ? constants_ : stackConstants_;
    uint32_t constCount = static_cast<uint32_t>(consts.size());
    os.write(reinterpret_cast<const char*>(&constCount), sizeof(constCount));
    for (const auto& constant : consts) {
        constant.serialize(os, this, sourceName_.empty() ? "=?" : sourceName_, strip);
    }
}

template<typename T>
static void readValue(std::istream& is, T& dest) {
    if (!is.read(reinterpret_cast<char*>(&dest), sizeof(T)) || is.gcount() < static_cast<std::streamsize>(sizeof(T))) {
        throw TruncatedError("bad binary format (truncated chunk)");
    }
}

static void readBytes(std::istream& is, char* dest, size_t size) {
    if (size == 0) return;
    if (!is.read(dest, size) || is.gcount() < static_cast<std::streamsize>(size)) {
        throw TruncatedError("bad binary format (truncated chunk)");
    }
}

void checkCount(uint32_t value, uint32_t max, const char* what) {
    if (value > max) {
        throw std::runtime_error(std::string("bad binary format (absurd ") + what + ")");
    }
}


// Bytecode verifier: validates hostile/corrupted bytecode at load time.
// Checks opcode validity, operand bounds, constant indices, jump targets,
// and upvalue indices. Throws std::runtime_error on any violation.
namespace {

// Operand sizes in bytes for each opcode, indexed by OpCode value.
// Derived from opcode.hpp comments and verified against run_impl.cpp.
const uint8_t kOperandSizes[] = {
    1,  // OP_CONSTANT [index: u8]
    3,  // OP_CONSTANT_LONG [index: u24]
    0,  // OP_NIL
    0,  // OP_TRUE
    0,  // OP_FALSE
    1,  // OP_GET_LOCAL [slot: u8]
    1,  // OP_SET_LOCAL [slot: u8]
    1,  // OP_GET_UPVALUE [index: u8]
    1,  // OP_SET_UPVALUE [index: u8]
    2,  // OP_GET_TABUP [upIndex: u8, constIndex: u8]
    2,  // OP_SET_TABUP [upIndex: u8, constIndex: u8]
    4,  // OP_GET_TABUP_LONG [upIndex: u8, constIndex: u24]
    4,  // OP_SET_TABUP_LONG [upIndex: u8, constIndex: u24]
    0,  // OP_CLOSE_UPVALUE
    2,  // OP_TBC [slot: u8, nameIdx: u8]
    1,  // OP_CLOSE [index: u8]
    0,  // OP_ADD
    0,  // OP_SUB
    0,  // OP_MUL
    0,  // OP_DIV
    0,  // OP_IDIV
    0,  // OP_MOD
    0,  // OP_POW
    0,  // OP_BAND
    0,  // OP_BOR
    0,  // OP_BXOR
    0,  // OP_SHL
    0,  // OP_SHR
    0,  // OP_CONCAT
    0,  // OP_NEG
    0,  // OP_NOT
    0,  // OP_BNOT
    0,  // OP_LEN
    0,  // OP_EQUAL
    0,  // OP_LESS
    0,  // OP_LESS_EQUAL
    0,  // OP_GREATER
    0,  // OP_GREATER_EQUAL
    0,  // OP_POP
    0,  // OP_DUP
    0,  // OP_SWAP
    1,  // OP_ROTATE [n: u8]
    2,  // OP_JUMP [offset: u16]
    2,  // OP_JUMP_IF_FALSE [offset: u16]
    2,  // OP_LOOP [offset: u16]
    0xFF,// OP_CLOSURE (variable: [index: u8] + 2 bytes per upvalue)
    0xFF,// OP_CLOSURE_LONG (variable: [index: u24] + 2 bytes per upvalue)
    2,  // OP_CALL [arg_count: u8, ret_count: u8]
    2,  // OP_CALL_MULTI [fixed_arg_count: u8, ret_count: u8]
    1,  // OP_TAILCALL [arg_count: u8]
    1,  // OP_TAILCALL_MULTI [fixed_arg_count: u8]
    1,  // OP_RETURN_VALUE [count: u8]
    1,  // OP_RETURN_VALUE_MULTI [fixed_count: u8]
    0,  // OP_NEW_TABLE
    0,  // OP_GET_TABLE
    0,  // OP_SET_TABLE
    0,  // OP_SET_TABLE_MULTI
    0,  // OP_IO_OPEN
    0,  // OP_IO_WRITE
    0,  // OP_IO_READ
    0,  // OP_IO_CLOSE
    1,  // OP_GET_VARARG [ret_count: u8]
    0,  // OP_PACK_VARARG_TABLE
    0,  // OP_GET_VARARG_ITEM
    0,  // OP_GET_VARARG_COUNT
    2,  // OP_DEF_GLOBAL [upvalue: u8, const_index: u8]
    4,  // OP_DEF_GLOBAL_LONG [upvalue: u8, const_index: u24]
    0,  // OP_DEF_GLOBAL_TABLE
    2,  // OP_YIELD_MULTI [fixed_args: u8, returns: u8]
    3,  // OP_FORPREP [base: u8, offset: u16]
    3,  // OP_FORLOOP [base: u8, offset: u16]
    0,  // OP_RETURN
};

uint32_t readU24(const std::vector<uint8_t>& code, size_t pos) {
    return static_cast<uint32_t>(code[pos])
         | (static_cast<uint32_t>(code[pos + 1]) << 8)
         | (static_cast<uint32_t>(code[pos + 2]) << 16);
}

}  // namespace

void Chunk::verify(int upvalueCount) const {
    const auto& code = code_;
    const size_t n = code.size();
    const size_t numConsts = constants_.size();

    // Pass 1: decode instructions, check opcode/operand bounds, collect boundaries.
    std::vector<char> isInstrStart(n + 1, 0);
    isInstrStart[n] = 1;  // end-of-code is a valid jump target
    size_t pos = 0;
    while (pos < n) {
        uint8_t opByte = code[pos];
        if (opByte >= sizeof(kOperandSizes)) {
            throw std::runtime_error("bad binary format (invalid opcode "
                + std::to_string(opByte) + " at offset " + std::to_string(pos) + ")");
        }
        isInstrStart[pos] = 1;
        uint8_t opSize = kOperandSizes[opByte];
        OpCode op = static_cast<OpCode>(opByte);
        size_t o = pos + 1;  // operand start
        // OP_CLOSURE / OP_CLOSURE_LONG are variable-length: the constant index
        // is followed by 2 bytes (isLocal, index) per upvalue of the target
        // function. Resolve the target to compute the true instruction size.
        if (op == OpCode::OP_CLOSURE || op == OpCode::OP_CLOSURE_LONG) {
            bool isLong = (op == OpCode::OP_CLOSURE_LONG);

            size_t idxSize = isLong ? 3 : 1;
            if (o + idxSize > n)
                throw std::runtime_error("bad binary format (truncated operands at offset "
                    + std::to_string(pos) + ")");
            uint32_t cidx = isLong ? readU24(code, o) : code[o];
            if (cidx >= numConsts)
                throw std::runtime_error("bad binary format (constant index out of range)");
            const Value& cv = constants_[cidx];
            if (!cv.isFunctionObject())
                throw std::runtime_error("bad binary format (OP_CLOSURE target is not a function)");
            FunctionObject* target = getFunction(cv.asFunctionIndex());
            if (!target)
                throw std::runtime_error("bad binary format (OP_CLOSURE function index out of range)");
            size_t uvCount = static_cast<size_t>(target->upvalueCount());
            size_t total = 1 + idxSize + 2 * uvCount;
            if (pos + total > n)
                throw std::runtime_error("bad binary format (truncated upvalue descriptors at offset "
                    + std::to_string(pos) + ")");
            // Validate parent-upvalue references (isLocal == 0).
            for (size_t i = 0; i < uvCount; i++) {
                uint8_t isLocal = code[o + idxSize + 2 * i];
                uint8_t uvIdx = code[o + idxSize + 2 * i + 1];
                if (isLocal > 1)
                    throw std::runtime_error("bad binary format (invalid upvalue descriptor)");
                if (!isLocal && static_cast<int>(uvIdx) >= upvalueCount)
                    throw std::runtime_error("bad binary format (upvalue index out of range)");
            }
            isInstrStart[pos] = 1;
            pos += total;
            continue;
        }
        if (opSize != 0xFF && pos + 1 + opSize > n) {
            throw std::runtime_error("bad binary format (truncated operands at offset "
                + std::to_string(pos) + ")");
        }


        // Constant-index checks
        auto checkConstU8 = [&](size_t at) {
            if (code[at] >= numConsts)
                throw std::runtime_error("bad binary format (constant index out of range)");
        };
        auto checkConstU24 = [&](size_t at) {
            if (readU24(code, at) >= numConsts)
                throw std::runtime_error("bad binary format (constant index out of range)");
        };
        auto checkUvU8 = [&](size_t at) {
            if (static_cast<int>(code[at]) >= upvalueCount)
                throw std::runtime_error("bad binary format (upvalue index out of range)");
        };

        switch (op) {
            case OpCode::OP_CONSTANT: checkConstU8(o); break;
            case OpCode::OP_CONSTANT_LONG: checkConstU24(o); break;
            case OpCode::OP_GET_UPVALUE: case OpCode::OP_SET_UPVALUE: checkUvU8(o); break;
            case OpCode::OP_GET_TABUP: case OpCode::OP_SET_TABUP:
                checkUvU8(o); checkConstU8(o + 1); break;
            case OpCode::OP_GET_TABUP_LONG: case OpCode::OP_SET_TABUP_LONG:
                checkUvU8(o); checkConstU24(o + 1); break;
            case OpCode::OP_CLOSURE: checkConstU8(o); break;
            case OpCode::OP_CLOSURE_LONG: checkConstU24(o); break;
            case OpCode::OP_DEF_GLOBAL: checkUvU8(o); checkConstU8(o + 1); break;
            case OpCode::OP_DEF_GLOBAL_LONG: checkUvU8(o); checkConstU24(o + 1); break;
            default: break;
        }
        pos += 1 + opSize;
    }

    // Pass 2: jump targets must be in bounds and on instruction boundaries.
    pos = 0;
    while (pos < n) {
        OpCode op = static_cast<OpCode>(code[pos]);
        uint8_t opSize = kOperandSizes[static_cast<uint8_t>(op)];
        size_t after;
        if (op == OpCode::OP_CLOSURE || op == OpCode::OP_CLOSURE_LONG) {
            // Variable length: recompute from the target function's upvalue count
            // (already validated in pass 1).
            bool isLong = (op == OpCode::OP_CLOSURE_LONG);
            size_t idxSize = isLong ? 3 : 1;
            uint32_t cidx = isLong ? readU24(code, pos + 1) : code[pos + 1];
            FunctionObject* target = getFunction(constants_[cidx].asFunctionIndex());
            size_t uvCount = static_cast<size_t>(target->upvalueCount());
            after = pos + 1 + idxSize + 2 * uvCount;
        } else {
            after = pos + 1 + opSize;
        }
        if (op == OpCode::OP_JUMP || op == OpCode::OP_JUMP_IF_FALSE) {
            uint16_t offset = static_cast<uint16_t>(code[pos + 1])
                            | (static_cast<uint16_t>(code[pos + 2]) << 8);
            size_t target = after + offset;
            if (target > n || !isInstrStart[target])
                throw std::runtime_error("bad binary format (jump target out of range)");
        } else if (op == OpCode::OP_LOOP) {
            uint16_t offset = static_cast<uint16_t>(code[pos + 1])
                            | (static_cast<uint16_t>(code[pos + 2]) << 8);
            if (offset > after)
                throw std::runtime_error("bad binary format (loop target out of range)");
            size_t target = after - offset;
            if (!isInstrStart[target])
                throw std::runtime_error("bad binary format (loop target misaligned)");
        } else if (op == OpCode::OP_FORPREP) {
            // Forward jump (ip += offset)
            uint16_t offset = static_cast<uint16_t>(code[pos + 2])
                            | (static_cast<uint16_t>(code[pos + 3]) << 8);
            size_t target = after + offset;
            if (target > n || !isInstrStart[target])
                throw std::runtime_error("bad binary format (for-prep target out of range)");
        } else if (op == OpCode::OP_FORLOOP) {
            // Backward jump (ip -= offset), like OP_LOOP
            uint16_t offset = static_cast<uint16_t>(code[pos + 2])
                            | (static_cast<uint16_t>(code[pos + 3]) << 8);
            if (offset > after)
                throw std::runtime_error("bad binary format (for-loop target out of range)");
            size_t target = after - offset;
            if (!isInstrStart[target])
                throw std::runtime_error("bad binary format (for-loop target misaligned)");
        }
        pos = after;
    }
}

std::unique_ptr<Chunk> Chunk::deserialize(std::istream& is, const std::string& parentSource) {
    auto chunk = std::make_unique<Chunk>();
    
    // Source Name
    uint32_t nameLen = 0;
    readValue(is, nameLen);
    checkCount(nameLen, MAX_CHUNK_NAME_LEN, "source name length");
    if (nameLen == 0) {
        chunk->sourceName_ = !parentSource.empty() ? parentSource : "=?";
    } else {
        std::string sourceName(nameLen, '\0');
        readBytes(is, &sourceName[0], nameLen);
        chunk->sourceName_ = sourceName;
    }

    // Bytecode
    uint32_t codeSize = 0;
    readValue(is, codeSize);
    checkCount(codeSize, MAX_CODE_SIZE, "code size");
    chunk->code_.resize(codeSize);
    readBytes(is, reinterpret_cast<char*>(chunk->code_.data()), codeSize);
    
    // Lines
    uint32_t linesSize = 0;
    readValue(is, linesSize);
    checkCount(linesSize, MAX_LINES_SIZE, "line info size");
    chunk->lines_.resize(linesSize);
    readBytes(is, reinterpret_cast<char*>(chunk->lines_.data()), linesSize * sizeof(int));
    
    // Identifiers
    uint32_t idCount = 0;
    readValue(is, idCount);
    checkCount(idCount, MAX_ID_COUNT, "identifier count");
    for (uint32_t i = 0; i < idCount; i++) {
        uint32_t len = 0;
        readValue(is, len);
        checkCount(len, MAX_ID_LEN, "identifier length");
        std::string id(len, '\0');
        readBytes(is, &id[0], len);
        chunk->identifiers_.push_back(id);
    }
    
    // Constants
    uint32_t constCount = 0;
    readValue(is, constCount);
    checkCount(constCount, MAX_CONST_COUNT, "constant count");
    for (uint32_t i = 0; i < constCount; i++) {
        chunk->constants_.push_back(Value::deserialize(is, chunk.get(), chunk->sourceName_));
    }
    
    return chunk;
}

void FunctionObject::serialize(std::ostream& os, const std::string& parentSource, bool strip) const {
    if (parentSource.empty()) {
        // 1. Signature (4 bytes)
        os.write("\x1bLua", 4);
        // 2. Version (1 byte: 0x55)
        uint8_t version = 0x55;
        os.write(reinterpret_cast<const char*>(&version), 1);
        // 3. Format (1 byte: 0)
        uint8_t format = 0;
        os.write(reinterpret_cast<const char*>(&format), 1);
        // 4. LUAC_DATA (6 bytes: "\x19\x93\r\n\x1a\n")
        os.write("\x19\x93\r\n\x1a\n", 6);
        // 5. Size of int (1 byte)
        uint8_t intSize = sizeof(int); // 4
        os.write(reinterpret_cast<const char*>(&intSize), 1);
        // 6. LUAC_INT (4 bytes)
        int32_t luacInt = -0x5678;
        os.write(reinterpret_cast<const char*>(&luacInt), sizeof(luacInt));
        // 7. Size of instruction (1 byte: 4)
        uint8_t insnSize = 4;
        os.write(reinterpret_cast<const char*>(&insnSize), 1);
        // 8. LUAC_NUM (4 bytes: 0x12345678)
        uint32_t luacNum = 0x12345678;
        os.write(reinterpret_cast<const char*>(&luacNum), sizeof(luacNum));
        // 9. Size of lua_Integer (1 byte: 8)
        uint8_t lintSize = sizeof(int64_t); // 8
        os.write(reinterpret_cast<const char*>(&lintSize), 1);
        // 10. LUAC_LINT (8 bytes: -0x5678)
        int64_t luacLint = -0x5678;
        os.write(reinterpret_cast<const char*>(&luacLint), sizeof(luacLint));
        // 11. Size of lua_Number (1 byte: 8)
        uint8_t lnumSize = sizeof(double); // 8
        os.write(reinterpret_cast<const char*>(&lnumSize), 1);
        // 12. LUAC_LNUM (8 bytes: -370.5)
        double luacLnum = -370.5;
        os.write(reinterpret_cast<const char*>(&luacLnum), sizeof(luacLnum));
    }

    uint32_t nameLen = static_cast<uint32_t>(name_.length());
    os.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
    os.write(name_.c_str(), nameLen);
    
    os.write(reinterpret_cast<const char*>(&arity_), sizeof(arity_));
    os.write(reinterpret_cast<const char*>(&upvalueCount_), sizeof(upvalueCount_));
    uint8_t varargs = (hasVarargs_ ? 1 : 0) | (hasNamedVarargs_ ? 2 : 0) | (isVarargOptimized_ ? 4 : 0);
    os.write(reinterpret_cast<const char*>(&varargs), sizeof(varargs));
    if (hasNamedVarargs_) {
        int32_t slot = namedVarargSlot_;
        os.write(reinterpret_cast<const char*>(&slot), sizeof(slot));
    }
    os.write(reinterpret_cast<const char*>(&lineDefined_), sizeof(lineDefined_));
    os.write(reinterpret_cast<const char*>(&lastLineDefined_), sizeof(lastLineDefined_));
    
    chunk_->serialize(os, parentSource, strip);

    if (strip) {
        uint32_t localCount = 0;
        os.write(reinterpret_cast<const char*>(&localCount), sizeof(localCount));
        uint32_t uvNameCount = 0;
        os.write(reinterpret_cast<const char*>(&uvNameCount), sizeof(uvNameCount));
    } else {
        // Local variable info
        uint32_t localCount = static_cast<uint32_t>(localVars_.size());
        os.write(reinterpret_cast<const char*>(&localCount), sizeof(localCount));
        for (const auto& l : localVars_) {
            uint32_t lNameLen = static_cast<uint32_t>(l.name.length());
            os.write(reinterpret_cast<const char*>(&lNameLen), sizeof(lNameLen));
            os.write(l.name.c_str(), lNameLen);
            os.write(reinterpret_cast<const char*>(&l.startPC), sizeof(l.startPC));
            os.write(reinterpret_cast<const char*>(&l.endPC), sizeof(l.endPC));
            os.write(reinterpret_cast<const char*>(&l.slot), sizeof(l.slot));
        }

        // Upvalue names
        uint32_t uvNameCount = static_cast<uint32_t>(upvalueNames_.size());
        os.write(reinterpret_cast<const char*>(&uvNameCount), sizeof(uvNameCount));
        for (const auto& uName : upvalueNames_) {
            uint32_t uNameLen = static_cast<uint32_t>(uName.length());
            os.write(reinterpret_cast<const char*>(&uNameLen), sizeof(uNameLen));
            os.write(uName.c_str(), uNameLen);
        }
    }
}

std::unique_ptr<FunctionObject> FunctionObject::deserialize(std::istream& is, const std::string& parentSource) {
    if (parentSource.empty()) {
        char magic[4];
        readBytes(is, magic, 4);
        if (std::memcmp(magic, "\x1bLua", 4) != 0) {
            throw std::runtime_error("bad binary format (not a binary chunk)");
        }

        uint8_t version = 0;
        readValue(is, version);
        if (version != 0x55) throw std::runtime_error("bad binary format (version mismatch)");

        uint8_t format = 0;
        readValue(is, format);
        if (format != 0) throw std::runtime_error("bad binary format (format mismatch)");

        char data[6];
        readBytes(is, data, 6);
        if (std::memcmp(data, "\x19\x93\r\n\x1a\n", 6) != 0) throw std::runtime_error("bad binary format (corrupted header)");

        uint8_t intSize = 0;
        readValue(is, intSize);
        if (intSize != sizeof(int)) throw std::runtime_error("bad binary format (int size mismatch)");

        int32_t luacInt = 0;
        readValue(is, luacInt);
        if (luacInt != -0x5678) throw std::runtime_error("bad binary format (int check mismatch)");

        uint8_t insnSize = 0;
        readValue(is, insnSize);
        if (insnSize != 4) throw std::runtime_error("bad binary format (instruction size mismatch)");

        uint32_t luacNum = 0;
        readValue(is, luacNum);
        if (luacNum != 0x12345678) throw std::runtime_error("bad binary format (instruction check mismatch)");

        uint8_t lintSize = 0;
        readValue(is, lintSize);
        if (lintSize != sizeof(int64_t)) throw std::runtime_error("bad binary format (lua_Integer size mismatch)");

        int64_t luacLint = 0;
        readValue(is, luacLint);
        if (luacLint != -0x5678) throw std::runtime_error("bad binary format (lua_Integer check mismatch)");

        uint8_t lnumSize = 0;
        readValue(is, lnumSize);
        if (lnumSize != sizeof(double)) throw std::runtime_error("bad binary format (lua_Number size mismatch)");

        double luacLnum = 0;
        readValue(is, luacLnum);
    }

    uint32_t nameLen = 0;
    readValue(is, nameLen);
    checkCount(nameLen, MAX_CHUNK_NAME_LEN, "function name length");
    std::string name(nameLen, '\0');
    readBytes(is, &name[0], nameLen);
    
    int arity = 0, upvalueCount = 0;
    readValue(is, arity);
    readValue(is, upvalueCount);
    // Defensive: corrupted bytecode can encode negative or absurd arity /
    // upvalue counts, which would crash call setup or closure allocation.
    // Lua functions are limited to 255 parameters and 255 upvalues.
    if (arity < 0 || arity > 255 || upvalueCount < 0 || upvalueCount > 255) {
        throw std::runtime_error("bad binary format (invalid arity/upvalue count)");
    }
    uint8_t varargs = 0;
    readValue(is, varargs);
    bool hasVarargs = (varargs & 1) != 0;
    bool hasNamedVarargs = (varargs & 2) != 0;
    bool isVarargOptimized = (varargs & 4) != 0;
    int32_t namedVarargSlot = -1;
    if (hasNamedVarargs) {
        readValue(is, namedVarargSlot);
    }
    int lineDefined = 0, lastLineDefined = 0;
    readValue(is, lineDefined);
    readValue(is, lastLineDefined);
    
    auto chunk = Chunk::deserialize(is, parentSource);
    chunk->verify(upvalueCount);  // reject hostile/corrupted bytecode at load
    auto function = std::make_unique<FunctionObject>(name, arity, std::move(chunk), upvalueCount, hasVarargs);
    function->setLines(lineDefined, lastLineDefined);
    if (hasNamedVarargs) {
        function->setNamedVarargs(namedVarargSlot, isVarargOptimized);
    }

    // Local variable info
    uint32_t localCount = 0;
    readValue(is, localCount);
    checkCount(localCount, MAX_LOCAL_COUNT, "local count");
    for (uint32_t i = 0; i < localCount; i++) {
        uint32_t lNameLen = 0;
        readValue(is, lNameLen);
        checkCount(lNameLen, MAX_ID_LEN, "local name length");
        std::string lName(lNameLen, '\0');
        readBytes(is, &lName[0], lNameLen);
        size_t startPC = 0, endPC = 0;
        int slot = 0;
        readValue(is, startPC);
        readValue(is, endPC);
        readValue(is, slot);
        function->addLocalVar(lName, startPC, endPC, slot);
    }

    // Upvalue names
    uint32_t uvNameCount = 0;
    readValue(is, uvNameCount);
    checkCount(uvNameCount, MAX_LOCAL_COUNT, "upvalue name count");
    for (uint32_t i = 0; i < uvNameCount; i++) {
        uint32_t uNameLen = 0;
        readValue(is, uNameLen);
        checkCount(uNameLen, MAX_ID_LEN, "upvalue name length");
        std::string uName(uNameLen, '\0');
        readBytes(is, &uName[0], uNameLen);
        function->addUpvalueName(uName);
    }

    return function;
}

void copyStackBytecode(FunctionObject* rfunc, FunctionObject* sfunc) {
    if (!rfunc || !sfunc || !rfunc->chunk() || !sfunc->chunk()) return;
    rfunc->chunk()->code() = sfunc->chunk()->code();
    rfunc->chunk()->setLines(std::vector<int>(sfunc->chunk()->lines()));
    // Copy stack constants and string pool: stack bytecode references stack constant
    // pool indices, but the function's main constant pool holds register constants.
    // string.dump uses stackConstants() when dumping register-compiled functions.
    // We deep-copy the string pool so indices remain valid in the destination chunk.
    rfunc->chunk()->setStackConstants(sfunc->chunk()->constants());
    // Deep-copy string pool for stackConstants_ indices to be valid.
    // Stack constant string indices are offset by the destination's existing string count.
    auto& destStrings = rfunc->chunk()->mutableStrings();
    size_t stringOffset = destStrings.size();
    for (size_t i = 0; i < sfunc->chunk()->numStrings(); i++) {
        StringObject* srcStr = sfunc->chunk()->getString(i);
        if (srcStr) {
            destStrings.push_back(new StringObject(srcStr->chars(), srcStr->length()));
        }
    }
    // Adjust string indices in stackConstants_ by the offset
    auto& stackConsts = rfunc->chunk()->mutableStackConstants();
    for (auto& c : stackConsts) {
        if (c.isString() && !c.isRuntimeString()) {
            size_t oldIdx = c.asStringIndex();
            // Create new Value with offset index
            c = Value::string(oldIdx + stringOffset);
        }
    }
    // Recursively copy for nested functions
    size_t rcount = rfunc->chunk()->numFunctions();
    size_t scount = sfunc->chunk()->numFunctions();
    for (size_t i = 0; i < rcount && i < scount; i++) {
        copyStackBytecode(rfunc->chunk()->getFunction(i), sfunc->chunk()->getFunction(i));
    }
}
