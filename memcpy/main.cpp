#include <iostream>
#include <string>
#include <emmintrin.h>
#include <ctime>
#include <random>

const size_t MIN_SIZE = 32;


std::string get_random_string(int min_len=100000, int max_len=500000)
{
    std::string ret;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> len_dis(min_len, max_len);
    int len = len_dis(gen);
    auto next_rand = [&](int max) { return std::uniform_int_distribution<>(0, max)(gen);};

    for(size_t i = 0; i < len; i++) {
        ret += (!(next_rand(3)) ? ' ' : 'a' + next_rand(25));
    }
    return ret;
}

void memcpy_cpp(void *dist, void const *source, size_t size) {
	for(size_t i = 0; i < size; i++) {
		*((char *)dist + i) = *((char *)source + i);
	}
}

void memcpy_asm(void *dist, void const *source, size_t size, bool with_cache=true)
{
	if (size < MIN_SIZE) return memcpy_cpp(dist, source, size);

	size_t i = 0;

    for (i = 0; ((size_t)dist + i) % 16 != 0; ++i) {
        *((char *)dist + i) = *((char *)source + i);
    }

	size_t last_page = (size - i) % 16;
	for(; i < size - last_page; i += 16) {
		__m128i reg0;
		__asm__ (
		"movdqu (%1), %0\n"
		"movdntq %0, (%2)\n"
		    :"=x"(reg0)
		    :"r"((char *)source + i), "r"((char *)dist + i)
		    :"memory");
	}

	for (size_t j = size - last_page; j < size; j++) {
        *((char *)dist + j) = *((char *)source + j);
    }
	_mm_sfence();
}

void random_test(int times=100) {
    std::time_t time_asm = 0, time_cpp = 0, timer;

    for (int i = 0; i < times; ++i) {
        std::string str = get_random_string();

        size_t size = str.size();
        char* buffer_cpp = new char[size];
        timer = clock();
        memcpy_cpp(buffer_cpp, str.c_str(), size);
        time_cpp += clock() - timer;

        char* buffer_asm = new char[size];
        timer = clock();
        memcpy_asm(buffer_asm, str.c_str(), size);
        time_asm += clock() - timer;

        for (int j = 0; j < size; ++j) {
            if (*(buffer_cpp + i) != *(buffer_asm + i)) {
                std::cout << "WRONG ON TEST " << str;
                return;
            }
        }
    }
    std::cout << "OK\n " << time_cpp / (double)CLOCKS_PER_SEC << " - time cpp\n" << time_asm / (double)CLOCKS_PER_SEC << " - time asm\n";
}

void do_tests(size_t num = 500)
{
    time_t seed = std::time(0);
    std::cerr << "Seed: " << seed << std::endl;
    std::srand(seed);

    clock_t slow_clocks = 0, fast_clocks = 0;

    int visualization_step = num / 100;
    int prev_step = 0;

    std::cerr << "0% done\n";

    for(size_t i = 0; i < num; i++)
    {
        if (i == prev_step + visualization_step)
        {
            prev_step = i;
            std::cerr << prev_step / visualization_step << "% done\n";
        }

        std::string str = get_random_string(1e4, 2e5);

        char* buffer1 = new char[str.size()];
        char* buffer2 = new char[str.size()];

        clock_t begin = clock();
        memcpy_asm(buffer1, str.c_str(), str.size());
        fast_clocks += (clock() - begin);

        begin = clock();
        memcpy_cpp(buffer2, str.c_str(), str.size());
        slow_clocks += (clock() - begin);


        delete[] buffer1;
        delete[] buffer2;
    }
    std::cerr << num << " tests done, my congratulations.\n"
    << "Fast counter has taken " << ((double)fast_clocks) / CLOCKS_PER_SEC
    << ", while slow: " << ((double)slow_clocks) / CLOCKS_PER_SEC << ".\n"
    << "Optimized code is " << (double) slow_clocks / (double) fast_clocks << " times faster\n";
}

int main() {
	random_test(30);
}