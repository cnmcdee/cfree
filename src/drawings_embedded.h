// Generated from src/drawings.h by tools/embed_drawings.sh.
// Do not edit; run 'make' to regenerate.
#pragma once
static const char kDrawingsHeader[] = R"FDDRAWINGS9b2c(
// drawings.h — chart annotation module for the StockChart widget.
//
// One self-contained header (namespace fdw), in the same spirit as
// indicators.h. A Drawing is stored in DATA coordinates — an anchor is
// (bar index, price) — never in pixels, so annotations stay glued to
// the candles they were drawn on through widget resizes, zooms and
// pans: every render re-maps data -> pixels through the current Map.
//
// Kinds: trend line, ray, horizontal line, vertical line, rectangle,
// ellipse, arrow, text label, Fibonacci retracement, freehand stroke,
// and a measure tool (delta price / percent / bars between two points).
//
// Drawings can be created three ways: interactively (the generated
// StockChart's mouse tools build them), from a text spec via
// parse_spec()/parse_specs(), or directly in code. Spec grammar
// (semicolon-separated in parse_specs):
//
//   trend   x1 y1 x2 y2 [colour] [dash]
//   ray     x1 y1 x2 y2 [colour] [dash]
//   arrow   x1 y1 x2 y2 [colour] [dash]
//   rect    x1 y1 x2 y2 [colour] [dash]
//   ellipse x1 y1 x2 y2 [colour] [dash]
//   fib     x1 y1 x2 y2 [colour]
//   measure x1 y1 x2 y2 [colour]
//   hline   price [colour] [dash]
//   vline   bar [colour] [dash]
//   text    x y [colour] label words...
//   free    x1 y1 x2 y2 ... [colour]
//
// x is a bar index (fractional allowed), y a price. Colours by name:
// blue red green orange purple teal magenta brown black slate.
#pragma once

#include <cairo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace fdw {

// One anchor point in data coordinates.
struct Anchor {
    double bar = 0;    // bar index (fractional = between bars)
    double price = 0;  // price
};

enum class Kind {
    Trend,
    Ray,
    HLine,
    VLine,
    Rect,
    Ellipse,
    Arrow,
    Text,
    Fib,
    Freehand,
    Measure
};

struct Drawing {
    Kind kind = Kind::Trend;
    std::vector<Anchor> pts;  // 1 for hline/vline/text, 2 for most,
                              // N for freehand
    int color = 0;            // palette index
    double width = 1.5;       // line width, px
    bool dash = false;
    std::string text;         // Text kind only
};

// Drawing palette (strong, saturated colours that read on white).
inline const double kPalette[][3] = {
    {0.10, 0.25, 0.80},  // 0 blue
    {0.85, 0.10, 0.20},  // 1 red
    {0.00, 0.55, 0.15},  // 2 green
    {0.95, 0.50, 0.05},  // 3 orange
    {0.55, 0.20, 0.75},  // 4 purple
    {0.00, 0.60, 0.60},  // 5 teal
    {0.85, 0.20, 0.65},  // 6 magenta
    {0.55, 0.35, 0.15},  // 7 brown
    {0.10, 0.10, 0.10},  // 8 black
    {0.35, 0.40, 0.50},  // 9 slate
};
inline const int kPaletteSize = 10;

inline int color_index_of(const std::string& name) {
    static const char* kNames[] = {"blue",   "red",  "green", "orange",
                                   "purple", "teal", "magenta", "brown",
                                   "black",  "slate"};
    std::string low;
    for (char c : name) low += (char)std::tolower((unsigned char)c);
    for (int i = 0; i < kPaletteSize; ++i)
        if (low == kNames[i]) return i;
    return -1;
}

inline const char* kind_name(Kind k) {
    switch (k) {
        case Kind::Trend: return "trend";
        case Kind::Ray: return "ray";
        case Kind::HLine: return "hline";
        case Kind::VLine: return "vline";
        case Kind::Rect: return "rect";
        case Kind::Ellipse: return "ellipse";
        case Kind::Arrow: return "arrow";
        case Kind::Text: return "text";
        case Kind::Fib: return "fib";
        case Kind::Freehand: return "freehand";
        case Kind::Measure: return "measure";
    }
    return "?";
}

inline bool kind_from_name(const std::string& s, Kind& out) {
    std::string low;
    for (char c : s) low += (char)std::tolower((unsigned char)c);
    if (low == "trend") out = Kind::Trend;
    else if (low == "ray") out = Kind::Ray;
    else if (low == "hline" || low == "horizontal") out = Kind::HLine;
    else if (low == "vline" || low == "vertical") out = Kind::VLine;
    else if (low == "rect" || low == "rectangle") out = Kind::Rect;
    else if (low == "ellipse" || low == "oval") out = Kind::Ellipse;
    else if (low == "arrow") out = Kind::Arrow;
    else if (low == "text" || low == "label") out = Kind::Text;
    else if (low == "fib" || low == "fibonacci") out = Kind::Fib;
    else if (low == "free" || low == "freehand") out = Kind::Freehand;
    else if (low == "measure") out = Kind::Measure;
    else return false;
    return true;
}

// The data -> pixel mapping for one render. x/y/w/h is the price-pane
// plot rectangle in pixels; begin/count is the visible window in bar
// units; lo/hi is the visible price range. Bar centres sit at
// (bar - begin + 0.5) slots, matching the candle layout.
struct Map {
    double x = 0, y = 0, w = 0, h = 0;
    double begin = 0, count = 1;
    double lo = 0, hi = 1;

    double px(double bar) const {
        return x + (bar - begin + 0.5) * (w / count);
    }
    double py(double price) const {
        return y + (hi - price) / (hi - lo) * h;
    }
    double bar_at(double px_) const {
        return begin + (px_ - x) / (w / count) - 0.5;
    }
    double price_at(double py_) const {
        return hi - (py_ - y) / h * (hi - lo);
    }
};

namespace detail {

inline void set_style(cairo_t* cr, const Drawing& d, bool selected) {
    int ci = ((d.color % kPaletteSize) + kPaletteSize) % kPaletteSize;
    const double* c = kPalette[ci];
    cairo_set_source_rgba(cr, c[0], c[1], c[2], 0.92);
    cairo_set_line_width(cr, d.width + (selected ? 1.0 : 0.0));
    if (d.dash) {
        static const double kDash[] = {6, 4};
        cairo_set_dash(cr, kDash, 2, 0);
    } else {
        cairo_set_dash(cr, nullptr, 0, 0);
    }
}

inline double seg_dist(double px, double py, double x1, double y1,
                       double x2, double y2) {
    double dx = x2 - x1, dy = y2 - y1;
    double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? ((px - x1) * dx + (py - y1) * dy) / len2 : 0;
    t = std::max(0.0, std::min(1.0, t));
    double ex = x1 + t * dx - px, ey = y1 + t * dy - py;
    return std::sqrt(ex * ex + ey * ey);
}

// A small filled label with a light backing box (text + measure).
inline void label_box(cairo_t* cr, double x, double y,
                      const std::string& s, const double* col) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 11);
    cairo_text_extents_t te;
    cairo_text_extents(cr, s.c_str(), &te);
    cairo_set_source_rgba(cr, 1, 1, 1, 0.85);
    cairo_rectangle(cr, x - 3, y - te.height - 4, te.width + 6,
                    te.height + 5);
    cairo_fill(cr);
    cairo_set_source_rgba(cr, col[0], col[1], col[2], 1.0);
    cairo_move_to(cr, x, y - 3);
    cairo_show_text(cr, s.c_str());
}

}  // namespace detail

// Render one drawing. Clips to the plot rectangle (rays and lines may
// run off the edge). `selected` draws it slightly bolder.
inline void draw(cairo_t* cr, const Map& m, const Drawing& d,
                 bool selected = false) {
    if (d.pts.empty() || m.w < 2 || m.h < 2 || m.hi - m.lo < 1e-12)
        return;
    int ci = ((d.color % kPaletteSize) + kPaletteSize) % kPaletteSize;
    const double* col = kPalette[ci];
    cairo_save(cr);
    cairo_rectangle(cr, m.x - 40, m.y - 40, m.w + 80, m.h + 80);
    cairo_clip(cr);
    detail::set_style(cr, d, selected);
    auto X = [&](const Anchor& a) { return m.px(a.bar); };
    auto Y = [&](const Anchor& a) { return m.py(a.price); };
    switch (d.kind) {
        case Kind::Trend:
        case Kind::Measure: {
            if (d.pts.size() < 2) break;
            cairo_move_to(cr, X(d.pts[0]), Y(d.pts[0]));
            cairo_line_to(cr, X(d.pts[1]), Y(d.pts[1]));
            cairo_stroke(cr);
            if (d.kind == Kind::Measure) {
                double dp = d.pts[1].price - d.pts[0].price;
                double pct = d.pts[0].price != 0
                                 ? dp / std::fabs(d.pts[0].price) * 100
                                 : 0;
                long bars = std::lround(d.pts[1].bar - d.pts[0].bar);
                char buf[96];
                std::snprintf(buf, sizeof buf, "%+.2f (%+.2f%%) / %ld bars",
                              dp, pct, bars);
                detail::label_box(cr,
                                  (X(d.pts[0]) + X(d.pts[1])) / 2,
                                  (Y(d.pts[0]) + Y(d.pts[1])) / 2 - 4,
                                  buf, col);
            }
            break;
        }
        case Kind::Ray: {
            if (d.pts.size() < 2) break;
            double x1 = X(d.pts[0]), y1 = Y(d.pts[0]);
            double x2 = x1 + (X(d.pts[1]) - x1) * 1000;
            double y2 = y1 + (Y(d.pts[1]) - y1) * 1000;
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x2, y2);
            cairo_stroke(cr);
            break;
        }
        case Kind::Arrow: {
            if (d.pts.size() < 2) break;
            double x1 = X(d.pts[0]), y1 = Y(d.pts[0]);
            double x2 = X(d.pts[1]), y2 = Y(d.pts[1]);
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, x2, y2);
            cairo_stroke(cr);
            double ang = std::atan2(y2 - y1, x2 - x1);
            double alen = 11;
            cairo_move_to(cr, x2, y2);
            cairo_line_to(cr, x2 - alen * std::cos(ang - 0.42),
                          y2 - alen * std::sin(ang - 0.42));
            cairo_move_to(cr, x2, y2);
            cairo_line_to(cr, x2 - alen * std::cos(ang + 0.42),
                          y2 - alen * std::sin(ang + 0.42));
            cairo_stroke(cr);
            break;
        }
        case Kind::HLine: {
            double y = Y(d.pts[0]);
            cairo_move_to(cr, m.x, y);
            cairo_line_to(cr, m.x + m.w, y);
            cairo_stroke(cr);
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.2f", d.pts[0].price);
            detail::label_box(cr, m.x + 4, y - 2, buf, col);
            break;
        }
        case Kind::VLine: {
            double x = X(d.pts[0]);
            cairo_move_to(cr, x, m.y);
            cairo_line_to(cr, x, m.y + m.h);
            cairo_stroke(cr);
            break;
        }
        case Kind::Rect:
        case Kind::Ellipse: {
            if (d.pts.size() < 2) break;
            double x1 = X(d.pts[0]), y1 = Y(d.pts[0]);
            double x2 = X(d.pts[1]), y2 = Y(d.pts[1]);
            double rx = std::min(x1, x2), ry = std::min(y1, y2);
            double rw = std::fabs(x2 - x1), rh = std::fabs(y2 - y1);
            cairo_set_source_rgba(cr, col[0], col[1], col[2], 0.08);
            if (d.kind == Kind::Rect) {
                cairo_rectangle(cr, rx, ry, rw, rh);
                cairo_fill(cr);
                detail::set_style(cr, d, selected);
                cairo_rectangle(cr, rx, ry, rw, rh);
                cairo_stroke(cr);
            } else {
                cairo_save(cr);
                cairo_translate(cr, rx + rw / 2, ry + rh / 2);
                cairo_scale(cr, std::max(rw, 0.1), std::max(rh, 0.1));
                cairo_arc(cr, 0, 0, 0.5, 0, 2 * M_PI);
                cairo_restore(cr);
                cairo_fill(cr);
                detail::set_style(cr, d, selected);
                cairo_save(cr);
                cairo_translate(cr, rx + rw / 2, ry + rh / 2);
                cairo_scale(cr, std::max(rw, 0.1), std::max(rh, 0.1));
                cairo_arc(cr, 0, 0, 0.5, 0, 2 * M_PI);
                cairo_restore(cr);
                cairo_stroke(cr);
            }
            break;
        }
        case Kind::Text: {
            if (d.text.empty()) break;
            detail::label_box(cr, X(d.pts[0]) + 2, Y(d.pts[0]) - 2,
                              d.text, col);
            // Anchor dot.
            cairo_set_source_rgba(cr, col[0], col[1], col[2], 1.0);
            cairo_arc(cr, X(d.pts[0]), Y(d.pts[0]), 2.5, 0, 2 * M_PI);
            cairo_fill(cr);
            break;
        }
        case Kind::Fib: {
            if (d.pts.size() < 2) break;
            static const double kLevels[] = {0,    0.236, 0.382, 0.5,
                                             0.618, 0.786, 1.0};
            double x1 = X(d.pts[0]);
            cairo_set_line_width(cr, 1.0);
            for (double lv : kLevels) {
                double price =
                    d.pts[0].price + (d.pts[1].price - d.pts[0].price) * lv;
                double y = m.py(price);
                static const double kDash[] = {4, 3};
                cairo_set_dash(cr, kDash, 2, 0);
                cairo_set_source_rgba(cr, col[0], col[1], col[2], 0.75);
                cairo_move_to(cr, x1, y);
                cairo_line_to(cr, m.x + m.w, y);
                cairo_stroke(cr);
                cairo_set_dash(cr, nullptr, 0, 0);
                char buf[48];
                std::snprintf(buf, sizeof buf, "%.1f%% %.2f", lv * 100,
                              price);
                detail::label_box(cr, x1 + 4, y - 2, buf, col);
            }
            // Connector between the anchors.
            detail::set_style(cr, d, selected);
            cairo_move_to(cr, X(d.pts[0]), Y(d.pts[0]));
            cairo_line_to(cr, X(d.pts[1]), Y(d.pts[1]));
            cairo_stroke(cr);
            break;
        }
        case Kind::Freehand: {
            if (d.pts.size() < 2) break;
            cairo_move_to(cr, X(d.pts[0]), Y(d.pts[0]));
            for (size_t i = 1; i < d.pts.size(); ++i)
                cairo_line_to(cr, X(d.pts[i]), Y(d.pts[i]));
            cairo_stroke(cr);
            break;
        }
    }
    if (selected) {
        // Corner dots on the anchors.
        cairo_set_source_rgba(cr, col[0], col[1], col[2], 1.0);
        for (const Anchor& a : d.pts) {
            cairo_arc(cr, X(a), Y(a), 3, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }
    cairo_restore(cr);
}

// Is pixel (px, py) within `tol` px of the drawing? Used by the
// select/move and erase tools.
inline bool hit_test(const Map& m, const Drawing& d, double px,
                     double py, double tol = 6) {
    if (d.pts.empty()) return false;
    auto X = [&](const Anchor& a) { return m.px(a.bar); };
    auto Y = [&](const Anchor& a) { return m.py(a.price); };
    switch (d.kind) {
        case Kind::Trend:
        case Kind::Ray:
        case Kind::Arrow:
        case Kind::Measure:
            if (d.pts.size() < 2) return false;
            return detail::seg_dist(px, py, X(d.pts[0]), Y(d.pts[0]),
                                    X(d.pts[1]), Y(d.pts[1])) <= tol;
        case Kind::HLine:
            return std::fabs(py - Y(d.pts[0])) <= tol && px >= m.x &&
                   px <= m.x + m.w;
        case Kind::VLine:
            return std::fabs(px - X(d.pts[0])) <= tol && py >= m.y &&
                   py <= m.y + m.h;
        case Kind::Rect:
        case Kind::Ellipse: {
            if (d.pts.size() < 2) return false;
            double x1 = X(d.pts[0]), y1 = Y(d.pts[0]);
            double x2 = X(d.pts[1]), y2 = Y(d.pts[1]);
            bool inside = px >= std::min(x1, x2) && px <= std::max(x1, x2) &&
                          py >= std::min(y1, y2) && py <= std::max(y1, y2);
            bool near_edge =
                detail::seg_dist(px, py, x1, y1, x2, y1) <= tol ||
                detail::seg_dist(px, py, x2, y1, x2, y2) <= tol ||
                detail::seg_dist(px, py, x2, y2, x1, y2) <= tol ||
                detail::seg_dist(px, py, x1, y2, x1, y1) <= tol;
            return inside || near_edge;
        }
        case Kind::Text: {
            double x = X(d.pts[0]), y = Y(d.pts[0]);
            double w = 10 + (double)d.text.size() * 6.5;
            return px >= x - 4 && px <= x + w && py >= y - 16 &&
                   py <= y + 4;
        }
        case Kind::Fib: {
            if (d.pts.size() < 2) return false;
            static const double kLevels[] = {0,    0.236, 0.382, 0.5,
                                             0.618, 0.786, 1.0};
            for (double lv : kLevels) {
                double price =
                    d.pts[0].price + (d.pts[1].price - d.pts[0].price) * lv;
                if (std::fabs(py - m.py(price)) <= tol && px >= X(d.pts[0]))
                    return true;
            }
            return false;
        }
        case Kind::Freehand:
            for (size_t i = 1; i < d.pts.size(); ++i)
                if (detail::seg_dist(px, py, X(d.pts[i - 1]),
                                     Y(d.pts[i - 1]), X(d.pts[i]),
                                     Y(d.pts[i])) <= tol)
                    return true;
            return d.pts.size() == 1 &&
                   std::hypot(px - X(d.pts[0]), py - Y(d.pts[0])) <= tol;
    }
    return false;
}

// Shift every anchor by a data-space delta (select/move drags).
inline void move(Drawing& d, double dbar, double dprice) {
    for (Anchor& a : d.pts) {
        a.bar += dbar;
        a.price += dprice;
    }
    if (d.kind == Kind::HLine)
        for (Anchor& a : d.pts) a.bar -= dbar;  // bar is meaningless
    if (d.kind == Kind::VLine)
        for (Anchor& a : d.pts) a.price -= dprice;
}

// Parse one spec (grammar in the header comment).
inline std::optional<Drawing> parse_spec(const std::string& spec) {
    // Tokenize on whitespace, remembering where the tail starts (text).
    std::vector<std::string> tok;
    std::vector<size_t> tok_pos;
    size_t i = 0;
    while (i < spec.size()) {
        while (i < spec.size() &&
               (spec[i] == ' ' || spec[i] == '\t'))
            ++i;
        if (i >= spec.size()) break;
        size_t s = i;
        while (i < spec.size() && spec[i] != ' ' && spec[i] != '\t')
            ++i;
        tok.push_back(spec.substr(s, i - s));
        tok_pos.push_back(s);
    }
    if (tok.empty()) return std::nullopt;
    Drawing d;
    if (!kind_from_name(tok[0], d.kind)) return std::nullopt;
    size_t t = 1;
    auto number = [&](double& v) {
        if (t >= tok.size()) return false;
        char* end = nullptr;
        v = std::strtod(tok[t].c_str(), &end);
        if (end == tok[t].c_str()) return false;
        ++t;
        return true;
    };
    auto anchor = [&](Anchor& a) {
        return number(a.bar) && number(a.price);
    };
    auto tail_options = [&]() {
        while (t < tok.size()) {
            int ci = color_index_of(tok[t]);
            if (ci >= 0) {
                d.color = ci;
                ++t;
            } else if (tok[t] == "dash") {
                d.dash = true;
                ++t;
            } else {
                break;
            }
        }
    };
    switch (d.kind) {
        case Kind::HLine: {
            double p;
            if (!number(p)) return std::nullopt;
            d.pts.push_back({0, p});
            tail_options();
            return d;
        }
        case Kind::VLine: {
            double b;
            if (!number(b)) return std::nullopt;
            d.pts.push_back({b, 0});
            tail_options();
            return d;
        }
        case Kind::Text: {
            Anchor a;
            if (!anchor(a)) return std::nullopt;
            d.pts.push_back(a);
            tail_options();
            if (t < tok.size())
                d.text = spec.substr(tok_pos[t]);
            if (d.text.empty()) return std::nullopt;
            return d;
        }
        case Kind::Freehand: {
            while (t < tok.size()) {
                char* end = nullptr;
                double v = std::strtod(tok[t].c_str(), &end);
                if (end == tok[t].c_str()) break;
                Anchor a;
                a.bar = v;
                ++t;
                if (!number(a.price)) return std::nullopt;
                d.pts.push_back(a);
            }
            tail_options();
            if (d.pts.size() < 2) return std::nullopt;
            return d;
        }
        default: {  // two-anchor kinds
            Anchor a, b;
            if (!anchor(a) || !anchor(b)) return std::nullopt;
            d.pts.push_back(a);
            d.pts.push_back(b);
            tail_options();
            return d;
        }
    }
}

// Parse a semicolon-separated list of specs; bad entries are skipped.
inline std::vector<Drawing> parse_specs(const std::string& specs) {
    std::vector<Drawing> out;
    size_t start = 0;
    for (size_t i = 0; i <= specs.size(); ++i) {
        if (i == specs.size() || specs[i] == ';') {
            auto d = parse_spec(specs.substr(start, i - start));
            if (d) out.push_back(*d);
            start = i + 1;
        }
    }
    return out;
}

// Canonical spec for a drawing (parse_spec(to_spec(d)) round-trips).
inline std::string to_spec(const Drawing& d) {
    static const char* kNames[] = {"blue",   "red",  "green", "orange",
                                   "purple", "teal", "magenta", "brown",
                                   "black",  "slate"};
    int ci = ((d.color % kPaletteSize) + kPaletteSize) % kPaletteSize;
    char buf[64];
    std::string s = kind_name(d.kind);
    auto num = [&](double v) {
        std::snprintf(buf, sizeof buf, " %g", v);
        s += buf;
    };
    if (d.kind == Kind::HLine) {
        num(d.pts.empty() ? 0 : d.pts[0].price);
    } else if (d.kind == Kind::VLine) {
        num(d.pts.empty() ? 0 : d.pts[0].bar);
    } else {
        for (const Anchor& a : d.pts) {
            num(a.bar);
            num(a.price);
        }
    }
    s += " ";
    s += kNames[ci];
    if (d.dash) s += " dash";
    if (d.kind == Kind::Text) {
        s += " ";
        s += d.text;
    }
    return s;
}

}  // namespace fdw
)FDDRAWINGS9b2c";
