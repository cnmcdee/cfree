// FDesigner stepping debugger — GDB/MI backend implementation.
// See debugger.h. Talks to `gdb -q -i=mi2` over pipes; the protocol notes
// in the probe (target stdout arrives as bare lines) are handled in
// handle_line().
#include "debugger.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// ---------------------------------------------------------------- MI parse

std::string mi_unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                default: out += c; break;  // \0nnn octal etc: keep char
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

static void mi_skip_ws(const std::string& s, size_t& p) {
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
}

static bool mi_parse_string(const std::string& s, size_t& p, std::string& out) {
    if (p >= s.size() || s[p] != '"') return false;
    ++p;
    std::string raw;
    while (p < s.size()) {
        char c = s[p++];
        if (c == '\\' && p < s.size()) { raw += '\\'; raw += s[p++]; }
        else if (c == '"') { out = mi_unescape(raw); return true; }
        else raw += c;
    }
    return false;
}

bool mi_parse_value(const std::string& s, size_t& pos, MiValue& out) {
    mi_skip_ws(s, pos);
    if (pos >= s.size()) return false;
    out = MiValue();
    char c = s[pos];
    if (c == '"') return mi_parse_string(s, pos, out.str);
    if (c == '{') {
        out.is_tuple = true;
        ++pos;
        while (true) {
            mi_skip_ws(s, pos);
            if (pos >= s.size()) return false;
            if (s[pos] == '}') { ++pos; return true; }
            size_t ks = pos;
            while (pos < s.size() && s[pos] != '=' && s[pos] != ',' &&
                   s[pos] != '}')
                ++pos;
            std::string key = s.substr(ks, pos - ks);
            if (pos < s.size() && s[pos] == '=') {
                ++pos;
                MiValue v;
                if (!mi_parse_value(s, pos, v)) return false;
                out.tuple[key] = v;
            } else {
                // bare value inside a tuple (e.g. {number="1.1",...} lists)
                MiValue v;
                v.str = key;
                out.list.push_back(v);
            }
            mi_skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == '}') { ++pos; return true; }
            return false;
        }
    }
    if (c == '[') {
        out.is_list = true;
        ++pos;
        while (true) {
            mi_skip_ws(s, pos);
            if (pos >= s.size()) return false;
            if (s[pos] == ']') { ++pos; return true; }
            // list element: either value or key=value (result-record style).
            // NOTE: an element starting with '{', '[' or '"' is a plain
            // value (e.g. frame args=[{name="argc",value="1"}]); scanning
            // it for '=' would mistake the tuple's first key for a
            // list-level key and corrupt the parse.
            size_t save = pos;
            if (pos < s.size() && (s[pos] == '{' || s[pos] == '[' ||
                                   s[pos] == '"')) {
                MiValue v;
                if (!mi_parse_value(s, pos, v)) return false;
                out.list.push_back(v);
                mi_skip_ws(s, pos);
                if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
                if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
                return false;
            }
            size_t ks = pos;
            while (pos < s.size() && s[pos] != '=' && s[pos] != ',' &&
                   s[pos] != ']')
                ++pos;
            if (pos < s.size() && s[pos] == '=') {
                std::string key = s.substr(ks, pos - ks);
                ++pos;
                MiValue v;
                if (!mi_parse_value(s, pos, v)) return false;
                MiValue pair;
                pair.is_tuple = true;
                pair.tuple[key] = v;
                out.list.push_back(pair);
            } else {
                pos = save;
                MiValue v;
                if (!mi_parse_value(s, pos, v)) return false;
                out.list.push_back(v);
            }
            mi_skip_ws(s, pos);
            if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
            if (pos < s.size() && s[pos] == ']') { ++pos; return true; }
            return false;
        }
    }
    // bare token up to , } ]
    size_t start = pos;
    while (pos < s.size() && s[pos] != ',' && s[pos] != '}' && s[pos] != ']')
        ++pos;
    out.str = s.substr(start, pos - start);
    return true;
}

// Parse the comma-separated key=value fields after a record prefix into a
// tuple MiValue (e.g. after "*stopped," or "1^done,").
static MiValue mi_parse_fields(const std::string& s) {
    MiValue out;
    out.is_tuple = true;
    size_t p = 0;
    while (p < s.size()) {
        mi_skip_ws(s, p);
        if (p >= s.size()) break;
        size_t ks = p;
        while (p < s.size() && s[p] != '=' && s[p] != ',') ++p;
        std::string key = s.substr(ks, p - ks);
        if (p < s.size() && s[p] == '=') {
            ++p;
            MiValue v;
            if (!mi_parse_value(s, p, v)) break;
            out.tuple[key] = v;
        } else if (!key.empty()) {
            MiValue v;
            v.str = key;
            out.list.push_back(v);
        }
        if (p < s.size() && s[p] == ',') ++p;
    }
    return out;
}

bool parse_breakpoint_location(const std::string& text, std::string& file,
                               int& line, std::string& function) {
    file.clear();
    line = 0;
    function.clear();
    std::string t;
    for (char c : text)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') t += c;
    if (t.empty()) return false;
    size_t colon = t.rfind(':');
    if (colon != std::string::npos && colon + 1 < t.size()) {
        std::string num = t.substr(colon + 1);
        bool all_digit = !num.empty();
        for (char c : num)
            if (!std::isdigit((unsigned char)c)) all_digit = false;
        if (all_digit) {
            file = t.substr(0, colon);
            line = std::atoi(num.c_str());
            return !file.empty() && line > 0;
        }
    }
    // Function name: identifier-ish (letters, digits, _, :, <>, ~).
    bool ok = std::isalpha((unsigned char)t[0]) || t[0] == '_' ||
              t[0] == '~';
    for (char c : t)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == ':' ||
              c == '<' || c == '>' || c == '~'))
            ok = false;
    if (ok) {
        function = t;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- settings

static std::string home_dir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

std::string DebuggerSettings::default_path() {
    const char* e = std::getenv("FDESIGNER_DEBUGGER_CONF");
    if (e && *e) return e;
    return home_dir() + "/.config/fdesigner/debugger.conf";
}

std::string GdbSession::default_bps_path() {
    return home_dir() + "/.config/fdesigner/debugger_bps.conf";
}

static bool ensure_parent_dir(const std::string& path) {
    size_t slash = path.rfind('/');
    if (slash == std::string::npos) return true;
    std::string dir = path.substr(0, slash);
    std::string cur;
    for (size_t i = 1; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            cur = dir.substr(0, i);
            if (!cur.empty()) ::mkdir(cur.c_str(), 0755);
        }
    }
    return true;
}

static std::string trim_s(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- breakpoint
// handlers

const char* bp_kind_name(BpKind kind) {
    switch (kind) {
        case BpKind::Conditional: return "conditional";
        case BpKind::Logpoint: return "logpoint";
        case BpKind::Plain: break;
    }
    return "plain";
}

bool bp_kind_from_name(const std::string& name, BpKind& kind) {
    if (name == "plain") kind = BpKind::Plain;
    else if (name == "conditional" || name == "cond")
        kind = BpKind::Conditional;
    else if (name == "logpoint" || name == "log" || name == "dprintf")
        kind = BpKind::Logpoint;
    else
        return false;
    return true;
}

// Quote one argument as an MI C-string: "s" with \ and " escaped.
static std::string mi_quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

bool logpoint_to_printf(const std::string& tmpl, std::string& fmt,
                        std::vector<std::string>& args) {
    fmt.clear();
    args.clear();
    for (size_t i = 0; i < tmpl.size();) {
        char c = tmpl[i];
        if (c == '%') {
            fmt += "%%";  // literal % must not become a format directive
            ++i;
            continue;
        }
        if (c != '{') {
            fmt += c;
            ++i;
            continue;
        }
        size_t close = tmpl.find('}', i + 1);
        if (close == std::string::npos) return false;  // unbalanced {
        std::string inner = tmpl.substr(i + 1, close - i - 1);
        if (trim_s(inner).empty()) return false;  // empty {}
        // Optional one-letter format prefix before the first colon:
        // {s:name} -> %s. Anything else (including a:b ternaries and
        // a::b qualified names) is the whole expression, printed %d.
        std::string spec = "%d";
        std::string expr = inner;
        size_t colon = inner.find(':');
        if (colon == 1) {
            const char* m = nullptr;
            switch (inner[0]) {
                case 'd': m = "%d"; break;
                case 's': m = "%s"; break;
                case 'f': m = "%f"; break;
                case 'x': m = "%#x"; break;
                case 'p': m = "%p"; break;
                default: break;
            }
            if (m) {
                spec = m;
                expr = inner.substr(2);
            }
        }
        expr = trim_s(expr);
        if (expr.empty()) return false;
        fmt += spec;
        args.push_back(expr);
        i = close + 1;
    }
    // dprintf output without a trailing newline would run into gdb's own
    // console output; terminate the message (fmt holds a literal \n as
    // backslash+n, which gdb's console parser turns into a newline).
    if (fmt.size() < 2 || fmt.compare(fmt.size() - 2, 2, "\\n") != 0)
        fmt += "\\n";
    return true;
}

bool DebuggerSettings::save(const std::string& path) const {
    std::string p = path.empty() ? default_path() : path;
    ensure_parent_dir(p);
    std::ofstream f(p);
    if (!f) return false;
    f << "# FDesigner debugger settings\n";
    f << "gdb_path=" << gdb_path << "\n";
    f << "break_on_main=" << (break_on_main ? "1" : "0") << "\n";
    f << "stop_on_entry=" << (stop_on_entry ? "1" : "0") << "\n";
    f << "pretty_print=" << (pretty_print ? "1" : "0") << "\n";
    f << "print_all_locals=" << (print_all_locals ? "1" : "0") << "\n";
    f << "default_args=" << default_args << "\n";
    f << "stop_timeout_ms=" << stop_timeout_ms << "\n";
    f << "cmd_timeout_ms=" << cmd_timeout_ms << "\n";
    return (bool)f;
}

bool DebuggerSettings::load(const std::string& path) {
    std::string p = path.empty() ? default_path() : path;
    std::ifstream f(p);
    if (!f) return false;
    auto boolean = [](const std::string& v) {
        return v == "1" || v == "true" || v == "yes" || v == "on";
    };
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim_s(line);
        if (t.empty() || t[0] == '#') continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim_s(t.substr(0, eq));
        std::string v = trim_s(t.substr(eq + 1));
        if (k == "gdb_path" && !v.empty()) gdb_path = v;
        else if (k == "break_on_main") break_on_main = boolean(v);
        else if (k == "stop_on_entry") stop_on_entry = boolean(v);
        else if (k == "pretty_print") pretty_print = boolean(v);
        else if (k == "print_all_locals") print_all_locals = boolean(v);
        else if (k == "default_args") default_args = v;
        else if (k == "stop_timeout_ms") stop_timeout_ms = std::atoi(v.c_str());
        else if (k == "cmd_timeout_ms") cmd_timeout_ms = std::atoi(v.c_str());
    }
    if (stop_timeout_ms <= 0) stop_timeout_ms = 10000;
    if (cmd_timeout_ms <= 0) cmd_timeout_ms = 5000;
    return true;
}

// ---------------------------------------------------------------- session

GdbSession::GdbSession(const DebuggerSettings& s) : settings_(s) {}
GdbSession::~GdbSession() { shutdown(); }

bool GdbSession::send_raw(const std::string& text) {
    if (gdb_stdin_ < 0) return false;
    ssize_t n = ::write(gdb_stdin_, text.data(), text.size());
    return n == (ssize_t)text.size();
}

bool GdbSession::spawn_gdb(std::string& err) {
    if (gdb_pid_ > 0) return true;
    if (::access(settings_.gdb_path.c_str(), X_OK) != 0) {
        err = "gdb not executable: " + settings_.gdb_path;
        state_ = DbgState::Error;
        return false;
    }
    int in_pipe[2], out_pipe[2];
    if (::pipe(in_pipe) != 0 || ::pipe(out_pipe) != 0) {
        err = "pipe() failed";
        return false;
    }
    pid_t pid = ::fork();
    if (pid < 0) {
        err = "fork() failed";
        return false;
    }
    if (pid == 0) {
        // Child: gdb's stdin from in_pipe, stdout+stderr to out_pipe.
        ::dup2(in_pipe[0], 0);
        ::dup2(out_pipe[1], 1);
        ::dup2(out_pipe[1], 2);
        ::close(in_pipe[0]); ::close(in_pipe[1]);
        ::close(out_pipe[0]); ::close(out_pipe[1]);
        ::signal(SIGPIPE, SIG_DFL);
        ::execl(settings_.gdb_path.c_str(), "gdb", "-q", "-i=mi2",
                (char*)nullptr);
        ::_exit(127);
    }
    ::close(in_pipe[0]);
    ::close(out_pipe[1]);
    gdb_stdin_ = in_pipe[1];
    gdb_stdout_ = out_pipe[0];
    gdb_pid_ = pid;
    int flags = ::fcntl(gdb_stdout_, F_GETFL, 0);
    ::fcntl(gdb_stdout_, F_SETFL, flags | O_NONBLOCK);
    state_ = DbgState::Starting;
    // Wait for the initial "(gdb)" prompt / thread-group-added notice.
    pump(2000);
    if (!is_alive()) {
        err = "gdb exited immediately";
        state_ = DbgState::Error;
        return false;
    }
    // Pretty printing is a session-wide gdb setting: apply now so every
    // later variable value comes back formatted.
    if (settings_.pretty_print) {
        MiValue r;
        std::string e;
        command("-enable-pretty-printing", r, e, 2000);
    }
    return true;
}

void GdbSession::shutdown() {
    if (gdb_pid_ > 0) {
        if (gdb_stdin_ >= 0) {
            std::string bye = "-gdb-exit\n";
            (void)::write(gdb_stdin_, bye.data(), bye.size());
        }
        // Give gdb a moment, then SIGKILL the group if it lingers.
        for (int i = 0; i < 10; ++i) {
            int status = 0;
            pid_t r = ::waitpid(gdb_pid_, &status, WNOHANG);
            if (r == gdb_pid_) break;
            if (i == 4) ::kill(gdb_pid_, SIGKILL);
            ::usleep(100000);
        }
        int status = 0;
        ::waitpid(gdb_pid_, &status, WNOHANG);
    }
    if (gdb_stdin_ >= 0) ::close(gdb_stdin_);
    if (gdb_stdout_ >= 0) ::close(gdb_stdout_);
    gdb_pid_ = -1;
    gdb_stdin_ = gdb_stdout_ = -1;
    state_ = DbgState::Idle;
    bps_.clear();
}

bool GdbSession::is_alive() const {
    if (gdb_pid_ <= 0) return false;
    int status = 0;
    pid_t r = ::waitpid(gdb_pid_, &status, WNOHANG);
    return r == 0;
}

void GdbSession::handle_line(const std::string& line) {
    if (line.empty()) return;
    if (line == "(gdb)") return;
    char c0 = line[0];
    // Result record: [token]^done/^error/^running/^exit[,fields]
    if (c0 == '^' || (std::isdigit((unsigned char)c0))) {
        size_t caret = line.find('^');
        if (caret != std::string::npos) {
            int token = 0;
            if (caret > 0) token = std::atoi(line.substr(0, caret).c_str());
            std::string rest = line.substr(caret + 1);
            std::string kind;
            size_t comma = rest.find(',');
            kind = comma == std::string::npos ? rest : rest.substr(0, comma);
            MiValue fields;
            if (comma != std::string::npos)
                fields = mi_parse_fields(rest.substr(comma + 1));
            if (kind == "done" || kind == "error" || kind == "running" ||
                kind == "exit") {
                if (token > 0) {
                    pending_results_[token] = {kind != "error", fields};
                    if (kind == "error") {
                        const MiValue* m = fields.get("msg");
                        pending_error_ = m ? m->str : "gdb error";
                    }
                }
                if (kind == "running") state_ = DbgState::Running;
                if (kind == "exit") state_ = DbgState::Exited;
            }
            return;
        }
    }
    if (c0 == '*') {
        // Async exec record: *stopped / *running
        size_t comma = line.find(',');
        std::string kind = comma == std::string::npos
                               ? line.substr(1)
                               : line.substr(1, comma - 1);
        MiValue fields;
        if (comma != std::string::npos)
            fields = mi_parse_fields(line.substr(comma + 1));
        if (kind == "stopped") {
            ++stop_seq_;
            StopInfo si;
            si.reason = fields.get_str("reason");
            si.breakpoint_id =
                std::atoi(fields.get_str("bkptno", "0").c_str());
            const MiValue* fr = fields.get("frame");
            if (fr) {
                si.frame.function = fr->get_str("func");
                si.frame.file = fr->get_str("fullname");
                if (si.frame.file.empty()) si.frame.file = fr->get_str("file");
                si.frame.line = std::atoi(fr->get_str("line", "0").c_str());
                si.frame.address = fr->get_str("addr");
                si.frame.level = std::atoi(fr->get_str("level", "0").c_str());
            }
            if (si.reason == "exited-normally" ||
                si.reason == "exited" ||
                si.reason == "exited-signalled") {
                si.exited = true;
                si.exit_code = std::atoi(fields.get_str("exit-code", "0").c_str());
                if (si.reason == "exited-signalled") si.exit_code = -1;
                state_ = DbgState::Exited;
            } else {
                state_ = DbgState::Stopped;
            }
            // Track breakpoint hit counts.
            if (si.breakpoint_id > 0)
                for (auto& b : bps_)
                    if (b.id == si.breakpoint_id) b.hits++;
            last_stop_ = si;
        } else if (kind == "running") {
            state_ = DbgState::Running;
        }
        return;
    }
    if (c0 == '=') {
        // Notify records (thread/breakpoint/library). Track the inferior
        // pid from =thread-group-started so kill_target() can SIGKILL it
        // directly: gdb 13's MI has no -exec-kill command.
        if (line.rfind("=thread-group-started,", 0) == 0) {
            MiValue f = mi_parse_fields(line.substr(
                std::string("=thread-group-started,").size()));
            inferior_pid_ = std::atoi(f.get_str("pid", "0").c_str());
        } else if (line.rfind("=thread-group-exited,", 0) == 0) {
            inferior_pid_ = -1;
        }
        return;
    }
    if (c0 == '~' || c0 == '&') {
        // Console / log stream: "~\"...\"" — decode the quoted body.
        size_t q1 = line.find('"');
        size_t q2 = line.rfind('"');
        if (q1 != std::string::npos && q2 > q1)
            gdb_log_ += mi_unescape(line.substr(q1 + 1, q2 - q1 - 1));
        return;
    }
    if (c0 == '@') {
        size_t q1 = line.find('"');
        size_t q2 = line.rfind('"');
        if (q1 != std::string::npos && q2 > q1)
            target_output_ += mi_unescape(line.substr(q1 + 1, q2 - q1 - 1));
        return;
    }
    // Anything else is the inferior's own stdout/stderr, forwarded raw
    // by gdb in MI mode (verified by probe: plain "i=0 x=1 ..." lines).
    target_output_ += line + "\n";
}

bool GdbSession::pump(int timeout_ms) {
    auto deadline = [&] {
        return std::chrono::steady_clock::now() +
               std::chrono::milliseconds(timeout_ms);
    }();
    bool got_any = false;
    while (true) {
        int remain = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                         deadline - std::chrono::steady_clock::now())
                         .count();
        if (remain <= 0) break;
        struct pollfd pfd {};
        pfd.fd = gdb_stdout_;
        pfd.events = POLLIN;
        int r = ::poll(&pfd, 1, std::min(remain, 200));
        if (r < 0) break;
        if (r == 0) {
            if (!is_alive()) { state_ = DbgState::Error; break; }
            continue;
        }
        char buf[8192];
        ssize_t n = ::read(gdb_stdout_, buf, sizeof buf);
        if (n > 0) {
            got_any = true;
            read_buf_.append(buf, (size_t)n);
            size_t pos = 0;
            while (true) {
                size_t nl = read_buf_.find('\n', pos);
                if (nl == std::string::npos) break;
                std::string line = read_buf_.substr(pos, nl - pos);
                while (!line.empty() &&
                       (line.back() == '\r' || line.back() == ' ' ||
                        line.back() == '\t'))
                    line.pop_back();
                handle_line(line);
                pos = nl + 1;
            }
            read_buf_.erase(0, pos);
        } else if (n == 0) {
            state_ = DbgState::Error;
            break;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }
    return got_any;
}

bool GdbSession::wait_result(int token, MiValue& result, std::string& err,
                             int timeout_ms) {
    if (timeout_ms < 0) timeout_ms = settings_.cmd_timeout_ms;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        auto it = pending_results_.find(token);
        if (it != pending_results_.end()) {
            bool ok = it->second.first;
            result = it->second.second;
            pending_results_.erase(it);
            if (!ok) {
                err = pending_error_.empty() ? "gdb reported an error"
                                             : pending_error_;
                last_error_ = err;
                return false;
            }
            return true;
        }
        if (!is_alive()) {
            err = "gdb process died";
            last_error_ = err;
            return false;
        }
        pump(100);
    }
    err = "timed out waiting for gdb reply";
    last_error_ = err;
    return false;
}

bool GdbSession::command(const std::string& mi_cmd, MiValue& result,
                         std::string& err, int timeout_ms) {
    if (gdb_pid_ <= 0) {
        err = "gdb is not running";
        return false;
    }
    int token = next_token_++;
    std::string line = std::to_string(token) + mi_cmd + "\n";
    pending_results_.erase(token);
    if (!send_raw(line)) {
        err = "failed to write to gdb";
        return false;
    }
    return wait_result(token, result, err, timeout_ms);
}

bool GdbSession::start_gdb_only(std::string& err) {
    if (gdb_pid_ > 0) return true;
    return spawn_gdb(err);
}

bool GdbSession::load_executable(const std::string& executable,
                                 std::string& err) {
    if (::access(executable.c_str(), X_OK) != 0) {
        err = "target not executable: " + executable;
        return false;
    }
    if (!start_gdb_only(err)) return false;
    MiValue r;
    // -file-exec-and-symbols <path> (path may contain spaces: quote it).
    std::string cmd = "-file-exec-and-symbols \"" + executable + "\"";
    if (!command(cmd, r, err)) return false;
    executable_ = executable;
    return true;
}

bool GdbSession::start(const std::string& executable, const std::string& args,
                       std::string& err) {
    if (!load_executable(executable, err)) return false;
    args_ = args;
    if (!args.empty()) {
        MiValue r;
        if (!command("-exec-arguments " + args, r, err)) return false;
    }
    // Re-apply saved breakpoints (restart keeps them), with their full
    // handler options (conditional / logpoint / ignore count / group).
    std::vector<Breakpoint> old = bps_;
    bps_.clear();
    for (auto& b : old) {
        Breakpoint nb;
        std::string e;
        add_breakpoint_opts(b.location, b, nb, e);  // best effort: a stale
        // location must not abort the run
    }
    if (settings_.break_on_main || settings_.stop_on_entry) {
        Breakpoint bp;
        std::string e;
        add_breakpoint("main", bp, e);  // best effort; main may not exist
    }
    return run(err);
}

bool GdbSession::issue_exec(const std::string& mi_cmd, std::string& err) {
    // A stop event for this very command can arrive while command() is
    // still draining the ^running reply (e.g. a breakpoint on the first
    // line, or a one-line step). The stop counter tells a fresh stop
    // apart from a stale state left over from a previous run (Exited):
    // only move to Running when no new stop arrived.
    unsigned long gen = stop_seq_;
    MiValue r;
    if (!command(mi_cmd, r, err)) return false;
    if (stop_seq_ == gen) state_ = DbgState::Running;
    return true;
}

bool GdbSession::run(std::string& err) {
    if (gdb_pid_ <= 0 || executable_.empty()) {
        err = "no target loaded";
        return false;
    }
    return issue_exec("-exec-run", err);
}

bool GdbSession::cont(std::string& err) {
    return issue_exec("-exec-continue", err);
}

bool GdbSession::step_over(std::string& err) {
    return issue_exec("-exec-next", err);
}

bool GdbSession::step_into(std::string& err) {
    return issue_exec("-exec-step", err);
}

bool GdbSession::step_out(std::string& err) {
    return issue_exec("-exec-finish", err);
}

bool GdbSession::kill_target(std::string& err) {
    // gdb 13 MI has no -exec-kill: SIGKILL the inferior directly (its pid
    // comes from =thread-group-started), then let gdb report the exit.
    if (inferior_pid_ > 0) {
        if (::kill(inferior_pid_, SIGKILL) != 0 && errno != ESRCH) {
            err = "failed to kill target process";
            return false;
        }
        pump(500);
        if (state_ == DbgState::Running) {
            // gdb has not reported the exit yet; give it another moment.
            pump(500);
        }
        return true;
    }
    // No inferior pid known (never started / already exited): fall back to
    // interrupting, then the console "kill" command.
    MiValue r;
    if (!command("-exec-interrupt", r, err)) return false;
    pump(300);
    if (!command("-interpreter-exec console \"kill\"", r, err)) return false;
    pump(500);
    return true;
}

bool GdbSession::interrupt(std::string& err) {
    MiValue r;
    if (!command("-exec-interrupt", r, err)) return false;
    return true;
}

bool GdbSession::restart(std::string& err) {
    if (state_ == DbgState::Running) {
        std::string e;
        if (!kill_target(e)) return false;
        pump(300);
    }
    last_stop_ = StopInfo();
    return run(err);
}

void GdbSession::poll_events() {
    if (gdb_pid_ > 0) pump(20);
}

bool GdbSession::wait_for_stop(int timeout_ms) {
    if (timeout_ms < 0) timeout_ms = settings_.stop_timeout_ms;
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (state_ == DbgState::Stopped || state_ == DbgState::Exited)
            return true;
        if (state_ == DbgState::Error) return false;
        pump(100);
    }
    return state_ == DbgState::Stopped || state_ == DbgState::Exited;
}

// ---------------------------------------------------------------- breakpoints

bool GdbSession::stage_breakpoint(const std::string& location) {
    return stage_breakpoint_opts(location, Breakpoint());
}

bool GdbSession::stage_breakpoint_opts(const std::string& location,
                                       const Breakpoint& opts) {
    std::string file;
    int line = 0;
    std::string func;
    if (!parse_breakpoint_location(location, file, line, func)) return false;
    Breakpoint b = opts;
    b.location = location;
    b.file = file;
    b.line = line;
    b.function = func;
    b.id = 0;  // not in gdb yet
    // A condition without an explicit kind is a conditional breakpoint.
    if (b.kind == BpKind::Plain && !b.condition.empty())
        b.kind = BpKind::Conditional;
    for (auto& old : bps_) {
        if (old.location != location) continue;
        if (old.id == 0) {
            old = b;  // re-stage: replace the staged entry's options
            return true;
        }
        return true;  // live breakpoint at this location: leave it alone
    }
    bps_.push_back(b);
    return true;
}

bool GdbSession::add_breakpoint(const std::string& location, Breakpoint& out,
                                std::string& err) {
    return add_breakpoint_opts(location, Breakpoint(), out, err);
}

// Apply condition (-break-condition) and ignore count (-break-after) to a
// breakpoint that already exists in gdb. The condition is MI-quoted, so
// expressions with spaces or string literals survive the trip.
bool GdbSession::apply_bp_options(int id, const Breakpoint& opts,
                                  std::string& err) {
    MiValue r;
    if (!opts.condition.empty()) {
        if (!command("-break-condition " + std::to_string(id) + " " +
                         mi_quote(opts.condition),
                     r, err))
            return false;
    }
    if (opts.ignore_count > 0) {
        if (!command("-break-after " + std::to_string(id) + " " +
                         std::to_string(opts.ignore_count),
                     r, err))
            return false;
    }
    return true;
}

bool GdbSession::add_breakpoint_opts(const std::string& location,
                                     const Breakpoint& opts, Breakpoint& out,
                                     std::string& err) {
    std::string file;
    int line = 0;
    std::string func;
    if (!parse_breakpoint_location(location, file, line, func)) {
        err = "not a breakpoint location (need file:line or function): " +
              location;
        return false;
    }
    Breakpoint want = opts;
    want.location = location;
    if (want.kind == BpKind::Plain && !want.condition.empty())
        want.kind = BpKind::Conditional;
    if (want.kind == BpKind::Conditional &&
        trim_s(want.condition).empty()) {
        err = "conditional breakpoint needs a condition";
        return false;
    }
    if (want.kind == BpKind::Logpoint && trim_s(want.log_message).empty()) {
        err = "logpoint needs a log message";
        return false;
    }
    if (want.ignore_count < 0) want.ignore_count = 0;

    Breakpoint b;
    b.location = location;
    b.enabled = true;
    if (want.kind == BpKind::Logpoint) {
        // Logpoint = gdb dprintf: prints and never stops. There is no MI
        // -break-insert flag for it, so create it through the console
        // interpreter: dprintf LOC,"FMT", arg, ...  The whole console
        // command is one MI-quoted string; mi_quote escapes the quotes
        // and backslashes inside it, and the \n in the format survives
        // as a gdb string escape. The ^done reply carries no breakpoint
        // number, so find the new dprintf entry with a re-list.
        std::string fmt;
        std::vector<std::string> args;
        if (!logpoint_to_printf(want.log_message, fmt, args)) {
            err = "bad log message template (unbalanced {braces} or empty "
                  "{}) : " +
                  want.log_message;
            return false;
        }
        std::string console_cmd = "dprintf " + location + ",\"" + fmt + "\"";
        for (const auto& a : args) console_cmd += ", " + a;
        std::vector<int> before;
        for (const auto& old : bps_) before.push_back(old.id);
        MiValue r;
        if (!command("-interpreter-exec console " + mi_quote(console_cmd),
                     r, err))
            return false;
        if (!refresh_breakpoints(err)) return false;
        Breakpoint* found = nullptr;
        for (auto& cand : bps_) {
            if (cand.kind != BpKind::Logpoint) continue;
            bool is_new = std::find(before.begin(), before.end(),
                                    cand.id) == before.end();
            if (is_new && (!found || cand.id > found->id)) found = &cand;
        }
        if (!found) {
            err = "logpoint was created but gdb did not list it";
            return false;
        }
        // Condition / ignore count also apply to dprintf breakpoints.
        if (!apply_bp_options(found->id, want, err)) {
            std::string e2;
            remove_breakpoint(found->id, e2);  // roll back
            return false;
        }
        found->condition = want.condition;
        found->ignore_count = want.ignore_count;
        found->log_message = want.log_message;
        found->group = want.group;
        if (!want.enabled) {
            std::string e2;
            enable_breakpoint(found->id, false, e2);
            found->enabled = false;
        }
        out = *found;
        return true;
    }

    MiValue r;
    if (!command("-break-insert " + location, r, err)) return false;
    const MiValue* bk = r.get("bkpt");
    if (bk) {
        b.id = std::atoi(bk->get_str("number", "0").c_str());
        b.file = bk->get_str("fullname");
        if (b.file.empty()) b.file = bk->get_str("file");
        b.line = std::atoi(bk->get_str("line", "0").c_str());
        b.function = bk->get_str("func");
        // Multi-location breakpoints report number "1" with sub-entries;
        // keep the parent id, file/line from the first sub-entry if needed.
        if (b.line == 0) {
            const MiValue* first = nullptr;
            // bkpt may itself be a list of locations in some gdb versions.
            if (!bk->list.empty()) first = &bk->list[0];
            if (first) {
                b.line = std::atoi(first->get_str("line", "0").c_str());
                b.file = first->get_str("fullname");
                b.function = first->get_str("func");
            }
        }
    }
    if (b.id <= 0) {
        err = "gdb did not return a breakpoint number";
        return false;
    }
    b.kind = want.kind;
    b.condition = want.condition;
    b.ignore_count = want.ignore_count;
    b.log_message = want.log_message;
    b.group = want.group;
    // Replace any existing entry with the same id.
    for (auto& old : bps_)
        if (old.id == b.id) old = b;
    bool found = false;
    for (auto& old : bps_)
        if (old.id == b.id) found = true;
    if (!found) bps_.push_back(b);
    // gdb validates the condition here (unknown variable -> ^error);
    // roll the breakpoint back so a bad condition leaves nothing behind.
    if (!apply_bp_options(b.id, want, err)) {
        std::string e2;
        remove_breakpoint(b.id, e2);
        return false;
    }
    if (!want.enabled) {
        std::string e2;
        enable_breakpoint(b.id, false, e2);
        b.enabled = false;
        for (auto& old : bps_)
            if (old.id == b.id) old.enabled = false;
    }
    out = b;
    return true;
}

bool GdbSession::update_breakpoint(int id, const Breakpoint& opts,
                                   Breakpoint& out, std::string& err) {
    Breakpoint* cur = nullptr;
    for (auto& b : bps_)
        if (b.id == id) {
            cur = &b;
            break;
        }
    if (!cur) {
        // Staged entry (id 0): match by location and update in place.
        for (auto& b : bps_)
            if (b.id == 0 && b.location == opts.location) {
                cur = &b;
                break;
            }
        if (!cur) {
            err = "no breakpoint with id " + std::to_string(id);
            return false;
        }
        Breakpoint want = opts;
        want.location = cur->location;  // location is not editable here
        want.id = 0;
        want.file = cur->file;
        want.line = cur->line;
        want.function = cur->function;
        want.hits = cur->hits;
        if (want.kind == BpKind::Plain && !want.condition.empty())
            want.kind = BpKind::Conditional;
        *cur = want;
        out = *cur;
        return true;
    }
    if (!is_alive()) {
        // gdb is down: keep the entry, options included, for start().
        Breakpoint want = opts;
        want.location = cur->location;
        want.id = cur->id;
        want.file = cur->file;
        want.line = cur->line;
        want.function = cur->function;
        want.hits = cur->hits;
        if (want.kind == BpKind::Plain && !want.condition.empty())
            want.kind = BpKind::Conditional;
        *cur = want;
        out = *cur;
        return true;
    }
    // Live: a kind change needs a new breakpoint (a plain breakpoint
    // cannot become a dprintf in place), so delete + re-insert at the
    // same location. The gdb id may change; `out` carries the new one.
    std::string loc = cur->location;
    Breakpoint want = opts;
    want.location = loc;
    std::string e;
    if (!remove_breakpoint(id, e)) {
        err = e;
        return false;
    }
    Breakpoint nb;
    if (!add_breakpoint_opts(loc, want, nb, err)) return false;
    out = nb;
    return true;
}

bool GdbSession::remove_staged_breakpoint(const std::string& location) {
    auto it = std::find_if(bps_.begin(), bps_.end(), [&](const Breakpoint& b) {
        return b.location == location && b.id == 0;
    });
    if (it == bps_.end()) return false;
    bps_.erase(it);    return true;
}

bool GdbSession::remove_breakpoint(int id, std::string& err) {
    MiValue r;
    if (!command("-break-delete " + std::to_string(id), r, err)) return false;
    bps_.erase(std::remove_if(bps_.begin(), bps_.end(),
                              [&](const Breakpoint& b) { return b.id == id; }),
               bps_.end());
    return true;
}

bool GdbSession::enable_breakpoint(int id, bool enable, std::string& err) {
    MiValue r;
    std::string cmd = enable ? "-break-enable " : "-break-disable ";
    if (!command(cmd + std::to_string(id), r, err)) return false;
    for (auto& b : bps_)
        if (b.id == id) b.enabled = enable;
    return true;
}

bool GdbSession::refresh_breakpoints(std::string& err) {
    MiValue r;
    if (!command("-break-list", r, err)) return false;
    const MiValue* list = r.get("BreakpointTable");
    std::vector<Breakpoint> fresh;
    if (list) {
        const MiValue* body = list->get("body");
        if (body) {
            for (const auto& item : body->list) {
                const MiValue* bk = nullptr;
                // body entries are {bkpt={...}} pairs from list parsing.
                if (item.tuple.count("bkpt")) bk = &item.tuple.at("bkpt");
                else if (item.is_tuple) bk = &item;
                if (!bk) continue;
                Breakpoint b;
                b.id = std::atoi(bk->get_str("number", "0").c_str());
                if (b.id <= 0) continue;
                b.file = bk->get_str("fullname");
                if (b.file.empty()) b.file = bk->get_str("file");
                b.line = std::atoi(bk->get_str("line", "0").c_str());
                b.function = bk->get_str("func");
                b.enabled = bk->get_str("enabled", "y") == "y";
                b.location = bk->get_str("original-location");
                if (b.location.empty()) {
                    b.location = b.file.empty()
                                     ? b.function
                                     : b.file + ":" + std::to_string(b.line);
                }
                b.hits = std::atoi(bk->get_str("times", "0").c_str());
                // Handler fields straight from gdb: dprintf breakpoints
                // report type="dprintf"; conditions and the remaining
                // ignore count come back as cond=/ignore=. (gdb drops
                // `ignore` once the count is spent, so a missing field
                // means 0 remaining. The log template itself is not
                // reported — it is merged back from our own record below.)
                std::string type = bk->get_str("type");
                b.condition = bk->get_str("cond");
                b.ignore_count =
                    std::atoi(bk->get_str("ignore", "0").c_str());
                if (type == "dprintf") b.kind = BpKind::Logpoint;
                else if (!b.condition.empty()) b.kind = BpKind::Conditional;
                fresh.push_back(b);
            }
        }
    }
    // Merge: gdb is authoritative for kind/condition/ignore/hits, but
    // our own records keep the log template and group (gdb cannot
    // report them). Staged entries (id 0, not in gdb yet) are kept.
    std::vector<Breakpoint> merged;
    for (auto& f : fresh) {
        for (const auto& old : bps_) {
            if (old.id != f.id) continue;
            f.log_message = old.log_message;
            f.group = old.group;
            if (f.condition.empty()) f.condition = old.condition;
            break;
        }
        merged.push_back(f);
    }
    for (const auto& old : bps_)
        if (old.id == 0) merged.push_back(old);
    bps_ = merged;
    return true;
}

// Escape one persistence field: \ -> \\, | -> \|, newline -> \n, CR -> \r.
static std::string bps_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '|': out += "\\|"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

static std::string bps_unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[++i];
            switch (c) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                default: out += c; break;  // \\ and \| (and anything else)
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

// Split a persistence line on unescaped '|' (escapes stay in the parts
// for bps_unescape to handle).
static std::vector<std::string> bps_split(const std::string& line) {
    std::vector<std::string> parts;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            cur += c;
            cur += line[++i];
            continue;
        }
        if (c == '|') {
            parts.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    parts.push_back(cur);
    return parts;
}

bool GdbSession::save_breakpoints(const std::string& path) const {
    std::string p = path.empty() ? default_bps_path() : path;
    ensure_parent_dir(p);
    std::ofstream f(p);
    if (!f) return false;
    f << "# location|enabled|kind|condition|ignore|log_message|group\n";
    for (const auto& b : bps_)
        f << bps_escape(b.location) << "|" << (b.enabled ? "1" : "0") << "|"
          << bp_kind_name(b.kind) << "|" << bps_escape(b.condition) << "|"
          << b.ignore_count << "|" << bps_escape(b.log_message) << "|"
          << bps_escape(b.group) << "\n";
    return (bool)f;
}

bool GdbSession::load_breakpoints(const std::string& path) {
    std::string p = path.empty() ? default_bps_path() : path;
    std::ifstream f(p);
    if (!f) return false;
    bps_.clear();
    std::string line;
    while (std::getline(f, line)) {
        std::string t = trim_s(line);
        if (t.empty() || t[0] == '#') continue;
        std::vector<std::string> parts = bps_split(t);
        Breakpoint b;
        b.location = bps_unescape(parts[0]);
        b.enabled = parts.size() < 2 || parts[1] != "0";
        if (parts.size() >= 3 && !parts[2].empty()) {
            BpKind k;
            if (bp_kind_from_name(parts[2], k)) b.kind = k;
        }
        if (parts.size() >= 4) b.condition = bps_unescape(parts[3]);
        if (parts.size() >= 5) b.ignore_count = std::atoi(parts[4].c_str());
        if (parts.size() >= 6) b.log_message = bps_unescape(parts[5]);
        if (parts.size() >= 7) b.group = bps_unescape(parts[6]);
        // Split "file:line" into file/line so the code-view gutter can
        // show persisted breakpoints (debugger_breakpoint_lines matches on
        // file/line). Function-only locations ("compute") have neither.
        size_t colon = b.location.rfind(':');
        if (colon != std::string::npos &&
            colon + 1 < b.location.size()) {
            std::string tail = b.location.substr(colon + 1);
            bool all_digits = !tail.empty();
            for (char ch : tail)
                if (!std::isdigit((unsigned char)ch)) all_digits = false;
            if (all_digits) {
                b.file = b.location.substr(0, colon);
                b.line = std::atoi(tail.c_str());
            }
        }
        // A condition without an explicit kind is a conditional
        // breakpoint (also covers hand-edited files).
        if (b.kind == BpKind::Plain && !b.condition.empty())
            b.kind = BpKind::Conditional;
        bps_.push_back(b);
    }
    return true;
}

// ---------------------------------------------------------------- inspection

bool GdbSession::list_locals(std::vector<Variable>& out, std::string& err) {
    out.clear();
    MiValue r;
    if (!command("-stack-list-variables --simple-values", r, err)) return false;
    const MiValue* vars = r.get("variables");
    if (vars) {
        for (const auto& item : vars->list) {
            const MiValue* v = &item;
            if (item.tuple.count("name")) v = &item;
            Variable var;
            var.name = v->get_str("name");
            var.type = v->get_str("type");
            var.value = v->get_str("value");
            if (!var.name.empty()) out.push_back(var);
        }
    }
    locals_ = out;
    return true;
}

bool GdbSession::list_frames(std::vector<StackFrame>& out, std::string& err) {
    out.clear();
    MiValue r;
    if (!command("-stack-list-frames", r, err)) return false;
    const MiValue* stack = r.get("stack");
    if (stack) {
        for (const auto& item : stack->list) {
            const MiValue* fr = nullptr;
            if (item.tuple.count("frame")) fr = &item.tuple.at("frame");
            else if (item.is_tuple) fr = &item;
            if (!fr) continue;
            StackFrame f;
            f.level = std::atoi(fr->get_str("level", "0").c_str());
            f.function = fr->get_str("func");
            f.file = fr->get_str("fullname");
            if (f.file.empty()) f.file = fr->get_str("file");
            f.line = std::atoi(fr->get_str("line", "0").c_str());
            f.address = fr->get_str("addr");
            out.push_back(f);
        }
    }
    frames_ = out;
    return true;
}

bool GdbSession::evaluate(const std::string& expr, std::string& value,
                          std::string& err) {
    MiValue r;
    if (!command("-data-evaluate-expression \"" + expr + "\"", r, err))
        return false;
    value = r.get_str("value");
    return true;
}

bool GdbSession::add_watch(const std::string& expr, std::string& err) {
    std::string t = trim_s(expr);
    if (t.empty()) {
        err = "empty watch expression";
        return false;
    }
    for (const auto& w : watch_exprs_)
        if (w == t) return true;
    watch_exprs_.push_back(t);
    return refresh_watches(err);
}

bool GdbSession::remove_watch(const std::string& expr) {
    watch_exprs_.erase(
        std::remove(watch_exprs_.begin(), watch_exprs_.end(), expr),
        watch_exprs_.end());
    watches_.erase(std::remove_if(watches_.begin(), watches_.end(),
                                  [&](const Variable& v) {
                                      return v.name == expr;
                                  }),
                   watches_.end());
    return true;
}

bool GdbSession::refresh_watches(std::string& err) {
    (void)err;
    watches_.clear();
    for (const auto& expr : watch_exprs_) {
        std::string value, e;
        Variable v;
        v.name = expr;
        if (evaluate(expr, value, e)) {
            v.value = value;
            // Type via a second query is expensive; leave blank unless
            // the expression is a plain variable name (locals carry it).
            for (const auto& l : locals_)
                if (l.name == expr) v.type = l.type;
        } else {
            v.value = "<error: " + e + ">";
        }
        watches_.push_back(v);
    }
    return true;
}

void GdbSession::refresh_on_stop() {
    if (state_ != DbgState::Stopped) return;
    std::string err;
    if (settings_.print_all_locals) list_locals(locals_, err);
    list_frames(frames_, err);
    refresh_watches(err);
}

