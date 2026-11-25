// Generated from src/indicators.h by tools/embed_indicators.sh.
// Do not edit; run 'make' to regenerate.
#pragma once
static const char kIndicatorsHeader[] = R"FDINDICATORS7f3a(
// indicators.h — 250 technical indicator submodules for the FDesigner
// StockChart widget, in ONE self-contained header.
//
// Every indicator is a named module in the registry below: it consumes
// Massive-format OHLCV bars (the same data StockChart loads from CSV),
// computes one or more series, and plots itself onto the chart's canvas —
// as a price-pane overlay, in its own oscillator pane, or as markers.
// Indicators have no palette button of their own; a StockChart enables
// them by name, e.g. add_indicator("SMA(20)") or the designer's
// "indicators" property: "SMA(20), EMA(50), RSI, Bollinger Bands".
//
// Shared math primitives (rolling SMA/EMA/stdev/ATR...) live at the top;
// each indicator's own formula is its own function further down.

#pragma once

#include <gtk/gtk.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fdi {

// One OHLCV bar (Massive aggregates: open/high/low/close/volume,
// window_start in ns).
struct Bar {
    double open = 0, high = 0, low = 0, close = 0, volume = 0;
    long long time = 0;
};

using Series = std::vector<double>;
using Lines = std::vector<Series>;

inline double nanv() { return std::numeric_limits<double>::quiet_NaN(); }
inline bool fin(double v) { return std::isfinite(v); }

// ------------------------------------------------------------- fields

inline Series field(const std::vector<Bar>& b, double Bar::* member) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i) s[i] = b[i].*member;
    return s;
}
inline Series closes(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i) s[i] = b[i].close;
    return s;
}
inline Series highs(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i) s[i] = b[i].high;
    return s;
}
inline Series lows(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i) s[i] = b[i].low;
    return s;
}
inline Series opens(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i) s[i] = b[i].open;
    return s;
}
inline Series vols(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i) s[i] = b[i].volume;
    return s;
}
inline Series hl2(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i) s[i] = (b[i].high + b[i].low) / 2;
    return s;
}
inline Series hlc3(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i)
        s[i] = (b[i].high + b[i].low + b[i].close) / 3;
    return s;
}
inline Series ohlc4(const std::vector<Bar>& b) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i)
        s[i] = (b[i].open + b[i].high + b[i].low + b[i].close) / 4;
    return s;
}
inline Series true_range(const std::vector<Bar>& b) {
    Series s(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i) {
        double tr = b[i].high - b[i].low;
        if (i > 0) {
            tr = std::max(tr, std::fabs(b[i].high - b[i - 1].close));
            tr = std::max(tr, std::fabs(b[i].low - b[i - 1].close));
        }
        s[i] = tr;
    }
    return s;
}

// ------------------------------------------------------------- rolling

inline Series sma_s(const Series& in, int n) {
    Series out(in.size(), nanv());
    if (n < 1) return out;
    double sum = 0;
    int cnt = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        if (fin(in[i])) {
            sum += in[i];
            ++cnt;
        }
        if (i >= (size_t)n && fin(in[i - n])) {
            sum -= in[i - n];
            --cnt;
        }
        if (cnt == n) out[i] = sum / n;
    }
    return out;
}

inline Series ema_s(const Series& in, int n) {
    Series out(in.size(), nanv());
    if (n < 1) return out;
    double k = 2.0 / (n + 1), e = 0;
    bool started = false;
    for (size_t i = 0; i < in.size(); ++i) {
        if (!fin(in[i])) continue;
        if (!started) {
            e = in[i];
            started = true;
        } else {
            e = in[i] * k + e * (1 - k);
        }
        if (i >= (size_t)(n - 1)) out[i] = e;
    }
    return out;
}

// Wilder's smoothing (SMMA / RMA).
inline Series smma_s(const Series& in, int n) {
    Series out(in.size(), nanv());
    if (n < 1) return out;
    double e = 0;
    bool started = false;
    for (size_t i = 0; i < in.size(); ++i) {
        if (!fin(in[i])) continue;
        if (!started) {
            e = in[i];
            started = true;
        } else {
            e = (e * (n - 1) + in[i]) / n;
        }
        if (i >= (size_t)(n - 1)) out[i] = e;
    }
    return out;
}

inline Series wma_s(const Series& in, int n) {
    Series out(in.size(), nanv());
    if (n < 1) return out;
    double denom = n * (n + 1) / 2.0;
    for (size_t i = (size_t)(n - 1); i < in.size(); ++i) {
        double sum = 0;
        bool ok = true;
        for (int j = 0; j < n; ++j) {
            double v = in[i - j];
            if (!fin(v)) {
                ok = false;
                break;
            }
            sum += v * (n - j);
        }
        if (ok) out[i] = sum / denom;
    }
    return out;
}

inline Series rolling_max(const Series& in, int n) {
    Series out(in.size(), nanv());
    for (size_t i = 0; i < in.size(); ++i) {
        if (i + 1 < (size_t)n) continue;
        double m = -1e300;
        bool ok = true;
        for (int j = 0; j < n; ++j) {
            if (!fin(in[i - j])) ok = false;
            m = std::max(m, in[i - j]);
        }
        if (ok) out[i] = m;
    }
    return out;
}

inline Series rolling_min(const Series& in, int n) {
    Series out(in.size(), nanv());
    for (size_t i = 0; i < in.size(); ++i) {
        if (i + 1 < (size_t)n) continue;
        double m = 1e300;
        bool ok = true;
        for (int j = 0; j < n; ++j) {
            if (!fin(in[i - j])) ok = false;
            m = std::min(m, in[i - j]);
        }
        if (ok) out[i] = m;
    }
    return out;
}

inline Series rolling_sum(const Series& in, int n) {
    Series out(in.size(), nanv());
    double sum = 0;
    int cnt = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        if (fin(in[i])) {
            sum += in[i];
            ++cnt;
        }
        if (i >= (size_t)n && fin(in[i - n])) {
            sum -= in[i - n];
            --cnt;
        }
        if (cnt == n) out[i] = sum;
    }
    return out;
}

inline Series stdev_s(const Series& in, int n, bool sample = false) {
    Series out(in.size(), nanv());
    Series mean = sma_s(in, n);
    for (size_t i = 0; i < in.size(); ++i) {
        if (!fin(mean[i])) continue;
        double ss = 0;
        for (int j = 0; j < n; ++j) {
            double d = in[i - j] - mean[i];
            ss += d * d;
        }
        out[i] = std::sqrt(ss / (sample ? n - 1 : n));
    }
    return out;
}

inline Series atr_s(const std::vector<Bar>& b, int n) {
    return smma_s(true_range(b), n);
}

// RSI on a price series (Wilder).
inline Series rsi_s(const Series& in, int n) {
    Series out(in.size(), nanv());
    if (n < 1 || in.size() < 2) return out;
    double gain = 0, loss = 0;
    for (size_t i = 1; i < in.size(); ++i) {
        double d = in[i] - in[i - 1];
        double g = d > 0 ? d : 0, l = d < 0 ? -d : 0;
        if (i == 1) {
            gain = g;
            loss = l;
        } else {
            gain = (gain * (n - 1) + g) / n;
            loss = (loss * (n - 1) + l) / n;
        }
        if (i >= (size_t)n) {
            if (loss == 0)
                out[i] = 100;
            else {
                double rs = gain / loss;
                out[i] = 100 - 100 / (1 + rs);
            }
        }
    }
    return out;
}

// Element-wise helpers.
inline Series combine(const Series& a, const Series& b, char op) {
    Series out(a.size(), nanv());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (!fin(a[i]) || !fin(b[i])) continue;
        switch (op) {
            case '+': out[i] = a[i] + b[i]; break;
            case '-': out[i] = a[i] - b[i]; break;
            case '*': out[i] = a[i] * b[i]; break;
            case '/': out[i] = b[i] != 0 ? a[i] / b[i] : nanv(); break;
        }
    }
    return out;
}
inline Series scale_s(const Series& a, double k) {
    Series out(a.size(), nanv());
    for (size_t i = 0; i < a.size(); ++i)
        if (fin(a[i])) out[i] = a[i] * k;
    return out;
}
inline Series shift_s(const Series& a, int n) {  // delay by n bars
    Series out(a.size(), nanv());
    for (size_t i = 0; i < a.size(); ++i) {
        long src = (long)i - n;
        if (src >= 0 && src < (long)a.size() && fin(a[src]))
            out[i] = a[src];
    }
    return out;
}
inline Series diff_s(const Series& a, int n = 1) {
    Series out(a.size(), nanv());
    for (size_t i = 0; i < a.size(); ++i)
        if ((int)i - n >= 0 && fin(a[i]) && fin(a[i - n]))
            out[i] = a[i] - a[i - n];
    return out;
}

// ------------------------------------------------------------- framework

enum class PlotKind { Overlay, Pane, Markers };
enum class Style { Lines, Bars };

using ComputeFn =
    std::function<Lines(const std::vector<Bar>&, const std::vector<double>&)>;

struct Def {
    const char* name;
    PlotKind kind;
    Style style;
    int color;             // palette index (line j uses color + j)
    const char* defaults;  // comma-separated default parameters
    ComputeFn fn;
};

// Area an indicator draws into. For Overlay/Markers, lo/hi are the price
// scale; for Pane the indicator scales its own lines into the area.
struct PlotArea {
    cairo_t* cr = nullptr;
    double x = 0, y = 0, w = 0, h = 0;
    size_t begin = 0, count = 0;
    double lo = 0, hi = 1;
};

inline const double kPalette[][3] = {
    {0.13, 0.35, 0.85},  // blue
    {0.95, 0.55, 0.10},  // orange
    {0.55, 0.25, 0.80},  // purple
    {0.00, 0.60, 0.60},  // teal
    {0.85, 0.20, 0.55},  // magenta
    {0.55, 0.35, 0.15},  // brown
    {0.20, 0.65, 0.25},  // green
    {0.80, 0.15, 0.15},  // red
    {0.10, 0.55, 0.80},  // steel blue
    {0.65, 0.65, 0.10},  // olive
    {0.35, 0.35, 0.35},  // grey
    {0.90, 0.70, 0.10},  // gold
    {0.25, 0.25, 0.70},  // indigo
    {0.60, 0.20, 0.20},  // brick
};
inline constexpr int kPaletteSize = 14;

class Indicator {
public:
    Indicator(const Def* def, std::vector<double> params)
        : def_(def), params_(std::move(params)) {}

    const char* name() const { return def_->name; }
    PlotKind kind() const { return def_->kind; }
    Style style() const { return def_->style; }
    int color() const { return def_->color; }
    const std::vector<double>& params() const { return params_; }

    Lines compute(const std::vector<Bar>& bars) const {
        return def_->fn(bars, params_);
    }

    // Draw this indicator into `area` (which covers bars
    // [begin, begin+count) of `bars`).
    void draw(const PlotArea& area, const std::vector<Bar>& bars) const {
        if (!area.cr || area.count == 0 || area.w < 4 || area.h < 4) return;
        Lines lines = compute(bars);
        if (lines.empty()) return;
        cairo_t* cr = area.cr;
        size_t end = std::min(bars.size(), area.begin + area.count);
        auto X = [&](size_t i) {
            return area.x + (i - area.begin + 0.5) * (area.w / area.count);
        };
        if (def_->kind == PlotKind::Markers) {
            const Series& m = lines[0];
            const double* col = kPalette[def_->color % kPaletteSize];
            double slot = area.w / area.count;
            double hw = std::max(3.0, slot * 0.35);  // visible at any zoom
            for (size_t i = area.begin; i < end; ++i) {
                if (i >= m.size() || !fin(m[i]) || m[i] == 0) continue;
                double cx = X(i);
                bool bull = m[i] > 0;
                double py = bull ? area.y + area.h -
                                       (bars[i].low - area.lo) /
                                           (area.hi - area.lo) * area.h +
                                       8
                                 : area.y +
                                       (area.hi - bars[i].high) /
                                           (area.hi - area.lo) * area.h -
                                       8;
                py = std::min(std::max(py, area.y + 8),
                              area.y + area.h - 8);
                cairo_set_source_rgba(cr, col[0], col[1], col[2], 0.9);
                cairo_move_to(cr, cx, py);
                cairo_line_to(cr, cx - hw, py + (bull ? 7 : -7));
                cairo_line_to(cr, cx + hw, py + (bull ? 7 : -7));
                cairo_close_path(cr);
                cairo_fill(cr);
            }
            return;
        }
        // Scale: overlays use the price scale; panes scale their own.
        double lo = area.lo, hi = area.hi;
        if (def_->kind == PlotKind::Pane) {
            lo = 1e300;
            hi = -1e300;
            for (const Series& s : lines)
                for (size_t i = area.begin; i < end && i < s.size(); ++i)
                    if (fin(s[i])) {
                        lo = std::min(lo, s[i]);
                        hi = std::max(hi, s[i]);
                    }
            if (lo > hi) return;  // nothing finite to draw
            if (def_->style == Style::Bars) {
                lo = std::min(lo, 0.0);
                hi = std::max(hi, 0.0);
            }
            if (hi - lo < 1e-12) hi = lo + 1;
        }
        auto Y = [&](double v) {
            return area.y + (hi - v) / (hi - lo) * area.h;
        };
        for (size_t j = 0; j < lines.size(); ++j) {
            const Series& s = lines[j];
            const double* col =
                kPalette[(def_->color + (int)j) % kPaletteSize];
            cairo_set_source_rgba(cr, col[0], col[1], col[2], 1.0);
            if (def_->style == Style::Bars) {
                double bw = std::max(1.0, area.w / area.count * 0.7);
                double y0 = Y(0);
                for (size_t i = area.begin; i < end && i < s.size(); ++i) {
                    if (!fin(s[i])) continue;
                    double y1 = Y(s[i]);
                    cairo_rectangle(cr, X(i) - bw / 2, std::min(y0, y1),
                                    bw, std::max(1.0, std::fabs(y1 - y0)));
                    cairo_fill(cr);
                }
            } else {
                cairo_set_line_width(cr, 1.2);
                bool pen = false;
                for (size_t i = area.begin; i < end && i < s.size(); ++i) {
                    if (!fin(s[i])) {
                        pen = false;
                        continue;
                    }
                    if (!pen) {
                        cairo_move_to(cr, X(i), Y(s[i]));
                        pen = true;
                    } else {
                        cairo_line_to(cr, X(i), Y(s[i]));
                    }
                }
                cairo_stroke(cr);
            }
        }
    }

private:
    const Def* def_;
    std::vector<double> params_;
};

// Registry (defined by the family tables at the bottom of this file).
inline const std::vector<Def>& all_defs();
inline const Def* find_def(const std::string& name);
inline std::vector<std::string> indicator_names();
inline std::unique_ptr<Indicator> make_indicator(
    const std::string& spec, std::string* err = nullptr);

// ============================================================ averages
// Moving averages and price filters (price-pane overlays).

static int pi(const std::vector<double>& p, size_t i, int dflt) {
    return i < p.size() && p[i] > 0 ? (int)p[i] : dflt;
}
static double pd(const std::vector<double>& p, size_t i, double dflt) {
    return i < p.size() ? p[i] : dflt;
}

static Lines f_sma(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {sma_s(closes(b), pi(p, 0, 20))};
}
static Lines f_ema(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {ema_s(closes(b), pi(p, 0, 20))};
}
static Lines f_wma(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {wma_s(closes(b), pi(p, 0, 20))};
}
static Lines f_smma(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {smma_s(closes(b), pi(p, 0, 20))};
}
static Lines f_dema(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series e1 = ema_s(closes(b), pi(p, 0, 20));
    Series e2 = ema_s(e1, pi(p, 0, 20));
    return {combine(scale_s(e1, 2), e2, '-')};
}
static Lines f_tema(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series e1 = ema_s(closes(b), n), e2 = ema_s(e1, n), e3 = ema_s(e2, n);
    return {combine(combine(scale_s(e1, 3), scale_s(e2, 3), '-'), e3, '+')};
}
static Lines f_trima(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    return {sma_s(sma_s(closes(b), (n + 1) / 2), n / 2 + 1)};
}
static Lines f_kama(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    Series c = closes(b), out(c.size(), nanv());
    Series ad = diff_s(c);
    for (size_t i = 0; i < ad.size(); ++i)
        if (fin(ad[i])) ad[i] = std::fabs(ad[i]);
    Series vol = rolling_sum(ad, n);
    double fast = 2.0 / 3, slow = 2.0 / 31;
    for (size_t i = (size_t)n; i < c.size(); ++i) {
        if (!fin(vol[i]) || vol[i] == 0) continue;
        double er = std::fabs(c[i] - c[i - n]) / vol[i];
        double sc = er * (fast - slow) + slow;
        sc *= sc;
        out[i] = fin(out[i - 1]) ? out[i - 1] + sc * (c[i] - out[i - 1])
                                 : c[i];
    }
    return {out};
}
static Lines f_t3(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    double a = pd(p, 1, 0.7);
    Series e = closes(b);
    for (int i = 0; i < 6; ++i) e = ema_s(e, n);
    Series e3 = closes(b);
    for (int i = 0; i < 3; ++i) e3 = ema_s(e3, n);
    Series e4 = ema_s(e3, n), e5 = ema_s(e4, n), e6 = ema_s(e5, n);
    double c1 = -a * a * a, c2 = 3 * a * a + 3 * a * a * a,
           c3 = -6 * a * a - 3 * a - 3 * a * a * a,
           c4 = 1 + 3 * a + a * a * a + 3 * a * a;
    (void)e;
    return {combine(
        combine(scale_s(e6, c1), scale_s(e5, c2), '+'),
        combine(scale_s(e4, c3), scale_s(e3, c4), '+'), '+')};
}
static Lines f_hma(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series diff = combine(scale_s(wma_s(closes(b), n / 2), 2),
                          wma_s(closes(b), n), '-');
    return {wma_s(diff, (int)std::sqrt(n))};
}
static Lines f_zlema(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    int n = pi(p, 0, 20), lag = (n - 1) / 2;
    Series adj = combine(scale_s(closes(b), 2), shift_s(closes(b), lag), '-');
    return {ema_s(adj, n)};
}
static Lines f_vwma(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series cv(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i) cv[i] = b[i].close * b[i].volume;
    return {combine(rolling_sum(cv, n), rolling_sum(vols(b), n), '/')};
}
static Lines f_lsma(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 25);
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = (size_t)(n - 1); i < c.size(); ++i) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        bool ok = true;
        for (int j = 0; j < n; ++j) {
            double x = j, y = c[i - n + 1 + j];
            if (!fin(y)) ok = false;
            sx += x;
            sy += y;
            sxx += x * x;
            sxy += x * y;
        }
        if (!ok) continue;
        double den = n * sxx - sx * sx;
        if (den == 0) continue;
        double slope = (n * sxy - sx * sy) / den;
        double inter = (sy - slope * sx) / n;
        out[i] = inter + slope * (n - 1);
    }
    return {out};
}
static Lines f_alma(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 9);
    double off = pd(p, 1, 0.85), sig = pd(p, 2, 6.0);
    Series c = closes(b), out(c.size(), nanv());
    double m = std::floor(off * (n - 1)), s = n / sig, wsum = 0;
    std::vector<double> w(n);
    for (int j = 0; j < n; ++j) {
        w[j] = std::exp(-((j - m) * (j - m)) / (2 * s * s));
        wsum += w[j];
    }
    for (size_t i = (size_t)(n - 1); i < c.size(); ++i) {
        double sum = 0;
        for (int j = 0; j < n; ++j) sum += w[j] * c[i - n + 1 + j];
        out[i] = sum / wsum;
    }
    return {out};
}
static Lines f_mcginley(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i) {
        if (i == 0 || !fin(out[i - 1])) {
            out[i] = c[i];
            continue;
        }
        double ratio = c[i] / out[i - 1];
        double denom = n * std::pow(ratio, 4);
        out[i] = out[i - 1] + (denom > 0 ? (c[i] - out[i - 1]) / denom : 0);
    }
    return {out};
}
static Lines f_gma(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series lg = closes(b);
    for (double& v : lg) v = v > 0 ? std::log(v) : nanv();
    Series m = sma_s(lg, pi(p, 0, 20));
    for (double& v : m)
        if (fin(v)) v = std::exp(v);
    return {m};
}
static Lines f_median_price(const std::vector<Bar>& b,
                            const std::vector<double>&) {
    return {hl2(b)};
}
static Lines f_typical_price(const std::vector<Bar>& b,
                             const std::vector<double>&) {
    return {hlc3(b)};
}
static Lines f_weighted_close(const std::vector<Bar>& b,
                              const std::vector<double>&) {
    Series s(b.size());
    for (size_t i = 0; i < b.size(); ++i)
        s[i] = (b[i].high + b[i].low + 2 * b[i].close) / 4;
    return {s};
}
static Lines f_midprice(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    return {scale_s(combine(rolling_max(highs(b), n),
                            rolling_min(lows(b), n), '+'),
                    0.5)};
}
static Lines f_midpoint(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    return {scale_s(combine(rolling_max(closes(b), n),
                            rolling_min(closes(b), n), '+'),
                    0.5)};
}
static Lines f_vwap(const std::vector<Bar>& b, const std::vector<double>&) {
    Series tp = hlc3(b), out(b.size(), nanv());
    double pv = 0, vv = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        pv += tp[i] * b[i].volume;
        vv += b[i].volume;
        if (vv > 0) out[i] = pv / vv;
    }
    return {out};
}
static Lines f_heikin_ashi(const std::vector<Bar>& b,
                           const std::vector<double>&) {
    Series out(b.size(), nanv());
    double hao = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        double hac = (b[i].open + b[i].high + b[i].low + b[i].close) / 4;
        hao = i == 0 ? (b[i].open + b[i].close) / 2
                     : (hao + out[i - 1]) / 2;
        (void)hao;
        out[i] = hac;
    }
    return {out};
}
// Ehlers two-pole super smoother (price filter).
static Series super_smoother_s(const Series& in, int n) {
    Series out(in.size(), nanv());
    double a = std::exp(-1.414 * M_PI / n);
    double b = 2 * a * std::cos(1.414 * M_PI / n);
    double c2 = b, c3 = -a * a, c1 = 1 - c2 - c3;
    for (size_t i = 0; i < in.size(); ++i) {
        if (!fin(in[i])) continue;
        double x1 = i > 0 && fin(in[i - 1]) ? in[i - 1] : in[i];
        double y1 = i > 0 && fin(out[i - 1]) ? out[i - 1] : in[i];
        double y2 = i > 1 && fin(out[i - 2]) ? out[i - 2] : y1;
        out[i] = c1 * (in[i] + x1) / 2 + c2 * y1 + c3 * y2;
    }
    return out;
}
static Lines f_super_smoother(const std::vector<Bar>& b,
                              const std::vector<double>& p) {
    return {super_smoother_s(closes(b), pi(p, 0, 14))};
}
static Lines f_decycler(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 30);
    Series c = closes(b), hp(c.size(), nanv()), out(c.size(), nanv());
    double w = 0.707 * 2 * M_PI / n;
    double alpha = (std::cos(w) + std::sin(w) - 1) / std::cos(w);
    for (size_t i = 0; i < c.size(); ++i) {
        double x1 = i > 0 ? c[i - 1] : c[i], x2 = i > 1 ? c[i - 2] : c[i];
        double h1 = i > 0 && fin(hp[i - 1]) ? hp[i - 1] : 0;
        double h2 = i > 1 && fin(hp[i - 2]) ? hp[i - 2] : 0;
        double k = 1 - alpha / 2;
        hp[i] = k * k * (c[i] - 2 * x1 + x2) + 2 * (1 - alpha) * h1 -
                (1 - alpha) * (1 - alpha) * h2;
        out[i] = c[i] - hp[i];
    }
    return {out};
}
static Lines f_gaussian(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series s = closes(b);
    double alpha = 2.0 / (n + 1);
    for (int pole = 0; pole < 4; ++pole) {  // 4-pole cascade approximates
        Series o(s.size(), nanv());         // a gaussian filter
        for (size_t i = 0; i < s.size(); ++i) {
            if (!fin(s[i])) continue;
            double prev = i > 0 && fin(o[i - 1]) ? o[i - 1] : s[i];
            o[i] = prev + alpha * (s[i] - prev);
        }
        s = o;
    }
    return {s};
}
static Lines f_butterworth(const std::vector<Bar>& b,
                           const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), out(c.size(), nanv());
    double a = std::exp(-1.414 * M_PI / n);
    double bb = 2 * a * std::cos(1.414 * M_PI / n), cc = a * a;
    for (size_t i = 0; i < c.size(); ++i) {
        double x1 = i > 0 ? c[i - 1] : c[i], x2 = i > 1 ? c[i - 2] : c[i];
        double y1 = i > 0 && fin(out[i - 1]) ? out[i - 1] : c[i];
        double y2 = i > 1 && fin(out[i - 2]) ? out[i - 2] : y1;
        out[i] = (1 - bb + cc) / 4 * (c[i] + 2 * x1 + x2) + bb * y1 -
                 cc * y2;
    }
    return {out};
}
static Lines f_laguerre(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    double g = pd(p, 0, 0.8);
    Series c = closes(b), out(c.size(), nanv());
    double L[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < c.size(); ++i) {
        double o0 = L[0], o1 = L[1], o2 = L[2], o3 = L[3];
        L[0] = (1 - g) * c[i] + g * o0;
        L[1] = -g * L[0] + o0 + g * o1;
        L[2] = -g * L[1] + o1 + g * o2;
        L[3] = -g * L[2] + o2 + g * o3;
        out[i] = (L[0] + 2 * L[1] + 2 * L[2] + L[3]) / 6;
    }
    return {out};
}
static Lines f_vidya(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), out(c.size(), nanv());
    Series sd = stdev_s(c, n);
    Series sd_lag = shift_s(sd, n);
    for (size_t i = 0; i < c.size(); ++i) {
        if (!fin(sd[i]) || !fin(sd_lag[i]) || sd_lag[i] == 0) continue;
        double vr = sd[i] / sd_lag[i];
        double alpha = vr * 2.0 / (n + 1);
        out[i] = fin(out[i - 1]) ? out[i - 1] + alpha * (c[i] - out[i - 1])
                                 : c[i];
    }
    return {out};
}
static Lines f_frama(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    int n = pi(p, 0, 16);
    if (n % 2) ++n;
    Series c = closes(b), out(c.size(), nanv());
    Series hh = highs(b), ll = lows(b);
    for (size_t i = (size_t)(2 * n - 1); i < c.size(); ++i) {
        auto range = [&](size_t from, size_t to) {
            double mx = -1e300, mn = 1e300;
            for (size_t k = from; k <= to; ++k) {
                mx = std::max(mx, hh[k]);
                mn = std::min(mn, ll[k]);
            }
            return mx - mn;
        };
        double n1 = range(i - 2 * n + 1, i - n) / n;
        double n2 = range(i - n + 1, i) / n;
        double n3 = range(i - 2 * n + 1, i) / (2.0 * n);
        if (n1 <= 0 || n2 <= 0 || n3 <= 0) continue;
        double dim = (std::log(n1 + n2) - std::log(n3)) / std::log(2);
        double alpha = std::exp(-4.6 * (dim - 1));
        alpha = std::min(1.0, std::max(0.01, alpha));
        out[i] = fin(out[i - 1]) ? alpha * c[i] + (1 - alpha) * out[i - 1]
                                 : c[i];
    }
    return {out};
}
static Lines f_vma(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), out(c.size(), nanv());
    Series sd = stdev_s(c, n);
    for (size_t i = 0; i < c.size(); ++i) {
        double sdl = i >= (size_t)n && fin(sd[i - n]) ? sd[i - n] : nanv();
        if (!fin(sd[i]) || !fin(sdl) || sdl == 0) continue;
        double vr = sd[i] / sdl;
        double alpha = (2.0 / (n + 1)) * vr;
        alpha = std::min(1.0, alpha);
        out[i] = fin(out[i - 1]) ? out[i - 1] + alpha * (c[i] - out[i - 1])
                                 : c[i];
    }
    return {out};
}
static Lines f_jma(const std::vector<Bar>& b, const std::vector<double>& p) {
    // Jurik MA (published approximation of the proprietary filter).
    int n = pi(p, 0, 14);
    double phase = pd(p, 1, 0);
    double pr = phase < -100 ? 0.5 : phase > 100 ? 2.5 : phase / 100 + 1.5;
    Series c = closes(b), out(c.size(), nanv());
    double beta = 0.45 * (n - 1) / (0.45 * (n - 1) + 2);
    double alpha = std::pow(beta, pr);
    double e0 = 0, e1 = 0, e2 = 0, jma = 0;
    bool started = false;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!started) {
            e0 = jma = c[i];
            started = true;
        }
        e0 = (1 - alpha) * c[i] + alpha * e0;
        e1 = (c[i] - e0) * (1 - beta) + beta * e1;
        e2 = (e0 + pr * e1 - jma) * (1 - alpha) * (1 - alpha) +
             alpha * alpha * e2;
        jma += e2;
        out[i] = jma;
    }
    return {out};
}

static void defs_averages(std::vector<Def>& v) {
    auto O = PlotKind::Overlay;
    auto L = Style::Lines;
    v.push_back({"SMA", O, L, 0, "20", f_sma});
    v.push_back({"EMA", O, L, 1, "20", f_ema});
    v.push_back({"WMA", O, L, 2, "20", f_wma});
    v.push_back({"SMMA", O, L, 3, "20", f_smma});
    v.push_back({"DEMA", O, L, 4, "20", f_dema});
    v.push_back({"TEMA", O, L, 5, "20", f_tema});
    v.push_back({"TRIMA", O, L, 6, "20", f_trima});
    v.push_back({"KAMA", O, L, 7, "10", f_kama});
    v.push_back({"T3", O, L, 8, "10,0.7", f_t3});
    v.push_back({"HMA", O, L, 9, "20", f_hma});
    v.push_back({"ZLEMA", O, L, 10, "20", f_zlema});
    v.push_back({"VWMA", O, L, 11, "20", f_vwma});
    v.push_back({"LSMA", O, L, 12, "25", f_lsma});
    v.push_back({"ALMA", O, L, 13, "9,0.85,6", f_alma});
    v.push_back({"McGinley Dynamic", O, L, 0, "14", f_mcginley});
    v.push_back({"Geometric MA", O, L, 1, "20", f_gma});
    v.push_back({"Median Price", O, L, 2, "", f_median_price});
    v.push_back({"Typical Price", O, L, 3, "", f_typical_price});
    v.push_back({"Weighted Close", O, L, 4, "", f_weighted_close});
    v.push_back({"Midprice", O, L, 5, "14", f_midprice});
    v.push_back({"Midpoint", O, L, 6, "14", f_midpoint});
    v.push_back({"VWAP", O, L, 7, "", f_vwap});
    v.push_back({"Heikin Ashi Close", O, L, 8, "", f_heikin_ashi});
    v.push_back({"Super Smoother", O, L, 9, "14", f_super_smoother});
    v.push_back({"Decycler", O, L, 10, "30", f_decycler});
    v.push_back({"Gaussian Filter", O, L, 11, "14", f_gaussian});
    v.push_back({"Butterworth Filter", O, L, 12, "14", f_butterworth});
    v.push_back({"Laguerre Filter", O, L, 13, "0.8", f_laguerre});
    v.push_back({"VIDYA", O, L, 0, "14", f_vidya});
    v.push_back({"FRAMA", O, L, 1, "16", f_frama});
    v.push_back({"Variable MA", O, L, 2, "14", f_vma});
    v.push_back({"Jurik MA", O, L, 3, "14,0", f_jma});
}

// ============================================================ bands & channels

static Lines f_bollinger(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    double k = pd(p, 1, 2.0);
    Series mid = sma_s(closes(b), n), sd = stdev_s(closes(b), n);
    return {combine(mid, scale_s(sd, k), '+'), mid,
            combine(mid, scale_s(sd, k), '-')};
}
static Lines f_bollinger_pb(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    Lines bb = f_bollinger(b, p);
    Series width = combine(bb[0], bb[2], '-');
    return {combine(combine(closes(b), bb[2], '-'), width, '/')};
}
static Lines f_bollinger_bw(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    Lines bb = f_bollinger(b, p);
    return {combine(combine(bb[0], bb[2], '-'), bb[1], '/')};
}
static Lines f_keltner(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    double k = pd(p, 1, 2.0);
    Series mid = ema_s(closes(b), n), atr = atr_s(b, pi(p, 2, 10));
    return {combine(mid, scale_s(atr, k), '+'), mid,
            combine(mid, scale_s(atr, k), '-')};
}
static Lines f_donchian(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series up = rolling_max(highs(b), n), dn = rolling_min(lows(b), n);
    return {up, scale_s(combine(up, dn, '+'), 0.5), dn};
}
static Lines f_starc(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    int n = pi(p, 0, 15);
    double k = pd(p, 1, 2.0);
    Series mid = sma_s(closes(b), n), atr = atr_s(b, pi(p, 2, 15));
    return {combine(mid, scale_s(atr, k), '+'), mid,
            combine(mid, scale_s(atr, k), '-')};
}
static Lines f_envelope(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    double pct = pd(p, 1, 2.5) / 100;
    Series mid = sma_s(closes(b), n);
    Series up(mid.size(), nanv()), dn(mid.size(), nanv());
    for (size_t i = 0; i < mid.size(); ++i)
        if (fin(mid[i])) {
            up[i] = mid[i] * (1 + pct);
            dn[i] = mid[i] * (1 - pct);
        }
    return {up, mid, dn};
}
static Lines f_ichimoku(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int t = pi(p, 0, 9), k = pi(p, 1, 26), s = pi(p, 2, 52);
    Series ten = f_midprice(b, {(double)t})[0];
    Series kij = f_midprice(b, {(double)k})[0];
    Series sen_b_raw = f_midprice(b, {(double)s})[0];
    Series sen_a_raw = scale_s(combine(ten, kij, '+'), 0.5);
    Series sen_a(b.size(), nanv()), sen_b(b.size(), nanv()),
        chikou(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i) {
        if (i >= (size_t)k) {
            if (fin(sen_a_raw[i - k])) sen_a[i] = sen_a_raw[i - k];
            if (fin(sen_b_raw[i - k])) sen_b[i] = sen_b_raw[i - k];
        }
        if (i + k < b.size()) chikou[i] = b[i + k].close;
    }
    return {ten, kij, sen_a, sen_b, chikou};
}
static Lines f_supertrend(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    double mult = pd(p, 1, 3.0);
    Series atr = atr_s(b, n), out(b.size(), nanv());
    Series fup(b.size(), nanv()), flo(b.size(), nanv());
    int dir = 1;
    for (size_t i = 0; i < b.size(); ++i) {
        if (!fin(atr[i])) continue;
        double mid = (b[i].high + b[i].low) / 2;
        double bu = mid + mult * atr[i], bl = mid - mult * atr[i];
        if (i == 0 || !fin(fup[i - 1])) {
            fup[i] = bu;
            flo[i] = bl;
        } else {
            fup[i] = (bu < fup[i - 1] || b[i - 1].close > fup[i - 1])
                         ? bu
                         : fup[i - 1];
            flo[i] = (bl > flo[i - 1] || b[i - 1].close < flo[i - 1])
                         ? bl
                         : flo[i - 1];
        }
        if (fin(out[i - 1 < b.size() ? i - 1 : 0]) && i > 0) {
            if (out[i - 1] == fup[i - 1] && b[i].close > fup[i])
                dir = 1;
            else if (out[i - 1] == flo[i - 1] && b[i].close < flo[i])
                dir = -1;
        }
        out[i] = dir > 0 ? flo[i] : fup[i];
    }
    return {out};
}
static Lines f_sar(const std::vector<Bar>& b, const std::vector<double>& p) {
    double step = pd(p, 0, 0.02), mx = pd(p, 1, 0.2);
    Series out(b.size(), nanv());
    if (b.size() < 2) return {out};
    bool up = b[1].close >= b[0].close;
    double sar = up ? b[0].low : b[0].high;
    double ep = up ? b[0].high : b[0].low, af = step;
    for (size_t i = 1; i < b.size(); ++i) {
        sar = sar + af * (ep - sar);
        if (up) {
            sar = std::min(sar, std::min(b[i - 1].low,
                                         i > 1 ? b[i - 2].low : b[i - 1].low));
            if (b[i].low < sar) {
                up = false;
                sar = ep;
                ep = b[i].low;
                af = step;
            } else {
                if (b[i].high > ep) {
                    ep = b[i].high;
                    af = std::min(mx, af + step);
                }
            }
        } else {
            sar = std::max(sar, std::max(b[i - 1].high,
                                         i > 1 ? b[i - 2].high : b[i - 1].high));
            if (b[i].high > sar) {
                up = true;
                sar = ep;
                ep = b[i].high;
                af = step;
            } else {
                if (b[i].low < ep) {
                    ep = b[i].low;
                    af = std::min(mx, af + step);
                }
            }
        }
        out[i] = sar;
    }
    return {out};
}
// Pivot points from the PREVIOUS day (by window_start UTC day).
static Lines f_pivots(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    int mode = pi(p, 0, 0);  // 0 standard 1 woodie 2 camarilla 3 fib 4 demark
    const int NL = 8;
    Lines out(NL, Series(b.size(), nanv()));
    double H = 0, L = 0, C = 0, O = 0;
    bool have_prev = false, in_day = false;
    long long day = -1;
    double dH = 0, dL = 0, dC = 0, dO = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        long long d = b[i].time / 86400000000000LL;
        if (!in_day || d != day) {
            if (in_day) {
                H = dH; L = dL; C = dC; O = dO;
                have_prev = true;
            }
            day = d;
            in_day = true;
            dH = b[i].high; dL = b[i].low; dO = b[i].open;
        }
        dH = std::max(dH, b[i].high);
        dL = std::min(dL, b[i].low);
        dC = b[i].close;
        if (!have_prev) continue;
        double P = 0, R1 = 0, S1 = 0, R2 = 0, S2 = 0, R3 = 0, S3 = 0,
               R4 = 0, S4 = 0;
        double range = H - L;
        switch (mode) {
            case 1:  // Woodie
                P = (H + L + 2 * C) / 4;
                R1 = 2 * P - L; S1 = 2 * P - H;
                R2 = P + range; S2 = P - range;
                R3 = H + 2 * (P - L); S3 = L - 2 * (H - P);
                break;
            case 2:  // Camarilla
                P = (H + L + C) / 3;
                R1 = C + range * 1.1 / 12; S1 = C - range * 1.1 / 12;
                R2 = C + range * 1.1 / 6; S2 = C - range * 1.1 / 6;
                R3 = C + range * 1.1 / 4; S3 = C - range * 1.1 / 4;
                R4 = C + range * 1.1 / 2; S4 = C - range * 1.1 / 2;
                break;
            case 3:  // Fibonacci
                P = (H + L + C) / 3;
                R1 = P + 0.382 * range; S1 = P - 0.382 * range;
                R2 = P + 0.618 * range; S2 = P - 0.618 * range;
                R3 = P + range; S3 = P - range;
                break;
            case 4: {  // DeMark
                double X = C < O ? H + 2 * L + C
                             : C > O ? 2 * H + L + C : H + L + 2 * C;
                P = X / 4;
                R1 = X / 2 - L; S1 = X / 2 - H;
                break;
            }
            default:  // Standard (floor)
                P = (H + L + C) / 3;
                R1 = 2 * P - L; S1 = 2 * P - H;
                R2 = P + range; S2 = P - range;
                R3 = H + 2 * (P - L); S3 = L - 2 * (H - P);
                break;
        }
        double vals[8] = {P, R1, S1, R2, S2, R3, S3, mode == 2 ? R4 : nanv()};
        (void)S4;
        for (int j = 0; j < NL; ++j) out[j][i] = vals[j];
        if (mode == 2) out[7][i] = R4, out[6][i] = S3;  // keep R4 visible
    }
    return out;
}
static Lines f_lr_channel(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    double k = pd(p, 1, 2.0);
    Lines mid = f_lsma(b, {(double)n});
    Series sd = stdev_s(closes(b), n);
    return {combine(mid[0], scale_s(sd, k), '+'), mid[0],
            combine(mid[0], scale_s(sd, k), '-')};
}
static Lines f_projection_bands(const std::vector<Bar>& b,
                                const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), up(c.size(), nanv()), dn(c.size(), nanv());
    for (size_t i = (size_t)(n - 1); i < c.size(); ++i) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (int j = 0; j < n; ++j) {
            double x = j, y = c[i - n + 1 + j];
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        double den = n * sxx - sx * sx;
        if (den == 0) continue;
        double slope = (n * sxy - sx * sy) / den;
        double inter = (sy - slope * sx) / n;
        double mx = -1e300, mn = 1e300;
        for (int j = 0; j < n; ++j) {
            double proj = b[i - n + 1 + j].high + slope * (n - 1 - j);
            double projl = b[i - n + 1 + j].low + slope * (n - 1 - j);
            mx = std::max(mx, proj);
            mn = std::min(mn, projl);
        }
        (void)inter;
        up[i] = mx;
        dn[i] = mn;
    }
    return {up, dn};
}
static Lines f_atr_bands(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    double k = pd(p, 1, 2.0);
    Series atr = atr_s(b, n), c = closes(b);
    return {combine(c, scale_s(atr, k), '+'),
            combine(c, scale_s(atr, k), '-')};
}
static Lines f_hilo_bands(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 21);
    Series hi = shift_s(sma_s(highs(b), n), 1), lo = shift_s(sma_s(lows(b), n), 1);
    return {hi, lo};
}
static Lines f_fractal_bands(const std::vector<Bar>& b,
                             const std::vector<double>& p) {
    int n = pi(p, 0, 21);
    return {rolling_max(highs(b), n), rolling_min(lows(b), n)};
}
static Lines f_chandelier(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 22);
    double k = pd(p, 1, 3.0);
    Series atr = atr_s(b, n);
    Series lng = combine(rolling_max(highs(b), n), scale_s(atr, -k), '+');
    Series sht = combine(rolling_min(lows(b), n), scale_s(atr, k), '+');
    return {lng, sht};
}
static Lines f_alligator(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    (void)p;
    Series med = hl2(b);
    Series jaw = shift_s(smma_s(med, 13), 8);
    Series teeth = shift_s(smma_s(med, 8), 5);
    Series lips = shift_s(smma_s(med, 5), 3);
    return {jaw, teeth, lips};
}
static Lines f_guppy(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    (void)p;
    Series c = closes(b);
    return {ema_s(c, 3), ema_s(c, 8), ema_s(c, 15),
            ema_s(c, 30), ema_s(c, 45), ema_s(c, 60)};
}
static Lines f_rainbow(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    (void)p;
    Series s = closes(b);
    Lines out;
    for (int i = 0; i < 6; ++i) {
        s = sma_s(s, 2);
        out.push_back(s);
    }
    return out;
}
static Lines f_vwap_bands(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    double k = pd(p, 0, 1.0);
    Series tp = hlc3(b), vw(b.size(), nanv()), up(b.size(), nanv()),
        dn(b.size(), nanv());
    double pv = 0, vv = 0, mean = 0, m2 = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        pv += tp[i] * b[i].volume;
        vv += b[i].volume;
        if (vv <= 0) continue;
        vw[i] = pv / vv;
        double d = tp[i] - mean;
        mean += d / (i + 1);
        m2 += d * (tp[i] - mean);
        double sd = std::sqrt(m2 / (i + 1));
        up[i] = vw[i] + k * sd;
        dn[i] = vw[i] - k * sd;
    }
    return {up, vw, dn};
}
static Lines f_zigzag(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    double pct = pd(p, 0, 5.0) / 100;
    Series c = closes(b), out(c.size(), nanv());
    if (c.size() < 2) return {out};
    std::vector<std::pair<size_t, double>> piv;
    int dir = 0;
    double hi = c[0], lo = c[0];
    size_t hii = 0, loi = 0;
    for (size_t i = 1; i < c.size(); ++i) {
        if (dir >= 0 && c[i] > hi) { hi = c[i]; hii = i; }
        if (dir <= 0 && c[i] < lo) { lo = c[i]; loi = i; }
        if (dir >= 0 && c[i] < hi * (1 - pct)) {
            piv.push_back({hii, hi});
            dir = -1; lo = c[i]; loi = i;
        } else if (dir <= 0 && c[i] > lo * (1 + pct)) {
            piv.push_back({loi, lo});
            dir = 1; hi = c[i]; hii = i;
        }
    }
    piv.push_back({c.size() - 1, c.back()});
    for (size_t k = 1; k < piv.size(); ++k) {
        size_t i0 = piv[k - 1].first, i1 = piv[k].first;
        if (i1 == i0) continue;
        for (size_t i = i0; i <= i1; ++i)
            out[i] = piv[k - 1].second +
                     (piv[k].second - piv[k - 1].second) * (i - i0) /
                         (double)(i1 - i0);
    }
    return {out};
}
static Lines f_fractals(const std::vector<Bar>& b,
                        const std::vector<double>&) {
    Series out(b.size(), 0.0);
    for (size_t i = 2; i + 2 < b.size(); ++i) {
        if (b[i].high > b[i - 1].high && b[i].high > b[i - 2].high &&
            b[i].high > b[i + 1].high && b[i].high > b[i + 2].high)
            out[i] = -1;  // up-fractal (resistance)
        if (b[i].low < b[i - 1].low && b[i].low < b[i - 2].low &&
            b[i].low < b[i + 1].low && b[i].low < b[i + 2].low)
            out[i] = 1;  // down-fractal (support)
    }
    return {out};
}
static Lines f_impulse(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    int n = pi(p, 0, 13);
    Series e = ema_s(closes(b), n);
    Series de = diff_s(e);
    Series out(b.size(), 0.0);
    // MACD histogram slope as the momentum leg.
    Series macd = combine(ema_s(closes(b), 12), ema_s(closes(b), 26), '-');
    Series dh = diff_s(macd);
    for (size_t i = 0; i < b.size(); ++i) {
        if (!fin(de[i]) || !fin(dh[i])) continue;
        if (de[i] > 0 && dh[i] > 0) out[i] = 1;
        else if (de[i] < 0 && dh[i] < 0) out[i] = -1;
    }
    return {out};
}

static void defs_bands(std::vector<Def>& v) {
    auto O = PlotKind::Overlay, P = PlotKind::Pane, M = PlotKind::Markers;
    auto L = Style::Lines, B = Style::Bars;
    (void)B;
    v.push_back({"Bollinger Bands", O, L, 0, "20,2", f_bollinger});
    v.push_back({"Bollinger %B", P, L, 1, "20,2", f_bollinger_pb});
    v.push_back({"Bollinger Bandwidth", P, L, 2, "20,2", f_bollinger_bw});
    v.push_back({"Keltner Channels", O, L, 3, "20,2,10", f_keltner});
    v.push_back({"Donchian Channels", O, L, 4, "20", f_donchian});
    v.push_back({"STARC Bands", O, L, 6, "15,2,15", f_starc});
    v.push_back({"MA Envelope", O, L, 7, "20,2.5", f_envelope});
    v.push_back({"Ichimoku", O, L, 8, "9,26,52", f_ichimoku});
    v.push_back({"Supertrend", O, L, 9, "10,3", f_supertrend});
    v.push_back({"Parabolic SAR", O, L, 10, "0.02,0.2", f_sar});
    v.push_back({"Pivot Points Standard", O, L, 11, "0", f_pivots});
    v.push_back({"Pivot Points Woodie", O, L, 12, "1", f_pivots});
    v.push_back({"Pivot Points Camarilla", O, L, 13, "2", f_pivots});
    v.push_back({"Pivot Points Fibonacci", O, L, 0, "3", f_pivots});
    v.push_back({"Pivot Points DeMark", O, L, 1, "4", f_pivots});
    v.push_back({"Linear Regression Channel", O, L, 2, "20,2", f_lr_channel});
    v.push_back({"Projection Bands", O, L, 3, "14", f_projection_bands});
    v.push_back({"ATR Bands", O, L, 4, "14,2", f_atr_bands});
    v.push_back({"High-Low Bands", O, L, 5, "21", f_hilo_bands});
    v.push_back({"Fractal Chaos Bands", O, L, 6, "21", f_fractal_bands});
    v.push_back({"Chandelier Exit", O, L, 7, "22,3", f_chandelier});
    v.push_back({"Alligator", O, L, 8, "", f_alligator});
    v.push_back({"Guppy MMA", O, L, 9, "", f_guppy});
    v.push_back({"Rainbow MA", O, L, 10, "", f_rainbow});
    v.push_back({"VWAP Bands", O, L, 11, "1", f_vwap_bands});
    v.push_back({"ZigZag", O, L, 12, "5", f_zigzag});
    v.push_back({"Williams Fractals", M, L, 13, "", f_fractals});
    v.push_back({"Elder Impulse", M, L, 0, "13", f_impulse});
}

// ============================================================ momentum

static Series stoch_k(const std::vector<Bar>& b, int n) {
    Series hh = rolling_max(highs(b), n), ll = rolling_min(lows(b), n);
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(hh[i]) && fin(ll[i]) && hh[i] > ll[i])
            out[i] = (c[i] - ll[i]) / (hh[i] - ll[i]) * 100;
    return out;
}
static Lines f_rsi(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {rsi_s(closes(b), pi(p, 0, 14))};
}
static Lines f_laguerre_rsi(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    double g = pd(p, 0, 0.5);
    Series c = closes(b), out(c.size(), nanv());
    double L[4] = {0, 0, 0, 0};
    bool started = false;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!started) {
            for (double& v : L) v = c[i];
            started = true;
        }
        double o0 = L[0], o1 = L[1], o2 = L[2], o3 = L[3];
        L[0] = (1 - g) * c[i] + g * o0;
        L[1] = -g * L[0] + o0 + g * o1;
        L[2] = -g * L[1] + o1 + g * o2;
        L[3] = -g * L[2] + o2 + g * o3;
        double cu = 0, cd = 0;
        double lv[4] = {L[0], L[1], L[2], L[3]};
        for (int j = 0; j < 3; ++j) {
            if (lv[j] > lv[j + 1]) cu += lv[j] - lv[j + 1];
            else cd += lv[j + 1] - lv[j];
        }
        out[i] = (cu + cd) > 0 ? cu / (cu + cd) * 100 : 50;
    }
    return {out};
}
static Lines f_connors_rsi(const std::vector<Bar>& b,
                           const std::vector<double>& p) {
    (void)p;
    Series c = closes(b);
    Series r1 = rsi_s(c, 3);
    // Up/down streak length series.
    Series streak(c.size(), 0.0);
    for (size_t i = 1; i < c.size(); ++i) {
        if (c[i] > c[i - 1]) streak[i] = streak[i - 1] > 0 ? streak[i - 1] + 1 : 1;
        else if (c[i] < c[i - 1]) streak[i] = streak[i - 1] < 0 ? streak[i - 1] - 1 : -1;
        else streak[i] = 0;
    }
    Series r2 = rsi_s(streak, 2);
    Series roc = diff_s(c);
    Series rank(c.size(), nanv());
    for (size_t i = 100; i < c.size(); ++i) {
        if (!fin(roc[i])) continue;
        int below = 0, tot = 0;
        for (int j = 0; j < 100; ++j)
            if (fin(roc[i - j])) {
                ++tot;
                if (roc[i - j] < roc[i]) ++below;
            }
        if (tot) rank[i] = (double)below / tot * 100;
    }
    Series out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(r1[i]) && fin(r2[i]) && fin(rank[i]))
            out[i] = (r1[i] + r2[i] + rank[i]) / 3;
    return {out};
}
static Lines f_inv_fisher_rsi(const std::vector<Bar>& b,
                              const std::vector<double>& p) {
    Series r = rsi_s(closes(b), pi(p, 0, 14));
    Series out(r.size(), nanv());
    for (size_t i = 0; i < r.size(); ++i)
        if (fin(r[i])) {
            double v = 0.1 * (r[i] - 50);
            double e = std::exp(2 * v);
            out[i] = ((e - 1) / (e + 1) + 1) * 50;
        }
    return {out};
}
static Lines f_stoch_fast(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    Series k = stoch_k(b, pi(p, 0, 14));
    return {k, sma_s(k, 3)};
}
static Lines f_stoch_slow(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    Series k = sma_s(stoch_k(b, pi(p, 0, 14)), 3);
    return {k, sma_s(k, 3)};
}
static Lines f_stoch_full(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    Series k = sma_s(stoch_k(b, pi(p, 0, 14)), pi(p, 1, 3));
    return {k, sma_s(k, pi(p, 2, 3))};
}
static Lines f_stoch_rsi(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series r = rsi_s(closes(b), n);
    Series mx = rolling_max(r, n), mn = rolling_min(r, n);
    Series k(r.size(), nanv());
    for (size_t i = 0; i < r.size(); ++i)
        if (fin(mx[i]) && fin(mn[i]) && mx[i] > mn[i])
            k[i] = (r[i] - mn[i]) / (mx[i] - mn[i]) * 100;
    return {k, sma_s(k, 3)};
}
static Lines f_smi(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b);
    Series hh = rolling_max(highs(b), n), ll = rolling_min(lows(b), n);
    Series mid(c.size(), nanv()), rel(c.size(), nanv()), rng(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(hh[i]) && fin(ll[i])) {
            mid[i] = (hh[i] + ll[i]) / 2;
            rel[i] = c[i] - mid[i];
            rng[i] = hh[i] - ll[i];
        }
    Series num = ema_s(ema_s(rel, 5), 5);
    Series den = ema_s(ema_s(rng, 5), 5);
    return {combine(scale_s(num, 100), den, '/')};
}
static Lines f_williams_r(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    Series k = stoch_k(b, pi(p, 0, 14));
    for (double& v : k)
        if (fin(v)) v -= 100;
    return {k};
}
static Lines f_cci(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series tp = hlc3(b), m = sma_s(tp, n), out(tp.size(), nanv());
    for (size_t i = 0; i < tp.size(); ++i) {
        if (!fin(m[i])) continue;
        double mad = 0;
        for (int j = 0; j < n; ++j) mad += std::fabs(tp[i - j] - m[i]);
        mad /= n;
        if (mad > 0) out[i] = (tp[i] - m[i]) / (0.015 * mad);
    }
    return {out};
}
static Lines f_cmo(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series d = diff_s(closes(b));
    Series g(d.size(), nanv()), l(d.size(), nanv());
    for (size_t i = 0; i < d.size(); ++i)
        if (fin(d[i])) {
            g[i] = d[i] > 0 ? d[i] : 0;
            l[i] = d[i] < 0 ? -d[i] : 0;
        }
    Series gs = rolling_sum(g, n), ls = rolling_sum(l, n);
    return {combine(scale_s(combine(gs, ls, '-'), 100),
                    combine(gs, ls, '+'), '/')};
}
static Lines f_roc(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 12);
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = (size_t)n; i < c.size(); ++i)
        if (c[i - n] != 0) out[i] = (c[i] - c[i - n]) / c[i - n] * 100;
    return {out};
}
static Lines f_momentum(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    return {diff_s(closes(b), pi(p, 0, 10))};
}
static Series macd_line(const std::vector<Bar>& b, int fast, int slow) {
    return combine(ema_s(closes(b), fast), ema_s(closes(b), slow), '-');
}
static Lines f_macd(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series m = macd_line(b, pi(p, 0, 12), pi(p, 1, 26));
    Series sig = ema_s(m, pi(p, 2, 9));
    return {m, sig};
}
static Lines f_macd_hist(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    Lines m = f_macd(b, p);
    return {combine(m[0], m[1], '-')};
}
static Lines f_ppo(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series slow = ema_s(closes(b), pi(p, 1, 26));
    Series m = combine(macd_line(b, pi(p, 0, 12), pi(p, 1, 26)), slow, '/');
    m = scale_s(m, 100);
    return {m, ema_s(m, pi(p, 2, 9))};
}
static Lines f_apo(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {macd_line(b, pi(p, 0, 12), pi(p, 1, 26))};
}
static Lines f_trix(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 15);
    Series e = ema_s(ema_s(ema_s(closes(b), n), n), n);
    Series out(e.size(), nanv());
    for (size_t i = 1; i < e.size(); ++i)
        if (fin(e[i]) && fin(e[i - 1]) && e[i - 1] != 0)
            out[i] = (e[i] - e[i - 1]) / e[i - 1] * 100;
    return {out};
}
static Lines f_tsi(const std::vector<Bar>& b, const std::vector<double>& p) {
    int r = pi(p, 0, 25), s = pi(p, 1, 13);
    Series d = diff_s(closes(b));
    Series ad(d.size(), nanv());
    for (size_t i = 0; i < d.size(); ++i)
        if (fin(d[i])) ad[i] = std::fabs(d[i]);
    Series num = ema_s(ema_s(d, r), s);
    Series den = ema_s(ema_s(ad, r), s);
    return {combine(scale_s(num, 100), den, '/')};
}
static Lines f_rmi(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14), m = pi(p, 1, 5);
    Series d = diff_s(closes(b), m);
    Series out(d.size(), nanv());
    double gain = 0, loss = 0;
    for (size_t i = 0; i < d.size(); ++i) {
        if (!fin(d[i])) continue;
        double g = d[i] > 0 ? d[i] : 0, l = d[i] < 0 ? -d[i] : 0;
        gain = gain == 0 && loss == 0 ? g : (gain * (n - 1) + g) / n;
        loss = gain == g && loss == 0 ? l : (loss * (n - 1) + l) / n;
        if (i >= (size_t)(n + m))
            out[i] = loss == 0 ? 100 : 100 - 100 / (1 + gain / loss);
    }
    return {out};
}
static Lines f_ultimate(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    (void)p;
    Series bp(b.size(), nanv()), tr(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i) {
        double pc = i > 0 ? b[i - 1].close : b[i].open;
        bp[i] = b[i].close - std::min(b[i].low, pc);
        tr[i] = std::max(b[i].high, pc) - std::min(b[i].low, pc);
    }
    Series a1 = combine(rolling_sum(bp, 7), rolling_sum(tr, 7), '/');
    Series a2 = combine(rolling_sum(bp, 14), rolling_sum(tr, 14), '/');
    Series a3 = combine(rolling_sum(bp, 28), rolling_sum(tr, 28), '/');
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(a1[i]) && fin(a2[i]) && fin(a3[i]))
            out[i] = (4 * a1[i] + 2 * a2[i] + a3[i]) / 7 * 100;
    return {out};
}
static Lines f_awesome(const std::vector<Bar>& b,
                       const std::vector<double>&) {
    return {combine(sma_s(hl2(b), 5), sma_s(hl2(b), 34), '-')};
}
static Lines f_accelerator(const std::vector<Bar>& b,
                           const std::vector<double>& p) {
    Lines ao = f_awesome(b, p);
    return {combine(ao[0], sma_s(ao[0], 5), '-')};
}
static Lines f_fisher(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    Series hh = rolling_max(highs(b), n), ll = rolling_min(lows(b), n);
    Series c = closes(b), fish(c.size(), nanv());
    double x = 0, prev = 0;
    for (size_t i = 0; i < c.size(); ++i) {
        if (!fin(hh[i]) || !fin(ll[i]) || hh[i] <= ll[i]) continue;
        double v = (c[i] - ll[i]) / (hh[i] - ll[i]);
        x = 0.33 * 2 * (v - 0.5) + 0.67 * x;
        x = std::min(0.999, std::max(-0.999, x));
        double f = 0.5 * std::log((1 + x) / (1 - x)) + 0.5 * prev;
        fish[i] = f;
        prev = f;
    }
    return {fish, shift_s(fish, 1)};
}
static Lines f_stc(const std::vector<Bar>& b, const std::vector<double>& p) {
    (void)p;
    Series m = macd_line(b, 23, 50);
    auto stoch_of = [&](const Series& s, int n) {
        Series mx = rolling_max(s, n), mn = rolling_min(s, n);
        Series out(s.size(), nanv());
        for (size_t i = 0; i < s.size(); ++i)
            if (fin(mx[i]) && fin(mn[i]) && mx[i] > mn[i])
                out[i] = (s[i] - mn[i]) / (mx[i] - mn[i]);
        return out;
    };
    Series s1 = stoch_of(m, 10);
    Series sm1 = ema_s(s1, 3);
    Series s2 = stoch_of(sm1, 10);
    return {scale_s(ema_s(s2, 3), 100)};
}
static Lines f_coppock(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    (void)p;
    Series r = combine(f_roc(b, {14})[0], f_roc(b, {11})[0], '+');
    return {wma_s(r, 10)};
}
static Lines f_kst(const std::vector<Bar>& b, const std::vector<double>& p) {
    (void)p;
    Series k = scale_s(sma_s(f_roc(b, {10})[0], 10), 1);
    k = combine(k, scale_s(sma_s(f_roc(b, {15})[0], 10), 2), '+');
    k = combine(k, scale_s(sma_s(f_roc(b, {20})[0], 10), 3), '+');
    k = combine(k, scale_s(sma_s(f_roc(b, {30})[0], 15), 4), '+');
    return {k, sma_s(k, 9)};
}
static Series sym_smooth(const Series& in) {  // (x + 2x1 + 2x2 + x3)/6
    Series out(in.size(), nanv());
    for (size_t i = 3; i < in.size(); ++i)
        out[i] = (in[i] + 2 * in[i - 1] + 2 * in[i - 2] + in[i - 3]) / 6;
    return out;
}
static Lines f_rvi_vigor(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    Series co(b.size()), hl(b.size());
    for (size_t i = 0; i < b.size(); ++i) {
        co[i] = b[i].close - b[i].open;
        hl[i] = b[i].high - b[i].low;
    }
    Series rvi = combine(sma_s(sym_smooth(co), n),
                         sma_s(sym_smooth(hl), n), '/');
    return {rvi, sym_smooth(rvi)};
}
static Lines f_demarker(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series mx(b.size(), 0.0), mn(b.size(), 0.0);
    for (size_t i = 1; i < b.size(); ++i) {
        mx[i] = std::max(0.0, b[i].high - b[i - 1].high);
        mn[i] = std::max(0.0, b[i - 1].low - b[i].low);
    }
    Series a = sma_s(mx, n), d = sma_s(mn, n);
    return {combine(a, combine(a, d, '+'), '/')};
}
static Lines f_bop(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].high > b[i].low)
            out[i] = (b[i].close - b[i].open) / (b[i].high - b[i].low);
    return {sma_s(out, pi(p, 0, 14))};
}
static Lines f_bull_bear(const std::vector<Bar>& b,
                         const std::vector<double>& p, bool bull) {
    Series e = ema_s(closes(b), pi(p, 0, 13));
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(e[i])) out[i] = (bull ? b[i].high : b[i].low) - e[i];
    return {out};
}
static Lines f_bull_power(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    return f_bull_bear(b, p, true);
}
static Lines f_bear_power(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    return f_bull_bear(b, p, false);
}
static Lines f_force(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    Series d = diff_s(closes(b));
    Series f(d.size(), nanv());
    for (size_t i = 0; i < d.size(); ++i)
        if (fin(d[i])) f[i] = d[i] * b[i].volume;
    return {ema_s(f, pi(p, 0, 13))};
}
static Lines f_eom(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series m = hl2(b), dm = diff_s(m);
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i) {
        if (!fin(dm[i]) || b[i].volume <= 0 || b[i].high <= b[i].low)
            continue;
        double box = b[i].volume / (b[i].high - b[i].low);
        out[i] = dm[i] / box * 10000;
    }
    return {sma_s(out, pi(p, 0, 14))};
}
static Lines f_mass_index(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 25);
    Series range = combine(highs(b), lows(b), '-');
    Series e1 = ema_s(range, 9);
    Series ratio = combine(e1, ema_s(e1, 9), '/');
    return {rolling_sum(ratio, n)};
}
static Lines f_vortex(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series vp(b.size(), nanv()), vm(b.size(), nanv());
    for (size_t i = 1; i < b.size(); ++i) {
        vp[i] = std::fabs(b[i].high - b[i - 1].low);
        vm[i] = std::fabs(b[i].low - b[i - 1].high);
    }
    Series trs = rolling_sum(true_range(b), n);
    return {combine(rolling_sum(vp, n), trs, '/'),
            combine(rolling_sum(vm, n), trs, '/')};
}
static Lines f_aroon(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    int n = pi(p, 0, 25);
    Series up(b.size(), nanv()), dn(b.size(), nanv());
    for (size_t i = (size_t)n; i < b.size(); ++i) {
        int since_hi = 0, since_lo = 0;
        double mx = -1e300, mn = 1e300;
        for (int j = 0; j <= n; ++j) {
            if (b[i - j].high >= mx) { mx = b[i - j].high; since_hi = j; }
            if (b[i - j].low <= mn) { mn = b[i - j].low; since_lo = j; }
        }
        up[i] = (double)(n - since_hi) / n * 100;
        dn[i] = (double)(n - since_lo) / n * 100;
    }
    return {up, dn};
}
static Lines f_aroon_osc(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    Lines a = f_aroon(b, p);
    return {combine(a[0], a[1], '-')};
}
struct DiAdx {
    Series plus_di, minus_di, dx, adx;
};
static DiAdx di_adx(const std::vector<Bar>& b, int n) {
    DiAdx r;
    size_t N = b.size();
    r.plus_di.assign(N, nanv());
    r.minus_di.assign(N, nanv());
    r.dx.assign(N, nanv());
    r.adx.assign(N, nanv());
    Series pdm(N, 0.0), mdm(N, 0.0), tr = true_range(b);
    for (size_t i = 1; i < N; ++i) {
        double up = b[i].high - b[i - 1].high;
        double dn = b[i - 1].low - b[i].low;
        pdm[i] = up > dn && up > 0 ? up : 0;
        mdm[i] = dn > up && dn > 0 ? dn : 0;
    }
    Series sp = smma_s(pdm, n), sm = smma_s(mdm, n), st = smma_s(tr, n);
    for (size_t i = 0; i < N; ++i)
        if (fin(st[i]) && st[i] > 0 && fin(sp[i]) && fin(sm[i])) {
            r.plus_di[i] = 100 * sp[i] / st[i];
            r.minus_di[i] = 100 * sm[i] / st[i];
            double den = r.plus_di[i] + r.minus_di[i];
            if (den > 0)
                r.dx[i] = 100 * std::fabs(r.plus_di[i] - r.minus_di[i]) / den;
        }
    r.adx = smma_s(r.dx, n);
    return r;
}
static Lines f_adx(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {di_adx(b, pi(p, 0, 14)).adx};
}
static Lines f_plus_di(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    return {di_adx(b, pi(p, 0, 14)).plus_di};
}
static Lines f_minus_di(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    return {di_adx(b, pi(p, 0, 14)).minus_di};
}
static Lines f_dx(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {di_adx(b, pi(p, 0, 14)).dx};
}
static Lines f_adxr(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series a = di_adx(b, n).adx;
    return {scale_s(combine(a, shift_s(a, n), '+'), 0.5)};
}
static Lines f_dpo(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series m = sma_s(closes(b), n);
    return {combine(shift_s(closes(b), n / 2 + 1), m, '-')};
}
static Lines f_qstick(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    Series co(b.size());
    for (size_t i = 0; i < b.size(); ++i) co[i] = b[i].close - b[i].open;
    return {sma_s(co, pi(p, 0, 14))};
}
static Lines f_imi(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series g(b.size(), 0.0), l(b.size(), 0.0);
    for (size_t i = 0; i < b.size(); ++i) {
        double d = b[i].close - b[i].open;
        if (d > 0) g[i] = d;
        else l[i] = -d;
    }
    Series gs = rolling_sum(g, n), ls = rolling_sum(l, n);
    return {combine(scale_s(gs, 100), combine(gs, ls, '+'), '/')};
}
static Lines f_rwi(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series atr = atr_s(b, n);
    Series hi_s(b.size(), nanv()), lo_s(b.size(), nanv());
    double sq = std::sqrt(n);
    for (size_t i = (size_t)n; i < b.size(); ++i)
        if (fin(atr[i]) && atr[i] > 0) {
            hi_s[i] = (b[i].high - b[i - n].low) / (atr[i] * sq);
            lo_s[i] = (b[i - n].high - b[i].low) / (atr[i] * sq);
        }
    return {hi_s, lo_s};
}
static Series swing_index_s(const std::vector<Bar>& b) {
    Series out(b.size(), nanv());
    for (size_t i = 1; i < b.size(); ++i) {
        const Bar& c = b[i];
        const Bar& pc = b[i - 1];
        double r1 = std::fabs(c.high - pc.close);
        double r2 = std::fabs(c.low - pc.close);
        double r3 = std::fabs(c.high - c.low);
        double R;
        if (r1 > r2 && r1 > r3)
            R = r1 - r2 / 2 + std::fabs(pc.close - pc.open) / 4;
        else if (r2 > r1 && r2 > r3)
            R = r2 - r1 / 2 + std::fabs(pc.close - pc.open) / 4;
        else
            R = r3 + std::fabs(pc.close - pc.open) / 4;
        if (R == 0) {
            out[i] = 0;
            continue;
        }
        double K = std::max(std::fabs(c.high - pc.close),
                            std::fabs(c.low - pc.close));
        out[i] = 50 * ((c.close - pc.close) + 0.5 * (c.close - c.open) +
                       0.25 * (pc.close - pc.open)) /
                 R * K / 10;
    }
    return out;
}
static Lines f_swing(const std::vector<Bar>& b,
                     const std::vector<double>&) {
    return {swing_index_s(b)};
}
static Lines f_aswing(const std::vector<Bar>& b,
                      const std::vector<double>&) {
    Series si = swing_index_s(b), out(b.size(), nanv());
    double acc = 0;
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(si[i])) {
            acc += si[i];
            out[i] = acc;
        }
    return {out};
}
static Lines f_gator(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    Lines a = f_alligator(b, p);
    Series up(b.size(), nanv()), dn(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i) {
        if (fin(a[0][i]) && fin(a[1][i]))
            up[i] = std::fabs(a[0][i] - a[1][i]);
        if (fin(a[1][i]) && fin(a[2][i]))
            dn[i] = -std::fabs(a[1][i] - a[2][i]);
    }
    return {up, dn};
}
static Lines f_projection_osc(const std::vector<Bar>& b,
                              const std::vector<double>& p) {
    Lines bands = f_projection_bands(b, p);
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(bands[0][i]) && fin(bands[1][i]) &&
            bands[0][i] > bands[1][i])
            out[i] = (c[i] - bands[1][i]) / (bands[0][i] - bands[1][i]) * 100;
    return {out};
}

static void defs_momentum(std::vector<Def>& v) {
    auto P = PlotKind::Pane;
    auto L = Style::Lines, B = Style::Bars;
    v.push_back({"RSI", P, L, 0, "14", f_rsi});
    v.push_back({"Laguerre RSI", P, L, 1, "0.5", f_laguerre_rsi});
    v.push_back({"Connors RSI", P, L, 2, "", f_connors_rsi});
    v.push_back({"Inverse Fisher RSI", P, L, 3, "14", f_inv_fisher_rsi});
    v.push_back({"Stochastic Fast", P, L, 4, "14", f_stoch_fast});
    v.push_back({"Stochastic Slow", P, L, 5, "14", f_stoch_slow});
    v.push_back({"Stochastic Full", P, L, 6, "14,3,3", f_stoch_full});
    v.push_back({"Stochastic RSI", P, L, 7, "14", f_stoch_rsi});
    v.push_back({"SMI", P, L, 8, "14", f_smi});
    v.push_back({"Williams %R", P, L, 9, "14", f_williams_r});
    v.push_back({"CCI", P, L, 10, "20", f_cci});
    v.push_back({"CMO", P, L, 11, "14", f_cmo});
    v.push_back({"ROC", P, L, 12, "12", f_roc});
    v.push_back({"Momentum", P, L, 13, "10", f_momentum});
    v.push_back({"MACD", P, L, 0, "12,26,9", f_macd});
    v.push_back({"MACD Histogram", P, B, 1, "12,26,9", f_macd_hist});
    v.push_back({"PPO", P, L, 2, "12,26,9", f_ppo});
    v.push_back({"APO", P, L, 3, "12,26", f_apo});
    v.push_back({"TRIX", P, L, 4, "15", f_trix});
    v.push_back({"TSI", P, L, 5, "25,13", f_tsi});
    v.push_back({"RMI", P, L, 6, "14,5", f_rmi});
    v.push_back({"Ultimate Oscillator", P, L, 7, "", f_ultimate});
    v.push_back({"Awesome Oscillator", P, B, 8, "", f_awesome});
    v.push_back({"Accelerator Oscillator", P, B, 9, "", f_accelerator});
    v.push_back({"Fisher Transform", P, L, 10, "10", f_fisher});
    v.push_back({"Schaff Trend Cycle", P, L, 11, "", f_stc});
    v.push_back({"Coppock Curve", P, L, 12, "", f_coppock});
    v.push_back({"KST", P, L, 13, "", f_kst});
    v.push_back({"Relative Vigor Index", P, L, 0, "10", f_rvi_vigor});
    v.push_back({"DeMarker", P, L, 1, "14", f_demarker});
    v.push_back({"Balance of Power", P, L, 2, "14", f_bop});
    v.push_back({"Bull Power", P, L, 3, "13", f_bull_power});
    v.push_back({"Bear Power", P, L, 4, "13", f_bear_power});
    v.push_back({"Force Index", P, L, 5, "13", f_force});
    v.push_back({"Ease of Movement", P, L, 6, "14", f_eom});
    v.push_back({"Mass Index", P, L, 7, "25", f_mass_index});
    v.push_back({"Vortex Indicator", P, L, 8, "14", f_vortex});
    v.push_back({"Aroon", P, L, 9, "25", f_aroon});
    v.push_back({"Aroon Oscillator", P, L, 10, "25", f_aroon_osc});
    v.push_back({"ADX", P, L, 11, "14", f_adx});
    v.push_back({"Plus DI", P, L, 12, "14", f_plus_di});
    v.push_back({"Minus DI", P, L, 13, "14", f_minus_di});
    v.push_back({"DX", P, L, 0, "14", f_dx});
    v.push_back({"ADXR", P, L, 1, "14", f_adxr});
    v.push_back({"DPO", P, L, 2, "20", f_dpo});
    v.push_back({"Qstick", P, L, 3, "14", f_qstick});
    v.push_back({"IMI", P, L, 4, "14", f_imi});
    v.push_back({"Random Walk Index", P, L, 5, "14", f_rwi});
    v.push_back({"Swing Index", P, L, 6, "", f_swing});
    v.push_back({"Accumulative Swing Index", P, L, 7, "", f_aswing});
    v.push_back({"Gator Oscillator", P, B, 8, "", f_gator});
    v.push_back({"Projection Oscillator", P, L, 9, "14", f_projection_osc});
}

// ============================================================ volume

static Lines f_volume(const std::vector<Bar>& b,
                      const std::vector<double>&) {
    return {vols(b)};
}
static Lines f_volume_ma(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    return {sma_s(vols(b), pi(p, 0, 20))};
}
static Lines f_obv(const std::vector<Bar>& b, const std::vector<double>&) {
    Series out(b.size(), nanv());
    double acc = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i > 0) {
            if (b[i].close > b[i - 1].close) acc += b[i].volume;
            else if (b[i].close < b[i - 1].close) acc -= b[i].volume;
        }
        out[i] = acc;
    }
    return {out};
}
static Lines f_pvt(const std::vector<Bar>& b, const std::vector<double>&) {
    Series out(b.size(), nanv());
    double acc = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i > 0 && b[i - 1].close != 0)
            acc += b[i].volume * (b[i].close - b[i - 1].close) /
                   b[i - 1].close;
        out[i] = acc;
    }
    return {out};
}
static Series ad_line_s(const std::vector<Bar>& b) {
    Series out(b.size(), nanv());
    double acc = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        if (b[i].high > b[i].low) {
            double mfm = ((b[i].close - b[i].low) - (b[i].high - b[i].close)) /
                         (b[i].high - b[i].low);
            acc += mfm * b[i].volume;
        }
        out[i] = acc;
    }
    return out;
}
static Lines f_ad_line(const std::vector<Bar>& b,
                       const std::vector<double>&) {
    return {ad_line_s(b)};
}
static Lines f_adosc(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    Series ad = ad_line_s(b);
    return {combine(ema_s(ad, pi(p, 0, 3)), ema_s(ad, pi(p, 1, 10)), '-')};
}
static Lines f_cmf(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series mfv(b.size(), 0.0);
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].high > b[i].low)
            mfv[i] = ((b[i].close - b[i].low) - (b[i].high - b[i].close)) /
                     (b[i].high - b[i].low) * b[i].volume;
    return {combine(rolling_sum(mfv, n), rolling_sum(vols(b), n), '/')};
}
static Lines f_mfi(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series tp = hlc3(b);
    Series pos(b.size(), 0.0), neg(b.size(), 0.0);
    for (size_t i = 1; i < b.size(); ++i) {
        double flow = tp[i] * b[i].volume;
        if (tp[i] > tp[i - 1]) pos[i] = flow;
        else if (tp[i] < tp[i - 1]) neg[i] = flow;
    }
    Series ps = rolling_sum(pos, n), ns = rolling_sum(neg, n);
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(ps[i]) && fin(ns[i]))
            out[i] = ns[i] == 0 ? 100 : 100 - 100 / (1 + ps[i] / ns[i]);
    return {out};
}
static Lines f_vol_osc(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    Series fast = sma_s(vols(b), pi(p, 0, 5)), slow = sma_s(vols(b), pi(p, 1, 34));
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(slow[i]) && slow[i] != 0)
            out[i] = (fast[i] - slow[i]) / slow[i] * 100;
    return {out};
}
static Lines f_pvo(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series fast = ema_s(vols(b), pi(p, 0, 12)), slow = ema_s(vols(b), pi(p, 1, 26));
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(slow[i]) && slow[i] != 0)
            out[i] = (fast[i] - slow[i]) / slow[i] * 100;
    return {out};
}
static Lines f_vol_roc(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    int n = pi(p, 0, 12);
    Series v = vols(b), out(v.size(), nanv());
    for (size_t i = (size_t)n; i < v.size(); ++i)
        if (v[i - n] != 0) out[i] = (v[i] - v[i - n]) / v[i - n] * 100;
    return {out};
}
static Lines f_nvi_pvi(const std::vector<Bar>& b,
                       const std::vector<double>&, bool nvi) {
    Series out(b.size(), nanv());
    double acc = 1000;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i > 0 && b[i - 1].close != 0) {
            bool down_day = b[i].volume < b[i - 1].volume;
            if (down_day == nvi)
                acc *= 1 + (b[i].close - b[i - 1].close) / b[i - 1].close;
        }
        out[i] = acc;
    }
    return {out};
}
static Lines f_nvi(const std::vector<Bar>& b, const std::vector<double>& p) {
    return f_nvi_pvi(b, p, true);
}
static Lines f_pvi(const std::vector<Bar>& b, const std::vector<double>& p) {
    return f_nvi_pvi(b, p, false);
}
static Lines f_klinger(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    (void)p;
    Series vf(b.size(), 0.0);
    for (size_t i = 1; i < b.size(); ++i) {
        double trend = (b[i].high + b[i].low + b[i].close) -
                       (b[i - 1].high + b[i - 1].low + b[i - 1].close);
        vf[i] = trend >= 0 ? b[i].volume : -b[i].volume;
    }
    Series ko = combine(ema_s(vf, 34), ema_s(vf, 55), '-');
    return {ko, ema_s(ko, 13)};
}
static Lines f_twiggs(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    int n = pi(p, 0, 21);
    Series trl(b.size(), nanv()), adv(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i) {
        double pc = i > 0 ? b[i - 1].close : b[i].low;
        trl[i] = std::max(b[i].high, pc) - std::min(b[i].low, pc);
        adv[i] = trl[i] > 0 ? (b[i].close - std::min(b[i].low, pc)) / trl[i] *
                                  b[i].volume
                            : 0;
    }
    Series num = ema_s(adv, n), den = ema_s(vols(b), n);
    return {combine(num, den, '/')};
}
static Lines f_williams_ad(const std::vector<Bar>& b,
                           const std::vector<double>&) {
    Series out(b.size(), nanv());
    double acc = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i > 0) {
            if (b[i].close > b[i - 1].close)
                acc += b[i].close - std::min(b[i].low, b[i - 1].close);
            else if (b[i].close < b[i - 1].close)
                acc -= std::max(b[i].high, b[i - 1].close) - b[i].close;
        }
        out[i] = acc;
    }
    return {out};
}
static Lines f_intensity(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 21);
    Series ii(b.size(), 0.0);
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].high > b[i].low)
            ii[i] = (2 * b[i].close - b[i].high - b[i].low) /
                    (b[i].high - b[i].low) * b[i].volume;
    return {rolling_sum(ii, n)};
}
static Lines f_ad_percent(const std::vector<Bar>& b,
                          const std::vector<double>&) {
    Series ad = ad_line_s(b), cumv(b.size(), nanv());
    double acc = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        acc += b[i].volume;
        cumv[i] = acc;
    }
    return {combine(scale_s(ad, 100), cumv, '/')};
}
static Lines f_demand(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series bp(b.size(), 0.0), sp(b.size(), 0.0);
    for (size_t i = 1; i < b.size(); ++i) {
        double w = std::pow(1.0 + std::fabs(b[i].close - b[i - 1].close) /
                                       b[i - 1].close,
                            10);
        if (b[i].close > b[i - 1].close) bp[i] = b[i].volume * w;
        else sp[i] = b[i].volume * w;
    }
    Series bs = sma_s(bp, n), ss = sma_s(sp, n);
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(bs[i]) && fin(ss[i]))
            out[i] = bs[i] > ss[i] ? 1 - ss[i] / bs[i]
                                   : -(1 - bs[i] / ss[i]);
    return {scale_s(out, 100)};
}
static Lines f_tvi(const std::vector<Bar>& b, const std::vector<double>&) {
    Series out(b.size(), nanv());
    double acc = 0;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i > 0) {
            if (b[i].close > b[i - 1].close) acc += b[i].volume;
            else if (b[i].close < b[i - 1].close) acc -= b[i].volume;
        }
        out[i] = acc;
    }
    return {out};
}

static void defs_volume(std::vector<Def>& v) {
    auto P = PlotKind::Pane;
    auto L = Style::Lines, B = Style::Bars;
    v.push_back({"Volume", P, B, 10, "", f_volume});
    v.push_back({"Volume MA", P, L, 11, "20", f_volume_ma});
    v.push_back({"OBV", P, L, 0, "", f_obv});
    v.push_back({"Price Volume Trend", P, L, 1, "", f_pvt});
    v.push_back({"A/D Line", P, L, 2, "", f_ad_line});
    v.push_back({"Chaikin Oscillator", P, L, 3, "3,10", f_adosc});
    v.push_back({"Chaikin Money Flow", P, L, 4, "20", f_cmf});
    v.push_back({"MFI", P, L, 5, "14", f_mfi});
    v.push_back({"Volume Oscillator", P, L, 6, "5,34", f_vol_osc});
    v.push_back({"PVO", P, L, 7, "12,26", f_pvo});
    v.push_back({"Volume ROC", P, L, 8, "12", f_vol_roc});
    v.push_back({"NVI", P, L, 9, "", f_nvi});
    v.push_back({"PVI", P, L, 10, "", f_pvi});
    v.push_back({"Klinger Oscillator", P, L, 11, "", f_klinger});
    v.push_back({"Twiggs Money Flow", P, L, 12, "21", f_twiggs});
    v.push_back({"Williams A/D", P, L, 13, "", f_williams_ad});
    v.push_back({"Intraday Intensity", P, L, 0, "21", f_intensity});
    v.push_back({"Accumulation Percent", P, L, 1, "", f_ad_percent});
    v.push_back({"Demand Index", P, L, 2, "20", f_demand});
    v.push_back({"Trade Volume Index", P, L, 3, "", f_tvi});
}

// ============================================================ volatility & statistics

static Lines f_atr(const std::vector<Bar>& b, const std::vector<double>& p) {
    return {atr_s(b, pi(p, 0, 14))};
}
static Lines f_natr(const std::vector<Bar>& b, const std::vector<double>& p) {
    Series a = atr_s(b, pi(p, 0, 14)), c = closes(b);
    return {combine(scale_s(a, 100), c, '/')};
}
static Lines f_tr(const std::vector<Bar>& b, const std::vector<double>&) {
    return {true_range(b)};
}
static Lines f_stddev(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    return {stdev_s(closes(b), pi(p, 0, 20))};
}
static Lines f_variance(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    Series sd = stdev_s(closes(b), pi(p, 0, 20));
    for (double& v : sd)
        if (fin(v)) v *= v;
    return {sd};
}
static Series log_returns(const std::vector<Bar>& b) {
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = 1; i < c.size(); ++i)
        if (c[i] > 0 && c[i - 1] > 0) out[i] = std::log(c[i] / c[i - 1]);
    return out;
}
static Lines f_hist_vol(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    return {scale_s(stdev_s(log_returns(b), pi(p, 0, 20)), 100)};
}
static Lines f_chaikin_vol(const std::vector<Bar>& b,
                           const std::vector<double>& p) {
    Series e = ema_s(combine(highs(b), lows(b), '-'), pi(p, 0, 10));
    Series out(e.size(), nanv());
    int n = pi(p, 1, 10);
    for (size_t i = (size_t)n; i < e.size(); ++i)
        if (fin(e[i]) && fin(e[i - n]) && e[i - n] != 0)
            out[i] = (e[i] - e[i - n]) / e[i - n] * 100;
    return {out};
}
static Lines f_ulcer(const std::vector<Bar>& b,
                     const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), mx = rolling_max(c, n), dd(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(mx[i]) && mx[i] > 0) {
            double d = (c[i] - mx[i]) / mx[i] * 100;
            dd[i] = d * d;
        }
    Series ms = sma_s(dd, n), out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(ms[i])) out[i] = std::sqrt(ms[i]);
    return {out};
}
static Lines f_choppiness(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series atr_sum = rolling_sum(true_range(b), n);
    Series hh = rolling_max(highs(b), n), ll = rolling_min(lows(b), n);
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(atr_sum[i]) && fin(hh[i]) && hh[i] > ll[i] && atr_sum[i] > 0)
            out[i] = 100 * std::log10(atr_sum[i] / (hh[i] - ll[i])) /
                     std::log10(n);
    return {out};
}
static Lines f_rvol_idx(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    Series d = diff_s(closes(b));
    Series up(b.size(), 0.0), dn(b.size(), 0.0);
    for (size_t i = 0; i < d.size(); ++i)
        if (fin(d[i])) {
            if (d[i] > 0) up[i] = d[i];
            else dn[i] = -d[i];
        }
    Series su = sma_s(stdev_s(up, n), n), sd = sma_s(stdev_s(dn, n), n);
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(su[i]) && fin(sd[i]) && su[i] + sd[i] > 0)
            out[i] = su[i] / (su[i] + sd[i]) * 100;
    return {out};
}
static Lines f_parkinson(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series v(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].low > 0) {
            double x = std::log(b[i].high / b[i].low);
            v[i] = x * x;
        }
    Series m = sma_s(v, n), out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(m[i])) out[i] = std::sqrt(m[i] / (4 * std::log(2))) * 100;
    return {out};
}
static Lines f_garman_klass(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series v(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].low > 0 && b[i].open > 0) {
            double a = std::log(b[i].high / b[i].low);
            double c = std::log(b[i].close / b[i].open);
            v[i] = 0.5 * a * a - (2 * std::log(2) - 1) * c * c;
        }
    Series m = sma_s(v, n), out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(m[i]) && m[i] > 0) out[i] = std::sqrt(m[i]) * 100;
    return {out};
}
static Lines f_rogers_satchell(const std::vector<Bar>& b,
                               const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series v(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (b[i].open > 0 && b[i].close > 0) {
            v[i] = std::log(b[i].high / b[i].close) *
                       std::log(b[i].high / b[i].open) +
                   std::log(b[i].low / b[i].close) *
                       std::log(b[i].low / b[i].open);
        }
    Series m = sma_s(v, n), out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(m[i]) && m[i] > 0) out[i] = std::sqrt(m[i]) * 100;
    return {out};
}
static Lines f_yang_zhang(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series on(b.size(), 0.0), oc(b.size(), 0.0), rs(b.size(), 0.0);
    for (size_t i = 0; i < b.size(); ++i) {
        if (b[i].open <= 0 || b[i].close <= 0) continue;
        if (i > 0 && b[i - 1].close > 0) {
            double x = std::log(b[i].open / b[i - 1].close);
            on[i] = x * x;
        }
        double y = std::log(b[i].close / b[i].open);
        oc[i] = y * y;
        if (b[i].high > 0 && b[i].low > 0)
            rs[i] = std::log(b[i].high / b[i].close) *
                        std::log(b[i].high / b[i].open) +
                    std::log(b[i].low / b[i].close) *
                        std::log(b[i].low / b[i].open);
    }
    Series mo = sma_s(on, n), mc = sma_s(oc, n), mr = sma_s(rs, n);
    Series out(b.size(), nanv());
    double k = 0.34;
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(mo[i]) && fin(mc[i]) && fin(mr[i]))
            out[i] = std::sqrt(std::max(0.0, mo[i] + k * mc[i] +
                                                 (1 - k) * mr[i])) *
                     100;
    return {out};
}
static Lines f_thermometer(const std::vector<Bar>& b,
                           const std::vector<double>& p) {
    Series m(b.size(), nanv());
    for (size_t i = 1; i < b.size(); ++i)
        m[i] = std::max(std::fabs(b[i].high - b[i - 1].high),
                        std::fabs(b[i].low - b[i - 1].low));
    return {ema_s(m, pi(p, 0, 22))};
}
static Lines f_std_error(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    return {scale_s(stdev_s(closes(b), n), 1 / std::sqrt((double)n))};
}
static Lines f_mean_dev(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series c = closes(b), m = sma_s(c, n), out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i) {
        if (!fin(m[i])) continue;
        double s = 0;
        for (int j = 0; j < n; ++j) s += std::fabs(c[i - j] - m[i]);
        out[i] = s / n;
    }
    return {out};
}
static Lines f_moment(const std::vector<Bar>& b,
                      const std::vector<double>& p, int power, bool norm) {
    int n = pi(p, 0, 20);
    Series c = closes(b), m = sma_s(c, n), sd = stdev_s(c, n);
    Series out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i) {
        if (!fin(m[i])) continue;
        double s = 0;
        for (int j = 0; j < n; ++j) s += std::pow(c[i - j] - m[i], power);
        s /= n;
        out[i] = norm && fin(sd[i]) && sd[i] > 0
                     ? s / std::pow(sd[i], power)
                     : s;
    }
    return {out};
}
static Lines f_skewness(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    return f_moment(b, p, 3, true);
}
static Lines f_kurtosis(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    Lines k = f_moment(b, p, 4, true);
    for (double& v : k[0])
        if (fin(v)) v -= 3;
    return k;
}
static Lines f_zscore(const std::vector<Bar>& b,
                      const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series c = closes(b);
    return {combine(combine(c, sma_s(c, n), '-'), stdev_s(c, n), '/')};
}
struct LinReg {
    Series slope, intercept, value;
};
static LinReg linreg(const Series& in, int n) {
    LinReg r;
    r.slope.assign(in.size(), nanv());
    r.intercept.assign(in.size(), nanv());
    r.value.assign(in.size(), nanv());
    for (size_t i = (size_t)(n - 1); i < in.size(); ++i) {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        bool ok = true;
        for (int j = 0; j < n; ++j) {
            double x = j, y = in[i - n + 1 + j];
            if (!fin(y)) ok = false;
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        if (!ok) continue;
        double den = n * sxx - sx * sx;
        if (den == 0) continue;
        r.slope[i] = (n * sxy - sx * sy) / den;
        r.intercept[i] = (sy - r.slope[i] * sx) / n;
        r.value[i] = r.intercept[i] + r.slope[i] * (n - 1);
    }
    return r;
}
static Lines f_lr_slope(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    return {linreg(closes(b), pi(p, 0, 20)).slope};
}
static Lines f_lr_intercept(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    return {linreg(closes(b), pi(p, 0, 20)).intercept};
}
static Lines f_lr_angle(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    Series s = linreg(closes(b), pi(p, 0, 20)).slope;
    for (double& v : s)
        if (fin(v)) v = std::atan(v) * 180 / M_PI;
    return {s};
}
static Lines f_r_squared(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series c = closes(b), idx(c.size());
    for (size_t i = 0; i < c.size(); ++i) idx[i] = (double)i;
    // Rolling correlation of close vs index, squared.
    Series out(c.size(), nanv());
    for (size_t i = (size_t)(n - 1); i < c.size(); ++i) {
        double mx = 0, my = 0;
        for (int j = 0; j < n; ++j) {
            mx += idx[i - j];
            my += c[i - j];
        }
        mx /= n; my /= n;
        double sxy = 0, sxx = 0, syy = 0;
        for (int j = 0; j < n; ++j) {
            double dx = idx[i - j] - mx, dy = c[i - j] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        if (sxx > 0 && syy > 0) out[i] = (sxy * sxy) / (sxx * syy);
    }
    return {out};
}
static Lines f_tsf(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    LinReg r = linreg(closes(b), n);
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(r.intercept[i])) out[i] = r.intercept[i] + r.slope[i] * n;
    return {out};
}
static Lines f_forecast_osc(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    Series tsf = f_tsf(b, p)[0];
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = 1; i < c.size(); ++i)
        if (fin(tsf[i - 1]) && c[i] != 0)
            out[i] = (c[i] - tsf[i - 1]) / c[i] * 100;
    return {out};
}

// ============================================================ Ehlers / Hilbert

struct HilbertData {
    Series period, phase, inphase, quadrature;
};
// Homodyne discriminator (Ehlers): dominant cycle period + phase.
static HilbertData hilbert(const Series& price) {
    size_t N = price.size();
    HilbertData r;
    r.period.assign(N, nanv());
    r.phase.assign(N, nanv());
    r.inphase.assign(N, nanv());
    r.quadrature.assign(N, nanv());
    std::vector<double> smooth(N, 0), detr(N, 0), q1(N, 0), i1(N, 0),
        ji(N, 0), jq(N, 0), re(N, 0), im(N, 0);
    auto at = [&](const std::vector<double>& v, long i) {
        if (i < 0) return v.empty() ? 0.0 : v[0];
        if (i >= (long)v.size()) return v.back();
        return v[(size_t)i];
    };
    double period = 20, phase = 0;
    for (size_t i = 0; i < N; ++i) {
        long ii = (long)i;
        smooth[i] = (4 * price[i] + 3 * at(price, ii - 1) +
                     2 * at(price, ii - 2) + at(price, ii - 3)) /
                    10;
        double adj = 0.075 * period + 0.54;
        detr[i] = (0.0962 * smooth[i] + 0.5769 * at(smooth, ii - 2) -
                   0.5769 * at(smooth, ii - 4) - 0.0962 * at(smooth, ii - 6)) *
                  adj;
        q1[i] = (0.0962 * detr[i] + 0.5769 * at(detr, ii - 2) -
                 0.5769 * at(detr, ii - 4) - 0.0962 * at(detr, ii - 6)) *
                adj;
        i1[i] = at(detr, ii - 3);
        ji[i] = (0.0962 * i1[i] + 0.5769 * at(i1, ii - 2) -
                 0.5769 * at(i1, ii - 4) - 0.0962 * at(i1, ii - 6)) *
                adj;
        jq[i] = (0.0962 * q1[i] + 0.5769 * at(q1, ii - 2) -
                 0.5769 * at(q1, ii - 4) - 0.0962 * at(q1, ii - 6)) *
                adj;
        double re_raw = i1[i] * ji[i] + q1[i] * jq[i];
        double im_raw = i1[i] * jq[i] - q1[i] * ji[i];
        re[i] = 0.2 * re_raw + 0.8 * at(re, ii - 1);
        im[i] = 0.2 * im_raw + 0.8 * at(im, ii - 1);
        if (im[i] != 0 && re[i] != 0) {
            double p = 2 * M_PI / std::atan(im[i] / re[i]);
            p = std::min(p, 1.5 * period);
            p = std::max(p, 0.67 * period);
            p = std::min(50.0, std::max(6.0, p));
            period = 0.2 * p + 0.8 * period;
        }
        if (i > 6) {
            r.period[i] = period;
            r.inphase[i] = i1[i];
            r.quadrature[i] = q1[i];
            double dp = i1[i] != 0 ? std::atan(q1[i] / i1[i]) * 180 / M_PI : 0;
            phase += dp;
            while (phase >= 360) phase -= 360;
            while (phase < 0) phase += 360;
            r.phase[i] = phase;
        }
    }
    return r;
}
static Lines f_ht_period(const std::vector<Bar>& b,
                         const std::vector<double>&) {
    return {hilbert(closes(b)).period};
}
static Lines f_ht_phase(const std::vector<Bar>& b,
                        const std::vector<double>&) {
    return {hilbert(closes(b)).phase};
}
static Lines f_ht_phasor(const std::vector<Bar>& b,
                         const std::vector<double>&) {
    HilbertData h = hilbert(closes(b));
    return {h.inphase, h.quadrature};
}
static Lines f_ht_sine(const std::vector<Bar>& b,
                       const std::vector<double>&) {
    HilbertData h = hilbert(closes(b));
    Series s1(b.size(), nanv()), s2(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(h.phase[i])) {
            double rad = h.phase[i] * M_PI / 180;
            s1[i] = std::sin(rad);
            s2[i] = std::sin(rad + M_PI / 4);
        }
    return {s1, s2};
}
static Series instantaneous_trendline(const Series& price) {
    Series out(price.size(), nanv());
    double alpha = 0.07;
    for (size_t i = 0; i < price.size(); ++i) {
        double p1 = i > 0 ? price[i - 1] : price[i];
        double p2 = i > 1 ? price[i - 2] : price[i];
        double t1 = i > 0 && fin(out[i - 1]) ? out[i - 1] : price[i];
        double t2 = i > 1 && fin(out[i - 2]) ? out[i - 2] : price[i];
        out[i] = (alpha - alpha * alpha / 4) * price[i] +
                 0.5 * alpha * alpha * p1 -
                 (alpha - 0.75 * alpha * alpha) * p2 +
                 2 * (1 - alpha) * t1 - (1 - alpha) * (1 - alpha) * t2;
    }
    return out;
}
static Lines f_ht_trendline(const std::vector<Bar>& b,
                            const std::vector<double>&) {
    return {instantaneous_trendline(closes(b))};
}
static Lines f_ht_trendmode(const std::vector<Bar>& b,
                            const std::vector<double>&) {
    HilbertData h = hilbert(closes(b));
    Series it = instantaneous_trendline(closes(b));
    Series c = closes(b), out(c.size(), nanv());
    int age = 0;
    for (size_t i = 1; i < c.size(); ++i) {
        if (!fin(it[i])) continue;
        if ((c[i] - it[i]) * (c[i - 1] - it[i - 1]) < 0) age = 0;
        else ++age;
        double per = fin(h.period[i]) ? h.period[i] : 20;
        out[i] = age > per ? 1 : 0;  // 1 = trending, 0 = cycling
    }
    return {out};
}
static Lines f_mama(const std::vector<Bar>& b, const std::vector<double>& p,
                    bool fama) {
    double fast_lim = pd(p, 0, 0.5), slow_lim = pd(p, 1, 0.05);
    HilbertData h = hilbert(closes(b));
    Series c = closes(b), out(c.size(), nanv());
    double m = 0, f = 0;
    bool started = false;
    for (size_t i = 1; i < c.size(); ++i) {
        if (!fin(h.phase[i]) || !fin(h.phase[i - 1])) continue;
        double dp = h.phase[i - 1] - h.phase[i];
        if (dp < 0) dp += 360;
        dp = std::max(1.0, dp);
        double alpha = std::min(fast_lim, std::max(slow_lim, fast_lim / dp));
        if (!started) {
            m = c[i];
            f = c[i];
            started = true;
        }
        m = alpha * c[i] + (1 - alpha) * m;
        f = 0.5 * alpha * m + (1 - 0.5 * alpha) * f;
        out[i] = fama ? f : m;
    }
    return {out};
}
static Lines f_mama_line(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    return f_mama(b, p, false);
}
static Lines f_fama_line(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    return f_mama(b, p, true);
}
static Lines f_cyber_cycle(const std::vector<Bar>& b,
                           const std::vector<double>& p) {
    double a = pd(p, 0, 0.07);
    Series c = closes(b), cyc(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i) {
        auto sm = [&](long k) {
            long i0 = (long)i - k;
            double v0 = c[std::max(0L, i0)], v1 = c[std::max(0L, i0 - 1)],
                   v2 = c[std::max(0L, i0 - 2)], v3 = c[std::max(0L, i0 - 3)];
            return (v0 + 2 * v1 + 2 * v2 + v3) / 6;
        };
        double s0 = sm(0), s1 = sm(1), s2 = sm(2);
        double c1 = i > 0 && fin(cyc[i - 1]) ? cyc[i - 1] : 0;
        double c2 = i > 1 && fin(cyc[i - 2]) ? cyc[i - 2] : 0;
        double k = 1 - 0.5 * a;
        cyc[i] = k * k * (s0 - 2 * s1 + s2) + 2 * (1 - a) * c1 -
                 (1 - a) * (1 - a) * c2;
    }
    return {cyc, shift_s(cyc, 1)};
}
static Lines f_cg(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = (size_t)(n - 1); i < c.size(); ++i) {
        double num = 0, den = 0;
        for (int j = 0; j < n; ++j) {
            num += (j + 1) * c[i - j];
            den += c[i - j];
        }
        if (den != 0) out[i] = -num / den + (n + 1) / 2.0;
    }
    return {out, shift_s(out, 1)};
}
static Lines f_ehlers_stoch(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series sm = super_smoother_s(closes(b), 10);
    Series mx = rolling_max(sm, n), mn = rolling_min(sm, n);
    Series st(sm.size(), nanv());
    for (size_t i = 0; i < sm.size(); ++i)
        if (fin(mx[i]) && fin(mn[i]) && mx[i] > mn[i])
            st[i] = (sm[i] - mn[i]) / (mx[i] - mn[i]);
    Series s1 = super_smoother_s(st, 5);
    return {s1, super_smoother_s(s1, 5)};
}
static Series highpass_s(const Series& in, int n) {
    Series out(in.size(), nanv());
    double w = 0.707 * 2 * M_PI / n;
    double a = (std::cos(w) + std::sin(w) - 1) / std::cos(w);
    for (size_t i = 0; i < in.size(); ++i) {
        double x1 = i > 0 ? in[i - 1] : in[i], x2 = i > 1 ? in[i - 2] : in[i];
        double h1 = i > 0 && fin(out[i - 1]) ? out[i - 1] : 0;
        double h2 = i > 1 && fin(out[i - 2]) ? out[i - 2] : 0;
        double k = 1 - a / 2;
        out[i] = k * k * (in[i] - 2 * x1 + x2) + 2 * (1 - a) * h1 -
                 (1 - a) * (1 - a) * h2;
    }
    return out;
}
static Lines f_roofing(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    (void)p;
    return {super_smoother_s(highpass_s(closes(b), 48), 10)};
}
static Lines f_bandpass(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    double bw = pd(p, 1, 0.3);
    Series c = closes(b), out(c.size(), nanv());
    double beta = std::cos(2 * M_PI / n);
    double gamma = 1 / std::cos(2 * M_PI * bw / n);
    double alpha = gamma - std::sqrt(gamma * gamma - 1);
    for (size_t i = 0; i < c.size(); ++i) {
        double x2 = i > 1 ? c[i - 2] : c[i];
        double b1 = i > 0 && fin(out[i - 1]) ? out[i - 1] : 0;
        double b2 = i > 1 && fin(out[i - 2]) ? out[i - 2] : 0;
        out[i] = 0.5 * (1 - alpha) * (c[i] - x2) +
                 beta * (1 + alpha) * b1 - alpha * b2;
    }
    return {out};
}
static Lines f_highpass(const std::vector<Bar>& b,
                        const std::vector<double>& p) {
    return {highpass_s(closes(b), pi(p, 0, 48))};
}
static Lines f_squeeze(const std::vector<Bar>& b,
                       const std::vector<double>& p) {
    int n = pi(p, 0, 20);
    Series c = closes(b);
    Series mid = scale_s(combine(rolling_max(highs(b), n),
                                 rolling_min(lows(b), n), '+'),
                         0.5);
    Series d = combine(c, scale_s(combine(mid, sma_s(c, n), '+'), 0.5), '-');
    LinReg r = linreg(d, n);
    return {r.value};
}
static Lines f_wavetrend(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    (void)p;
    Series ap = hlc3(b);
    Series esa = ema_s(ap, 10);
    Series d = ema_s(combine(ap, esa, '-'), 10);
    Series absd(b.size(), nanv());
    Series diff = combine(ap, esa, '-');
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(diff[i])) absd[i] = std::fabs(diff[i]);
    Series dd = ema_s(absd, 10);
    Series ci(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(dd[i]) && dd[i] > 0 && fin(d[i]))
            ci[i] = d[i] / (0.015 * dd[i]);
    Series wt1 = ema_s(ci, 21);
    return {wt1, sma_s(wt1, 4)};
}
static Lines f_pmo(const std::vector<Bar>& b, const std::vector<double>& p) {
    (void)p;
    Series roc1(b.size(), nanv());
    Series c = closes(b);
    for (size_t i = 1; i < c.size(); ++i)
        if (c[i - 1] != 0) roc1[i] = (c[i] / c[i - 1] - 1) * 100;
    Series s1 = ema_s(roc1, 35);
    Series s2 = ema_s(scale_s(s1, 10), 20);
    return {s2, ema_s(s2, 10)};
}

static void defs_volatility(std::vector<Def>& v) {
    auto O = PlotKind::Overlay, P = PlotKind::Pane;
    auto L = Style::Lines, B = Style::Bars;
    v.push_back({"ATR", P, L, 0, "14", f_atr});
    v.push_back({"NATR", P, L, 1, "14", f_natr});
    v.push_back({"True Range", P, L, 2, "", f_tr});
    v.push_back({"Standard Deviation", P, L, 3, "20", f_stddev});
    v.push_back({"Variance", P, L, 4, "20", f_variance});
    v.push_back({"Historical Volatility", P, L, 5, "20", f_hist_vol});
    v.push_back({"Chaikin Volatility", P, L, 6, "10,10", f_chaikin_vol});
    v.push_back({"Ulcer Index", P, L, 7, "14", f_ulcer});
    v.push_back({"Choppiness Index", P, L, 8, "14", f_choppiness});
    v.push_back({"Relative Volatility Index", P, L, 9, "10", f_rvol_idx});
    v.push_back({"Parkinson Volatility", P, L, 10, "20", f_parkinson});
    v.push_back({"Garman-Klass Volatility", P, L, 11, "20", f_garman_klass});
    v.push_back({"Rogers-Satchell Volatility", P, L, 12, "20", f_rogers_satchell});
    v.push_back({"Yang-Zhang Volatility", P, L, 13, "20", f_yang_zhang});
    v.push_back({"Market Thermometer", P, L, 0, "22", f_thermometer});
    v.push_back({"Standard Error", P, L, 1, "20", f_std_error});
    v.push_back({"Mean Deviation", P, L, 2, "20", f_mean_dev});
    v.push_back({"Skewness", P, L, 3, "20", f_skewness});
    v.push_back({"Kurtosis", P, L, 4, "20", f_kurtosis});
    v.push_back({"Z-Score", P, L, 5, "20", f_zscore});
    v.push_back({"Linear Regression Slope", P, L, 6, "20", f_lr_slope});
    v.push_back({"Linear Regression Intercept", P, L, 7, "20", f_lr_intercept});
    v.push_back({"Linear Regression Angle", P, L, 8, "20", f_lr_angle});
    v.push_back({"R-Squared", P, L, 9, "20", f_r_squared});
    v.push_back({"TSF", O, L, 10, "20", f_tsf});
    v.push_back({"Forecast Oscillator", P, L, 11, "20", f_forecast_osc});
    v.push_back({"HT DC Period", P, L, 12, "", f_ht_period});
    v.push_back({"HT DC Phase", P, L, 13, "", f_ht_phase});
    v.push_back({"HT Phasor", P, L, 0, "", f_ht_phasor});
    v.push_back({"HT Sine", P, L, 1, "", f_ht_sine});
    v.push_back({"HT Trend Mode", P, L, 2, "", f_ht_trendmode});
    v.push_back({"HT Trendline", O, L, 3, "", f_ht_trendline});
    v.push_back({"MAMA", O, L, 4, "0.5,0.05", f_mama_line});
    v.push_back({"FAMA", O, L, 5, "0.5,0.05", f_fama_line});
    v.push_back({"Cyber Cycle", P, L, 6, "0.07", f_cyber_cycle});
    v.push_back({"CG Oscillator", P, L, 7, "10", f_cg});
    v.push_back({"Ehlers Stochastic", P, L, 8, "14", f_ehlers_stoch});
    v.push_back({"Roofing Filter", P, L, 9, "", f_roofing});
    v.push_back({"Band-Pass Filter", P, L, 10, "20,0.3", f_bandpass});
    v.push_back({"High-Pass Filter", P, L, 11, "48", f_highpass});
    v.push_back({"Squeeze Momentum", P, B, 12, "20", f_squeeze});
    v.push_back({"WaveTrend", P, L, 13, "", f_wavetrend});
    v.push_back({"PMO", P, L, 0, "", f_pmo});
}

// ============================================================ candlestick patterns
// TA-Lib's 61 patterns plus five extras. Each returns markers:
// +1 bullish (triangle under the bar), -1 bearish (triangle above).

struct PC {
    double o, h, l, c, body, range, upper, lower;
    bool bull;
};
static std::vector<PC> pcs_of(const std::vector<Bar>& b) {
    std::vector<PC> v(b.size());
    for (size_t i = 0; i < b.size(); ++i) {
        PC& p = v[i];
        p.o = b[i].open; p.h = b[i].high; p.l = b[i].low; p.c = b[i].close;
        p.body = std::fabs(p.c - p.o);
        p.range = p.h - p.l;
        p.upper = p.h - std::max(p.o, p.c);
        p.lower = std::min(p.o, p.c) - p.l;
        p.bull = p.c >= p.o;
    }
    return v;
}
static double avg_before(const std::vector<PC>& v, size_t i, int n,
                         bool body) {
    double s = 0;
    int cnt = 0;
    for (size_t k = i >= (size_t)n ? i - n : 0; k < i; ++k) {
        s += body ? v[k].body : v[k].range;
        ++cnt;
    }
    return cnt ? s / cnt : 0;
}
static Series marks(const std::vector<Bar>& b) {
    return Series(b.size(), 0.0);
}
static bool is_doji(const PC& p) { return p.range > 0 && p.body <= 0.1 * p.range; }
static bool long_body(const std::vector<PC>& v, size_t i) {
    return v[i].body > avg_before(v, i, 10, true) && v[i].body > 0;
}
static bool small_body(const std::vector<PC>& v, size_t i) {
    return v[i].body < avg_before(v, i, 10, true);
}

static Lines pat_doji(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i)
        if (is_doji(v[i])) m[i] = 1;
    return {m};
}
static Lines pat_dragonfly(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i)
        if (is_doji(v[i]) && v[i].lower > 3 * v[i].body &&
            v[i].upper < v[i].lower * 0.25)
            m[i] = 1;
    return {m};
}
static Lines pat_gravestone(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i)
        if (is_doji(v[i]) && v[i].upper > 3 * v[i].body &&
            v[i].lower < v[i].upper * 0.25)
            m[i] = -1;
    return {m};
}
static Lines pat_longlegged(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i)
        if (is_doji(v[i]) && v[i].upper > v[i].range * 0.3 &&
            v[i].lower > v[i].range * 0.3)
            m[i] = 1;
    return {m};
}
static Lines pat_rickshaw(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i)
        if (is_doji(v[i]) && v[i].upper > v[i].range * 0.35 &&
            v[i].lower > v[i].range * 0.35)
            m[i] = 1;
    return {m};
}
static Lines pat_highwave(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (small_body(v, i) && v[i].upper > v[i].body * 2 &&
            v[i].lower > v[i].body * 2)
            m[i] = 1;
    return {m};
}
static Lines pat_spinningtop(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (small_body(v, i) && v[i].upper > v[i].body &&
            v[i].lower > v[i].body && !is_doji(v[i]))
            m[i] = 1;
    return {m};
}
static Lines pat_hammer(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (small_body(v, i) && v[i].lower > 2 * v[i].body &&
            v[i].upper < v[i].body && v[i - 1].c < v[i - 1].o + v[i - 1].body)
            m[i] = 1;
    return {m};
}
static Lines pat_takuri(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i)
        if (small_body(v, i) && v[i].body > 0 &&
            v[i].lower > 3 * avg_before(v, i, 10, false) * 0.3 &&
            v[i].upper < v[i].body)
            m[i] = 1;
    return {m};
}
static Lines pat_inv_hammer(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i)
        if (small_body(v, i) && v[i].upper > 2 * v[i].body &&
            v[i].lower < v[i].body)
            m[i] = 1;
    return {m};
}
static Lines pat_shooting_star(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (small_body(v, i) && v[i].upper > 2 * v[i].body &&
            v[i].lower < v[i].body && v[i].l > v[i - 1].h)
            m[i] = -1;
    return {m};
}
static Lines pat_hanging_man(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (small_body(v, i) && v[i].lower > 2 * v[i].body &&
            v[i].upper < v[i].body && v[i - 1].bull)
            m[i] = -1;
    return {m};
}
static Lines pat_engulfing(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && c.bull && c.c >= a.o && c.o <= a.c) m[i] = 1;
        if (a.bull && !c.bull && c.o >= a.c && c.c <= a.o) m[i] = -1;
    }
    return {m};
}
static Lines pat_harami(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!long_body(v, i - 1)) continue;
        bool inside = std::max(c.o, c.c) <= std::max(a.o, a.c) &&
                      std::min(c.o, c.c) >= std::min(a.o, a.c);
        if (inside) m[i] = a.bull ? -1 : 1;
    }
    return {m};
}
static Lines pat_harami_cross(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!long_body(v, i - 1) || !is_doji(c)) continue;
        bool inside = std::max(c.o, c.c) <= std::max(a.o, a.c) &&
                      std::min(c.o, c.c) >= std::min(a.o, a.c);
        if (inside) m[i] = a.bull ? -1 : 1;
    }
    return {m};
}
static Lines pat_homing_pigeon(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && !c.bull && c.o < a.o && c.c > a.c) m[i] = 1;
    }
    return {m};
}
static Lines pat_piercing(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && long_body(v, i - 1) && c.bull && c.o < a.l &&
            c.c > (a.o + a.c) / 2 && c.c < a.o)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_dark_cloud(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (a.bull && long_body(v, i - 1) && !c.bull && c.o > a.h &&
            c.c < (a.o + a.c) / 2 && c.c > a.o)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_thrusting(const std::vector<Bar>& b, const std::vector<double>&) {
    // Long white candle, then a black candle opening above its high and
    // closing into its body but NOT past the midpoint (that would be
    // Dark Cloud Cover).
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (a.bull && long_body(v, i - 1) && !c.bull && c.o > a.h &&
            c.c < a.c && c.c > (a.o + a.c) / 2)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_in_neck(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && long_body(v, i - 1) && c.bull && small_body(v, i) &&
            std::fabs(c.c - a.l) <= a.range * 0.05)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_on_neck(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && long_body(v, i - 1) && c.bull && small_body(v, i) &&
            c.c > a.l && c.c <= a.l + a.range * 0.1)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_counterattack(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (long_body(v, i - 1) && long_body(v, i) && a.bull != c.bull &&
            std::fabs(c.c - a.c) <= a.range * 0.05)
            m[i] = c.bull ? 1 : -1;
    }
    return {m};
}
static Lines pat_separating(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (a.bull != c.bull && std::fabs(c.o - a.o) <= a.range * 0.05 &&
            long_body(v, i))
            m[i] = c.bull ? 1 : -1;
    }
    return {m};
}
static Lines pat_matching_low(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && !c.bull &&
            std::fabs(c.c - a.c) <= a.range * 0.02)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_belt_hold(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i) {
        if (!long_body(v, i)) continue;
        if (v[i].bull && v[i].lower < v[i].body * 0.1) m[i] = 1;
        if (!v[i].bull && v[i].upper < v[i].body * 0.1) m[i] = -1;
    }
    return {m};
}
static Lines pat_marubozu(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (long_body(v, i) && v[i].upper < v[i].body * 0.1 &&
            v[i].lower < v[i].body * 0.1)
            m[i] = v[i].bull ? 1 : -1;
    return {m};
}
static Lines pat_closing_marubozu(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        if (!long_body(v, i)) continue;
        if (v[i].bull && v[i].upper < v[i].body * 0.1) m[i] = 1;
        if (!v[i].bull && v[i].lower < v[i].body * 0.1) m[i] = -1;
    }
    return {m};
}
static Lines pat_long_line(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (long_body(v, i) && !is_doji(v[i])) m[i] = v[i].bull ? 1 : -1;
    return {m};
}
static Lines pat_short_line(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (small_body(v, i) && v[i].range < avg_before(v, i, 10, false) &&
            !is_doji(v[i]))
            m[i] = v[i].bull ? 1 : -1;
    return {m};
}
static Lines pat_doji_star(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        if (!long_body(v, i - 1) || !is_doji(c)) continue;
        if (a.bull && std::min(c.o, c.c) > a.c) m[i] = -1;
        if (!a.bull && std::max(c.o, c.c) < a.c) m[i] = 1;
    }
    return {m};
}
static Lines pat_morning_star(const std::vector<Bar>& b, const std::vector<double>&, bool doji) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& s = v[i - 1];
        const PC& c = v[i];
        bool star_ok = doji ? is_doji(s) : small_body(v, i - 1);
        if (!a.bull && long_body(v, i - 2) && star_ok && c.bull &&
            long_body(v, i) && std::max(s.o, s.c) < a.c &&
            c.c > (a.o + a.c) / 2)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_evening_star(const std::vector<Bar>& b, const std::vector<double>&, bool doji) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& s = v[i - 1];
        const PC& c = v[i];
        bool star_ok = doji ? is_doji(s) : small_body(v, i - 1);
        if (a.bull && long_body(v, i - 2) && star_ok && !c.bull &&
            long_body(v, i) && std::min(s.o, s.c) > a.c &&
            c.c < (a.o + a.c) / 2)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_morning_star_w(const std::vector<Bar>& b, const std::vector<double>& p) {
    return pat_morning_star(b, p, false);
}
static Lines pat_morning_doji_star_w(const std::vector<Bar>& b, const std::vector<double>& p) {
    return pat_morning_star(b, p, true);
}
static Lines pat_evening_star_w(const std::vector<Bar>& b, const std::vector<double>& p) {
    return pat_evening_star(b, p, false);
}
static Lines pat_evening_doji_star_w(const std::vector<Bar>& b, const std::vector<double>& p) {
    return pat_evening_star(b, p, true);
}
static Lines pat_abandoned_baby(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& s = v[i - 1];
        const PC& c = v[i];
        if (!is_doji(s)) continue;
        if (!a.bull && c.bull && s.h < a.l && s.h < c.l) m[i] = 1;
        if (a.bull && !c.bull && s.l > a.h && s.l > c.h) m[i] = -1;
    }
    return {m};
}
static Lines pat_soldiers(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (a.bull && d.bull && c.bull && d.o > a.o && d.o < a.c &&
            c.o > d.o && c.o < d.c && c.c > d.c && d.c > a.c)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_crows(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && !d.bull && !c.bull && d.o < a.o && d.o > a.c &&
            c.o < d.o && c.o > d.c && c.c < d.c && d.c < a.c)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_identical_crows(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && !d.bull && !c.bull &&
            std::fabs(d.o - a.c) <= a.range * 0.02 &&
            std::fabs(c.o - d.c) <= d.range * 0.02)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_two_crows(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (a.bull && long_body(v, i - 2) && !d.bull && !c.bull &&
            d.o > a.c && c.o > d.c && c.c < d.c && c.c > a.o && c.c < a.c)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_upside_gap_crows(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (a.bull && !d.bull && !c.bull && d.l > a.h && c.o > d.c &&
            c.c < d.c && c.c > a.h)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_advance_block(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (a.bull && d.bull && c.bull && long_body(v, i - 2) &&
            d.body < a.body && c.body < d.body && d.upper > d.body &&
            c.upper > c.body)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_stalled(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (a.bull && d.bull && long_body(v, i - 2) &&
            (is_doji(c) || small_body(v, i)) && c.o > d.o && c.o < d.c)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_three_inside(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& s = v[i - 1];
        const PC& c = v[i];
        bool inside = std::max(s.o, s.c) <= std::max(a.o, a.c) &&
                      std::min(s.o, s.c) >= std::min(a.o, a.c);
        if (!inside || !long_body(v, i - 2)) continue;
        if (!a.bull && c.bull && c.c > a.o) m[i] = 1;
        if (a.bull && !c.bull && c.c < a.o) m[i] = -1;
    }
    return {m};
}
static Lines pat_three_outside(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& e = v[i - 1];
        const PC& c = v[i];
        bool engulf = (!a.bull && e.bull && e.c >= a.o && e.o <= a.c) ||
                      (a.bull && !e.bull && e.o >= a.c && e.c <= a.o);
        if (!engulf) continue;
        if (e.bull && c.bull && c.c > e.c) m[i] = 1;
        if (!e.bull && !c.bull && c.c < e.c) m[i] = -1;
    }
    return {m};
}
static Lines pat_line_strike(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 3; i < v.size(); ++i) {
        const PC& a = v[i - 3];
        const PC& d = v[i - 2];
        const PC& e = v[i - 1];
        const PC& c = v[i];
        if (a.bull && d.bull && e.bull && !c.bull && c.o > e.c &&
            c.c < a.o)
            m[i] = -1;
        if (!a.bull && !d.bull && !e.bull && c.bull && c.o < e.c &&
            c.c > a.o)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_stars_south(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && !d.bull && !c.bull && long_body(v, i - 2) &&
            d.body < a.body && c.body < d.body && d.lower > d.body &&
            c.lower > c.body && c.l > d.l)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_concealing_swallow(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 3; i < v.size(); ++i) {
        const PC& a = v[i - 3];
        const PC& d = v[i - 2];
        const PC& e = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && !d.bull && !e.bull && !c.bull &&
            a.upper < a.body * 0.1 && a.lower < a.body * 0.1 &&
            e.h > d.c && c.o > e.h && c.c < e.l)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_ladder_bottom(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 4; i < v.size(); ++i) {
        const PC& a = v[i - 4];
        const PC& d = v[i - 3];
        const PC& e = v[i - 2];
        const PC& f = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && !d.bull && !e.bull && !f.bull && f.upper > f.body &&
            c.bull && c.o > f.o && c.c > f.h)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_breakaway(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 4; i < v.size(); ++i) {
        const PC& a = v[i - 4];
        const PC& c = v[i];
        bool mid_same = v[i - 3].bull == a.bull && v[i - 2].bull == a.bull &&
                        v[i - 1].bull == a.bull;
        if (!mid_same) continue;
        if (!a.bull && c.bull && v[i - 3].h < a.l && c.c < a.c &&
            c.c > v[i - 3].l)
            m[i] = 1;
        if (a.bull && !c.bull && v[i - 3].l > a.h && c.c > a.c &&
            c.c < v[i - 3].h)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_mat_hold(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 4; i < v.size(); ++i) {
        const PC& a = v[i - 4];
        const PC& c = v[i];
        bool smalls = small_body(v, i - 3) && small_body(v, i - 2) &&
                      small_body(v, i - 1);
        if (!smalls || !long_body(v, i - 4) || !long_body(v, i)) continue;
        if (a.bull && c.bull && v[i - 3].o > a.c && c.c > a.c) m[i] = 1;
        if (!a.bull && !c.bull && v[i - 3].o < a.c && c.c < a.c) m[i] = -1;
    }
    return {m};
}
static Lines pat_risefall_methods(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 4; i < v.size(); ++i) {
        const PC& a = v[i - 4];
        const PC& c = v[i];
        bool smalls = small_body(v, i - 3) && small_body(v, i - 2) &&
                      small_body(v, i - 1);
        bool within = std::max(v[i - 2].h, v[i - 1].h) <= a.h &&
                      std::min(v[i - 2].l, v[i - 1].l) >= a.l;
        if (!smalls || !within || !long_body(v, i - 4) ||
            !long_body(v, i))
            continue;
        if (a.bull && c.bull && !v[i - 2].bull && c.c > a.c) m[i] = 1;
        if (!a.bull && !c.bull && v[i - 2].bull && c.c < a.c) m[i] = -1;
    }
    return {m};
}
static Lines pat_kicking(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        bool maru_a = a.upper < a.body * 0.1 && a.lower < a.body * 0.1;
        bool maru_c = c.upper < c.body * 0.1 && c.lower < c.body * 0.1;
        if (!maru_a || !maru_c || a.bull == c.bull) continue;
        bool gap = c.bull ? c.l > a.h : c.h < a.l;
        if (gap) m[i] = c.bull ? 1 : -1;
    }
    return {m};
}
static Lines pat_kicking_length(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        const PC& a = v[i - 1];
        const PC& c = v[i];
        bool maru_a = a.upper < a.body * 0.1 && a.lower < a.body * 0.1;
        bool maru_c = c.upper < c.body * 0.1 && c.lower < c.body * 0.1;
        if (!maru_a || !maru_c || a.bull == c.bull) continue;
        bool gap = c.bull ? c.l > a.h : c.h < a.l;
        if (gap) m[i] = c.body >= a.body ? (c.bull ? 1 : -1)
                                         : (a.bull ? 1 : -1);
    }
    return {m};
}
static Lines pat_tasuki_gap(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (a.bull && d.bull && !c.bull && d.l > a.h && c.o < d.c &&
            c.o > d.o && c.c > a.h && c.c < d.l)
            m[i] = 1;
        if (!a.bull && !d.bull && c.bull && d.h < a.l && c.o > d.c &&
            c.o < d.o && c.c < a.l && c.c > d.h)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_gap_side_white(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (d.bull && c.bull && d.l > a.h &&
            std::fabs(c.o - d.o) <= d.range * 0.1)
            m[i] = 1;
        if (!d.bull && !c.bull && d.h < a.l &&
            std::fabs(c.o - d.o) <= d.range * 0.1)
            m[i] = -1;
    }
    return {m};
}
static Lines pat_xside_gap_methods(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& d = v[i - 1];
        const PC& c = v[i];
        const PC& a = v[i - 2];
        if (d.bull && !c.bull && d.l > a.h && c.o > d.c && c.c < d.o)
            m[i] = -1;
        if (!d.bull && c.bull && d.h < a.l && c.o < d.c && c.c > d.o)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_hikkake(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& in = v[i - 1];
        const PC& c = v[i];
        bool inside = in.h < a.h && in.l > a.l;
        if (!inside) continue;
        if (c.c < in.l) m[i] = 1;   // failed breakdown -> bullish hikkake
        if (c.c > in.h) m[i] = -1;  // failed breakout -> bearish
    }
    return {m};
}
static Lines pat_hikkake_mod(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 3; i < v.size(); ++i) {
        const PC& a = v[i - 3];
        const PC& in = v[i - 2];
        const PC& f = v[i - 1];
        const PC& c = v[i];
        bool inside = in.h < a.h && in.l > a.l;
        if (!inside) continue;
        if (f.c < in.l && c.c > in.l && c.c < in.h) m[i] = 1;
        if (f.c > in.h && c.c < in.h && c.c > in.l) m[i] = -1;
    }
    return {m};
}
static Lines pat_tristar(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        if (is_doji(v[i - 2]) && is_doji(v[i - 1]) && is_doji(v[i])) {
            if (v[i - 1].l > v[i - 2].h && v[i - 1].l > v[i].h) m[i] = -1;
            else if (v[i - 1].h < v[i - 2].l && v[i - 1].h < v[i].l)
                m[i] = 1;
            else m[i] = 1;
        }
    }
    return {m};
}
static Lines pat_unique_river(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && long_body(v, i - 2) && small_body(v, i - 1) &&
            d.lower > d.body * 2 && c.bull && c.c > d.h)
            m[i] = 1;
    }
    return {m};
}
static Lines pat_stick_sandwich(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 2; i < v.size(); ++i) {
        const PC& a = v[i - 2];
        const PC& d = v[i - 1];
        const PC& c = v[i];
        if (!a.bull && d.bull && !c.bull &&
            std::fabs(c.c - a.c) <= a.range * 0.02)
            m[i] = 1;
    }
    return {m};
}
// Extra patterns (beyond TA-Lib's 61).
static Lines pat_inside_bar(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i].h < v[i - 1].h && v[i].l > v[i - 1].l)
            m[i] = v[i].bull ? 1 : -1;
    return {m};
}
static Lines pat_outside_bar(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i)
        if (v[i].h > v[i - 1].h && v[i].l < v[i - 1].l)
            m[i] = v[i].bull ? 1 : -1;
    return {m};
}
static Lines pat_pin_bar(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].range <= 0) continue;
        if (v[i].lower > v[i].range * 0.6) m[i] = 1;
        if (v[i].upper > v[i].range * 0.6) m[i] = -1;
    }
    return {m};
}
static Lines pat_nr7(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 6; i < v.size(); ++i) {
        bool narrowest = true;
        for (int j = 1; j < 7; ++j)
            if (v[i - j].range < v[i].range) narrowest = false;
        if (narrowest) m[i] = 1;
    }
    return {m};
}
static Lines pat_gap(const std::vector<Bar>& b, const std::vector<double>&) {
    auto v = pcs_of(b);
    Series m = marks(b);
    for (size_t i = 1; i < v.size(); ++i) {
        if (v[i].l > v[i - 1].h) m[i] = 1;
        if (v[i].h < v[i - 1].l) m[i] = -1;
    }
    return {m};
}

static void defs_patterns(std::vector<Def>& v) {
    auto M = PlotKind::Markers;
    auto L = Style::Lines;
    v.push_back({"Doji", M, L, 0, "", pat_doji});
    v.push_back({"Dragonfly Doji", M, L, 1, "", pat_dragonfly});
    v.push_back({"Gravestone Doji", M, L, 2, "", pat_gravestone});
    v.push_back({"Long Legged Doji", M, L, 3, "", pat_longlegged});
    v.push_back({"Rickshaw Man", M, L, 4, "", pat_rickshaw});
    v.push_back({"High Wave", M, L, 5, "", pat_highwave});
    v.push_back({"Spinning Top", M, L, 6, "", pat_spinningtop});
    v.push_back({"Hammer", M, L, 7, "", pat_hammer});
    v.push_back({"Takuri", M, L, 8, "", pat_takuri});
    v.push_back({"Inverted Hammer", M, L, 9, "", pat_inv_hammer});
    v.push_back({"Shooting Star", M, L, 10, "", pat_shooting_star});
    v.push_back({"Hanging Man", M, L, 11, "", pat_hanging_man});
    v.push_back({"Engulfing", M, L, 12, "", pat_engulfing});
    v.push_back({"Harami", M, L, 13, "", pat_harami});
    v.push_back({"Harami Cross", M, L, 0, "", pat_harami_cross});
    v.push_back({"Homing Pigeon", M, L, 1, "", pat_homing_pigeon});
    v.push_back({"Piercing Line", M, L, 2, "", pat_piercing});
    v.push_back({"Dark Cloud Cover", M, L, 3, "", pat_dark_cloud});
    v.push_back({"Thrusting", M, L, 4, "", pat_thrusting});
    v.push_back({"In-Neck", M, L, 5, "", pat_in_neck});
    v.push_back({"On-Neck", M, L, 6, "", pat_on_neck});
    v.push_back({"Counterattack", M, L, 7, "", pat_counterattack});
    v.push_back({"Separating Lines", M, L, 8, "", pat_separating});
    v.push_back({"Matching Low", M, L, 9, "", pat_matching_low});
    v.push_back({"Belt Hold", M, L, 10, "", pat_belt_hold});
    v.push_back({"Marubozu", M, L, 11, "", pat_marubozu});
    v.push_back({"Closing Marubozu", M, L, 12, "", pat_closing_marubozu});
    v.push_back({"Long Line", M, L, 13, "", pat_long_line});
    v.push_back({"Short Line", M, L, 0, "", pat_short_line});
    v.push_back({"Doji Star", M, L, 1, "", pat_doji_star});
    v.push_back({"Morning Star", M, L, 2, "", pat_morning_star_w});
    v.push_back({"Morning Doji Star", M, L, 3, "", pat_morning_doji_star_w});
    v.push_back({"Evening Star", M, L, 4, "", pat_evening_star_w});
    v.push_back({"Evening Doji Star", M, L, 5, "", pat_evening_doji_star_w});
    v.push_back({"Abandoned Baby", M, L, 6, "", pat_abandoned_baby});
    v.push_back({"Three White Soldiers", M, L, 7, "", pat_soldiers});
    v.push_back({"Three Black Crows", M, L, 8, "", pat_crows});
    v.push_back({"Identical Three Crows", M, L, 9, "", pat_identical_crows});
    v.push_back({"Two Crows", M, L, 10, "", pat_two_crows});
    v.push_back({"Upside Gap Two Crows", M, L, 11, "", pat_upside_gap_crows});
    v.push_back({"Advance Block", M, L, 12, "", pat_advance_block});
    v.push_back({"Stalled Pattern", M, L, 13, "", pat_stalled});
    v.push_back({"Three Inside", M, L, 0, "", pat_three_inside});
    v.push_back({"Three Outside", M, L, 1, "", pat_three_outside});
    v.push_back({"Three Line Strike", M, L, 2, "", pat_line_strike});
    v.push_back({"Three Stars In The South", M, L, 3, "", pat_stars_south});
    v.push_back({"Concealing Baby Swallow", M, L, 4, "", pat_concealing_swallow});
    v.push_back({"Ladder Bottom", M, L, 5, "", pat_ladder_bottom});
    v.push_back({"Breakaway", M, L, 6, "", pat_breakaway});
    v.push_back({"Mat Hold", M, L, 7, "", pat_mat_hold});
    v.push_back({"Rising/Falling Three Methods", M, L, 8, "", pat_risefall_methods});
    v.push_back({"Kicking", M, L, 9, "", pat_kicking});
    v.push_back({"Kicking By Length", M, L, 10, "", pat_kicking_length});
    v.push_back({"Tasuki Gap", M, L, 11, "", pat_tasuki_gap});
    v.push_back({"Gap Side Side White", M, L, 12, "", pat_gap_side_white});
    v.push_back({"X Side Gap Three Methods", M, L, 13, "", pat_xside_gap_methods});
    v.push_back({"Hikkake", M, L, 0, "", pat_hikkake});
    v.push_back({"Hikkake Modified", M, L, 1, "", pat_hikkake_mod});
    v.push_back({"Tristar", M, L, 2, "", pat_tristar});
    v.push_back({"Unique Three River", M, L, 3, "", pat_unique_river});
    v.push_back({"Stick Sandwich", M, L, 4, "", pat_stick_sandwich});
    v.push_back({"Inside Bar", M, L, 5, "", pat_inside_bar});
    v.push_back({"Outside Bar", M, L, 6, "", pat_outside_bar});
    v.push_back({"Pin Bar", M, L, 7, "", pat_pin_bar});
    v.push_back({"NR7", M, L, 8, "", pat_nr7});
    v.push_back({"Gap", M, L, 9, "", pat_gap});
}

// ============================================================ final entries + registry

static Lines f_vhf(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 28);
    Series hh = rolling_max(closes(b), n), ll = rolling_min(closes(b), n);
    Series d = diff_s(closes(b));
    for (double& v : d)
        if (fin(v)) v = std::fabs(v);
    Series sum = rolling_sum(d, n);
    Series out(b.size(), nanv());
    for (size_t i = 0; i < b.size(); ++i)
        if (fin(hh[i]) && fin(sum[i]) && sum[i] > 0)
            out[i] = (hh[i] - ll[i]) / sum[i];
    return {out};
}
static Lines f_disparity(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), m = sma_s(c, n);
    Series out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(m[i]) && m[i] != 0) out[i] = (c[i] - m[i]) / m[i] * 100;
    return {out};
}
static Lines f_atr_trail(const std::vector<Bar>& b,
                         const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    double k = pd(p, 1, 3.0);
    Series atr = atr_s(b, n), c = closes(b), out(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i) {
        if (!fin(atr[i])) continue;
        double stop = c[i] - k * atr[i];
        out[i] = fin(out[i - 1 < c.size() ? i - 1 : 0]) && i > 0
                     ? std::max(stop, out[i - 1])
                     : stop;
        if (c[i] < out[i]) out[i] = stop;  // reset on close-through
    }
    return {out};
}
static Lines f_pfe(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 14);
    Series c = closes(b), out(c.size(), nanv());
    for (size_t i = (size_t)n; i < c.size(); ++i) {
        double dist = 0;
        for (int j = 0; j < n; ++j) {
            double d = c[i - j] - c[i - j - 1];
            dist += std::sqrt(1 + d * d);
        }
        double direct = std::sqrt(std::pow(c[i] - c[i - n], 2) + n * n);
        if (dist > 0)
            out[i] = direct / dist * 100 * (c[i] >= c[i - n] ? 1 : -1);
    }
    return {out};
}
static Lines f_efficiency(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    int n = pi(p, 0, 10);
    Series c = closes(b), d = diff_s(c);
    for (double& v : d)
        if (fin(v)) v = std::fabs(v);
    Series vol = rolling_sum(d, n), out(c.size(), nanv());
    for (size_t i = (size_t)n; i < c.size(); ++i)
        if (fin(vol[i]) && vol[i] > 0)
            out[i] = std::fabs(c[i] - c[i - n]) / vol[i] * 100;
    return {out};
}
static Lines f_tii(const std::vector<Bar>& b, const std::vector<double>& p) {
    int n = pi(p, 0, 30);
    Series c = closes(b), m = sma_s(c, n);
    Series dev(c.size(), nanv());
    for (size_t i = 0; i < c.size(); ++i)
        if (fin(m[i])) dev[i] = c[i] - m[i];
    Series out(c.size(), nanv());
    for (size_t i = (size_t)(n - 1); i < c.size(); ++i) {
        double pos = 0, neg = 0;
        bool ok = true;
        for (int j = 0; j < n; ++j) {
            if (!fin(dev[i - j])) ok = false;
            if (dev[i - j] > 0) pos += dev[i - j];
            else neg -= dev[i - j];
        }
        if (ok && pos + neg > 0) out[i] = pos / (pos + neg) * 100;
    }
    return {out};
}
static Lines f_rolling_sum(const std::vector<Bar>& b,
                           const std::vector<double>& p) {
    return {rolling_sum(closes(b), pi(p, 0, 14))};
}
static Lines f_highest_high(const std::vector<Bar>& b,
                            const std::vector<double>& p) {
    return {rolling_max(highs(b), pi(p, 0, 20))};
}
static Lines f_lowest_low(const std::vector<Bar>& b,
                          const std::vector<double>& p) {
    return {rolling_min(lows(b), pi(p, 0, 20))};
}

static void defs_final(std::vector<Def>& v) {
    auto O = PlotKind::Overlay, P = PlotKind::Pane;
    auto L = Style::Lines;
    v.push_back({"VHF", P, L, 0, "28", f_vhf});
    v.push_back({"Disparity Index", P, L, 1, "14", f_disparity});
    v.push_back({"ATR Trailing Stop", O, L, 3, "14,3", f_atr_trail});
    v.push_back({"Polarized Fractal Efficiency", P, L, 4, "14", f_pfe});
    v.push_back({"Efficiency Ratio", P, L, 5, "10", f_efficiency});
    v.push_back({"Trend Intensity Index", P, L, 6, "30", f_tii});
    v.push_back({"Rolling Sum", P, L, 7, "14", f_rolling_sum});
    v.push_back({"Highest High", O, L, 8, "20", f_highest_high});
    v.push_back({"Lowest Low", O, L, 9, "20", f_lowest_low});
}

// ------------------------------------------------------------- registry

inline const std::vector<Def>& all_defs() {
    static const std::vector<Def> defs = [] {
        std::vector<Def> v;
        defs_averages(v);
        defs_bands(v);
        defs_momentum(v);
        defs_volume(v);
        defs_volatility(v);
        defs_patterns(v);
        defs_final(v);
        return v;
    }();
    return defs;
}

static std::string lower_str(std::string s) {
    for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
    return s;
}
static std::string trim_str(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    size_t e = s.find_last_not_of(" \t");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

inline const Def* find_def(const std::string& name) {
    std::string want = lower_str(trim_str(name));
    for (const Def& d : all_defs())
        if (lower_str(d.name) == want) return &d;
    return nullptr;
}

inline std::vector<std::string> indicator_names() {
    std::vector<std::string> out;
    for (const Def& d : all_defs()) out.push_back(d.name);
    return out;
}

static std::vector<double> parse_params(const std::string& s) {
    std::vector<double> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t comma = s.find(',', pos);
        std::string tok =
            trim_str(s.substr(pos, comma == std::string::npos
                                       ? std::string::npos
                                       : comma - pos));
        if (!tok.empty()) out.push_back(std::atof(tok.c_str()));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return out;
}

inline std::unique_ptr<Indicator> make_indicator(const std::string& spec,
                                                 std::string* err) {
    std::string name = trim_str(spec);
    std::string param_str;
    size_t open = spec.find('(');
    if (open != std::string::npos) {
        size_t close = spec.rfind(')');
        name = trim_str(spec.substr(0, open));
        if (close != std::string::npos && close > open)
            param_str = spec.substr(open + 1, close - open - 1);
    }
    const Def* def = find_def(name);
    if (!def) {
        if (err) *err = "unknown indicator '" + name + "'";
        return nullptr;
    }
    std::vector<double> params = parse_params(def->defaults);
    std::vector<double> given = parse_params(param_str);
    for (size_t i = 0; i < given.size(); ++i) {
        if (i < params.size())
            params[i] = given[i];
        else
            params.push_back(given[i]);
    }
    return std::unique_ptr<Indicator>(new Indicator(def, params));
}

}  // namespace fdi
)FDINDICATORS7f3a";
