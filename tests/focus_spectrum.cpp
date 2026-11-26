// c++ -std=c++17 -O2 tests/focus_spectrum.cpp -o spectrum-test && ./spectrum-test
#include "../src/focus_spectrum.hpp"
#include <cassert>
#include <cstdio>

int main() {
    FocusSpectrum fft;
    auto tone = [&](double hz, float gain, bool opposite) {
        fft.reset();
        for (int i = 0; i < 6000; ++i) {
            float v = gain * float(std::sin(i * hz * 6.283185307179586 / 48000));
            fft.push(v, opposite ? -v : v);
        }
        return fft.read();
    };
    int previous = -1;
    for (double hz : {60., 211., 832., 1043., 1242., 2086., 4184., 12000.}) {
        auto a = tone(hz, .3f, false), b = tone(hz, .3f, true), quiet = tone(hz, .03f, false);
        auto peak = std::max_element(a.begin(), a.end());
        int band = int(peak - a.begin());
        assert(band > previous && *peak > 75 && *peak - quiet[band] >= 30);
        assert(std::abs(*peak-b[band]) <= 1);
        assert(std::count_if(a.begin(), a.end(), [&](int x){ return x > *peak-12; }) <= 4);
        previous = band;
    }
    fft.reset(); assert((fft.read() == std::array<int,128>{}));
    std::puts("PASS: frequency localization, stereo phase, amplitude and silence");
}
