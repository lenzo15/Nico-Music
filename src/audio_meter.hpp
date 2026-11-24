#pragma once
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <wrl.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// Read-only process loopback. Never open a microphone or the system mix.
class AudioMeter {
    template<class T> using Ptr = Microsoft::WRL::ComPtr<T>;
    struct Completion final : Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
            IActivateAudioInterfaceCompletionHandler, Microsoft::WRL::FtmBase> {
        std::atomic<bool> done{false};
        STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation*) override { done = true; return S_OK; }
    };
    Ptr<Completion> completion;
    Ptr<IActivateAudioInterfaceAsyncOperation> op;
    Ptr<IAudioClient> client;
    Ptr<IAudioCaptureClient> capture;
    std::array<double, 2> low{}, body{};
    bool open() {
        HRESULT hr{}; Ptr<IUnknown> device;
        if (FAILED(op->GetActivateResult(&hr, &device)) || FAILED(hr) || FAILED(device.As(&client))) return false;
        WAVEFORMATEX fmt{WAVE_FORMAT_PCM, 2, 48000, 192000, 4, 16, 0};
        return SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK |
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 2000000, 0, &fmt, nullptr)) &&
            SUCCEEDED(client->GetService(IID_PPV_ARGS(&capture))) && SUCCEEDED(client->Start());
    }
public:
    bool start() {
        AUDIOCLIENT_ACTIVATION_PARAMS params{};
        params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        params.ProcessLoopbackParams = {GetCurrentProcessId(), PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE};
        PROPVARIANT v{}; v.vt = VT_BLOB; v.blob = {sizeof(params), reinterpret_cast<BYTE*>(&params)};
        completion = Microsoft::WRL::Make<Completion>();
        return completion && SUCCEEDED(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
            __uuidof(IAudioClient), &v, completion.Get(), &op));
    }
    ~AudioMeter() { if (client) client->Stop(); }
    enum class Result { waiting, fresh, failed };
    Result poll(std::array<int, 3>& levels) {
        if (!capture) {
            if (!completion->done) return Result::waiting;
            if (!open()) return Result::failed;
        }
        std::array<double, 3> energy{}; unsigned samples = 0;
        for (;;) {
            UINT32 frames{}; DWORD flags{}; BYTE* data{};
            HRESULT hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
            if (FAILED(hr)) return Result::failed;
            if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) { low.fill(0); body.fill(0); }
            auto pcm = reinterpret_cast<const short*>(data);
            for (UINT32 i = 0; i < frames; ++i) for (int ch = 0; ch < 2; ++ch) {
                const double x = flags & AUDCLNT_BUFFERFLAGS_SILENT ? 0. : pcm[2*i+ch]/32768.;
                // One-pole crossovers at 180 Hz and 3.2 kHz (48 kHz PCM).
                low[ch] += .02329 * (x-low[ch]); body[ch] += .34222 * (x-body[ch]);
                const double mid = body[ch]-low[ch], high = x-body[ch];
                energy[0] += low[ch]*low[ch]; energy[1] += mid*mid; energy[2] += high*high;
            }
            samples += 2*frames;
            if (FAILED(capture->ReleaseBuffer(frames))) return Result::failed;
        }
        if (!samples) return Result::waiting;
        for (int i = 0; i < 3; ++i) {
            const double db = 10 * std::log10(1e-12 + energy[i]/samples);
            levels[i] = int(100 * std::clamp((db + 68 + 5*i)/62., 0., 1.));
        }
        return Result::fresh;
    }
};
