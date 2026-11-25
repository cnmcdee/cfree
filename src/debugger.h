// FDesigner stepping debugger — GDB/MI backend (no GTK dependency).
// Drives a real gdb process over the machine interface (gdb -i=mi2):
// launch target, breakpoints, step over/into/out, continue, kill, restart,
// locals / watches / stack frames, target stdout/stderr capture.
//
// Target output: in MI mode gdb forwards the inferior's stdout/stderr as
// bare lines on gdb's stdout (not @"..." records); the parser treats any
// line with no MI prefix as target output. Breakpoints may resolve to
// several locations (e.g. a one-line function body); the breakpoint id
// reported is the parent number.
//
// Breakpoint handlers: every breakpoint carries a kind — plain, conditional
// (stops only when a gdb expression is true, via -break-condition) or
// logpoint (a gdb dprintf: prints a message and never stops). Any kind may
// also carry an ignore count (first N hits are skipped, via -break-after;
// gdb counts every reach, ignored or not, in the breakpoint's `times`
// hit count) and a group label. A logpoint message is a template:
// literal text plus {expr} placeholders, printed as integers by default;
// {s:expr} prints a string, {f:expr} a float, {x:expr} hex, {p:expr} a
// pointer. A conditional breakpoint's `times` counts stops only — reaches
// where the condition is false are not hits at all.
//
// Settings live in ~/.config/fdesigner/debugger.conf (key=value, see
// DebuggerSettings::save/load). FDESIGNER_DEBUGGER_CONF overrides the
// path (used by tests). Breakpoints are persisted per session in
// ~/.config/fdesigner/debugger_bps.conf, one per line:
//   location|enabled|kind|condition|ignore|log_message|group
// (\, | and newline inside fields are backslash-escaped.) Old two-field
// lines (location|enabled) still load as plain breakpoints.
#pragma once

#include <string>
#include <vector>
#include <map>

struct DebuggerSettings {
    std::string gdb_path = "/usr/bin/gdb";
    bool break_on_main = false;   // insert a breakpoint at main before run
    bool stop_on_entry = false;   // stop at main on entry (temp bp at main)
    bool pretty_print = true;     // -enable-pretty-printing on session start
    bool print_all_locals = true; // refresh locals on every stop
    std::string default_args;     // args passed to the target by default
    int stop_timeout_ms = 10000;  // how long to wait for a stop event
    int cmd_timeout_ms = 5000;    // how long to wait for a ^done/^error

    static std::string default_path();  // ~/.config/fdesigner/debugger.conf
    bool save(const std::string& path = "") const;
    bool load(const std::string& path = "");
};

// What a breakpoint does when its location is reached.
enum class BpKind { Plain, Conditional, Logpoint };

// Display name for a breakpoint kind ("plain"/"conditional"/"logpoint").
const char* bp_kind_name(BpKind kind);
// Parse a kind name; accepts "plain", "conditional"/"cond",
// "logpoint"/"log"/"dprintf". Returns false for anything else.
bool bp_kind_from_name(const std::string& name, BpKind& kind);

struct Breakpoint {
    int id = 0;                    // gdb breakpoint number
    std::string location;          // as requested: "file:line" or "func"
    std::string file;              // resolved file (may be empty)
    int line = 0;                  // resolved line (0 if by function)
    std::string function;          // resolved function (may be empty)
    bool enabled = true;
    int hits = 0;                  // gdb `times`: reaches (conditional: stops)
    // Multiple-handler fields (see the header comment above).
    BpKind kind = BpKind::Plain;
    std::string condition;         // conditional: gdb expression, e.g. "i == 5"
    int ignore_count = 0;          // skip the first N hits (-break-after)
    std::string log_message;       // logpoint template, e.g. "i={i} sum={sum}"
    std::string group;             // optional group label (organisation only)
};

struct Variable {
    std::string name, type, value;
};

struct StackFrame {
    int level = 0;
    std::string function, file;
    int line = 0;
    std::string address;
};

struct StopInfo {
    std::string reason;  // breakpoint-hit, end-stepping-range, exited-normally, ...
    int breakpoint_id = 0;
    StackFrame frame;
    int exit_code = -1;  // >=0 once the target has exited
    bool exited = false;
};

enum class DbgState { Idle, Starting, Running, Stopped, Exited, Error };

// Minimal MI value: string, tuple {k=v,...} or list [v,...].
struct MiValue {
    std::string str;
    std::map<std::string, MiValue> tuple;
    std::vector<MiValue> list;
    bool is_list = false;
    bool is_tuple = false;
    const MiValue* get(const std::string& key) const {
        auto it = tuple.find(key);
        return it == tuple.end() ? nullptr : &it->second;
    }
    std::string get_str(const std::string& key,
                        const std::string& dflt = "") const {
        const MiValue* v = get(key);
        return v ? v->str : dflt;
    }
};

// Parse one MI value starting at s[pos]; advances pos. Exposed for tests.
bool mi_parse_value(const std::string& s, size_t& pos, MiValue& out);
// Decode an MI C-escaped string body (no surrounding quotes).
std::string mi_unescape(const std::string& s);
// Parse a "file:line" or "function" breakpoint location. Returns false if
// the text is not a plausible location (empty, no colon/function chars).
bool parse_breakpoint_location(const std::string& text, std::string& file,
                               int& line, std::string& function);
// Convert a logpoint message template ("reached loop, i={i} name={s:name}")
// to a gdb printf format string plus the argument expressions, in order.
// {expr} prints as %d; {s:expr} %s, {f:expr} %f, {x:expr} %#x, {p:expr} %p.
// Literal % in the template is escaped to %%. Returns false on an
// unbalanced brace or an empty placeholder. Exposed for tests.
bool logpoint_to_printf(const std::string& tmpl, std::string& fmt,
                        std::vector<std::string>& args);

class GdbSession {
public:
    explicit GdbSession(const DebuggerSettings& settings = DebuggerSettings());
    ~GdbSession();
    GdbSession(const GdbSession&) = delete;
    GdbSession& operator=(const GdbSession&) = delete;

    const DebuggerSettings& settings() const { return settings_; }
    void set_settings(const DebuggerSettings& s) { settings_ = s; }

    // Start gdb on the target. Returns false + err on failure (bad gdb
    // path, missing executable, gdb died). Applies pretty-print and
    // break_on_main/stop_on_entry breakpoints when set.
    bool start(const std::string& executable, const std::string& args,
               std::string& err);
    // Start gdb only (no target yet); load_executable() then run().
    bool start_gdb_only(std::string& err);
    bool load_executable(const std::string& executable, std::string& err);
    void shutdown();  // kill gdb, reap child

    bool is_alive() const;
    bool target_running() const { return state_ == DbgState::Running; }
    DbgState state() const { return state_; }
    const std::string& executable() const { return executable_; }
    int exit_code() const { return last_stop_.exit_code; }

    // Execution control. Each returns false + err if gdb rejects it or
    // the target is not in a state where it applies.
    bool run(std::string& err);       // (re)run from the start
    bool cont(std::string& err);      // continue
    bool step_over(std::string& err); // -exec-next
    bool step_into(std::string& err); // -exec-step
    bool step_out(std::string& err);  // -exec-finish
    bool kill_target(std::string& err);  // -exec-kill (target keeps gdb)
    bool restart(std::string& err);   // kill (if running) + run again
    bool interrupt(std::string& err); // SIGINT to the inferior

    // Wait until the target stops (breakpoint/step/exit) or timeout_ms
    // elapses. Returns true if a stop event arrived.
    bool wait_for_stop(int timeout_ms = -1);
    // Non-blocking drain of pending MI output (used by the GUI poll tick).
    void poll_events();
    const StopInfo& last_stop() const { return last_stop_; }
    // Monotonic stop counter (incremented on every *stopped event).
    // Lets a UI poll tell a fresh stop apart from a stale Stopped state.
    unsigned long stop_seq() const { return stop_seq_; }

    // Breakpoints.
    // Remember a breakpoint before gdb is running (e.g. typed in the UI
    // before Run); start() inserts it into gdb. Returns false if the
    // location does not parse.
    bool stage_breakpoint(const std::string& location);
    // As above, with handler options (kind/condition/ignore/log/group).
    // Replaces the options of an already-staged entry at the same
    // location; a live breakpoint at the location is left alone.
    bool stage_breakpoint_opts(const std::string& location,
                               const Breakpoint& opts);
    // Erase a staged (not yet inserted into gdb, id == 0) breakpoint
    // by location. Used by the code-view gutter toggle: remove_breakpoint
    // needs a live gdb, this one does not. Returns false if not staged.
    bool remove_staged_breakpoint(const std::string& location);    bool add_breakpoint(const std::string& location, Breakpoint& out,
                        std::string& err);
    // As above, with handler options. A conditional breakpoint needs a
    // non-empty condition, a logpoint a non-empty message; gdb validates
    // the condition expression (unknown variable -> error, and the
    // breakpoint is rolled back). The breakpoint is created disabled
    // when opts.enabled is false.
    bool add_breakpoint_opts(const std::string& location,
                             const Breakpoint& opts, Breakpoint& out,
                             std::string& err);
    bool remove_breakpoint(int id, std::string& err);
    bool enable_breakpoint(int id, bool enable, std::string& err);
    // Replace a breakpoint's handler options (location is unchanged).
    // Live: delete + re-insert with the new options (the gdb id may
    // change; `out` carries the new breakpoint). Staged (id 0, gdb not
    // running): updated in place. Returns false + err when the id is
    // unknown or gdb rejects the new options.
    bool update_breakpoint(int id, const Breakpoint& opts, Breakpoint& out,
                           std::string& err);
    bool refresh_breakpoints(std::string& err);  // re-list from gdb
    const std::vector<Breakpoint>& breakpoints() const { return bps_; }
    // Breakpoints kept across sessions (saved to disk by the UI).
    bool save_breakpoints(const std::string& path = "") const;
    bool load_breakpoints(const std::string& path = "");
    static std::string default_bps_path();

    // Inspection (valid while stopped).
    bool list_locals(std::vector<Variable>& out, std::string& err);
    bool list_frames(std::vector<StackFrame>& out, std::string& err);
    bool evaluate(const std::string& expr, std::string& value,
                  std::string& err);
    // Watch expressions: kept in-session, refreshed by refresh_watches().
    bool add_watch(const std::string& expr, std::string& err);
    bool remove_watch(const std::string& expr);
    bool refresh_watches(std::string& err);
    const std::vector<Variable>& watches() const { return watches_; }
    const std::vector<Variable>& locals() const { return locals_; }
    const std::vector<StackFrame>& frames() const { return frames_; }
    // Refresh locals+frames+watches after a stop (best effort).
    void refresh_on_stop();

    // Output accumulated so far (target stdout/stderr + gdb console text).
    // Logpoint (dprintf) output arrives on gdb's console stream, so it
    // lands in gdb_log(), not target_output().
    const std::string& target_output() const { return target_output_; }
    const std::string& gdb_log() const { return gdb_log_; }
    void clear_output() { target_output_.clear(); gdb_log_.clear(); }
    const std::string& last_error() const { return last_error_; }

    // Low-level: send one MI command, wait for its ^done/^error.
    // Public so the GUI/tests can run extra MI commands.
    bool command(const std::string& mi_cmd, MiValue& result,
                 std::string& err, int timeout_ms = -1);

private:
    bool spawn_gdb(std::string& err);
    bool pump(int timeout_ms);  // read available MI lines for up to timeout
    void handle_line(const std::string& line);
    bool wait_result(int token, MiValue& result, std::string& err,
                     int timeout_ms);
    bool send_raw(const std::string& text);
    // Send an -exec-* command; move to Running unless a fresh *stopped
    // arrived while waiting for the ^running reply (see debugger.cpp).
    bool issue_exec(const std::string& mi_cmd, std::string& err);
    // Apply a breakpoint's condition (-break-condition) and ignore count
    // (-break-after) to a breakpoint that already exists in gdb.
    bool apply_bp_options(int id, const Breakpoint& opts, std::string& err);

    DebuggerSettings settings_;
    unsigned long stop_seq_ = 0;  // incremented on every *stopped event
    pid_t gdb_pid_ = -1;
    pid_t inferior_pid_ = -1;  // target pid, from =thread-group-started
    int gdb_stdin_ = -1, gdb_stdout_ = -1;
    int next_token_ = 1;
    DbgState state_ = DbgState::Idle;
    std::string executable_, args_;
    std::string read_buf_;
    std::string target_output_, gdb_log_, last_error_;
    StopInfo last_stop_;
    std::vector<Breakpoint> bps_;
    std::vector<std::string> watch_exprs_;
    std::vector<Variable> watches_, locals_;
    std::vector<StackFrame> frames_;
    // Pending synchronous results, keyed by token.
    std::map<int, std::pair<bool, MiValue>> pending_results_;
    std::string pending_error_;
};
