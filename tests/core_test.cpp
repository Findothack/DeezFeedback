#define _USE_MATH_DEFINES
// Native test for FeedbackCore.h: g++ -O2 -std=c++17 -I../Source core_test.cpp -o core_test
#include "FeedbackCore.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <vector>

static const double FS = 48000.0;
static const int B = 128;

static double frand() { return 2.0 * (std::rand() / (double) RAND_MAX) - 1.0; }

struct Voice
{
    double ph[12] = {}, last = 0;
    std::function<double(double)> f0;
    double operator()(double t)
    {
        const double dt = t - last; last = t;
        const double fz = f0(t);
        double s = 0;
        for (int h = 1; h <= 12; ++h) { ph[h - 1] += 2 * M_PI * fz * h * dt; s += std::sin(ph[h - 1]) / h; }
        return 0.12 * s * std::sqrt(std::max(0.0, std::sin(2 * M_PI * 1.3 * t))) + 0.002 * frand();
    }
};

struct Room
{
    struct BP { float k, b0, b2, a1, a2, z1 = 0, z2 = 0; } r[3];
    std::vector<float> dly; int dw = 0, D;
    Room() : dly(8192, 0.0f), D((int) std::max(256.0, std::round(0.006 * FS)))
    {
        const double f[3] = { 620, 1180, 3140 }, k[3] = { 1.10, 1.30, 1.15 };
        for (int i = 0; i < 3; ++i)
        {
            const double w = 2 * M_PI * f[i] / FS, al = std::sin(w) / 80.0, a0 = 1 + al;
            r[i] = { (float) k[i], (float) (al / a0), (float) (-al / a0), (float) (-2 * std::cos(w) / a0), (float) ((1 - al) / a0) };
        }
    }
    float read() const { return dly[(dw - D + (int) dly.size()) % (int) dly.size()]; }
    void write(float y) { dly[dw] = y; dw = (dw + 1) % (int) dly.size(); }
    float filt(float x)
    {
        float sum = 0.12f * x;
        for (auto& b : r)
        {
            const float o = b.b0 * x + b.z1;
            b.z1 = -b.a1 * o + b.z2;
            b.z2 = b.b2 * x - b.a2 * o;
            sum += b.k * o;
        }
        return sum;
    }
};

static void report(const char* name, fbs::Core& c, double firstNotch)
{
    std::printf("%s: first notch %s", name, firstNotch < 0 ? "none" : "");
    if (firstNotch >= 0) std::printf("%.2fs", firstNotch);
    std::printf(" | active:");
    for (int i = 0; i < fbs::Core::MAXN; ++i)
    {
        float f, d;
        if (c.displayNotch(i, f, d)) std::printf(" %.0fHz/%.1fdB", f, d);
    }
    std::printf("\n");
}

static double runSignal(fbs::Core& c, double seconds, const std::function<double(double)>& fn)
{
    float in[B], out[B]; double first = -1, t = 0;
    const int blocks = (int) (seconds * FS / B);
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < B; ++i) { in[i] = (float) fn(t); t += 1.0 / FS; }
        c.process(in, out, B);
        if (first < 0) { float f, d; for (int i = 0; i < fbs::Core::MAXN; ++i) if (c.displayNotch(i, f, d)) { first = b * B / FS; break; } }
    }
    return first;
}

int main()
{
    std::srand(1);
    fbs::Params p;

    // T1: simulated room with ringing, processing on vs bypass
    for (int bypass = 1; bypass >= 0; --bypass)
    {
        auto* c = new fbs::Core; fbs::Params q = p; q.bypass = bypass; c->prepare(FS); c->setParams(q);
        Room room; Voice v; v.f0 = [](double t) { return 120 + 25 * std::sin(2 * M_PI * 0.45 * t); };
        float in[B], out[B]; double t = 0, first = -1, acc = 0; long n = 0;
        const int blocks = (int) (20 * FS / B);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < B; ++i)
            {
                in[i] = (float) v(t) + room.filt(room.read());   // read() is >=256 samples old, so block-safe
                t += 1.0 / FS;
                room.write(0.0f);                                  // placeholder, overwritten below
            }
            c->process(in, out, B);
            // write outputs into the delay line at the positions just advanced
            for (int i = 0; i < B; ++i) room.dly[(room.dw - B + i + (int) room.dly.size()) % (int) room.dly.size()] = out[i];
            if (b * B / FS >= 15) for (int i = 0; i < B; ++i) { acc += out[i] * out[i]; ++n; }
            if (first < 0) { float f, d; for (int i = 0; i < fbs::Core::MAXN; ++i) if (c->displayNotch(i, f, d)) { first = b * B / FS; break; } }
        }
        std::printf("T1 loop (%s): RMS of last 5 s = %.1f dB\n", bypass ? "bypass, no limiter: runs away" : "active", 20 * std::log10(std::sqrt(acc / n) + 1e-9));
        if (!bypass) report("   ", *c, first);
        delete c;
    }

    // T2: growing 2 kHz tone
    { auto* c = new fbs::Core; c->prepare(FS); c->setParams(p);
      double f = runSignal(*c, 3, [](double t) { return std::min(0.0005 * std::pow(10.0, t * 80 / 20), 0.5) * std::sin(2 * M_PI * 2000 * t) + 0.0005 * frand(); });
      report("T2 growing 2 kHz tone", *c, f); delete c; }

    // T3: voices only (must stay silent)
    { auto* c = new fbs::Core; c->prepare(FS); c->setParams(p); Voice v; v.f0 = [](double t) { return 120 + 25 * std::sin(2 * M_PI * 0.45 * t); };
      double f = runSignal(*c, 60, std::ref(v)); report("T3 gliding voice 60 s", *c, f); delete c; }
    { auto* c = new fbs::Core; c->prepare(FS); c->setParams(p); Voice v; v.f0 = [](double) { return 140.0; };
      double f = runSignal(*c, 60, std::ref(v)); report("T3b steady voice 60 s", *c, f); delete c; }

    // T5: voice plus real feedback starting at 3 s
    { auto* c = new fbs::Core; c->prepare(FS); c->setParams(p); Voice v; v.f0 = [](double t) { return 120 + 25 * std::sin(2 * M_PI * 0.45 * t); };
      double f = runSignal(*c, 8, [&](double t) { return v(t) + (t > 3 ? std::min(0.001 * std::pow(10.0, (t - 3) * 60 / 20), 0.3) * std::sin(2 * M_PI * 1800 * t) : 0.0); });
      report("T5 voice + feedback at 3 s", *c, f); delete c; }

    // T6: different sample rates / block sizes should not crash
    for (double sr : { 44100.0, 96000.0 })
    { auto* c = new fbs::Core; c->prepare(sr); c->setParams(p); std::vector<float> in(480), out(480);
      for (int b = 0; b < 2000; ++b) { for (auto& s : in) s = (float) (0.1 * frand()); c->process(in.data(), out.data(), 480); }
      std::printf("T6 %.0f Hz, 480-sample blocks on noise: ok\n", sr); delete c; }
    return 0;
}
