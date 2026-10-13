#ifndef TETR_UTIL_DETERMINISTIC_RANDOM_H
#define TETR_UTIL_DETERMINISTIC_RANDOM_H

#include <algorithm>
#include <random>
#include <vector>

namespace Util {
// Deterministic Fisher-Yates variant used by gameplay. Keep the exact RNG
// consumption order so identical seeds preserve historical/replay behavior.
void deterministic_shuffle(int values[], int first, int last, std::mt19937& random);
void deterministic_shuffle(std::vector<int>& values, std::mt19937& random);

template<class T>
void deterministic_shuffle(std::vector<T>& values, std::mt19937& random){
    for(int i=0;i<static_cast<int>(values.size());++i) std::swap(values[i],values[random()%(i+1)]);
}
}

#endif
