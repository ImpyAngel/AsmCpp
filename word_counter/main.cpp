#include <cstdint>
#include <emmintrin.h>
#include <iostream>
#include <ctime>
#include <fstream>
#include <random>

int const MIN_SIZE = 64;
int const SPACE = 32;
__m128i const spaces = _mm_set_epi8(SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE, SPACE);
__m128i const zeroes = _mm_set_epi32(0, 0, 0, 0);

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

uint32_t word_counter_cpp(std::string const &str) {
    uint32_t ans = 0;

    bool prev_space = true;
    for (char sym : str) {
        if (sym == ' ') {
            prev_space = true;
        } else if (prev_space) {
            ans++;
            prev_space = false;
        }
    }
    return ans;
}

uint32_t word_counter_asm(std::string const &str) {
    char const *arr = str.c_str();
    size_t const size = str.size();

    if (size <= MIN_SIZE) {
        return word_counter_cpp(str);
    }

    size_t i;
    uint32_t ans = 0;

    bool prev_space = false;

    for (i = 0; (size_t) (arr + i) % 16 != 0; ++i) {

        char cur_symbol = *(arr + i);
        if (prev_space && cur_symbol != ' ') ans++;
        prev_space = (cur_symbol == ' ');
    }

    if ((prev_space && *(arr + i) != ' ' && i != 0) || (i == 0 && *arr != ' ')) ans++;

    if (i != 0 && *arr != ' ') ans++;

    __m128i store = zeroes;
    __m128i curr_page, next_page;

    size_t last_page = size - (size - i) % 16 - 16;

    __m128i tmp;
    __asm__("movdqa\t" "(%2), %1\n"
            "pcmpeqb\t" "%1, %0"
    :"=x"(next_page), "=x"(tmp)
    :"0"(spaces), "r"(arr + i));

    while (true) {
        curr_page = next_page;
        uint32_t msk;

        __m128i reg3, reg4, reg5, reg6;
        __asm__("movdqa\t" "(%7), %3\n"
                "pcmpeqb\t" "%3, %2\n"
                "movdqa\t" "%2, %6\n"
                "palignr\t" "$1, %4, %6\n"
                "pandn\t" "%4, %6\n"
                "psubb\t" "%6, %5\n"
                "paddusb\t" "%5, %0\n"
                "pmovmskb\t" "%0, %1"
            :"=x"(store), "=r"(msk),"=x"(next_page), "=x"(reg3), "=x"(reg4), "=x"(reg5), "=x"(reg6)
            :"r"(arr + i + 16), "0"(store),  "2"(spaces), "4"(curr_page), "5"(zeroes));

        if (msk != 0 || i + 16 >= last_page) {
            __m128i reg0, reg1;
            uint32_t high, low;
            __asm__("psadbw\t" "%1, %0\n"
                    "movd\t" "%0, %3\n"
                    "movhlps\t" "%0, %0\n"
                    "movd\t" "%0, %2\n"
            :"=x" (reg0), "=x"(reg1), "=r"(high), "=r"(low)
            :"0"(zeroes), "1"(store));

            ans += high + low;
            store = zeroes;
            if (msk == 0) break;
        }
        i += 16;
    }
    i = last_page;

    if (*(arr + i - 1) == ' ' && *(arr + i) != ' ') {
        ans--;
    }

    prev_space = *(arr + i - 1) == ' ';
    for (size_t j = i; j < size; j++) {
        if (*(arr + j) != ' ' && prev_space) ans++;
        prev_space = *(arr + j) == ' ';
    }

    return ans;
}

void random_test(int times=100) {
    std::time_t time_asm = 0, time_cpp = 0, timer;

    for (int i = 0; i < times; ++i) {
        std::string str = get_random_string();

        timer = clock();
        uint32_t ans_cpp = word_counter_cpp(str);
        time_cpp += clock() - timer;

        timer = clock();
        uint32_t ans_asm = word_counter_asm(str);
        time_asm += clock() - timer;

        if (ans_asm != ans_cpp) {
            std::cout << "WRONG ON TEST " << str;
            return;
        }
    }
    std::cout << "OK\n " << time_cpp / (double)CLOCKS_PER_SEC << " - time cpp\n" << time_asm  / (double)CLOCKS_PER_SEC << " - time asm\n";
}


int main() {
    random_test(30);
}