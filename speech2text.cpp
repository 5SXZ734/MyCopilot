#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// whisper.cpp headers
// Assuming whisper.cpp is in ./third_party/whisper.cpp
#include "whisper.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

static std::atomic_bool g_running{true};

BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT ||
        ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_SHUTDOWN_EVENT) {
        g_running = false;
        return TRUE;
    }
    return FALSE;
}

static void die(const char* msg, HRESULT hr) {
    std::cerr << msg << " (hr=0x" << std::hex << (uint32_t)hr << std::dec << ")\n";
    std::exit(1);
}

static std::wstring get_device_friendly_name(IMMDevice* dev) {
    IPropertyStore* store = nullptr;
    HRESULT hr = dev->OpenPropertyStore(STGM_READ, &store);
    if (FAILED(hr)) return L"(unknown)";

    PROPVARIANT v;
    PropVariantInit(&v);
    hr = store->GetValue(PKEY_Device_FriendlyName, &v);
    std::wstring name = (SUCCEEDED(hr) && v.vt == VT_LPWSTR && v.pwszVal) ? v.pwszVal : L"(unknown)";
    PropVariantClear(&v);
    store->Release();
    return name;
}

struct AudioCaptureWASAPI {
    // Captured audio in PCM16 mono at 16kHz
    // We'll store it in a rolling deque of int16 samples.
    std::deque<int16_t> ring;
    std::mutex ring_mtx;

    uint32_t sample_rate = 16000;
    uint32_t channels = 1;

    // capture thread state
    std::thread th;
    std::atomic_bool started{false};

    // WASAPI objects
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* audioClient = nullptr;
    IAudioCaptureClient* captureClient = nullptr;

    WAVEFORMATEX* mixFormat = nullptr;
    WAVEFORMATEX  targetFormat{}; // PCM16 mono 16k

    HANDLE hEvent = nullptr;

    // For safety: max ring size in samples
    size_t max_ring_samples = 0;

    void init(uint32_t rate, uint32_t max_seconds) {
        sample_rate = rate;
        max_ring_samples = size_t(sample_rate) * max_seconds;

        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) die("CoInitializeEx failed", hr);

        hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
        if (FAILED(hr)) die("MMDeviceEnumerator create failed", hr);

        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        if (FAILED(hr)) die("GetDefaultAudioEndpoint failed", hr);

        std::wcout << L"Using input device: " << get_device_friendly_name(device) << L"\n";

        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&audioClient);
        if (FAILED(hr)) die("IAudioClient Activate failed", hr);

        hr = audioClient->GetMixFormat(&mixFormat);
        if (FAILED(hr)) die("GetMixFormat failed", hr);

        // Target format: PCM16 mono 16k
        targetFormat.wFormatTag = WAVE_FORMAT_PCM;
        targetFormat.nChannels = 1;
        targetFormat.nSamplesPerSec = sample_rate;
        targetFormat.wBitsPerSample = 16;
        targetFormat.nBlockAlign = (targetFormat.nChannels * targetFormat.wBitsPerSample) / 8;
        targetFormat.nAvgBytesPerSec = targetFormat.nSamplesPerSec * targetFormat.nBlockAlign;
        targetFormat.cbSize = 0;

        // We use shared mode + event-driven buffering.
        // Note: If the device doesn't support our target format in shared mode,
        // Windows will convert for us IF we initialize with mix format.
        // But we want 16k mono; easiest is: initialize with mix format and resample ourselves.
        // For MVP, we'll request mixFormat and do a simple resample/downmix to 16k mono.
        // (Most devices are 48k stereo float.)

        REFERENCE_TIME bufferDuration = 10000000; // 1s
        hEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!hEvent) die("CreateEvent failed", HRESULT_FROM_WIN32(GetLastError()));

        hr = audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            bufferDuration,
            0,
            mixFormat,
            nullptr
        );
        if (FAILED(hr)) die("AudioClient Initialize failed", hr);

        hr = audioClient->SetEventHandle(hEvent);
        if (FAILED(hr)) die("SetEventHandle failed", hr);

        hr = audioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&captureClient);
        if (FAILED(hr)) die("GetService(IAudioCaptureClient) failed", hr);
    }

    // Simple helpers for float32->int16 clamp
    static int16_t f32_to_i16(float x) {
        if (x > 1.0f) x = 1.0f;
        if (x < -1.0f) x = -1.0f;
        return (int16_t)lrintf(x * 32767.0f);
    }

    // Naive downmix+resample to 16k mono.
    // Works well enough for MVP; later you can swap in speexdsp or soxr.
    void push_converted(const uint8_t* data, uint32_t frames, const WAVEFORMATEX* fmt) {
        // Handle common case: IEEE float 32-bit, stereo, 48k
        // fmt->wFormatTag might be WAVE_FORMAT_IEEE_FLOAT or WAVE_FORMAT_EXTENSIBLE
        // We'll implement for:
        //  - 32-bit float
        //  - 16-bit PCM
        // channels: 1 or 2
        const uint32_t inRate = fmt->nSamplesPerSec;
        const uint32_t inCh   = fmt->nChannels;

        // Convert to mono float array
        std::vector<float> mono(frames);

        if (fmt->wBitsPerSample == 32) {
            const float* f = (const float*)data;
            for (uint32_t i = 0; i < frames; i++) {
                float s = 0.0f;
                if (inCh == 1) s = f[i];
                else if (inCh >= 2) s = 0.5f * (f[i*inCh + 0] + f[i*inCh + 1]);
                mono[i] = s;
            }
        } else if (fmt->wBitsPerSample == 16) {
            const int16_t* s = (const int16_t*)data;
            for (uint32_t i = 0; i < frames; i++) {
                float v = 0.0f;
                if (inCh == 1) v = s[i] / 32768.0f;
                else if (inCh >= 2) v = (0.5f * (s[i*inCh + 0] + s[i*inCh + 1])) / 32768.0f;
                mono[i] = v;
            }
        } else {
            return; // unsupported
        }

        // Resample to 16k using nearest/linear (very basic).
        // ratio = inRate / outRate. We want outRate=16k.
        const double outRate = (double)sample_rate;
        const double ratio = (double)inRate / outRate;
        const uint32_t outFrames = (uint32_t)(frames / ratio);

        std::vector<int16_t> out(outFrames);

        for (uint32_t i = 0; i < outFrames; i++) {
            double src = i * ratio;
            uint32_t i0 = (uint32_t)src;
            uint32_t i1 = (i0 + 1 < frames) ? i0 + 1 : i0;
            float t = (float)(src - i0);
            float v = (1.0f - t) * mono[i0] + t * mono[i1];
            out[i] = f32_to_i16(v);
        }

        // Append to ring
        std::lock_guard<std::mutex> lk(ring_mtx);
        for (auto s : out) ring.push_back(s);
        while (ring.size() > max_ring_samples) ring.pop_front();
    }

    void start() {
        HRESULT hr = audioClient->Start();
        if (FAILED(hr)) die("AudioClient Start failed", hr);

        started = true;
        th = std::thread([this] { this->run(); });
    }

    void stop() {
        if (!started) return;
        started = false;
        if (th.joinable()) th.join();

        if (audioClient) audioClient->Stop();

        if (captureClient) captureClient->Release();
        if (audioClient) audioClient->Release();
        if (device) device->Release();
        if (enumerator) enumerator->Release();
        if (mixFormat) CoTaskMemFree(mixFormat);
        if (hEvent) CloseHandle(hEvent);

        CoUninitialize();
    }

    void run() {
        while (g_running && started) {
            DWORD wait = WaitForSingleObject(hEvent, 200);
            if (wait != WAIT_OBJECT_0) continue;

            UINT32 packetLength = 0;
            HRESULT hr = captureClient->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) continue;

            while (packetLength != 0) {
                BYTE* pData = nullptr;
                UINT32 numFrames = 0;
                DWORD flags = 0;
                hr = captureClient->GetBuffer(&pData, &numFrames, &flags, nullptr, nullptr);
                if (FAILED(hr)) break;

                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData && numFrames > 0) {
                    push_converted(pData, numFrames, mixFormat);
                }

                captureClient->ReleaseBuffer(numFrames);
                captureClient->GetNextPacketSize(&packetLength);
            }
        }
    }

    // Snapshot last N seconds as float32 in [-1, 1] for whisper.cpp
    std::vector<float> get_last_seconds_float(uint32_t seconds) {
        const size_t need = size_t(sample_rate) * seconds;

        std::vector<int16_t> tmp;
        {
            std::lock_guard<std::mutex> lk(ring_mtx);
            const size_t have = ring.size();
            const size_t start = (have > need) ? (have - need) : 0;
            tmp.reserve(have - start);
            size_t idx = 0;
            for (auto s : ring) {
                if (idx++ >= start) tmp.push_back(s);
            }
        }

        std::vector<float> out(tmp.size());
        for (size_t i = 0; i < tmp.size(); i++) out[i] = tmp[i] / 32768.0f;
        return out;
    }
};

static std::string transcribe_whisper(whisper_context* ctx, const std::vector<float>& audio, const char* lang) {
    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);

    p.print_progress   = false;
    p.print_realtime   = false;
    p.print_timestamps = false;
    p.translate        = false;
    p.language         = lang;
    p.n_threads        = (int)std::max(2u, std::thread::hardware_concurrency() / 2);

    // VAD-ish behavior in whisper.cpp isn't the same as faster-whisper's vad_filter.
    // For MVP we just transcribe the window; later we'll add true segmentation.
    // (We can also use whisper.cpp's built-in "no_speech_threshold" tuning.)
//?    p.no_speech_threshold = 0.6f;

    if (whisper_full(ctx, p, audio.data(), (int)audio.size()) != 0) {
        return {};
    }

    const int n = whisper_full_n_segments(ctx);
    std::string text;
    for (int i = 0; i < n; i++) {
        const char* seg = whisper_full_get_segment_text(ctx, i);
        if (seg && *seg) {
            if (!text.empty()) text.push_back(' ');
            text += seg;
        }
    }

    // Trim
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) text.pop_back();
    return text;
}

int run() {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    // Use UTF-8 output (helps on some Windows terminals)
    SetConsoleOutputCP(CP_UTF8);

    const uint32_t RATE = 16000;
    const uint32_t WINDOW_S = 12;
    const uint32_t STEP_S = 3;

    // whisper model file path
    // Example: "models/ggml-base.en.bin" or a gguf model depending on your whisper.cpp version.
    const char* modelPath = "models/ggml-base.en.bin";

    // Load whisper model
    whisper_context* ctx = whisper_init_from_file(modelPath);
    if (!ctx) {
        std::cerr << "Failed to load whisper model: " << modelPath << "\n";
        std::cerr << "Put the model file under ./models/ and update modelPath.\n";
        return 1;
    }

    AudioCaptureWASAPI cap;
    cap.init(RATE, WINDOW_S);
    cap.start();

    std::cout << "Continuous STT (chunked) running.\n";
    std::cout << "Every " << STEP_S << "s it transcribes the last " << WINDOW_S << "s.\n";
    std::cout << "Stop with Ctrl+C.\n\n";

    auto last = std::chrono::steady_clock::now();

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto now = std::chrono::steady_clock::now();

        if (now - last >= std::chrono::seconds(STEP_S)) {
            last = now;

            auto audio = cap.get_last_seconds_float(WINDOW_S);
            if (audio.size() < RATE / 2) continue; // too little audio yet

            auto t0 = std::chrono::steady_clock::now();
            std::string text = transcribe_whisper(ctx, audio, "en");
            auto t1 = std::chrono::steady_clock::now();

            double dt = std::chrono::duration<double>(t1 - t0).count();
            if (!text.empty()) {
                std::cout << "[" << dt << "s] " << text << "\n";
            }
        }
    }

    std::cout << "\nStopping...\n";
    cap.stop();
    whisper_free(ctx);
    return 0;
}
