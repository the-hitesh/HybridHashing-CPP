#ifndef HASH_FUNCTIONS_HPP
#define HASH_FUNCTIONS_HPP

#include <functional>
#include <string>

namespace HashUtils {
    // Primary hash for Cuckoo
    template <typename Key>
    size_t hash1(const Key& key) {
        return std::hash<Key>{}(key);
    }

    // Secondary hash for Cuckoo
    template <typename Key>
    size_t hash2(const Key& key) {
        size_t h = std::hash<Key>{}(key);
        return (h ^ 0x9e3779b9) + (h << 6) + (h >> 2);
    }

    // Single hash for Hopscotch/Robin Hood
    template <typename Key>
    size_t hash(const Key& key) {
        return std::hash<Key>{}(key);
    }
}

#endif // HASH_FUNCTIONS_HPP