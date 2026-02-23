// Garbage Collector Implementation
// Temporary file - will be merged into vm.cpp

#include "vm/vm.hpp"
#include <iostream>

// Add a GC object to the linked list
void VM::addObject(GCObject* object) {
    object->setNext(gcObjects_);
    gcObjects_ = object;
}

// Mark a single value (if it references a GC object)
void VM::markValue(const Value& value) {
    // Mark objects based on value type
    if (value.isRuntimeString()) {
        // Runtime strings from VM pool
        size_t idx = value.asStringIndex();
        if (idx < strings_.size()) {
            markObject(strings_[idx]);
        }
    } else if (value.isTable()) {
        size_t idx = value.asTableIndex();
        if (idx < tables_.size()) {
            markObject(tables_[idx]);
        }
    } else if (value.isClosure()) {
        size_t idx = value.asClosureIndex();
        if (idx < closures_.size()) {
            markObject(closures_[idx]);
        }
    } else if (value.isFile()) {
        size_t idx = value.asFileIndex();
        if (idx < files_.size()) {
            markObject(files_[idx]);
        }
    } else if (value.isSocket()) {
        size_t idx = value.asSocketIndex();
        if (idx < sockets_.size()) {
            markObject(sockets_[idx]);
        }
    } else if (value.isThread()) {
        size_t idx = value.asThreadIndex();
        if (idx < coroutines_.size()) {
            markObject(coroutines_[idx]);
        }
    }
}

void CoroutineObject::markReferences() {
    // CoroutineObject references are marked by VM::markObject(COROUTINE)
}

// Mark a GC object and its references
void VM::markObject(GCObject* object) {
    if (object == nullptr) return;
    if (object->isMarked()) return;  // Already marked

    object->mark();

    // Mark referenced objects based on type
    switch (object->type()) {
        case GCObject::Type::STRING:
            // Strings don't reference other objects
            break;

        case GCObject::Type::TABLE: {
            TableObject* table = static_cast<TableObject*>(object);
            // Mark metatable if present
            if (!table->getMetatable().isNil()) {
                markValue(table->getMetatable());
            }
            // Mark all keys and values
            for (const auto& pair : table->data()) {
                markValue(pair.first);   // Mark key
                markValue(pair.second);  // Mark value
            }
            break;
        }

        case GCObject::Type::CLOSURE: {
            ClosureObject* closure = static_cast<ClosureObject*>(object);
            // Mark all strings in the function's chunk
            if (closure->function() && closure->function()->chunk()) {
                Chunk* chunk = closure->function()->chunk();
                for (size_t i = 0; i < chunk->numStrings(); i++) {
                    markObject(chunk->getString(i));
                }
                // Mark constants (which might be runtimeStrings)
                for (const auto& constant : chunk->constants()) {
                    markValue(constant);
                }
            }
            // Mark all upvalues
            for (size_t i = 0; i < closure->upvalueCount(); i++) {
                size_t upvalueIdx = closure->getUpvalue(i);
                if (upvalueIdx != SIZE_MAX && upvalueIdx < upvalues_.size()) {
                    markObject(upvalues_[upvalueIdx]);
                }
            }
            break;
        }

        case GCObject::Type::UPVALUE: {
            UpvalueObject* upvalue = static_cast<UpvalueObject*>(object);
            // If closed, mark the closed value
            if (upvalue->isClosed()) {
                markValue(upvalue->closedValue());
            }
            break;
        }

        case GCObject::Type::FILE:
            // Files don't reference other objects
            break;

        case GCObject::Type::SOCKET:
            // Sockets don't reference other objects
            break;

        case GCObject::Type::COROUTINE: {
            CoroutineObject* co = static_cast<CoroutineObject*>(object);
            // Mark root chunk constants if this was the root
            if (co->rootChunk) {
                for (size_t i = 0; i < co->rootChunk->numStrings(); i++) {
                    markObject(co->rootChunk->getString(i));
                }
                for (const auto& constant : co->rootChunk->constants()) {
                    markValue(constant);
                }
            }
            // Mark values on stack
            for (const auto& val : co->stack) {
                markValue(val);
            }
            // Mark closures in frames
            for (const auto& frame : co->frames) {
                if (frame.closure) {
                    markObject(frame.closure);
                }
            }
            // Mark open upvalues
            for (auto* uv : co->openUpvalues) {
                markObject(uv);
            }
            // Mark caller
            if (co->caller) {
                markObject(co->caller);
            }
            break;
        }
    }
}

// Mark all root objects
void VM::markRoots() {
    // Mark global variables
    for (const auto& pair : globals_) {
        markValue(pair.second);
    }

    // Mark main and current coroutines
    if (mainCoroutine_) markObject(mainCoroutine_);
    if (currentCoroutine_) markObject(currentCoroutine_);
}

// Sweep unmarked objects
void VM::sweep() {
    GCObject** current = &gcObjects_;

    while (*current != nullptr) {
        if (!(*current)->isMarked()) {
            // Unmarked object - remove from list and free
            GCObject* unreached = *current;
            *current = unreached->next();
            freeObject(unreached);
        } else {
            // Marked object - unmark for next GC cycle and advance to next element
            (*current)->unmark();
            current = &((*current)->nextRef());
        }
    }
}

// Free a GC object
void VM::freeObject(GCObject* object) {
    switch (object->type()) {
        case GCObject::Type::STRING: {
            StringObject* str = static_cast<StringObject*>(object);
            
            // Remove from string pool if it's there
            size_t foundIndex = SIZE_MAX;
            for (size_t i = 0; i < strings_.size(); i++) {
                if (strings_[i] == str) {
                    strings_[i] = nullptr;  // Leave gap for now
                    foundIndex = i;
                    break;
                }
            }
            
            // Remove from string indices map only if it points to this string
            if (foundIndex != SIZE_MAX) {
                auto it = stringIndices_.find(str->hash());
                if (it != stringIndices_.end() && it->second == foundIndex) {
                    stringIndices_.erase(it);
                }
            }
            
            delete str;
            break;
        }

        case GCObject::Type::TABLE: {
            TableObject* table = static_cast<TableObject*>(object);
            // Remove from table pool
            for (size_t i = 0; i < tables_.size(); i++) {
                if (tables_[i] == table) {
                    tables_[i] = nullptr;
                    break;
                }
            }
            delete table;
            break;
        }

        case GCObject::Type::CLOSURE: {
            ClosureObject* closure = static_cast<ClosureObject*>(object);
            // Remove from closure pool
            for (size_t i = 0; i < closures_.size(); i++) {
                if (closures_[i] == closure) {
                    closures_[i] = nullptr;
                    break;
                }
            }
            delete closure;
            break;
        }

        case GCObject::Type::UPVALUE: {
            UpvalueObject* upvalue = static_cast<UpvalueObject*>(object);
            // Remove from upvalue pool
            for (size_t i = 0; i < upvalues_.size(); i++) {
                if (upvalues_[i] == upvalue) {
                    upvalues_[i] = nullptr;
                    break;
                }
            }
            delete upvalue;
            break;
        }

        case GCObject::Type::FILE: {
            FileObject* file = static_cast<FileObject*>(object);
            // Remove from file pool
            for (size_t i = 0; i < files_.size(); i++) {
                if (files_[i] == file) {
                    files_[i] = nullptr;
                    break;
                }
            }
            delete file;
            break;
        }

        case GCObject::Type::SOCKET: {
            SocketObject* socket = static_cast<SocketObject*>(object);
            // Remove from socket pool
            for (size_t i = 0; i < sockets_.size(); i++) {
                if (sockets_[i] == socket) {
                    sockets_[i] = nullptr;
                    break;
                }
            }
            delete socket;
            break;
        }

        case GCObject::Type::COROUTINE: {
            CoroutineObject* co = static_cast<CoroutineObject*>(object);
            // Remove from coroutines pool
            for (size_t i = 0; i < coroutines_.size(); i++) {
                if (coroutines_[i] == co) {
                    coroutines_[i] = nullptr;
                    break;
                }
            }
            delete co;
            break;
        }
    }
}

// Run garbage collection
void VM::collectGarbage() {
    if (!gcEnabled_) return;

#ifdef DEBUG_LOG_GC
    std::cout << "-- GC begin\n";
    size_t before = bytesAllocated_;
#endif

    // Mark phase
    markRoots();

    // Sweep phase
    sweep();

#ifdef DEBUG_LOG_GC
    std::cout << "-- GC end\n";
    std::cout << "   collected " << (before - bytesAllocated_)
              << " bytes (from " << before << " to " << bytesAllocated_ << ")\n";
#endif

    // Adjust next GC threshold
    nextGC_ = bytesAllocated_ * 2;
}
