#include "HybridHashTable.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

void loadFromFile(HybridHashTable<std::string, std::string>& table, const std::string& filename, std::vector<std::string>& keys) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file " << filename << "\n";
        return;
    }

    std::string line;
    size_t inserted = 0;
    auto start = std::chrono::high_resolution_clock::now();
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string key, value;
        if (std::getline(ss, key, ',') && std::getline(ss, value)) {
            if (table.insert(key, value)) {
                inserted++;
                keys.push_back(key);  // Store keys for later operations
            }
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    double time = std::chrono::duration<double>(end - start).count();
    file.close();
    std::cout << inserted << " items inserted in " << time << "s (" << inserted / time << " inserts/sec)\n";
}

void performOperations(HybridHashTable<std::string, std::string>& table, const std::vector<std::string>& keys) {
    // Search all items
    size_t found = 0;
    auto searchStart = std::chrono::high_resolution_clock::now();
    for (const auto& key : keys) {
        if (table.search(key)) found++;
    }
    auto searchEnd = std::chrono::high_resolution_clock::now();
    double searchTime = std::chrono::duration<double>(searchEnd - searchStart).count();
    std::cout << found << " items searched (out of " << keys.size() << ") in " << searchTime << "s (" << keys.size() / searchTime << " searches/sec)\n";

    // Remove all items
    size_t removed = 0;
    auto removeStart = std::chrono::high_resolution_clock::now();
    for (const auto& key : keys) {
        if (table.remove(key)) removed++;
    }
    auto removeEnd = std::chrono::high_resolution_clock::now();
    double removeTime = std::chrono::duration<double>(removeEnd - removeStart).count();
    std::cout << removed << " items removed (out of " << keys.size() << ") in " << removeTime << "s (" << keys.size() / removeTime << " removes/sec)\n";
}

int main() {
    HybridHashTable<std::string, std::string> table(1000000, 2.0);
    table.setMode(HashMode::RobinHood);

    std::vector<std::string> keys;  // To store keys for operations

    // Load data from file
    loadFromFile(table, "data.csv", keys);
    std::cout << "Final size: " << table.size() << ", load factor: " << table.loadFactor() << "\n";

    // Perform operations on all items
    performOperations(table, keys);

    return 0;
}