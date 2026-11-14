#include <yateclass.h>
#include <cstring>
#include <iostream>
#include <thread>

int main()
{
    using namespace TelEngine;
    String text("sip:alice@example.com");
    Regexp expression("^sip:\\([^@]*\\)@.*$");
    if (!text.matches(expression) || text.matchString(1) != "alice") {
        std::cerr << "String/regular expression failure\n";
        return 1;
    }
    char sample[] = "abc";
    DataBlock data(sample,3);
    if (data.length() != 3 || std::memcmp(data.data(),"abc",3)) {
        std::cerr << "DataBlock failure\n";
        return 1;
    }
    // Проверяем POSIX-реализацию блокировки при конкуренции двух потоков.
    Mutex mutex(false,"cmake-test");
    unsigned count = 0;
    auto worker = [&]() {
        for (unsigned i = 0; i < 10000; ++i) {
            mutex.lock();
            ++count;
            mutex.unlock();
        }
    };
    std::thread first(worker);
    std::thread second(worker);
    first.join();
    second.join();
    if (count != 20000) {
        std::cerr << "POSIX mutex failure\n";
        return 1;
    }
    return 0;
}
