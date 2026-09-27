#include "value/coroutine.hpp"
#include "value/upvalue.hpp"

CoroutineObject::~CoroutineObject() {
    for (UpvalueObject* uv : openUpvalues) {
        if (uv && uv->owner() == this) {
            uv->close(stack);
        }
    }
    openUpvalues.clear();
}

void CoroutineObject::markReferences() {
    // Handled in blackenObject
}
