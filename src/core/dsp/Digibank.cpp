#include "Digibank.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <functional>

namespace mnm::dsp {

namespace {

using cplx = std::complex<double>;

// In-place radix-2 FFT (n a power of two). inverse = true divides by n.
void fft(std::vector<cplx>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * M_PI / double(len) * (inverse ? 1 : -1);
        const cplx wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cplx w(1);
            for (size_t k = 0; k < len / 2; ++k) {
                const cplx u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= double(n);
}

size_t nextPow2(size_t n) { size_t p = 1; while (p < n) p <<= 1; return p; }

// Spectrum of one cycle (zero-padded to a power of two is not band-limited-correct for arbitrary
// lengths, so the cycle is first resampled to a power of two by linear interpolation when needed).
std::vector<cplx> spectrum(const std::vector<double>& cycle)
{
    const size_t n = nextPow2(cycle.size());
    std::vector<cplx> a(n);
    if (n == cycle.size()) for (size_t i = 0; i < n; ++i) a[i] = cycle[i];
    else for (size_t i = 0; i < n; ++i) {
        const double pos = double(i) * double(cycle.size()) / double(n);
        const size_t i0 = size_t(pos) % cycle.size(), i1 = (i0 + 1) % cycle.size();
        const double f = pos - std::floor(pos);
        a[i] = cycle[i0] * (1 - f) + cycle[i1] * f;
    }
    fft(a, false);
    for (auto& x : a) x /= double(n);   // normalised: a[k] = harmonic k amplitude (complex)
    return a;
}

// Resynthesises `size` samples from the spectrum keeping harmonics below size/2 (band-limited).
std::vector<double> resynth(const std::vector<cplx>& spec, size_t size)
{
    std::vector<cplx> a(size);
    const size_t srcN = spec.size();
    const size_t keep = std::min(size / 2, srcN / 2);   // harmonics 1..keep-1
    a[0] = 0;   // no DC: the machines' track chain would only bleed it away
    for (size_t k = 1; k < keep; ++k) { a[k] = spec[k] * double(size); a[size - k] = spec[srcN - k] * double(size); }
    fft(a, true);
    std::vector<double> out(size);
    for (size_t i = 0; i < size; ++i) out[i] = a[i].real();
    return out;
}

} // namespace

void Digibank::buildSlot(const std::vector<double>& cycle, double gain, std::array<int32_t, kSlotWords>& out)
{
    out.fill(0);
    if (cycle.size() < 8) return;
    const auto spec = spectrum(cycle);
    const auto level0 = resynth(spec, size_t(kLevel0));
    for (int l = 0; l < kLevels; ++l) {
        const auto v = l == 0 ? level0 : resynth(spec, size_t(kLevelSize[l]));
        for (int i = 0; i < kLevelSize[l]; ++i) {
            const long s = std::lround(v[size_t(i)] * gain);
            out[size_t(kLevelOffset[l] + i)] = int32_t(std::clamp(s, -8388608L, 8388607L));
        }
    }
}

std::vector<double> Digibank::waveRecord(const fw::Firmware& fw, int index)
{
    const uint32_t addr = 0x101D7B + 0x100u * uint32_t(index);
    for (const auto& r : fw.payload.records) {
        if (r.space != fw::Space::P || r.addr != addr || r.words.size() != 256) continue;
        std::vector<double> cycle(512);
        for (size_t i = 0; i < 256; ++i) {
            const uint32_t w = r.words[i];
            const int32_t hi = int32_t(int16_t((w >> 12) & 0xFFF) << 4) >> 4;   // 12-bit two's complement, high half first
            const int32_t lo = int32_t(int16_t(w & 0xFFF) << 4) >> 4;
            cycle[2 * i] = hi / 2048.0; cycle[2 * i + 1] = lo / 2048.0;
        }
        return cycle;
    }
    return {};
}

std::shared_ptr<const Digibank> Digibank::standIn(const fw::Firmware& fw)
{
    auto bank = std::make_shared<Digibank>();
    for (int i = 0; i < 32; ++i) {
        const auto cycle = waveRecord(fw, i);   // native 12-bit amplitude, so the wave is as loud as in the WAVE machine
        buildSlot(cycle, kGain, bank->slot[size_t(i)]);
        bank->name[size_t(i)] = "W" + std::to_string(i + 1);
    }
    // 32 classic shapes, band-limited by the slot builder
    struct Shape { const char* name; std::function<double(double)> f; };
    auto pulse = [](double duty) { return [duty](double x) { return x < duty ? 1.0 : -1.0; }; };
    auto additive = [](std::vector<double> amps) { return [amps](double x) { double s = 0; for (size_t h = 0; h < amps.size(); ++h) s += amps[h] * std::sin(2 * M_PI * double(h + 1) * x); return s; }; };
    const Shape shapes[32] = {
        {"SIN", [](double x) { return std::sin(2 * M_PI * x); }},
        {"TRI", [](double x) { return 1 - 4 * std::fabs(x - 0.5); }},
        {"SAW", [](double x) { return 2 * x - 1; }},
        {"SQR", pulse(0.5)},
        {"PL25", pulse(0.25)},
        {"PL12", pulse(0.125)},
        {"PL06", pulse(0.0625)},
        {"RSAW", [](double x) { return 1 - 2 * x; }},
        {"OCT2", additive({1, 0.5})},
        {"OCT3", additive({1, 0.5, 0.25})},
        {"ORGN", additive({1, 0, 0, 0.5, 0, 0, 0, 0.25})},
        {"FIFT", additive({1, 0, 0.5})},
        {"SAW4", additive({1, 0.5, 0.333, 0.25})},
        {"SAW8", additive({1, 0.5, 0.333, 0.25, 0.2, 0.167, 0.143, 0.125})},
        {"SQR4", additive({1, 0, 0.333, 0, 0.2, 0, 0.143})},
        {"CLAR", additive({1, 0, 0.111, 0, 0.04, 0, 0.02})},
        {"SIN2", [](double x) { double s = std::sin(2 * M_PI * x); return s * std::fabs(s); }},
        {"SIN3", [](double x) { double s = std::sin(2 * M_PI * x); return s * s * s; }},
        {"HALF", [](double x) { return std::max(0.0, std::sin(2 * M_PI * x)) - 1 / M_PI; }},
        {"FULL", [](double x) { return std::fabs(std::sin(2 * M_PI * x)) - 2 / M_PI; }},
        {"TRSW", [](double x) { return x < 0.25 ? 4 * x : x < 0.75 ? 1 - 4 * (x - 0.25) * 0.5 - 0.0 : -1 + 4 * (x - 0.75); }},
        {"STEP", [](double x) { return std::floor(x * 8) / 3.5 - 1; }},
        {"ST16", [](double x) { return std::floor(x * 16) / 7.5 - 1; }},
        {"SINF", [](double x) { return std::sin(2 * M_PI * x + 0.9 * std::sin(2 * M_PI * x)); }},
        {"SINF2", [](double x) { return std::sin(2 * M_PI * x + 2.0 * std::sin(4 * M_PI * x)); }},
        {"SINF3", [](double x) { return std::sin(2 * M_PI * x + 1.5 * std::sin(6 * M_PI * x)); }},
        {"BELL", additive({1, 0, 0, 0.6, 0, 0, 0, 0, 0, 0.4, 0, 0, 0, 0, 0, 0.25})},
        {"NASL", additive({0.6, 1, 0.8, 0.3, 0.5, 0.2, 0.3})},
        {"BUZZ", additive({0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3})},
        {"HPLS", [](double x) { return x < 0.03 ? 1.0 : -0.03 / 0.97; }},
        {"RAMP", [](double x) { return x < 0.5 ? 4 * x - 1 : 3 - 4 * x; }},
        {"NOIZ", [](double x) { uint32_t s = uint32_t(x * 1024) * 2654435761u; s ^= s >> 13; s *= 2246822519u; s ^= s >> 16; return double(int32_t(s)) / 2147483648.0; }},
    };
    for (int i = 0; i < 32; ++i) {
        std::vector<double> cycle(1024);
        for (int k = 0; k < 1024; ++k) cycle[size_t(k)] = shapes[i].f(double(k) / 1024.0);
        buildSlot(cycle, kGain, bank->slot[size_t(32 + i)]);
        bank->name[size_t(32 + i)] = shapes[i].name;
    }
    return bank;
}

} // namespace mnm::dsp
