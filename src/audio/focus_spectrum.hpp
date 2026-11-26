#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

// 48 kHz process-loopback PCM. Pack L/R into one complex FFT; summing the
// power of +/-k recovers stereo energy without cancelling opposite-phase audio.
class FocusSpectrum {
    static constexpr int N = 4096, bands = 128;
    using C = std::complex<float>;
    std::array<C,N> pcm{}, work{};
    std::array<C,N/2> twiddle{};
    std::array<float,N> window{};
    std::array<int,N> reversed{};
    std::array<int,bands+1> edges{};
    int next = 0;
public:
    FocusSpectrum() {
        constexpr double tau = 6.283185307179586;
        for (int i=0; i<N; ++i) {
            window[i] = float(.5 - .5*std::cos(tau*i/N));
            reversed[i] = (reversed[i/2] >> 1) | ((i & 1) * (N/2));
            if (i<N/2) twiddle[i] = std::polar(1.f, float(-tau*i/N));
        }
        // Soft-log axis: each low band gets at least one FFT bin; treble
        // spans progressively wider intervals. 35 Hz to about 18 kHz.
        for (int i=0; i<=bands; ++i)
            edges[i] = 3 + int(std::lround(40*std::expm1(std::log1p(1536./40)*i/bands)));
    }
    void reset() { pcm.fill({}); next = 0; }
    void push(float left, float right) { pcm[next] = {left,right}; next = (next+1) % N; }
    std::array<int,bands> read() {
        for (int i=0; i<N; ++i) work[reversed[i]] = pcm[(next+i)%N] * window[i];
        for (int length=2; length<=N; length*=2) {
            const int half = length/2;
            for (int base=0; base<N; base+=length) for (int j=0; j<half; ++j) {
                const C a=work[base+j], b=work[base+j+half]*twiddle[j*(N/length)];
                work[base+j]=a+b; work[base+j+half]=a-b;
            }
        }
        std::array<int,bands> out{};
        for (int i=0; i<bands; ++i) {
            float peak = 0;
            for (int k=edges[i]; k<edges[i+1]; ++k)
                peak = std::max(peak, std::norm(work[k])+std::norm(work[N-k]));
            const double db = 10*std::log10(1e-12 + peak * (4./N/N));
            out[i] = int(std::lround(100 * std::clamp((db+64)/62, 0., 1.)));
        }
        return out;
    }
};
