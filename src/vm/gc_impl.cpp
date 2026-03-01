// Garbage Collector Implementation
#include "vm/vm.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "value/coroutine.hpp"
#include "value/userdata.hpp"
#include <iostream>

void VM::addObject(GCObject* object) {
    object->setNext(gcObjects_);
    gcObjects_ = object;
    bytesAllocated_ += object->size();
}

void VM::markValue(const Value& value) {
    if (value.isObj()) {
        grayObject(value.asObj());
    }
}

void VM::markObject(GCObject* object) {
    grayObject(object);
}

void VM::grayObject(GCObject* object) {
    if (object == nullptr || object->color() != GCObject::Color::WHITE) return;

    object->setColor(GCObject::Color::GRAY);
    grayStack_.push_back(object);
}

void VM::markRoots() {
    for (const auto& pair : globals_) markValue(pair.second);
    for (const auto& pair : registry_) markValue(pair.second);
    for (int i = 0; i < Value::NUM_TYPES; i++) markValue(typeMetatables_[i]);

    if (mainCoroutine_) markObject(mainCoroutine_);
    if (currentCoroutine_) markObject(currentCoroutine_);
}

void VM::processWeakTables() {
    bool changed;
    do {
        changed = false;
        for (TableObject* table : weakTables_) {
            Value modeVal = getMetamethod(Value::table(table), "__mode");
            bool weakKeys = false;
            bool weakValues = false;
            if (modeVal.isString()) {
                std::string mode = getStringValue(modeVal);
                if (mode.find('k') != std::string::npos) weakKeys = true;
                if (mode.find('v') != std::string::npos) weakValues = true;
            }

            if (weakKeys && !weakValues) {
                // Ephemeron logic: if key is marked but value is not, mark value
                for (const auto& pair : table->data()) {
                    bool keyMarked = true;
                    if (pair.first.isObj() && pair.first.asObj()->color() == GCObject::Color::WHITE) {
                        keyMarked = false;
                    }

                    if (keyMarked) {
                        if (pair.second.isObj() && pair.second.asObj()->color() == GCObject::Color::WHITE) {
                            markValue(pair.second);
                            changed = true;
                        }
                    }
                }
            }
        }
    } while (changed);
}

void VM::removeUnmarkedWeakEntries() {
    for (TableObject* table : weakTables_) {
        Value modeVal = getMetamethod(Value::table(table), "__mode");
        bool weakKeys = false;
        bool weakValues = false;
        if (modeVal.isString()) {
            std::string mode = getStringValue(modeVal);
            if (mode.find('k') != std::string::npos) weakKeys = true;
            if (mode.find('v') != std::string::npos) weakValues = true;
        }

        // Collect keys to remove
        std::vector<Value> toRemove;
        for (const auto& pair : table->data()) {
            bool remove = false;
            if (weakKeys && pair.first.isObj() && pair.first.asObj()->color() == GCObject::Color::WHITE) {
                remove = true;
            }
            if (weakValues && pair.second.isObj() && pair.second.asObj()->color() == GCObject::Color::WHITE) {
                remove = true;
            }
            if (remove) {
                toRemove.push_back(pair.first);
            }
        }

        // Remove entries
        for (const auto& key : toRemove) {
            table->set(key, Value::nil());
        }
    }
    weakTables_.clear();
}

void VM::sweep() {
    // 1. Sync runtimeStrings_ BEFORE freeing objects
    auto it = runtimeStrings_.begin();
    while (it != runtimeStrings_.end()) {
        if (it->second->color() == GCObject::Color::WHITE) {
            it = runtimeStrings_.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Sweep gcObjects_
    GCObject** current = &gcObjects_;
    while (*current != nullptr) {
        if ((*current)->color() == GCObject::Color::WHITE) {
            GCObject* unreached = *current;
            *current = unreached->next();
            freeObject(unreached);
        } else {
            (*current)->setColor(GCObject::Color::WHITE); // Reset for next cycle
            current = &((*current)->nextRef());
        }
    }
}

void VM::freeObject(GCObject* object) {
    bytesAllocated_ -= object->size();
    
    switch (object->type()) {
        case GCObject::Type::STRING: delete static_cast<StringObject*>(object); break;
        case GCObject::Type::TABLE: delete static_cast<TableObject*>(object); break;
        case GCObject::Type::CLOSURE: delete static_cast<ClosureObject*>(object); break;
        case GCObject::Type::UPVALUE: delete static_cast<UpvalueObject*>(object); break;
        case GCObject::Type::FILE: delete static_cast<FileObject*>(object); break;
        case GCObject::Type::SOCKET: delete static_cast<SocketObject*>(object); break;
        case GCObject::Type::USERDATA: delete static_cast<class UserdataObject*>(object); break;
        case GCObject::Type::COROUTINE: {
            CoroutineObject* co = static_cast<CoroutineObject*>(object);
            for (auto it = coroutines_.begin(); it != coroutines_.end(); ++it) {
                if (*it == co) {
                    coroutines_.erase(it);
                    break;
                }
            }
            delete co; 
            break;
        }
    }
}

// Write Barriers
void VM::writeBarrier(GCObject* object, const Value& value) {
    if (value.isObj()) {
        writeBarrier(object, value.asObj());
    }
}

void VM::writeBarrier(GCObject* object, GCObject* value) {
    // Forward barrier: if black object points to white object, mark white object gray
    if (gcState_ == GCState::MARK && 
        object->color() == GCObject::Color::BLACK && 
        value->color() == GCObject::Color::WHITE) {
        grayObject(value);
    }
}

void VM::writeBarrierBackward(GCObject* object, GCObject* /* value */) {
    // Backward barrier: if black object is modified, move it back to gray
    if (gcState_ == GCState::MARK && object->color() == GCObject::Color::BLACK) {
        object->setColor(GCObject::Color::GRAY);
        grayStack_.push_back(object);
    }
}

static void blackenObject(VM* vm, GCObject* object) {
    object->setColor(GCObject::Color::BLACK);

    switch (object->type()) {
        case GCObject::Type::STRING: break;
        case GCObject::Type::FILE: break;
        case GCObject::Type::SOCKET: break;

        case GCObject::Type::TABLE: {
            TableObject* table = static_cast<TableObject*>(object);
            vm->markValue(table->getMetatable());
            
            // For tables, we mark all keys and values
            for (const auto& pair : table->data()) {
                vm->markValue(pair.first);
                vm->markValue(pair.second);
            }
            break;
        }

        case GCObject::Type::CLOSURE: {
            ClosureObject* closure = static_cast<ClosureObject*>(object);
            for (size_t i = 0; i < closure->upvalueCount(); i++) {
                vm->markObject(closure->getUpvalueObj(i));
            }
            break;
        }

        case GCObject::Type::UPVALUE: {
            UpvalueObject* uv = static_cast<UpvalueObject*>(object);
            if (uv->isClosed()) {
                vm->markValue(uv->closedValue());
            }
            break;
        }

        case GCObject::Type::COROUTINE: {
            CoroutineObject* co = static_cast<CoroutineObject*>(object);
            for (const auto& val : co->stack) vm->markValue(val);
            for (const auto& val : co->yieldedValues) vm->markValue(val);
            for (const auto& frame : co->frames) {
                if (frame.closure) vm->markObject(frame.closure);
            }
            for (auto* uv : co->openUpvalues) vm->markObject(uv);
            if (co->caller) vm->markObject(co->caller);
            vm->markValue(co->hook);
            break;
        }

        case GCObject::Type::USERDATA: {
            UserdataObject* ud = static_cast<UserdataObject*>(object);
            vm->markValue(ud->metatable());
            break;
        }
    }
}

void VM::gcStep() {
    // printf("DEBUG GC STEP: state=%d gray=%zu allocated=%zu\n", (int)gcState_, grayStack_.size(), bytesAllocated_);
    switch (gcState_) {
        case GCState::PAUSE: {
            markRoots();
            gcState_ = GCState::MARK;
            break;
        }
        case GCState::MARK: {
            if (!grayStack_.empty()) {
                GCObject* object = grayStack_.back();
                grayStack_.pop_back();
                blackenObject(this, object);
            } else {
                gcState_ = GCState::ATOMIC;
            }
            break;
        }
        case GCState::ATOMIC: {
            // Re-mark roots to catch changes since MARK started
            markRoots();
            
            // Process remaining gray objects
            while (!grayStack_.empty()) {
                GCObject* object = grayStack_.back();
                grayStack_.pop_back();
                blackenObject(this, object);
            }

            // Handle weak tables
            weakTables_.clear();
            GCObject* obj = gcObjects_;
            while (obj) {
                if (obj->type() == GCObject::Type::TABLE && obj->color() == GCObject::Color::BLACK) {
                    TableObject* table = static_cast<TableObject*>(obj);
                    Value modeVal = getMetamethod(Value::table(table), "__mode");
                    if (modeVal.isString()) {
                        weakTables_.push_back(table);
                    }
                }
                obj = obj->next();
            }

            processWeakTables();
            removeUnmarkedWeakEntries();

            gcState_ = GCState::SWEEP;
            break;
        }
        case GCState::SWEEP: {
            sweep();
            gcState_ = GCState::PAUSE;
            
            // Recompute bytesAllocated_ and nextGC_
            bytesAllocated_ = 0;
            GCObject* obj = gcObjects_;
            while (obj) {
                bytesAllocated_ += obj->size();
                obj = obj->next();
            }
            nextGC_ = bytesAllocated_ * 2;
            if (nextGC_ < 1024 * 1024) nextGC_ = 1024 * 1024;
            // printf("DEBUG GC SWEEP: before=%zu after=%zu nextGC=%zu\n", before, bytesAllocated_, nextGC_);
            break;
        }
    }
}

void VM::collectGarbage() {
    if (!gcEnabled_) return;

    // Full collection
    if (gcState_ == GCState::PAUSE) {
        gcStep(); // PAUSE -> MARK
    }

    while (gcState_ == GCState::MARK) {
        gcStep();
    }

    if (gcState_ == GCState::ATOMIC) {
        gcStep(); // ATOMIC -> SWEEP
    }

    if (gcState_ == GCState::SWEEP) {
        gcStep(); // SWEEP -> PAUSE
    }
}

void VM::checkGC(size_t additionalBytes) {
    if (isHandlingError_) return;

    if (bytesAllocated_ + additionalBytes > nextGC_ || gcState_ != GCState::PAUSE) {
        // Perform steps of GC work
        // More aggressive: 4 steps per KB allocated
        size_t steps = 1 + (additionalBytes / 256); 
        for (size_t i = 0; i < steps; i++) {
            gcStep();
            if (gcState_ == GCState::PAUSE) break;
        }
    }
    
    if (bytesAllocated_ + additionalBytes > memoryLimit_) {
        collectGarbage();
        if (bytesAllocated_ + additionalBytes > memoryLimit_) {
            runtimeError("not enough memory");
        }
    }
}
