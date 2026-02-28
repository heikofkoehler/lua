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
}

void VM::markValue(const Value& value) {
    if (value.isObj()) {
        markObject(value.asObj());
    }
}

void CoroutineObject::markReferences() {
    // Already handled in VM::markObject
}

void VM::markObject(GCObject* object) {
    if (object == nullptr || object->isMarked()) return;

    object->mark();

    switch (object->type()) {
        case GCObject::Type::STRING: break;
        case GCObject::Type::FILE: break;
        case GCObject::Type::SOCKET: break;

        case GCObject::Type::TABLE: {
            TableObject* table = static_cast<TableObject*>(object);
            markValue(table->getMetatable());
            
            Value modeVal = getMetamethod(Value::table(table), "__mode");
            bool weakKeys = false;
            bool weakValues = false;
            if (modeVal.isString()) {
                std::string mode = getStringValue(modeVal);
                if (mode.find('k') != std::string::npos) weakKeys = true;
                if (mode.find('v') != std::string::npos) weakValues = true;
            }

            if (weakKeys || weakValues) {
                weakTables_.push_back(table);
            }

            for (const auto& pair : table->data()) {
                if (!weakKeys) {
                    markValue(pair.first);
                }
                // Ephemeron tables: if keys are weak, values are only marked if the key is marked.
                // Since we don't know if the key is marked yet, we delay marking the value.
                if (!weakValues && !weakKeys) {
                    markValue(pair.second);
                }
            }
            break;
        }

        case GCObject::Type::CLOSURE: {
            ClosureObject* closure = static_cast<ClosureObject*>(object);
            // Closure keeps its function alive, which is not a GCObject but owned by VM
            for (size_t i = 0; i < closure->upvalueCount(); i++) {
                markObject(closure->getUpvalueObj(i));
            }
            break;
        }

        case GCObject::Type::UPVALUE: {
            static_cast<UpvalueObject*>(object)->markReferences();
            break;
        }

        case GCObject::Type::COROUTINE: {
            CoroutineObject* co = static_cast<CoroutineObject*>(object);
            for (const auto& val : co->stack) markValue(val);
            for (const auto& val : co->yieldedValues) markValue(val);
            for (const auto& frame : co->frames) {
                if (frame.closure) markObject(frame.closure);
            }
            for (auto* uv : co->openUpvalues) markObject(uv);
            if (co->caller) markObject(co->caller);
            break;
        }

        case GCObject::Type::USERDATA: {
            // Userdata marks its metatable
            static_cast<class UserdataObject*>(object)->markReferences();
            break;
        }
    }
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
                    if (pair.first.isObj() && !pair.first.asObj()->isMarked()) {
                        keyMarked = false;
                    }

                    if (keyMarked) {
                        if (pair.second.isObj() && !pair.second.asObj()->isMarked()) {
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
            if (weakKeys && pair.first.isObj() && !pair.first.asObj()->isMarked()) {
                remove = true;
            }
            if (weakValues && pair.second.isObj() && !pair.second.asObj()->isMarked()) {
                remove = true;
            }
            if (remove) {
                toRemove.push_back(pair.first);
            }
        }

        // Remove entries
        for (const auto& key : toRemove) {
            table->set(key, Value::nil()); // Assuming setting to nil removes it or we use an erase method
            // Wait, does TableObject have an erase or does set to nil just mark it nil?
            // Usually set to nil is how Lua removes keys, but if the map retains the nil value, 
            // it's not truly removed from the map.
        }
    }
    weakTables_.clear();
}

void VM::sweep() {
    GCObject** current = &gcObjects_;

    while (*current != nullptr) {
        if (!(*current)->isMarked()) {
            GCObject* unreached = *current;
            *current = unreached->next();
            freeObject(unreached);
        } else {
            (*current)->unmark();
            current = &((*current)->nextRef());
        }
    }
    
    // Also clean up runtimeStrings_ map
    auto it = runtimeStrings_.begin();
    while (it != runtimeStrings_.end()) {
        if (!it->second->isMarked()) {
            it = runtimeStrings_.erase(it);
        } else {
            ++it;
        }
    }
}

void VM::freeObject(GCObject* object) {
    // Strings in VM::strings_ and functions_ are deleted in reset()
    // but here we only delete GC-allocated objects.
    
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
            // Coroutines are in coroutines_ vector, find and remove
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

void VM::collectGarbage() {
    if (!gcEnabled_) return;
    markRoots();
    processWeakTables();
    removeUnmarkedWeakEntries();
    sweep();
    nextGC_ = bytesAllocated_ * 2;
}
