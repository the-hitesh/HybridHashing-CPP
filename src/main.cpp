#include "HybridHashTable.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include <algorithm>

// Utility: Generate random strings (e.g., for URLs or keys)
std::string generateRandomString(size_t length) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; ++i) {
        result += charset[dist(rng)];
    }
    return result;
}

// Test function for big data
void runBigDataTest(size_t numElements, HashMode mode, bool multithreaded = false) {
    HybridHashTable<std::string, int> table(1000000, 2.0);  // Large initial size
    table.setMode(mode);

    std::cout << "\n=== Testing " << numElements << " elements in " 
              << (mode == HashMode::Cuckoo ? "Cuckoo" : mode == HashMode::Hopscotch ? "Hopscotch" : "Robin Hood") 
              << " mode (" << (multithreaded ? "Multithreaded" : "Single-threaded") << ") ===\n";

    // Generate dataset: Mix of random strings and sequential IDs for realism
    std::vector<std::pair<std::string, int>> dataset;
    dataset.reserve(numElements);
    for (size_t i = 0; i < numElements / 2; ++i) {
        dataset.emplace_back(generateRandomString(10), i);  // Random keys (e.g., URLs)
    }
    for (size_t i = numElements / 2; i < numElements; ++i) {
        dataset.emplace_back("user" + std::to_string(i), i);  // Sequential keys (e.g., user IDs)
    }

    // Insert test
    auto start = std::chrono::high_resolution_clock::now();
    if (multithreaded) {
        std::vector<std::thread> threads;
        size_t chunkSize = numElements / 4;  // 4 threads
        for (size_t t = 0; t < 4; ++t) {
            threads.emplace_back([&table, &dataset, t, chunkSize]() {
                for (size_t i = t * chunkSize; i < (t + 1) * chunkSize && i < dataset.size(); ++i) {
                    table.insert(dataset[i].first, dataset[i].second);
                }
            });
        }
        for (auto& th : threads) th.join();
    } else {
        for (const auto& item : dataset) {
            table.insert(item.first, item.second);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    double insertTime = std::chrono::duration<double>(end - start).count();
    std::cout << "Insert time: " << insertTime << "s (" << numElements / insertTime << " ops/sec)\n";
    std::cout << "Load factor after insert: " << table.loadFactor() << "\n";

    // Search test (random subset)
    std::shuffle(dataset.begin(), dataset.end(), std::mt19937(std::random_device{}()));
    size_t searchCount = std::min(numElements / 10, size_t(100000));  // Search 10% or 100k
    start = std::chrono::high_resolution_clock::now();
    size_t found = 0;
    for (size_t i = 0; i < searchCount; ++i) {
        if (table.search(dataset[i].first)) ++found;
    }
    end = std::chrono::high_resolution_clock::now();
    double searchTime = std::chrono::duration<double>(end - start).count();
    std::cout << "Search time (" << searchCount << " ops): " << searchTime << "s (" << searchCount / searchTime << " ops/sec)\n";
    std::cout << "Found: " << found << "/" << searchCount << "\n";

    // Remove test (random subset)
    start = std::chrono::high_resolution_clock::now();
    size_t removed = 0;
    for (size_t i = 0; i < searchCount; ++i) {
        if (table.remove(dataset[i].first)) ++removed;
    }
    end = std::chrono::high_resolution_clock::now();
    double removeTime = std::chrono::duration<double>(end - start).count();
    std::cout << "Remove time (" << searchCount << " ops): " << removeTime << "s (" << searchCount / removeTime << " ops/sec)\n";
    std::cout << "Removed: " << removed << "/" << searchCount << "\n";
    std::cout << "Final size: " << table.size() << ", load factor: " << table.loadFactor() << "\n";
}

int main() {
    std::vector<size_t> sizes = {100000,1000000};
    std::vector<HashMode> modes = {HashMode::Hopscotch, HashMode::RobinHood, HashMode::Cuckoo};
    for (size_t size : sizes) {
        for (HashMode mode : modes) {
            if (mode == HashMode::Hopscotch && size > 100000) continue;  // Skip large Hopscotch
            runBigDataTest(size, mode, false);
            if (size <= 100000) {  // Multithreaded for smaller sizes
                runBigDataTest(size, mode, true);
            }
        }
    }
    std::cout << "\n=== All tests completed ===\n";
    return 0;
}