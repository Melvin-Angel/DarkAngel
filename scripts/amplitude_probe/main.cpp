#include <SparkyStudios/Audio/Amplitude/Core/Engine.h>
#include <iostream>
int main() {
    auto (*volatile factory)() = &SparkyStudios::Audio::Amplitude::Engine::GetInstance;
    std::cout << "Amplitude static CMake import/link passed; engine not initialized\n";
    return factory ? 0 : 1;
}
