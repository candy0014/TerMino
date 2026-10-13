#include "tetr/util/numeric.h"
#include "tetr/compat/legacy_text.h"
#include "tetr/util/deterministic_random.h"
#include "tetr/util/text_codec.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace legacy {
int digit_count(long long value){
    if(value==0) return 1;
    int count=0;
    if(value<0) value=-value,++count;
    while(value) ++count,value/=10;
    return count;
}
std::string format_fixed_2(double value){
    if(std::fabs(value)<=1e-8) return "0.00";
    std::string result;
    if(value<0) value=-value,result+="-";
    result+=std::to_string((int)value)+".";
    result+=char((int)(value*10)%10+'0');
    result+=char((int)(value*100)%10+'0');
    return result;
}
long long parse_int_loose(const std::string& text){
    long long value=0;
    int sign=1;
    for(char ch:text){
        if('0'<=ch&&ch<='9') value=value*10+ch-'0';
        if(ch=='-'&&value==0&&sign==1) sign=-1;
    }
    return value*sign;
}
double parse_double_loose(const std::string& text){
    double value=0,weight=1;
    int decimal=0,sign=1;
    for(char ch:text){
        if('0'<=ch&&ch<='9'){
            if(!decimal) value=value*10+ch-'0';
            else weight/=10,value+=weight*(ch-'0');
        }
        if(ch=='-'&&value==0&&sign==1&&decimal==0) sign=-1;
        if(ch=='.') decimal=1;
    }
    return value*sign;
}
void shuffle_array(int values[],int first,int last,std::mt19937& random){
    for(int i=first;i<last;i++) std::swap(values[i],values[random()%(i-first+1)+first]);
}
void shuffle_vector(std::vector<int>& values,std::mt19937& random){
    for(int i=0;i<(int)values.size();i++) std::swap(values[i],values[random()%(i+1)]);
}
}

int main(){
    std::mt19937 rng(0x18A11u);

    assert(Util::digit_count(0)==1);
    assert(Util::digit_count(-1)==2);
    assert(Util::digit_count(std::numeric_limits<long long>::min())==20);
    assert(Compat::LegacyText::parse_int("999999999999999999999999999") == std::numeric_limits<long long>::max());
    assert(Compat::LegacyText::parse_int("-999999999999999999999999999") == std::numeric_limits<long long>::min());
    assert(Util::format_fixed_2(0)=="0.00");
    assert(Compat::LegacyText::parse_int("abc-0012x")==-12);
    assert(Compat::LegacyText::parse_real("--.1.2")==legacy::parse_double_loose("--.1.2"));

    for(int t=0;t<50000;t++){
        long long v=(static_cast<long long>(rng())<<31)^rng();
        v%=1000000000000000LL;
        if(rng()&1) v=-v;
        assert(Util::digit_count(v)==legacy::digit_count(v));

        const int whole=static_cast<int>(rng()%2000001)-1000000;
        const double d=whole+(rng()%100000)/100000.0;
        assert(Util::format_fixed_2(d)==legacy::format_fixed_2(d));

        static const std::string alphabet="0123456789-+.abcXYZ_";
        std::string text;
        const int len=rng()%16;
        for(int i=0;i<len;i++) text+=alphabet[rng()%alphabet.size()];
        // Bound digit count to avoid signed overflow in the deliberately loose parser.
        int digits=0;
        for(char c:text) digits+=('0'<=c&&c<='9');
        if(digits<=15){
            assert(Compat::LegacyText::parse_int(text)==legacy::parse_int_loose(text));
            assert(Compat::LegacyText::parse_real(text)==legacy::parse_double_loose(text));
        }
    }


    {
        const auto fields = Util::Text::split_exact<3>("1;2;3", ';');
        assert(fields && (*fields)[0] == "1" && (*fields)[1] == "2" && (*fields)[2] == "3");
        assert(!Util::Text::split_exact<3>("1;2", ';'));
        assert(!Util::Text::split_exact<3>("1;2;3;4", ';'));
    }

    for(int t=0;t<20000;t++){
        const int n=rng()%64;
        std::vector<int> a(n),b;
        for(int& x:a) x=static_cast<int>(rng());
        b=a;
        const unsigned seed=rng();
        std::mt19937 r1(seed),r2(seed);
        Util::deterministic_shuffle(a,r1);
        legacy::shuffle_vector(b,r2);
        assert(a==b);
        assert(r1()==r2());

        int aa[80]{},bb[80]{};
        const int total=1+rng()%80;
        for(int i=0;i<total;i++) aa[i]=bb[i]=static_cast<int>(rng());
        const int first=rng()%total;
        const int last=first+rng()%(total-first+1);
        const unsigned seed2=rng();
        std::mt19937 q1(seed2),q2(seed2);
        Util::deterministic_shuffle(aa,first,last,q1);
        legacy::shuffle_array(bb,first,last,q2);
        for(int i=0;i<total;i++) assert(aa[i]==bb[i]);
        assert(q1()==q2());
    }

    std::cout<<"util_equivalence_test: OK\n";
}
