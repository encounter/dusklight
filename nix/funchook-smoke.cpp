#include <funchook.h>

#include <cstdio>

static int (*original_call)(int);

__attribute__((noinline)) static int original(int value) {
    volatile int result = value;
    result = result + 1;
    result = result + 2;
    result = result + 3;
    return result;
}

__attribute__((noinline)) static int replacement(int value) {
    return original_call(value) + 100;
}

int main() {
    int (*volatile call)(int) = original;
    original_call = original;
    auto* hook = funchook_create();
    if (hook == nullptr) {
        return 1;
    }
    auto check = [hook](int error) {
        if (error != 0) {
            std::fprintf(stderr, "%s\n", funchook_error_message(hook));
        }
        return error == 0;
    };
    if (call(1) != 7 || !check(funchook_prepare(hook, reinterpret_cast<void**>(&original_call),
                                              reinterpret_cast<void*>(replacement))) ||
        !check(funchook_install(hook, 0)) || call(1) != 107 ||
        !check(funchook_uninstall(hook, 0)) || call(1) != 7)
    {
        return 1;
    }
    funchook_destroy(hook);
    std::puts("Native hook install, trampoline and uninstall verified");
    return 0;
}
