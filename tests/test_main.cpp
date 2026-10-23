#include <cstring>
#include <exception>
#include <iostream>

struct TestCase {const char* name;int (*run)(int,char**);};
#include TETR_TEST_REGISTRY

int main(int argc,char** argv) {
    if(argc<2 || std::strcmp(argv[1],"--list")==0) {
        for(const auto& test:testCases)std::cout<<test.name<<'\n';
        return argc<2?2:0;
    }
    for(const auto& test:testCases)if(std::strcmp(argv[1],test.name)==0) {
        try {return test.run(argc-1,argv+1);}
        catch(const std::exception& error) {std::cerr<<test.name<<": "<<error.what()<<'\n';return 1;}
    }
    std::cerr<<"Unknown test case: "<<argv[1]<<'\n';return 2;
}
