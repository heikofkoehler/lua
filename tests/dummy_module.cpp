#include "vm/vm.hpp"

extern "C" {

#ifdef _WIN32
__declspec(dllexport)
#endif
bool dummy_test_function(VM* vm, int argCount) {
    for (int i = 0; i < argCount; i++) {
        vm->pop();
    }
    vm->push(Value::number(42.0));
    vm->currentCoroutine()->lastResultCount = 1;
    return true;
}

}
