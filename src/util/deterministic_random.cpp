#include "tetr/util/deterministic_random.h"

#include <algorithm>

namespace Util {
void deterministic_shuffle(int values[],int first,int last,std::mt19937& random){
    for(int i=first;i<last;i++) std::swap(values[i],values[random()%(i-first+1)+first]);
}
void deterministic_shuffle(std::vector<int>& values,std::mt19937& random){
    for(int i=0;i<(int)values.size();i++) std::swap(values[i],values[random()%(i+1)]);
}
}
