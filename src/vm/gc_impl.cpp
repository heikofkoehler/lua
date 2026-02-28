// Garbage Collector Implementation
#include "vm/vm.hpp"
#include "value/string.hpp"
#include "value/table.hpp"
#include "value/closure.hpp"
#include "value/upvalue.hpp"
#include "value/coroutine.hpp"
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
            for (const auto& pair : table->data()) {
                markValue(pair.first);
                markValue(pair.second);
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
            UpvalueObject* upvalue = static_cast<UpvalueObject*>(object);
            if (upvalue->isClosed()) {
                markValue(upvalue->closedValue());
            }
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
    }
}

void VM::markRoots() {
    for (const auto& pair : globals_) markValue(pair.second);
    for (const auto& pair : registry_) markValue(pair.second);
    for (int i = 0; i < Value::NUM_TYPES; i++) markValue(typeMetatables_[i]);

    if (mainCoroutine_) markObject(mainCoroutine_);
    if (currentCoroutine_) markObject(currentCoroutine_);
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
    sweep();
    nextGC_ = bytesAllocated_ * 2;
}
