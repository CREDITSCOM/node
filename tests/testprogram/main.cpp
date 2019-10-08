#include <thread>
#include <string>
#include <chrono>

int main(int argc, char* argv[])
{
    std::string code = { "0" };
    std::string delay = { "10" };

    if (argc > 1) {
        delay = std::string(argv[1]);
    }

    if (argc > 2) {
        code = std::string(argv[2]);
    }

    int c = std::stoi(code);
    int ms = std::stoi(delay);

    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    std::exit(c);
}
