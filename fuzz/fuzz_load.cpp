// Fuzz driver for the bytecode deserializer (Chunk::deserialize /
// FunctionObject::deserialize). Mutational fuzzer: loads seed corpus,
// applies random mutations, feeds results to the deserializer.
// Run under ASan+UBSan; any sanitizer report is a bug.
//
// Usage: fuzz_load <seed_dir> <iterations>
//
// Mutations that must NOT happen:
//   - hang (deserializer must fail fast on corrupt counts)
//   - crash (ASan/UBSan report)
//   - uncaught exception (all errors must be std::exception-derived)
//
// Expected: every input either deserializes or throws a catchable error.

#include <iostream>
#include <fstream>
#include <sstream>
#include <random>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>

#include "value/function.hpp"

namespace fs = std::filesystem;

static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

// Apply 1-4 random mutations to data.
static void mutate(std::vector<uint8_t>& data, std::mt19937& rng) {
    if (data.empty()) return;
    std::uniform_int_distribution<size_t> posDist(0, data.size() - 1);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> nMut(1, 4);
    std::uniform_int_distribution<int> kind(0, 3);

    int n = nMut(rng);
    for (int i = 0; i < n; i++) {
        int k = kind(rng);
        size_t pos = posDist(rng);
        switch (k) {
            case 0: // random byte flip
                data[pos] = static_cast<uint8_t>(byteDist(rng));
                break;
            case 1: // bit flip
                data[pos] ^= static_cast<uint8_t>(1 << (byteDist(rng) % 8));
                break;
            case 2: // truncate (only if we can keep a valid-ish prefix)
                if (data.size() > 16) {
                    std::uniform_int_distribution<size_t> t(1, data.size() - 1);
                    data.resize(t(rng));
                    return; // one truncation per input is enough
                }
                break;
            case 3: // interesting value (0, 0xFF, 0x7F, 0x80)
            {
                static const uint8_t interesting[] = {0x00, 0xFF, 0x7F, 0x80, 0x01, 0xFE};
                data[pos] = interesting[byteDist(rng) % 6];
                break;
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <seed_dir> <iterations>\n";
        return 1;
    }

    std::string seedDir = argv[1];
    int iterations = std::stoi(argv[2]);

    // Load seed corpus
    std::vector<std::vector<uint8_t>> seeds;
    for (const auto& entry : fs::directory_iterator(seedDir)) {
        if (entry.is_regular_file()) {
            auto data = readFile(entry.path().string());
            if (!data.empty()) seeds.push_back(std::move(data));
        }
    }
    if (seeds.empty()) {
        std::cerr << "No seed files in " << seedDir << "\n";
        return 1;
    }
    std::cout << "Loaded " << seeds.size() << " seeds\n";

    std::mt19937 rng(0xC0FFEE);
    std::uniform_int_distribution<size_t> seedDist(0, seeds.size() - 1);

    int okCount = 0, errCount = 0;
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < iterations; i++) {
        std::vector<uint8_t> data = seeds[seedDist(rng)];
        mutate(data, rng);

        std::string buf(data.begin(), data.end());
        std::istringstream iss(buf, std::ios::binary);
        try {
            auto func = FunctionObject::deserialize(iss, "");
            // Deserialization succeeded; also run the verifier implicitly
            // (it's called inside deserialize). Just let func destruct.
            (void)func;
            okCount++;
        } catch (const std::exception& e) {
            // Expected: corrupt input throws a catchable error.
            errCount++;
        } catch (...) {
            std::cerr << "UNCAUGHT non-std exception at iteration " << i << "\n";
            return 2;
        }

        if ((i + 1) % 10000 == 0) {
            auto now = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            std::cout << (i + 1) << " iters (" << ms << "ms) ok=" << okCount
                      << " err=" << errCount << "\n";
        }
    }

    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Done: " << iterations << " iterations in " << ms << "ms\n";
    std::cout << "  deserialized: " << okCount << ", rejected: " << errCount << "\n";
    return 0;
}
