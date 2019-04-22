#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <iterator>
#include <sstream>

typedef int32_t (*func)(int32_t, int32_t);

enum class result_type : char {CONTINUE, ERROR, EXIT};

//int32_t my_function(int32_t a, int32_t b) {
//    return (a + b) * 1; // this constant can be changed in runtime
//}

static uint8_t function_data[] = {0x01, 0xf7, 0x69, 0xc7, /*start of num*/0x01, 0x00, 0x00, 0x00/*end of num*/, 0xc3};


void print_error(std::string const& message) {
    std::cerr << message << strerror(errno) << std::endl;
}

void incorrect_arguments_num(std::string const& command) {
    std::cout << "Incorrect number of arguments for " << command << std::endl;
}

class RAII_storage {
    void* mmapped_data;
    size_t size;
  public:
    RAII_storage(size_t size) : size(size) {
        mmapped_data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mmapped_data == MAP_FAILED) {
            print_error("mmap failed");
            throw std::bad_alloc();
        }
    }

    bool mprotect(int prot) {
        if (::mprotect(mmapped_data, size, prot) == -1) {
            print_error("mprotect failed");
            return false;
        }
        return true;
    }

    uint8_t* get_data() {
        return static_cast<uint8_t*>(mmapped_data);
    }

    ~RAII_storage() {
        if (mmapped_data != MAP_FAILED) {
            if (munmap(mmapped_data, size) == -1) {
                print_error("munmap failed");
            }
        }
    }
};


std::vector<std::string> split(std::string const& line) {
    std::istringstream iss{line};
    return std::vector<std::string>(std::istream_iterator<std::string> {iss}, std::istream_iterator<std::string> {});
}

void print_help() {
    std::cout << "This program reads commands from STDIN in a loop. All calculations are done with 32-bits signed integers.\n"
              "\texit\t\texits loop\n"
              "\thelp\t\tshows this message\n"
              "\tcalc a b\tcalculates (a + b) * C\n"
              "\tset C\t\tchanges C to a given value. Default value is 1.\n" << std::endl;
}


bool change_c(int32_t new_c, RAII_storage & storage) {
    if (!storage.mprotect(PROT_READ | PROT_WRITE)) {
        return false;
    }
    for (size_t i = 4; i <= 7; i++) {
        storage.get_data()[i] = new_c & 0xFF;
        new_c >>= 8;
    }
    if (!storage.mprotect(PROT_READ | PROT_EXEC)) {
        return false;
    }
    return true;
}

result_type process_line(std::string const& line, RAII_storage & storage) {
    auto splitted = split(line);
    if (splitted.empty()) {
        return result_type::CONTINUE;
    }
    if (splitted[0] == "calc") {
        if (splitted.size() != 3) {
            incorrect_arguments_num("calc");
            return result_type::CONTINUE;
        }
        func func_pointer = reinterpret_cast<func>(storage.get_data());
        try {
            std::cout << func_pointer(std::stoi(splitted[1]), std::stoi(splitted[2])) << std::endl;
        } catch (...) {
            std::cout << "Arguments for calc are incorrect " << std::endl;
        }
    } else if (splitted[0] == "set") {
        if (splitted.size() != 2) {
            incorrect_arguments_num("set");
            return result_type::CONTINUE;
        }
        try {
            int32_t new_c = std::stoi(splitted[1]);
            if (change_c(new_c, storage)) {
                std::cout << "New value of C was set" << std::endl;
            } else {
                return result_type::ERROR;
            }
        } catch (...) {
            std::cout << "Argument for set is incorrect " << std::endl;
        }

    } else if (splitted[0] == "help") {
        if (splitted.size() == 1) {
            print_help();
        } else {
            incorrect_arguments_num("help");
        }
    }  else if (splitted[0] == "exit") {
        if (splitted.size() == 1) {
            return result_type::EXIT;
        }
        incorrect_arguments_num("exit");
    } else {
        std::cout << "Unknown command: " + splitted[0] << std::endl;
    }
    return result_type::CONTINUE;
}


int main() {
    try {
        RAII_storage storage(sizeof(function_data));
        uint8_t* data = static_cast<uint8_t*>(storage.get_data());
        if (data) {
            std::memcpy(data, function_data, sizeof(function_data));
            if (!storage.mprotect(PROT_READ | PROT_EXEC)) {
                return EXIT_FAILURE;
            }
            print_help();
            std::string line;
            while (true) {
                std::cout << "$ ";
                std::cout.flush();
                std::getline(std::cin, line);
                if (std::cin.eof()) {
                    break;
                }
                auto res = process_line(line, storage);
                if (res == result_type::EXIT) {
                    break;
                }
                if (res == result_type::ERROR) {
                    return EXIT_FAILURE;
                }
            }
        } else {
            return EXIT_FAILURE;
        }
    } catch (std::bad_alloc const&) {
        return EXIT_FAILURE;
    }
}
