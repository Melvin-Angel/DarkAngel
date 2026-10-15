#include <exception>
#include <iostream>
void DAE_NATIVE_SUITE();
int main(){try{DAE_NATIVE_SUITE();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
