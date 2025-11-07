#include "HybridHashTable.hpp"
#include <iostream>  // For debugging

template <typename Key, typename Value>
HybridHashTable<Key, Value>::HybridHashTable(size_t initialSize, double maxLoadFactor)
    : capacity_(initialSize), numElements_(0), maxLoadFactor_(maxLoadFactor), currentMode_(HashMode::Hopscotch),
      totalInsertions_(0), totalCollisions_(0), totalProbes_(0) {
    table_.resize(capacity_, std::nullopt);
    table2_.resize(capacity_, std::nullopt);
    hopInfo_.resize(capacity_, 0);
    probeDistances_.resize(capacity_, 0);
    hash_ = HashUtils::hash<Key>;
    hash1_ = HashUtils::hash1<Key>;
    hash2_ = HashUtils::hash2<Key>;
}

template <typename Key, typename Value>
HybridHashTable<Key, Value>::~HybridHashTable() {
    // Cleanup if needed
}

template <typename Key, typename Value>
bool HybridHashTable<Key, Value>::insert(const Key& key, const Value& value) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (searchInternal(key)) return false;
    totalInsertions_++;
    bool success = false;

    if (currentMode_ == HashMode::Hopscotch) {
        size_t baseIndex = hash(key);
        size_t start = getNeighborhoodStart(baseIndex);
        size_t end = getNeighborhoodEnd(baseIndex);
        size_t emptyIndex = findEmptySlot(start, end);
        if (emptyIndex != capacity_) {
            table_[emptyIndex] = {key, value};
            updateHopInfo(baseIndex, emptyIndex, true);
            numElements_++;
            success = true;
        } else if (displace(baseIndex)) {
            // After displacement, find the new empty slot and insert directly
            size_t newEmptyIndex = findEmptySlot(start, end);
            if (newEmptyIndex != capacity_) {
                table_[newEmptyIndex] = {key, value};
                updateHopInfo(baseIndex, newEmptyIndex, true);
                numElements_++;
                success = true;
            }
        } else {
            totalCollisions_++;
        }
    } else if (currentMode_ == HashMode::RobinHood) {
        std::pair<Key, Value> item = {key, value};
        size_t idealIndex = hash(key);
        size_t currentIndex = idealIndex;
        size_t currentDistance = 0;
        for (size_t probe = 0; probe < MAX_PROBE_DISTANCE; ++probe) {
            totalProbes_++;
            currentIndex = (idealIndex + probe) % capacity_;
            if (!table_[currentIndex] || isTombstone(table_[currentIndex])) {
                table_[currentIndex] = item;
                probeDistances_[currentIndex] = currentDistance;
                numElements_++;
                success = true;
                break;
            }
            size_t existingDistance = probeDistances_[currentIndex];
            if (currentDistance > existingDistance) {
                std::swap(item, *table_[currentIndex]);
                std::swap(currentDistance, probeDistances_[currentIndex]);
            } else {
                totalCollisions_++;
            }
            currentDistance++;
        }
    } else if (currentMode_ == HashMode::Cuckoo) {
        std::pair<Key, Value> item = {key, value};
        int evictions = 0;
        while (evictions < MAX_EVICTIONS) {
            size_t idx1 = hash1(item.first);
            if (!table_[idx1] || isTombstone(table_[idx1])) {
                table_[idx1] = item;
                numElements_++;
                success = true;
                break;
            }
            std::swap(item, *table_[idx1]);
            evictions++;
            size_t idx2 = hash2(item.first);
            if (!table2_[idx2] || isTombstone(table2_[idx2])) {
                table2_[idx2] = item;
                numElements_++;
                success = true;
                break;
            }
            std::swap(item, *table2_[idx2]);
            evictions++;
            totalCollisions_++;
        }
    }

    if (!success) {
        success = insertIntoStash(key, value);
    }

    // Re-enable hybrid switching (safe, as load factor is computed without locking)
    double currentLoad = static_cast<double>(numElements_) / (capacity_ + stash_.size());
    //switchModeIfNeeded(currentLoad);
    return success;
}

template <typename Key, typename Value>
bool HybridHashTable<Key, Value>::remove(const Key& key) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (currentMode_ == HashMode::Cuckoo) {
        size_t idx1 = hash1(key);
        if (table_[idx1] && table_[idx1]->first == key) {
            table_[idx1] = std::make_pair(TOMBSTONE, Value{});
            numElements_--;
            return true;
        }
        size_t idx2 = hash2(key);
        if (table2_[idx2] && table2_[idx2]->first == key) {
            table2_[idx2] = std::make_pair(TOMBSTONE, Value{});
            numElements_--;
            return true;
        }
    } else if (currentMode_ == HashMode::Hopscotch) {
        size_t baseIndex = hash(key);
        size_t start = getNeighborhoodStart(baseIndex);
        size_t end = getNeighborhoodEnd(baseIndex);
        uint32_t hopBitmap = hopInfo_[baseIndex];
        for (size_t i = 0; i < HOP_RANGE && (start + i) < end; ++i) {
            if (hopBitmap & (1U << i)) {
                size_t checkIndex = start + i;
                if (table_[checkIndex] && table_[checkIndex]->first == key) {
                    table_[checkIndex] = std::make_pair(TOMBSTONE, Value{});
                    updateHopInfo(baseIndex, checkIndex, false);
                    numElements_--;
                    return true;
                }
            }
        }
    } else if (currentMode_ == HashMode::RobinHood) {
        size_t index = hash(key);
        for (size_t probe = 0; probe < MAX_PROBE_DISTANCE; ++probe) {
            size_t currentIndex = (index + probe) % capacity_;
            if (!table_[currentIndex]) break;
            if (table_[currentIndex]->first == key && !isTombstone(table_[currentIndex])) {
                table_[currentIndex] = std::make_pair(TOMBSTONE, Value{});
                probeDistances_[currentIndex] = 0;
                numElements_--;
                backwardShift(currentIndex);
                return true;
            }
        }
    }
    return removeFromStash(key);
}

template <typename Key, typename Value>
std::optional<Value> HybridHashTable<Key, Value>::search(const Key& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // Shared lock for reads
    return searchInternal(key);
}

template <typename Key, typename Value>
// Update search method (searchInternal):
std::optional<Value> HybridHashTable<Key, Value>::searchInternal(const Key& key) const {
    if (currentMode_ == HashMode::Cuckoo) {
        size_t idx1 = hash1(key);
        if (table_[idx1] && table_[idx1]->first == key && !isTombstone(table_[idx1])) return table_[idx1]->second;
        size_t idx2 = hash2(key);
        if (table2_[idx2] && table2_[idx2]->first == key && !isTombstone(table2_[idx2])) return table2_[idx2]->second;
    } else if (currentMode_ == HashMode::Hopscotch) {
        size_t baseIndex = hash(key);
        size_t start = getNeighborhoodStart(baseIndex);
        size_t end = getNeighborhoodEnd(baseIndex);
        uint32_t hopBitmap = hopInfo_[baseIndex];
        for (size_t i = 0; i < HOP_RANGE && (start + i) < end; ++i) {
            if (hopBitmap & (1U << i)) {
                size_t checkIndex = start + i;
                if (table_[checkIndex] && table_[checkIndex]->first == key && !isTombstone(table_[checkIndex])) {
                    return table_[checkIndex]->second;
                }
            }
        }
    } else if (currentMode_ == HashMode::RobinHood) {
        size_t index = hash(key);
        for (size_t probe = 0; probe < MAX_PROBE_DISTANCE; ++probe) {
            size_t currentIndex = (index + probe) % capacity_;
            if (!table_[currentIndex]) break;
            if (table_[currentIndex]->first == key && !isTombstone(table_[currentIndex])) {
                return table_[currentIndex]->second;
            }
        }
    }
    return searchStash(key);
}

template <typename Key, typename Value>
size_t HybridHashTable<Key, Value>::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // Shared lock for reads
    return numElements_;
}

template <typename Key, typename Value>
double HybridHashTable<Key, Value>::loadFactor() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);  // Shared lock for reads
    return static_cast<double>(numElements_) / (capacity_ + stash_.size());
}

template <typename Key, typename Value>
void HybridHashTable<Key, Value>::resize(size_t newSize) {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // Exclusive lock for writes
    capacity_ = newSize;
    rehash();
}

template <typename Key, typename Value>
void HybridHashTable<Key, Value>::setMode(HashMode mode) {
    std::unique_lock<std::shared_mutex> lock(mutex_);  // Exclusive lock for writes
    currentMode_ = mode;
    table_.assign(capacity_, std::nullopt);
    table2_.assign(capacity_, std::nullopt);
    hopInfo_.assign(capacity_, 0);
    probeDistances_.assign(capacity_, 0);
    stash_.clear();
    numElements_ = 0;
}

template <typename Key, typename Value>
// Update switchModeIfNeeded to take load factor as param
void HybridHashTable<Key, Value>::switchModeIfNeeded(double currentLoad) {
    double collRate = collisionRate();
    if (currentLoad > HIGH_LOAD_THRESHOLD && currentMode_ != HashMode::RobinHood) {
        setMode(HashMode::RobinHood);
    } else if (collRate > HIGH_COLLISION_RATE && currentMode_ != HashMode::Cuckoo) {
        setMode(HashMode::Cuckoo);
    } else if (currentLoad < 0.5 && currentMode_ != HashMode::Hopscotch) {
        setMode(HashMode::Hopscotch);
    }
    totalInsertions_ = 0;
    totalCollisions_ = 0;
    totalProbes_ = 0;
}

// Helpers
template <typename Key, typename Value>
void HybridHashTable<Key, Value>::backwardShift(size_t startIndex) {
    size_t currentIndex = startIndex;
    for (size_t probe = 1; probe < capacity_; ++probe) {
        size_t nextIndex = (startIndex + probe) % capacity_;
        if (!table_[nextIndex] || isTombstone(table_[nextIndex])) break;
        size_t nextIdeal = hash(table_[nextIndex]->first);
        size_t nextDistance = getProbeDistance(nextIdeal, nextIndex);
        if (nextDistance == 0) break;
        table_[currentIndex] = *table_[nextIndex];
        probeDistances_[currentIndex] = nextDistance - 1;
        table_[nextIndex] = std::nullopt;
        probeDistances_[nextIndex] = 0;
        currentIndex = nextIndex;
    }
}

template <typename Key, typename Value>
bool HybridHashTable<Key, Value>::insertIntoStash(const Key& key, const Value& value) {
    if (stash_.size() >= MAX_STASH_SIZE) return false;
    stash_.emplace_back(key, value);
    numElements_++;
    return true;
}

template <typename Key, typename Value>
std::optional<Value> HybridHashTable<Key, Value>::searchStash(const Key& key) const {
    for (const auto& item : stash_) {
        if (item.first == key) return item.second;
    }
    return std::nullopt;
}

template <typename Key, typename Value>
bool HybridHashTable<Key, Value>::removeFromStash(const Key& key) {
    for (auto it = stash_.begin(); it != stash_.end(); ++it) {
        if (it->first == key) {
            stash_.erase(it);
            numElements_--;
            return true;
        }
    }
    return false;
}

template <typename Key, typename Value>
void HybridHashTable<Key, Value>::rehash() {
    std::vector<std::pair<Key, Value>> allElements;
    for (const auto& slot : table_) {
        if (slot && !isTombstone(slot)) allElements.push_back(*slot);
    }
    allElements.insert(allElements.end(), stash_.begin(), stash_.end());

    capacity_ *= 2;
    table_.assign(capacity_, std::nullopt);
    table2_.assign(capacity_, std::nullopt);
    hopInfo_.assign(capacity_, 0);
    probeDistances_.assign(capacity_, 0);
    stash_.clear();
    numElements_ = 0;

    for (const auto& elem : allElements) {
        insert(elem.first, elem.second);
    }
}

template <typename Key, typename Value>
void HybridHashTable<Key, Value>::updateHopInfo(size_t baseIndex, size_t targetIndex, bool add) {
    size_t start = getNeighborhoodStart(baseIndex);
    size_t bitPos = targetIndex - start;
    if (add) hopInfo_[baseIndex] |= (1U << bitPos);
    else hopInfo_[baseIndex] &= ~(1U << bitPos);
}

template <typename Key, typename Value>
size_t HybridHashTable<Key, Value>::findEmptySlot(size_t start, size_t end) {
    for (size_t i = start; i < end; ++i) {
        if (!table_[i] || isTombstone(table_[i])) return i;
    }
    return capacity_;
}

template <typename Key, typename Value>
bool HybridHashTable<Key, Value>::displace(size_t index) {
    for (size_t d = 1; d <= MAX_DISPLACEMENTS; ++d) {
        size_t checkIndex = (index + d) % capacity_;
        if (table_[checkIndex] && !isTombstone(table_[checkIndex])) {
            size_t targetBase = hash(table_[checkIndex]->first);
            size_t targetStart = getNeighborhoodStart(targetBase);
            size_t targetEnd = getNeighborhoodEnd(targetBase);
            if (checkIndex >= targetStart && checkIndex < targetEnd) {
                size_t emptyIndex = findEmptySlot(targetStart, targetEnd);
                if (emptyIndex != capacity_) {
                    table_[emptyIndex] = *table_[checkIndex];
                    table_[checkIndex] = std::nullopt;
                    updateHopInfo(targetBase, checkIndex, false);
                    updateHopInfo(targetBase, emptyIndex, true);
                    return true;
                }
            }
        }
    }
    return false;
}

// Explicit instantiations
template class HybridHashTable<std::string, int>;

// Explicit instantiation for std::string keys and values
template class HybridHashTable<std::string, std::string>;