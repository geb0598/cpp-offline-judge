#ifndef UTILS_H
#define UTILS_H

#include <algorithm>
#include <random>
#include <string>

namespace utils {

class RandomGenerator {
public:
    static RandomGenerator& get_instance() {
        static RandomGenerator generator;
        return generator;
    }

    RandomGenerator(const RandomGenerator&)           = delete;
    RandomGenerator(RandomGenerator&&)                = delete;
    RandomGenerator operator=(const RandomGenerator&) = delete;
    RandomGenerator operator=(RandomGenerator&&)      = delete;

    int get_int(int min = std::numeric_limits<int>::min(), int max = std::numeric_limits<int>::max()) {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(engine_);
    }

    double get_double(double min = std::numeric_limits<double>::min(), double max = std::numeric_limits<double>::max()) {
        std::uniform_real_distribution<double> dist(min, max);
        return dist(engine_);
    }

    std::string get_string(
        size_t length, 
        const std::string& chars = 
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789"
    ) {
        std::string buf(length, '\0');
        std::generate(buf.begin(), buf.end(), [&] { return chars[get_int(0, chars.size() - 1)]; });
        return buf;
    }

    std::vector<char> get_bytes(size_t length) {
        std::vector<char> buf(length);
        std::generate(buf.begin(), buf.end(), [&] { return get_int(0, 255); });
        return buf;
    }

private:
    RandomGenerator(unsigned int seed = std::random_device{}()) : engine_(seed) {}

    std::mt19937 engine_;
};

}

#endif