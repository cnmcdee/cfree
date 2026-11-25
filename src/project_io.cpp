// FDesigner project files (.fdj) — see project_io.h.

#include "project_io.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

// ---------------------------------------------------------------- writing

static std::string json_escape(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

bool save_project_file(const std::string& path, const Project& p,
                       std::string& err) {
    std::ostringstream o;
    o << "{\n"
      << "  \"format\": \"fdesigner\",\n"
      << "  \"version\": 1,\n"
      << "  \"window\": {\n"
      << "    \"title\": \"" << json_escape(p.title) << "\",\n"
      << "    \"class_name\": \"" << json_escape(p.class_name) << "\",\n"
      << "    \"width\": " << p.win_width << ",\n"
      << "    \"height\": " << p.win_height << ",\n"
      << "    \"background\": \"" << json_escape(p.background) << "\",\n"
      << "    \"menubar\": \"" << json_escape(p.menubar) << "\"\n"
      << "  },\n"
      << "  \"widgets\": [";
    for (size_t i = 0; i < p.widgets.size(); ++i) {
        const Widget& w = p.widgets[i];
        o << (i ? ",\n" : "\n") << "    {\n"
          << "      \"name\": \"" << json_escape(w.name) << "\",\n"
          << "      \"type\": \"" << json_escape(w.type) << "\",\n"
          << "      \"x\": " << w.x << ", \"y\": " << w.y
          << ", \"width\": " << w.width << ", \"height\": " << w.height
          << ",\n"
          << "      \"props\": {";
        bool first = true;
        for (const auto& [k, v] : w.props) {
            o << (first ? "" : ",") << "\n        \"" << json_escape(k)
              << "\": \"" << json_escape(v) << "\"";
            first = false;
        }
        o << (w.props.empty() ? "" : "\n      ") << "},\n"
          << "      \"signals\": [";
        for (size_t j = 0; j < w.signals.size(); ++j)
            o << (j ? ", " : "") << "\"" << json_escape(w.signals[j])
              << "\"";
        o << "]\n"
          << "    }";
    }
    o << (p.widgets.empty() ? "" : "\n  ") << "]\n}\n";

    std::ofstream f(path);
    if (!f) {
        err = "cannot open " + path + " for writing";
        return false;
    }
    f << o.str();
    if (!f) {
        err = "write failed for " + path;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- reading

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool boolean = false;
    double num = 0;
    std::string str;
    std::vector<Json> arr;
    std::map<std::string, Json> obj;
    const Json* find(const std::string& key) const {
        if (type != Obj) return nullptr;
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
};

struct Parser {
    const char* p;
    std::string err;

    void ws() {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    }
    bool consume(char c) {
        ws();
        if (*p == c) {
            ++p;
            return true;
        }
        return false;
    }
    static void utf8_encode(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    bool parse_string(std::string& out) {
        if (*p != '"') {
            err = "expected string";
            return false;
        }
        ++p;
        while (*p && *p != '"') {
            unsigned char c = (unsigned char)*p;
            if (c == '\\') {
                ++p;
                switch (*p) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case '/': out += '/'; break;
                    case '\\': out += '\\'; break;
                    case '"': out += '"'; break;
                    case 'u': {
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            ++p;
                            if (!std::isxdigit((unsigned char)*p)) {
                                err = "bad \\u escape";
                                return false;
                            }
                            cp = cp * 16 +
                                 (unsigned)(std::isdigit((unsigned char)*p)
                                                ? *p - '0'
                                                : (std::tolower(
                                                       (unsigned char)*p) -
                                                   'a' + 10));
                        }
                        // Surrogate pair?
                        if (cp >= 0xD800 && cp <= 0xDBFF && p[1] == '\\' &&
                            p[2] == 'u') {
                            unsigned lo = 0;
                            bool okpair = true;
                            const char* save = p;
                            p += 2;
                            for (int i = 0; i < 4; ++i) {
                                ++p;
                                if (!std::isxdigit((unsigned char)*p)) {
                                    okpair = false;
                                    break;
                                }
                                lo = lo * 16 +
                                     (unsigned)(std::isdigit(
                                                     (unsigned char)*p)
                                                    ? *p - '0'
                                                    : (std::tolower(
                                                           (unsigned char)*p) -
                                                       'a' + 10));
                            }
                            if (okpair && lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) +
                                     (lo - 0xDC00);
                            else
                                p = save;
                        }
                        utf8_encode(out, cp);
                        break;
                    }
                    default:
                        err = "bad escape";
                        return false;
                }
                ++p;
            } else {
                out += (char)c;
                ++p;
            }
        }
        if (*p != '"') {
            err = "unterminated string";
            return false;
        }
        ++p;
        return true;
    }
    bool parse_value(Json& v) {
        ws();
        if (*p == '{') {
            ++p;
            v.type = Json::Obj;
            if (consume('}')) return true;
            while (true) {
                ws();
                std::string key;
                if (!parse_string(key)) return false;
                if (!consume(':')) {
                    err = "expected ':'";
                    return false;
                }
                Json child;
                if (!parse_value(child)) return false;
                v.obj[key] = child;
                if (consume(',')) continue;
                if (consume('}')) return true;
                err = "expected ',' or '}'";
                return false;
            }
        }
        if (*p == '[') {
            ++p;
            v.type = Json::Arr;
            if (consume(']')) return true;
            while (true) {
                Json child;
                if (!parse_value(child)) return false;
                v.arr.push_back(child);
                if (consume(',')) continue;
                if (consume(']')) return true;
                err = "expected ',' or ']'";
                return false;
            }
        }
        if (*p == '"') {
            v.type = Json::Str;
            return parse_string(v.str);
        }
        if (std::strncmp(p, "true", 4) == 0) {
            p += 4;
            v.type = Json::Bool;
            v.boolean = true;
            return true;
        }
        if (std::strncmp(p, "false", 5) == 0) {
            p += 5;
            v.type = Json::Bool;
            v.boolean = false;
            return true;
        }
        if (std::strncmp(p, "null", 4) == 0) {
            p += 4;
            v.type = Json::Null;
            return true;
        }
        char* end = nullptr;
        v.num = std::strtod(p, &end);
        if (end == p) {
            err = "unexpected character";
            return false;
        }
        p = end;
        v.type = Json::Num;
        return true;
    }
};

static std::string jstr(const Json* j, const std::string& dflt) {
    return j && j->type == Json::Str ? j->str : dflt;
}
static int jint(const Json* j, int dflt) {
    return j && j->type == Json::Num ? (int)j->num : dflt;
}

bool load_project_file(const std::string& path, Project& out,
                       std::string& err) {
    std::ifstream f(path);
    if (!f) {
        err = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();

    Parser parser{text.c_str(), ""};
    Json root;
    if (!parser.parse_value(root)) {
        err = "invalid JSON in " + path + ": " + parser.err;
        return false;
    }
    parser.ws();
    if (*parser.p) {
        err = "trailing data after JSON in " + path;
        return false;
    }
    if (jstr(root.find("format"), "") != "fdesigner") {
        err = path + " is not an fdesigner project file";
        return false;
    }
    if (jint(root.find("version"), 0) != 1) {
        err = path + " has an unsupported format version";
        return false;
    }

    Project p;
    if (const Json* win = root.find("window")) {
        p.title = jstr(win->find("title"), p.title);
        p.class_name = jstr(win->find("class_name"), p.class_name);
        p.win_width = jint(win->find("width"), p.win_width);
        p.win_height = jint(win->find("height"), p.win_height);
        p.background = jstr(win->find("background"), "");
        p.menubar = jstr(win->find("menubar"), "");
    }
    if (p.win_width < 50 || p.win_width > 20000) p.win_width = 800;
    if (p.win_height < 50 || p.win_height > 20000) p.win_height = 600;

    if (const Json* widgets = root.find("widgets");
        widgets && widgets->type == Json::Arr) {
        for (const Json& jw : widgets->arr) {
            Widget w;
            w.name = jstr(jw.find("name"), "");
            w.type = jstr(jw.find("type"), "");
            if (w.name.empty() || w.type.empty()) continue;
            w.x = jint(jw.find("x"), 0);
            w.y = jint(jw.find("y"), 0);
            w.width = jint(jw.find("width"), 100);
            w.height = jint(jw.find("height"), 30);
            if (w.width < 10) w.width = 10;
            if (w.height < 10) w.height = 10;
            if (const Json* props = jw.find("props");
                props && props->type == Json::Obj) {
                for (const auto& [k, v] : props->obj)
                    if (v.type == Json::Str) w.props[k] = v.str;
            }
            if (const Json* sigs = jw.find("signals");
                sigs && sigs->type == Json::Arr) {
                for (const Json& s : sigs->arr)
                    if (s.type == Json::Str) w.signals.push_back(s.str);
            }
            p.widgets.push_back(w);
        }
    }
    out = p;
    return true;
}
