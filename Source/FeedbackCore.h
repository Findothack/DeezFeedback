#pragma once
// FeedbackCore.h - framework-free feedback detector + automatic notch filters.
// Mono. One instance per audio channel. No allocations after prepare().
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace fbs
{
constexpr double kPi = 3.14159265358979323846;

struct Params
{
    bool  bypass     = false;
    bool  limiter    = true;
    float pnpr       = 18.0f;   // dB a peak must stand above its neighbours
    float step       = 5.0f;    // dB of extra cut per detection
    float maxDepth   = -24.0f;  // deepest cut in dB (negative)
    float q          = 25.0f;
    float holdSec    = 30.0f;   // seconds before a notch starts releasing
    int   maxNotches = 12;
    float floorDb    = -75.0f;
};

class FFT
{
public:
    static constexpr int N = 2048;
    FFT()
    {
        for (int i = 0; i < N / 2; ++i)
        {
            cosT[i] = std::cos(2.0 * kPi * i / N);
            sinT[i] = -std::sin(2.0 * kPi * i / N);
        }
        for (int i = 0; i < N; ++i)
        {
            int r = 0;
            for (int b = 0; b < 11; ++b)
                if (i & (1 << b)) r |= 1 << (10 - b);
            rev[i] = r;
        }
    }
    void transform(float* re, float* im) const
    {
        for (int i = 0; i < N; ++i)
        {
            const int j = rev[i];
            if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
        }
        for (int size = 2; size <= N; size <<= 1)
        {
            const int half = size >> 1, step = N / size;
            for (int start = 0; start < N; start += size)
                for (int k = 0, t = 0; k < half; ++k, t += step)
                {
                    const float c = cosT[t], s = sinT[t];
                    const int a = start + k, b = a + half;
                    const float xr = re[b] * c - im[b] * s;
                    const float xi = re[b] * s + im[b] * c;
                    re[b] = re[a] - xr; im[b] = im[a] - xi;
                    re[a] += xr;        im[a] += xi;
                }
        }
    }
private:
    float cosT[N / 2], sinT[N / 2];
    int   rev[N];
};

class Core
{
public:
    static constexpr int N = 2048, HOP = 512, HALF = 1024, MAXN = 24;

    void prepare(double sampleRate)
    {
        fs = (float) sampleRate;
        binHz = fs / (float) N;
        for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * std::cos(2.0f * (float) kPi * i / N);
        reset();
    }

    void reset()
    {
        std::memset(ring, 0, sizeof ring);
        rp = 0; since = 0; nTracks = 0; hIdx = 0; nFrames = 0;
        for (auto& h : pwHist) std::memset(h, 0, sizeof h);
        std::memset(dispMag, 0, sizeof dispMag);
        for (auto& n : notches) n = Notch{};
        publishNotches();
    }

    void clearNotches()
    {
        for (auto& n : notches) n = Notch{};
        nTracks = 0;
        publishNotches();
    }

    void setParams(const Params& np)
    {
        if (np.q != p.q) for (auto& n : notches) n.dirty = true;
        p = np;
        p.maxNotches = std::max(1, std::min(MAXN, p.maxNotches));
    }

    // in and out may be the same pointer.
    void process(const float* in, float* out, int n)
    {
        Notch* act[MAXN];
        const int na = prepareNotches(n, act);
        for (int i = 0; i < n; ++i)
        {
            const float s = in[i];
            ring[rp] = s;
            rp = (rp + 1) & (N - 1);
            if (++since >= HOP) { since = 0; analyze(); }

            float y = s;
            if (!p.bypass)
                for (int j = 0; j < na; ++j)
                {
                    Notch& nt = *act[j];
                    const float o = nt.b0 * y + nt.z1;
                    nt.z1 = nt.b1 * y - nt.a1 * o + nt.z2;
                    nt.z2 = nt.b2 * y - nt.a2 * o;
                    y = o;
                }
            if (!(y > -1e6f && y < 1e6f))
            {
                y = 0.0f;
                for (auto& nt : notches) nt.z1 = nt.z2 = 0.0f;
            }
            if (p.limiter && !p.bypass)
            {
                const float ay = std::fabs(y);
                if (ay > 0.9f) y = std::copysign(0.9f + 0.1f * std::tanh((ay - 0.9f) / 0.1f), y);
            }
            out[i] = y;
        }
        publishNotches();
    }

    // ---- read-only display data (read from the GUI thread; benign races on floats)
    const float* spectrumDb() const { return dispMag; }
    float sampleRate() const { return fs; }
    int   numDisplayNotches() const { return MAXN; }
    bool  displayNotch(int i, float& f, float& depth) const
    {
        if (!dActive[i].load(std::memory_order_relaxed)) return false;
        f = dF[i].load(std::memory_order_relaxed);
        depth = dD[i].load(std::memory_order_relaxed);
        return true;
    }

private:
    struct Notch
    {
        bool  active = false, dirty = true;
        float f = 1000, depth = 0, target = 0, hold = 0;
        float z1 = 0, z2 = 0, b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        void setCoefs(float fs, float q)
        {
            const float A  = std::pow(10.0f, depth / 40.0f);
            const float w  = 2.0f * (float) kPi * f / fs;
            const float cw = std::cos(w), al = std::sin(w) / (2.0f * q);
            const float a0 = 1.0f + al / A;
            b0 = (1.0f + al * A) / a0; b1 = -2.0f * cw / a0; b2 = (1.0f - al * A) / a0;
            a1 = -2.0f * cw / a0;      a2 = (1.0f - al / A) / a0;
            dirty = false;
        }
    };
    struct Track { int k = 0, count = 0, hn = 0; float pn = 0, hist[24]; };
    struct Peak  { int k; float level, pn; };

    int prepareNotches(int n, Notch** act)
    {
        const float dt = (float) n / fs;
        const float att = 6.0f * (float) n / 128.0f;   // dB per block when deepening
        int na = 0;
        for (auto& nt : notches)
        {
            if (!nt.active) continue;
            if (nt.hold > 0.0f) nt.hold -= dt;
            else
            {
                nt.target += dt;                        // release at 1 dB per second
                if (nt.target > -0.3f) { nt = Notch{}; continue; }
            }
            float d = nt.depth;
            if (d > nt.target) d = std::max(nt.target, d - att); else d = nt.target;
            if (d != nt.depth) { nt.depth = d; nt.dirty = true; }
            if (nt.dirty) nt.setCoefs(fs, p.q);
            act[na++] = &nt;
        }
        return na;
    }

    void publishNotches()
    {
        for (int i = 0; i < MAXN; ++i)
        {
            dActive[i].store(notches[i].active, std::memory_order_relaxed);
            dF[i].store(notches[i].f, std::memory_order_relaxed);
            dD[i].store(notches[i].depth, std::memory_order_relaxed);
        }
    }

    void analyze()
    {
        for (int i = 0; i < N; ++i) { re[i] = ring[(rp + i) & (N - 1)] * win[i]; im[i] = 0.0f; }
        fft.transform(re, im);
        const float sc2 = 1.0f / ((N / 4.0f) * (N / 4.0f));
        for (int k = 0; k <= HALF; ++k)
        {
            const float pwr = (re[k] * re[k] + im[k] * im[k]) * sc2;
            pw[k] = pwr;
            mag[k] = 10.0f * std::log10(pwr + 1e-12f);
        }
        std::memcpy(pwHist[hIdx], pw, sizeof pw);
        hIdx = (hIdx + 1) % 6;
        ++nFrames;
        std::memcpy(dispMag, mag, sizeof mag);

        const int lo = std::max(3, (int) std::ceil(50.0f / binHz));
        const int hi = std::min(HALF - 2, (int) std::floor(16000.0f / binHz));
        Peak peaks[11]; int np = 0;
        for (int k = lo; k <= hi; ++k)
        {
            const float m = mag[k];
            if (m < p.floorDb || m <= mag[k - 1] || m < mag[k + 1]) continue;
            float s = 0; int c = 0;
            for (int d = 4; d <= 10; ++d)
            {
                const int a = k - d, b = k + d;
                if (a >= 1)   { s += pw[a]; ++c; }
                if (b <= HALF){ s += pw[b]; ++c; }
            }
            const float pn = m - 10.0f * std::log10(s / (float) c + 1e-12f);
            if (pn >= p.pnpr)
            {
                if (np >= 10) { np = 11; break; }
                peaks[np++] = { k, m, pn };
            }
        }
        if (np > 10) { nTracks = 0; return; }   // dense / broadband content, not feedback

        Track prev[16]; const int nPrev = nTracks;
        std::memcpy(prev, tracks, sizeof(Track) * (size_t) nPrev);
        bool used[16] = {};
        nTracks = 0;
        for (int i = 0; i < np; ++i)
        {
            int bi = -1, bd = 99;
            for (int j = 0; j < nPrev; ++j)
            {
                if (used[j]) continue;
                const int d = std::abs(prev[j].k - peaks[i].k);
                if (d <= 1 && d < bd) { bd = d; bi = j; }
            }
            Track t;
            if (bi >= 0)
            {
                used[bi] = true; t = prev[bi];
                t.k = peaks[i].k; ++t.count; t.pn = peaks[i].pn;
                if (t.hn == 24) { std::memmove(t.hist, t.hist + 1, 23 * sizeof(float)); t.hn = 23; }
                t.hist[t.hn++] = peaks[i].level;
            }
            else { t.k = peaks[i].k; t.count = 1; t.hn = 1; t.hist[0] = peaks[i].level; t.pn = peaks[i].pn; }
            tracks[nTracks++] = t;
        }
        if (p.bypass) return;

        constexpr int M = 4;
        constexpr float growth = 0.4f;
        for (int i = 0; i < nTracks; ++i)
        {
            Track& t = tracks[i];
            const int n = t.hn;
            bool fire = false;
            if (t.count >= M && n >= M && nFrames >= M)
            {
                const float slope = (t.hist[n - 1] - t.hist[n - M]) / (float) (M - 1);
                if (slope >= growth && slope - restSlope(t.k, M) >= growth) fire = true;
            }
            if (!fire && t.count >= 40 && t.pn >= p.pnpr + 10.0f) fire = true;
            if (fire && isHarmonic(t.k, t.hist[n - 1])) fire = false;
            if (fire)
            {
                trigger(t.k);
                const float last = t.hist[n - 1];
                t.count = 0; t.hn = 1; t.hist[0] = last;
            }
        }
    }

    float restSlope(int k, int M) const
    {
        const float* cur = pwHist[(hIdx - 1 + 12) % 6];
        const float* old = pwHist[(hIdx - 1 - (M - 1) + 12) % 6];
        const int hi = std::min(HALF, (int) std::floor(16000.0f / binHz));
        float a = 0, b = 0;
        for (int j = 3; j <= hi; ++j)
        {
            if (j >= k - 3 && j <= k + 3) continue;
            a += cur[j]; b += old[j];
        }
        return 10.0f * std::log10((a + 1e-12f) / (b + 1e-12f)) / (float) (M - 1);
    }

    bool isHarmonic(int k, float level) const
    {
        auto chk = [&](float c0)
        {
            const int c = (int) std::lround(c0);
            if (c < 6 || c > HALF - 6) return false;
            float m = -200.0f;
            for (int j = c - 1; j <= c + 1; ++j) m = std::max(m, mag[j]);
            if (m < level - 12.0f) return false;
            return m > std::max(mag[c - 5], mag[c + 5]) + 6.0f;
        };
        return chk(2.0f * (float) k) || chk((float) k * 0.5f);
    }

    void trigger(int k)
    {
        const float a = mag[k - 1], b = mag[k], c = mag[k + 1];
        const float den = a - 2.0f * b + c;
        float d = den < 0.0f ? 0.5f * (a - c) / den : 0.0f;
        d = std::max(-0.5f, std::min(0.5f, d));
        addNotch(((float) k + d) * binHz);
    }

    void addNotch(float f)
    {
        const float tol = std::max(1.5f * binHz, 0.012f * f);
        int count = 0; Notch* best = nullptr;
        for (auto& n : notches)
        {
            if (!n.active) continue;
            ++count;
            if (std::fabs(n.f - f) <= tol && (!best || std::fabs(n.f - f) < std::fabs(best->f - f))) best = &n;
        }
        if (best)
        {
            best->f = 0.5f * (best->f + f);
            best->target = std::max(p.maxDepth, best->target - p.step);
            best->hold = p.holdSec; best->dirty = true;
            return;
        }
        Notch* slot = nullptr;
        if (count < p.maxNotches) for (auto& n : notches) if (!n.active) { slot = &n; break; }
        if (!slot)
            for (auto& n : notches)
            {
                if (!n.active) continue;
                if (!slot || std::fabs(n.target) < std::fabs(slot->target)) slot = &n;
            }
        if (!slot) return;
        *slot = Notch{};
        slot->active = true; slot->f = f;
        slot->target = std::max(p.maxDepth, -p.step * 1.5f);
        slot->hold = p.holdSec;
    }

    Params p;
    float fs = 48000.0f, binHz = 48000.0f / N;
    FFT   fft;
    float win[N], ring[N], re[N], im[N], mag[HALF + 1], pw[HALF + 1], dispMag[HALF + 1];
    float pwHist[6][HALF + 1];
    int   rp = 0, since = 0, hIdx = 0, nFrames = 0;
    Track tracks[16]; int nTracks = 0;
    Notch notches[MAXN];
    std::atomic<bool>  dActive[MAXN];
    std::atomic<float> dF[MAXN], dD[MAXN];
};
} // namespace fbs
