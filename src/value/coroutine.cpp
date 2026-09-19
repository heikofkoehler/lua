#include "value/coroutine.hpp"
#include "value/upvalue.hpp"

CoroutineObject::~CoroutineObject() = default;

void CoroutineObject::markReferences() {
    // Handled in blackenObject
}
