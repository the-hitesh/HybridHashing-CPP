#ifndef HYBRID_HASH_TABLE_HPP
#define HYBRID_HASH_TABLE_HPP

#include <vector>
#include <optional>
#include <string>
#include <functional>
#include <utility>  // For std::pair
#include <cstdint>  // For uint32_t
#include <mutex>    // For multithreading
#include <shared_mutex>  // For read-write locks
#include "HashFunctions.hpp"

// Enum for hashing modes
enum class HashMode { Cuckoo, Hopscotch, RobinHood };

// Tombstone for deletions
const std::string TOMBSTONE = "__TOMBSTONE__";

template <typename Key, typename Value>
class HybridHashTable {
public:
    // Constructor
    HybridHashTable(size_t initialSize = 16, double maxLoadFactor = 0.75);

    // Destructor
    ~HybridHashTable();

    // Core operations (thread-safe)
    bool insert(const Key& key, const Value& value);
    bool remove(const Key& key);
    std::optional<Value> search(const Key& key) const;

    // Utility methods
    size_t size() const;
    double loadFactor() const;
    void resize(size_t newSize);
    void setMode(HashMode mode);

private:
    // Shared structures
    mutable std::shared_mutex mutex_;  // Read-write lock for thread safety
    std::vector<std::optional<std::pair<Key, Value>>> table_;  // Main table (used differently per mode)
    size_t capacity_;
    size_t numElements_;
    double maxLoadFactor_;
    HashMode currentMode_;

    // Cuckoo-specific
    std::vector<std::optional<std::pair<Key, Value>>> table2_;  // Second table for Cuckoo
    std::function<size_t(const Key&)> hash1_;
    std::function<size_t(const Key&)> hash2_;
    static const int MAX_EVICTIONS = 500;

    // Hopscotch/Robin Hood-specific (single hash)
    std::function<size_t(const Key&)> hash_;

    // Hopscotch-specific
    std::vector<uint32_t> hopInfo_;  // Bitmap for neighborhoods
    static const size_t HOP_RANGE = 32;
    static const size_t MAX_DISPLACEMENTS = 500;

    // Robin Hood-specific
    std::vector<size_t> probeDistances_;  // Probe distances
    static const size_t MAX_PROBE_DISTANCE = 500;

    // Overflow stash
    std::vector<std::pair<Key, Value>> stash_;
    static const size_t MAX_STASH_SIZE = 10000000;

    // Metrics for hybrid switching
    size_t totalInsertions_;
    size_t totalCollisions_;
    size_t totalProbes_;

    // Hybrid-specific thresholds
    static constexpr double HIGH_LOAD_THRESHOLD = 0.8;
    static constexpr double HIGH_COLLISION_RATE = 0.5;

    // Helpers
    size_t hash(const Key& key) const { return hash_(key) % capacity_; }  // For non-Cuckoo
    size_t hash1(const Key& key) const { return hash1_(key) % capacity_; }  // Cuckoo
    size_t hash2(const Key& key) const { return hash2_(key) % capacity_; }  // Cuckoo
    bool isTombstone(const std::optional<std::pair<Key, Value>>& slot) const {
        return slot && slot->first == TOMBSTONE;
    }
    size_t getNeighborhoodStart(size_t index) const { return (index / HOP_RANGE) * HOP_RANGE; }
    size_t getNeighborhoodEnd(size_t index) const { return std::min(getNeighborhoodStart(index) + HOP_RANGE, capacity_); }
    size_t getProbeDistance(size_t idealIndex, size_t currentIndex) const {
        return (currentIndex >= idealIndex) ? (currentIndex - idealIndex) : (capacity_ - idealIndex + currentIndex);
    }
    double computeLoadFactor() const { return static_cast<double>(numElements_) / (capacity_ + stash_.size()); }  // No lock version
    void switchModeIfNeeded(double currentLoad);  // Pass load factor to avoid locking
    void updateHopInfo(size_t baseIndex, size_t targetIndex, bool add);
    size_t findEmptySlot(size_t start, size_t end);
    bool displace(size_t index);
    void backwardShift(size_t startIndex);
    bool insertIntoStash(const Key& key, const Value& value);
    std::optional<Value> searchStash(const Key& key) const;
    bool removeFromStash(const Key& key);
    void switchModeIfNeeded();
    double collisionRate() const { return totalInsertions_ > 0 ? static_cast<double>(totalCollisions_) / totalInsertions_ : 0.0; }
    void rehash();
    std::optional<Value> searchInternal(const Key& key) const;  // No lock version for internal use
};

#endif // HYBRID_HASH_TABLE_HPP