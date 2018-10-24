#include <unistd.h>
#include <sys/mman.h>
#include <cstdio>
#include <iostream>
#include <cassert>
#include <vector>
#include <memory>

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCUnusedStructInspection"
#pragma ide diagnostic ignored "OCSimplifyInspection"
#pragma ide diagnostic ignored "OCDFAInspection"

template<typename... Args>
struct args_count;

template<>
struct args_count<> {
    const static int not_sse = 0;
    const static int sse = 0;
};

template<typename... Other>
struct args_count<float, Other...> {
    const static int not_sse = args_count<Other...>::not_sse;
    const static int sse = args_count<Other...>::sse + 1;
};

template<typename... Other>
struct args_count<double, Other...> {
    const static int not_sse = args_count<Other...>::not_sse;
    const static int sse = args_count<Other...>::sse + 1;
};

template<typename First, typename... Other>
struct args_count<First, Other...> {
    const static int not_sse = args_count<Other...>::not_sse + 1;
    const static int sse = args_count<Other...>::sse;
};

class trampoline_allocator {

    static const size_t PAGE_SIZE = 4096;
    static const size_t TRAMPOLINE_SIZE = 128;
    static void** memory_ptr;

public:

    static void* malloc() {
        if (memory_ptr == nullptr) {
            char* page_ptr = (char*)mmap(nullptr, PAGE_SIZE, PROT_EXEC | PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            memory_ptr = (void**)page_ptr;
            char* cur = nullptr;
            for (size_t i = TRAMPOLINE_SIZE; i < PAGE_SIZE; i += TRAMPOLINE_SIZE) {
                cur = page_ptr + i;
                *(void**)(cur - TRAMPOLINE_SIZE) = cur;
            }
            *(void**)cur = nullptr;
        }
        void* ptr = memory_ptr;
        memory_ptr = (void**)*memory_ptr;
        return ptr;
    }

    static void free(void *ptr) {
        *(void**)ptr = memory_ptr;
        memory_ptr = (void**)ptr;
    }
};

void** trampoline_allocator::memory_ptr = nullptr;

template <typename T>
struct trampoline;

template <typename Ret, typename... Args>
struct trampoline<Ret (Args...)> {

    template <typename Fun>
    trampoline(Fun const& func)
            : func_obj(new Fun(func))
            , caller(&call_obj<Fun>)
            , deleter(delete_obj < Fun>) {
        code = trampoline_allocator::malloc();
        machine_code = (char*)code;

        size_t sse = args_count<Args ...>::sse;
        size_t not_sse = args_count<Args ...>::not_sse;

        // args: rdi rsi rdx rcx r8 r9
        if (not_sse < 6) {
            for (size_t i = 6 - not_sse; i < 6; ++i) {
                write_code(shifts[i]);
            }

            //mov rdi, func
            write_code("\x48\xBF");
            *(void**)machine_code = func_obj;
            machine_code += 8;

            //mov rax, func_caller
            write_code("\x48\xB8");
            *(void**)machine_code = (void*) &call_obj<Fun>;
            machine_code += 8;

            //jmp rax
            write_code("\xFF\xE0");

        } else {

            size_t size = not_sse - 5;
            size += std::max(0, (int)sse - 8);
            size *= 8;

            //mov r11, [rsp]
            write_code("\x4C\x8B\x1C\x24");

            for (auto shift: shifts) {
                write_code(shift);
            }

            //mov rax, rsp
            write_code("\x48\x89\xE0");

            //add rax, $size
            write_code("\x48\x05");
            *(int32_t*)machine_code = size;
            machine_code += 4;

            //add rsp, $8
            write_code("\x48\x81\xC4");
            *(int32_t*)machine_code = 8;
            machine_code += 4;

            char* label_for_shifting = machine_code;

            //cmp rax, rsp
            write_code("\x48\x39\xE0");

            //je label_after
            write_code("\x74");

            char* label_after = machine_code;
            ++machine_code;

            //add rsp, $8
            write_code("\x48\x81\xC4");
            *(int32_t*)machine_code = 8;
            machine_code += 4;

            //mov rdi, [rsp]
            write_code("\x48\x8B\x3C\x24");

            //mov [rsp - 8], rdi
            write_code("\x48\x89\x7C\x24\xF8");

            //jmp label_for_shifting
            write_code("\xEB");

            *machine_code = static_cast<char>(label_for_shifting - machine_code - 1);
            ++machine_code;

            *label_after = static_cast<char>(machine_code - label_after - 1);

            //mov [rsp], r11
            write_code("\x4C\x89\x1C\x24");

            //sub rsp, size
            write_code("\x48\x81\xEC");
            *(int32_t*)machine_code = size;
            machine_code += 4;

            //mov rdi, func
            write_code("\x48\xBF");
            *(void**)machine_code = func_obj;
            machine_code += 8;

            //mov rax, func_caller
            write_code("\x48\xB8");
            *(void**)machine_code = (void*) &call_obj<Fun>;
            machine_code += 8;

            //call rax
            write_code("\xFF\xD0");

            //pop r9
            write_code("\x41\x59");

            //mov r11, [rsp + size - 8]
            write_code("\x4C\x8B\x9C\x24");
            *(int32_t*)machine_code = size - 8;
            machine_code += 4;

            //mov [rsp], r11
            write_code("\x4C\x89\x1C\x24");

            //ret
            write_code("\xC3");
        }
        machine_code = nullptr;
    }

    ~trampoline() {
        if (func_obj != nullptr) {
            deleter(func_obj);
        }
        trampoline_allocator::free(code);
    }

    trampoline(trampoline &&other) : code(other.code), deleter(other.deleter), func_obj(other.func_obj) {
        other.func_obj = nullptr;
    }

    trampoline(const trampoline &) = delete;

    trampoline& operator=(trampoline&& other) {
        deleter(func_obj);
        func_obj = other.func_obj;
        code = other.code;
        deleter = other.deleter;
        other.func_obj = nullptr;
        return *this;
    }

    trampoline& operator=(const trampoline& other) = delete;

    Ret (*get() const)(Args ...args) {
        return (Ret (*)(Args ...args))code;
    }

private:

    void write_code(const char *op_code) {
        while (*op_code) {
            *(machine_code++) = *(op_code++);
        }
    }

    const char* shifts[6] = {
            "\x41\x51", //push r9
            "\x4D\x89\xC1", //mov r8, r9
            "\x49\x89\xC8", //mov rcx, r8
            "\x48\x89\xD1", //mov rdx, rcx
            "\x48\x89\xF2", //mov rsi, rdx
            "\x48\x89\xFE" //mov rdi, rsi
    };

    template <typename F>
    static void delete_obj(void *obj) {
        delete ((F*)obj);
    }

    template <typename F>
    static Ret call_obj(void *obj, Args ...args) {
        return (*(F*)obj)(args...);
    }

    void* func_obj;
    void* code;
    char* machine_code;
    Ret (*caller)(void* obj,  Args ...args);
    void (*deleter)(void*);
};

void tests_macro() {

    trampoline<int (int, int, int, int, int, int, int, int)> t1(
            [&] (int var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7)
            { return var0 + var1 + var2 + var3 + var4 + var5 + var6 + var7; });
    assert (1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 == t1.get()(1, 2, 3, 4, 5, 6, 7, 8));

    trampoline<double (int, int, int, int, int, int, int, int, double, float)> t2(
            [&] (int var0, int var1, int var2, int var3, int var4, int var5, int var6, int var7, double y0, float z0)
            { return var0 + var1 + var2 + var3 + var4 + var5 + var6 + var7 + y0 * z0; });
    double ans = t2.get()(1, 2, 3, 4, 5, 6, 7, 8, 3.5, 2.5);
    double real_ans = 1 + 2 + 3 + 4 + 5 + 6 + 7 + 8 + 3.5 * 2.5;
    assert((ans - real_ans) > -0.0001 && ans - real_ans < 0.0001);

    trampoline<double (int, int, int, int, int, int, int, int, double, double, double, double, double, float, float, float, float, float)> t3(
            [&] (int x0, int x1, int x2, int x3, int x4, int x5, int x6, int x7,
                 double y0, double y1, double y2, double y3, double y4,
                 float z0, float z1, float z2, float z3, float z4) {
                return y0 * z0 + y1 * z1 + y2 * z2 + y3 * z3 + y4 * z4 + x0 + x1 + x2 + x3 + x4 + x5 + x6 + x7;
            });
    ans = t3.get()(1, 1, 1, 1, 1, 1, 1, 1, 3.5, 3.5, 3.5, 3.5, 3.5, 2.5, 2.5, 2.5, 2.5, 2.5);
    real_ans = 3.5 * 2.5 + 3.5 * 2.5 + 3.5 * 2.5 + 3.5 * 2.5 + 3.5 * 2.5 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1;
    assert(ans - real_ans > -0.001 && ans - real_ans < 0.001);

    std::cout << "tests with macro OK\n";
}

void tests_micro() {

    trampoline <int(int, int, int, int)>tramp1 = [&] (int var0, int var1, int var2, int var3) {return var0 + var1 + var2 + var3;};
    assert (10 == tramp1.get()(1, 2, 3, 4));

    trampoline<int(int, int, int, int)> tramp2(std::move(tramp1));
    assert (11 == tramp2.get()(1, 2, 3, 5));

    trampoline<int(int, int)>tramp3 = [&] (int var0, int var1) {
        return var0 - var1;};
    assert (-1 == tramp3.get()(1, 2));

    std::cout << "tests with micro OK\n";
}

int main() {
    tests_macro();
    tests_micro();
    return 0;
}
