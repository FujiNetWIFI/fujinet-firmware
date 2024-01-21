#pragma once

#include <array>
#include <functional>
// #include <cstdint>

namespace std {

template <> struct hash<std::array<unsigned char, 4>> {
  size_t operator()(const std::array<unsigned char, 4> &arr) const {
    // simple hash function for the 4 bytes. Note, addition would also do here
    // because of bit shift, but as it is 4 bytes we can fit it into return type
    // so there are never any clashes

    return (arr[0] << 24) ^ (arr[1] << 16) ^ (arr[2] << 8) ^ arr[3];

    // size_t hash = 0;
    // const size_t prime = 31; // Prime for multiplying values by
    // const size_t mod = 1013; // A prime number for the modulus operation

    // for (const uint8_t byte : arr) {
    //   hash = (hash * prime + byte) % mod;
    // }

    // return hash;
  }
};

} // namespace std
