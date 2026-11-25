// Headless driver for the FDesigner debugger backend (GdbSession).
// Usage: dbg_harness console <dir>   — console C++ tests
//        dbg_harness gtk <dir>       — GTK app tests (run under xvfb-run)
// Each test prints PASS/FAIL with its name; exit code is non-zero if any
// test fails. Line numbers come from // MARK:xxx comments in the targets,
// so editing the targets does not silently shift the expectations.
#include "../../src/debugger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

static std::string g_dir;
static int g_pass = 0, g_fail = 0;
static std::vector<std::string> g_failed;

static void report(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) {
        ++g_pass;
        std::printf("PASS %s\n", name.c_str());
    } else {
        ++g_fail;
        g_failed.push_back(name);
        std::printf("FAIL %s%s%s\n", name.c_str(),
                    detail.empty() ? "" : " — ", detail.c_str());
    }
    std::fflush(stdout);
}

static int mark_line(const std::string& file, const std::string& mark) {
    std::ifstream f(file);
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        ++n;
        if (line.find(mark) != std::string::npos) return n;
    }
    return -1;
}

static std::string loc(const std::string& file, int line) {
    return file + ":" + std::to_string(line);
}

static bool has(const std::vector<Variable>& vars, const std::string& name,
                const std::string& value) {
    for (const auto& v : vars)
        if (v.name == name && v.value == value) return true;
    return false;
}

static std::string var_value(const std::vector<Variable>& vars,
                             const std::string& name) {
    for (const auto& v : vars)
        if (v.name == name) return v.value;
    return "<missing>";
}

// ---- console tests -------------------------------------------------------

static void console_tests() {
    std::string console = g_dir + "/console";
    std::string src = g_dir + "/console.cpp";
    std::string spin = g_dir + "/spin";
    int L_LOOP = mark_line(src, "MARK:LOOP");
    int L_LOOP_NEXT = mark_line(src, "MARK:LOOP_NEXT");
    int L_CALL = mark_line(src, "MARK:CALL");
    int L_AFTER = mark_line(src, "MARK:AFTER_CALL");
    int L_COMPUTE = mark_line(src, "MARK:COMPUTE");
    int L_HELPER = mark_line(src, "MARK:HELPER");
    report("marks_found", L_LOOP > 0 && L_LOOP_NEXT > 0 && L_CALL > 0 &&
                             L_AFTER > 0 && L_COMPUTE > 0 && L_HELPER > 0);

    // 1. gdb exists at the default path
    {
        DebuggerSettings s;
        report("gdb_default_path_executable",
               ::access(s.gdb_path.c_str(), X_OK) == 0, s.gdb_path);
    }
    // 2. settings save/load round-trip
    {
        DebuggerSettings s;
        s.gdb_path = "/usr/bin/gdb";
        s.break_on_main = true;
        s.stop_on_entry = false;
        s.pretty_print = false;
        s.default_args = "a b";
        std::string p = "/tmp/dbgtest_conf";
        bool ok = s.save(p);
        DebuggerSettings s2;
        ok = ok && s2.load(p);
        ok = ok && s2.gdb_path == "/usr/bin/gdb" && s2.break_on_main &&
             !s2.pretty_print && s2.default_args == "a b";
        report("settings_save_load", ok);
    }
    // 3. bad gdb path fails cleanly
    {
        DebuggerSettings s;
        s.gdb_path = "/nonexistent/gdb";
        GdbSession sess(s);
        std::string err;
        bool ok = !sess.start(console, "", err) && !err.empty();
        report("bad_gdb_path_fails", ok, err);
    }
    // 4. run to completion, no breakpoints
    {
        GdbSession sess;
        std::string err;
        bool ok = sess.start(console, "", err);
        ok = ok && sess.wait_for_stop(10000);
        ok = ok && sess.state() == DbgState::Exited &&
             sess.exit_code() == 0;
        ok = ok && sess.target_output().find("DONE sum=9") != std::string::npos;
        report("run_to_completion", ok, err + " | out: " + sess.target_output());
    }
    // 5. breakpoint by file:line is hit
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        bool ok = sess.start(console, "", err);
        ok = ok && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().reason == "breakpoint-hit" &&
             sess.last_stop().frame.line == L_LOOP &&
             sess.last_stop().frame.function == "main";
        report("bp_file_line_hit", ok,
               err + " reason=" + sess.last_stop().reason +
                   " line=" + std::to_string(sess.last_stop().frame.line));
    }
    // 6. locals at the breakpoint (first iteration: i=0, sum=0)
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        sess.wait_for_stop();
        sess.refresh_on_stop();
        bool ok = has(sess.locals(), "i", "0") && has(sess.locals(), "sum", "0");
        report("locals_at_bp", ok,
               "i=" + var_value(sess.locals(), "i") +
                   " sum=" + var_value(sess.locals(), "sum"));
    }
    // 7. watch expression + evaluate
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        sess.wait_for_stop();
        sess.refresh_on_stop();
        bool ok = sess.add_watch("sum + 1", err);
        std::string v, e;
        ok = ok && sess.evaluate("i + 5", v, e) && v == "5";
        ok = ok && has(sess.watches(), "sum + 1", "1");
        report("watch_and_evaluate", ok, "i+5=" + v + " err=" + e);
    }
    // 8. step over: LOOP -> LOOP_NEXT, x == 1 after the call
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        sess.wait_for_stop();
        bool ok = sess.step_over(err) && sess.wait_for_stop();
        ok = ok && sess.last_stop().frame.line == L_LOOP_NEXT;
        std::string v, e;
        ok = ok && sess.evaluate("x", v, e) && v == "1";
        report("step_over", ok,
               "line=" + std::to_string(sess.last_stop().frame.line) +
                   " x=" + v);
    }
    // 9. step into: CALL line -> inside compute()
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_CALL));
        sess.start(console, "", err);
        sess.wait_for_stop();
        bool ok = sess.step_into(err) && sess.wait_for_stop();
        ok = ok && sess.last_stop().frame.function.find("compute") !=
                          std::string::npos;
        report("step_into", ok,
               "func=" + sess.last_stop().frame.function);
    }
    // 10. step out: from compute() back to main, line after the call
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_CALL));
        sess.start(console, "", err);
        sess.wait_for_stop();
        sess.step_into(err);
        sess.wait_for_stop();
        bool ok = sess.step_out(err) && sess.wait_for_stop();
        ok = ok && sess.last_stop().frame.function == "main" &&
             (sess.last_stop().frame.line == L_AFTER ||
              sess.last_stop().frame.line == L_CALL);
        std::string v, e;
        if (sess.last_stop().frame.line == L_CALL) {
            // finish stops on the call-site assignment line; one step
            // over executes the store, landing on the line after.
            ok = ok && sess.step_over(e) && sess.wait_for_stop() &&
                 sess.last_stop().frame.line == L_AFTER;
        }
        ok = ok && sess.evaluate("total", v, e) && v == "61";
        report("step_out", ok,
               "func=" + sess.last_stop().frame.function +
                   " line=" + std::to_string(sess.last_stop().frame.line) +
                   " total=" + v);
    }
    // 11. continue to the next breakpoint (loop bp, then helper bp)
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.stage_breakpoint(loc(src, L_HELPER));
        sess.start(console, "", err);
        bool ok = sess.wait_for_stop() &&
                  sess.last_stop().frame.line == L_LOOP;
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        ok = ok && sess.last_stop().reason == "breakpoint-hit" &&
             sess.last_stop().frame.function.find("helper") !=
                 std::string::npos;
        report("continue_to_next_bp", ok,
               "func=" + sess.last_stop().frame.function);
    }
    // 12. multiple breakpoints listed with ids
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.stage_breakpoint(loc(src, L_HELPER));
        sess.start(console, "", err);
        sess.wait_for_stop();
        bool ok = sess.refresh_breakpoints(err) &&
                  sess.breakpoints().size() >= 2;
        report("multiple_bps_listed", ok,
               "count=" + std::to_string(sess.breakpoints().size()));
    }
    // 13. disable a breakpoint: it is not hit
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.stage_breakpoint(loc(src, L_HELPER));
        sess.start(console, "", err);
        sess.wait_for_stop();  // at LOOP
        int loop_id = 0;
        for (const auto& b : sess.breakpoints())
            if (b.line == L_LOOP) loop_id = b.id;
        bool ok = loop_id > 0;
        ok = ok && sess.enable_breakpoint(loop_id, false, err);
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        // Next stop must be the helper bp, not the loop bp.
        ok = ok && sess.last_stop().frame.function.find("helper") !=
                          std::string::npos;
        report("disable_bp_skips", ok,
               "func=" + sess.last_stop().frame.function);
    }
    // 14. re-enable: the breakpoint is hit again
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        sess.wait_for_stop();
        int loop_id = 0;
        for (const auto& b : sess.breakpoints())
            if (b.line == L_LOOP) loop_id = b.id;
        bool ok = sess.enable_breakpoint(loop_id, false, err);
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        // Disabled: runs to completion (no more bps).
        ok = ok && sess.state() == DbgState::Exited;
        // Restart with the bp re-enabled: must stop at LOOP again.
        ok = ok && sess.enable_breakpoint(loop_id, true, err);
        ok = ok && sess.restart(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().frame.line == L_LOOP;
        report("enable_bp_hits_again", ok,
               "state=" + std::to_string((int)sess.state()));
    }
    // 15. delete a breakpoint: gone from the list, never hit
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        sess.wait_for_stop();
        int loop_id = 0;
        for (const auto& b : sess.breakpoints())
            if (b.line == L_LOOP) loop_id = b.id;
        bool ok = sess.remove_breakpoint(loop_id, err);
        ok = ok && sess.refresh_breakpoints(err) &&
             sess.breakpoints().empty();
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Exited;
        report("delete_bp", ok, "remaining=" +
                                    std::to_string(sess.breakpoints().size()));
    }
    // 16. breakpoint by function name
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint("compute");
        sess.start(console, "", err);
        bool ok = sess.wait_for_stop();
        ok = ok && sess.last_stop().frame.function.find("compute") !=
                          std::string::npos &&
             sess.last_stop().frame.line == L_COMPUTE;
        report("bp_by_function", ok,
               "func=" + sess.last_stop().frame.function +
                   " line=" + std::to_string(sess.last_stop().frame.line));
    }
    // 17. invalid breakpoints fail gracefully
    {
        GdbSession sess;
        std::string err;
        bool ok = !sess.stage_breakpoint("");
        ok = ok && !sess.stage_breakpoint("not a location!!!");
        // gdb-side rejection of a nonexistent file
        sess.start_gdb_only(err);
        sess.load_executable(console, err);
        Breakpoint b;
        std::string e2;
        ok = ok && !sess.add_breakpoint("no_such_file.cpp:9999", b, e2) &&
             !e2.empty();
        report("invalid_bp_rejected", ok, e2);
    }
    // 18. stack frames at a stop
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        sess.wait_for_stop();
        sess.refresh_on_stop();
        bool ok = !sess.frames().empty() &&
                  sess.frames()[0].function == "main" &&
                  sess.frames()[0].line == L_LOOP;
        report("frames_at_stop", ok,
               "n=" + std::to_string(sess.frames().size()));
    }
    // 19. stack depth inside nested calls (main > compute > helper)
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_HELPER));
        sess.start(console, "", err);
        bool ok19 = sess.wait_for_stop();
        // Hits 1-3 are helper() called from the loop (depth 2); hit 4 is
        // helper() called from compute() (depth 3: main > compute > helper).
        for (int i = 0; i < 3 && ok19; ++i)
            ok19 = sess.cont(err) && sess.wait_for_stop();
        sess.refresh_on_stop();
        bool ok = ok19 && sess.frames().size() >= 3;
        if (ok) {
            ok = sess.frames()[0].function.find("helper") != std::string::npos &&
                 sess.frames()[1].function.find("compute") != std::string::npos &&
                 sess.frames()[2].function == "main";
        }
        report("stack_depth_nested", ok,
               "n=" + std::to_string(sess.frames().size()));
    }
    // 20. kill a running (spinning) target
    {
        GdbSession sess;
        std::string err;
        bool ok = sess.start(spin, "", err);
        ::usleep(500000);
        ok = ok && sess.target_running();
        ok = ok && sess.kill_target(err);
        ok = ok && sess.wait_for_stop(5000);
        ok = ok && !sess.target_running() && sess.is_alive();
        report("kill_running_target", ok,
               "state=" + std::to_string((int)sess.state()));
    }
    // 21. restart after exit hits the breakpoint again
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        bool ok = sess.wait_for_stop();
        // The loop bp fires once per iteration (i=0,1,2): three continues
        // run the program to completion.
        for (int i = 0; i < 3 && ok; ++i)
            ok = sess.cont(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Exited;
        ok = ok && sess.restart(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().frame.line == L_LOOP;
        report("restart_after_exit", ok, "");
    }
    // 22. restart after kill: target runs again, can be killed again
    {
        GdbSession sess;
        std::string err;
        bool ok = sess.start(spin, "", err);
        ::usleep(300000);
        ok = ok && sess.kill_target(err) && sess.wait_for_stop(5000);
        ok = ok && sess.restart(err);
        ::usleep(300000);
        ok = ok && sess.target_running();
        ok = ok && sess.kill_target(err) && sess.wait_for_stop(5000);
        ok = ok && !sess.target_running();
        report("restart_after_kill", ok, "");
    }
    // 23. break_on_main setting stops at main
    {
        DebuggerSettings s;
        s.break_on_main = true;
        GdbSession sess(s);
        std::string err;
        bool ok = sess.start(console, "", err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().reason == "breakpoint-hit" &&
             sess.last_stop().frame.function == "main";
        report("setting_break_on_main", ok,
               "func=" + sess.last_stop().frame.function +
                   " reason=" + sess.last_stop().reason);
    }
    // 24. stop_on_entry setting stops at main
    {
        DebuggerSettings s;
        s.stop_on_entry = true;
        GdbSession sess(s);
        std::string err;
        bool ok = sess.start(console, "", err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().frame.function == "main";
        report("setting_stop_on_entry", ok,
               "func=" + sess.last_stop().frame.function);
    }
    // 25. pretty-print: vector value comes back expanded
    {
        DebuggerSettings s;
        s.pretty_print = true;
        GdbSession sess(s);
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        bool ok = sess.start(console, "", err) && sess.wait_for_stop();
        sess.refresh_on_stop();
        std::string v, e;
        ok = ok && sess.evaluate("vals", v, e);
        ok = ok && v.find("1") != std::string::npos &&
             v.find("3") != std::string::npos;
        // And the setting is what got saved to disk.
        DebuggerSettings s2;
        s2.pretty_print = true;
        ok = ok && s2.save("/tmp/dbgtest_conf2");
        DebuggerSettings s3;
        ok = ok && s3.load("/tmp/dbgtest_conf2") && s3.pretty_print;
        report("setting_pretty_print", ok, "vals=" + v);
    }
    // 26. settings file is actually read and applied (break_on_main via file)
    {
        DebuggerSettings s;
        s.gdb_path = "/usr/bin/gdb";
        s.break_on_main = true;
        s.save("/tmp/dbgtest_conf3");
        DebuggerSettings loaded;
        bool ok = loaded.load("/tmp/dbgtest_conf3") && loaded.break_on_main;
        GdbSession sess(loaded);
        std::string err;
        ok = ok && sess.start(console, "", err) && sess.wait_for_stop();
        ok = ok && sess.last_stop().frame.function == "main";
        report("settings_file_applied", ok, "");
    }
    // 27. target stdout/stderr captured
    {
        GdbSession sess;
        std::string err;
        bool ok = sess.start(console, "", err) && sess.wait_for_stop(10000);
        ok = ok && sess.target_output().find("i=0 x=1 sum=1") != std::string::npos &&
             sess.target_output().find("i=2 x=5 sum=9") != std::string::npos &&
             sess.target_output().find("TOTAL total=61 sum=9") != std::string::npos;
        report("target_output_captured", ok, sess.target_output());
    }
    // 28. evaluate arithmetic at a stop
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.start(console, "", err);
        sess.wait_for_stop();
        std::string v, e;
        bool ok = sess.evaluate("sum + 100", v, e) && v == "100";
        ok = ok && sess.evaluate("vals[2]", v, e) && v == "3";
        ok = ok && sess.evaluate("vals.size()", v, e) && v == "3";
        report("evaluate_arithmetic", ok, "sum+100=" + v);
    }
    // 29. target args are passed through
    {
        GdbSession sess;
        std::string err;
        bool ok = sess.start(console, "hello", err) && sess.wait_for_stop(10000);
        ok = ok && sess.target_output().find("ARG=hello") != std::string::npos;
        report("target_args_passed", ok, sess.target_output());
    }
    // 30. breakpoints persist to disk and load back
    {
        GdbSession sess;
        sess.stage_breakpoint(loc(src, L_LOOP));
        sess.stage_breakpoint("compute");
        bool ok = sess.save_breakpoints("/tmp/dbgtest_bps");
        GdbSession sess2;
        ok = ok && sess2.load_breakpoints("/tmp/dbgtest_bps");
        ok = ok && sess2.breakpoints().size() == 2 &&
             sess2.breakpoints()[0].location == loc(src, L_LOOP) &&
             sess2.breakpoints()[1].location == "compute";
        report("bps_persist_to_disk", ok, "");
    }
    // 31. conditional breakpoint: fires only when i == 2, never before
    {
        GdbSession sess;
        std::string err;
        Breakpoint opts;
        opts.kind = BpKind::Conditional;
        opts.condition = "i == 2";
        bool ok = sess.stage_breakpoint_opts(loc(src, L_LOOP), opts);
        ok = ok && sess.start(console, "", err);
        ok = ok && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().reason == "breakpoint-hit" &&
             sess.last_stop().frame.line == L_LOOP;
        std::string v, e;
        ok = ok && sess.evaluate("i", v, e) && v == "2";
        // It must NOT have stopped at i=0/1: both iterations printed
        // before the first (and only) stop.
        ok = ok && sess.target_output().find("i=0 x=1 sum=1") !=
                         std::string::npos &&
             sess.target_output().find("i=1 x=3 sum=4") != std::string::npos;
        // Hit count from gdb: a conditional breakpoint counts stops
        // only (reaches with a false condition are not hits) -> 1.
        ok = ok && sess.refresh_breakpoints(err);
        int hits = -1;
        for (const auto& b : sess.breakpoints())
            if (b.line == L_LOOP) hits = b.hits;
        ok = ok && hits == 1;
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Exited;
        report("bp_conditional_only_when_true", ok,
               "i=" + v + " hits=" + std::to_string(hits));
    }
    // 32. ignore count: first 2 hits skipped, stops on the 3rd (i == 2)
    {
        GdbSession sess;
        std::string err;
        Breakpoint opts;
        opts.ignore_count = 2;
        bool ok = sess.stage_breakpoint_opts(loc(src, L_LOOP), opts);
        ok = ok && sess.start(console, "", err);
        ok = ok && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().frame.line == L_LOOP;
        std::string v, e;
        ok = ok && sess.evaluate("i", v, e) && v == "2";
        ok = ok && sess.target_output().find("i=0 x=1 sum=1") !=
                         std::string::npos &&
             sess.target_output().find("i=1 x=3 sum=4") != std::string::npos;
        // gdb counts every reach, ignored or not, in `times` -> 3.
        ok = ok && sess.refresh_breakpoints(err);
        int hits = -1;
        for (const auto& b : sess.breakpoints())
            if (b.line == L_LOOP) hits = b.hits;
        ok = ok && hits == 3;
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Exited;
        report("bp_ignore_count_skips", ok,
               "i=" + v + " hits=" + std::to_string(hits));
    }
    // 33. logpoint: prints its message at every hit, never stops
    {
        GdbSession sess;
        std::string err;
        Breakpoint opts;
        opts.kind = BpKind::Logpoint;
        opts.log_message = "LOG i={i}";
        bool ok = sess.stage_breakpoint_opts(loc(src, L_LOOP), opts);
        ok = ok && sess.start(console, "", err);
        ok = ok && sess.wait_for_stop(10000);
        // No stop at all: straight to a clean exit.
        ok = ok && sess.state() == DbgState::Exited &&
             sess.exit_code() == 0;
        // dprintf output arrives on gdb's console stream (-> gdb_log()).
        ok = ok && sess.gdb_log().find("LOG i=0") != std::string::npos &&
             sess.gdb_log().find("LOG i=1") != std::string::npos &&
             sess.gdb_log().find("LOG i=2") != std::string::npos;
        ok = ok && sess.target_output().find("DONE sum=9") !=
                         std::string::npos;
        report("bp_logpoint_prints_no_stop", ok, sess.gdb_log());
    }
    // 34. logpoint template with several placeholders and formats
    {
        // Template conversion is pure string work: check it directly.
        std::string fmt;
        std::vector<std::string> args;
        bool ok = logpoint_to_printf("i={i} s={s:name} x={x:v}", fmt, args);
        ok = ok && fmt == "i=%d s=%s x=%#x\\n" && args.size() == 3 &&
             args[0] == "i" && args[1] == "name" && args[2] == "v";
        ok = ok && logpoint_to_printf("100% done", fmt, args) &&
             fmt == "100%% done\\n" && args.empty();
        ok = ok && !logpoint_to_printf("oops {i", fmt, args);
        ok = ok && !logpoint_to_printf("oops {}", fmt, args);
        // And end to end: two placeholders print at the loop.
        GdbSession sess;
        std::string err;
        Breakpoint opts;
        opts.kind = BpKind::Logpoint;
        opts.log_message = "LP i={i} sum={sum}";
        ok = ok && sess.stage_breakpoint_opts(loc(src, L_LOOP), opts);
        ok = ok && sess.start(console, "", err);
        ok = ok && sess.wait_for_stop(10000);
        ok = ok && sess.state() == DbgState::Exited;
        ok = ok && sess.gdb_log().find("LP i=0 sum=0") != std::string::npos &&
             sess.gdb_log().find("LP i=2 sum=4") != std::string::npos;
        report("bp_logpoint_template", ok, sess.gdb_log());
    }
    // 35. edit a live breakpoint: plain -> conditional, takes effect
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_LOOP));
        bool ok = sess.start(console, "", err);
        ok = ok && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped;
        std::string v, e;
        ok = ok && sess.evaluate("i", v, e) && v == "0";  // plain: stops at 0
        int loop_id = 0;
        for (const auto& b : sess.breakpoints())
            if (b.line == L_LOOP) loop_id = b.id;
        ok = ok && loop_id > 0;
        Breakpoint upd;
        upd.kind = BpKind::Conditional;
        upd.condition = "i == 2";
        upd.location = loc(src, L_LOOP);
        Breakpoint out;
        ok = ok && sess.update_breakpoint(loop_id, upd, out, err);
        ok = ok && out.kind == BpKind::Conditional &&
             out.condition == "i == 2";
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().frame.line == L_LOOP;
        ok = ok && sess.evaluate("i", v, e) && v == "2";
        ok = ok && sess.cont(err) && sess.wait_for_stop();
        ok = ok && sess.state() == DbgState::Exited;
        report("bp_update_plain_to_conditional", ok, "i=" + v);
    }
    // 36. handler fields persist to disk and load back; old two-field
    // files still load as plain breakpoints.
    {
        GdbSession sess;
        Breakpoint c;
        c.kind = BpKind::Conditional;
        c.condition = "i == 2 || sum > 3";  // exercises | escaping
        c.group = "loop";
        Breakpoint l;
        l.kind = BpKind::Logpoint;
        l.log_message = "LOG i={i} | sum={sum}";
        l.group = "trace";
        Breakpoint p;
        p.ignore_count = 3;
        bool ok = sess.stage_breakpoint_opts(loc(src, L_LOOP), c);
        ok = ok && sess.stage_breakpoint_opts(loc(src, L_HELPER), l);
        ok = ok && sess.stage_breakpoint_opts("compute", p);
        ok = ok && sess.save_breakpoints("/tmp/dbgtest_bps2");
        GdbSession sess2;
        ok = ok && sess2.load_breakpoints("/tmp/dbgtest_bps2");
        ok = ok && sess2.breakpoints().size() == 3;
        if (ok) {
            const auto& b0 = sess2.breakpoints()[0];
            const auto& b1 = sess2.breakpoints()[1];
            const auto& b2 = sess2.breakpoints()[2];
            ok = b0.kind == BpKind::Conditional &&
                 b0.condition == "i == 2 || sum > 3" && b0.group == "loop" &&
                 b0.file == src && b0.line == L_LOOP &&
                 b1.kind == BpKind::Logpoint &&
                 b1.log_message == "LOG i={i} | sum={sum}" &&
                 b1.group == "trace" && b1.file == src &&
                 b1.line == L_HELPER && b2.kind == BpKind::Plain &&
                 b2.ignore_count == 3 && b2.location == "compute" &&
                 b2.file.empty() && b2.line == 0;
        }
        // Old format: location|enabled, nothing else.
        {
            std::ofstream f("/tmp/dbgtest_bps_old");
            f << loc(src, L_LOOP) << "|1\ncompute|0\n";
        }
        GdbSession sess3;
        ok = ok && sess3.load_breakpoints("/tmp/dbgtest_bps_old");
        ok = ok && sess3.breakpoints().size() == 2 &&
             sess3.breakpoints()[0].kind == BpKind::Plain &&
             sess3.breakpoints()[0].enabled &&
             sess3.breakpoints()[0].file == src &&
             sess3.breakpoints()[0].line == L_LOOP &&
             sess3.breakpoints()[1].kind == BpKind::Plain &&
             !sess3.breakpoints()[1].enabled &&
             sess3.breakpoints()[1].file.empty();
        // A bad condition is rejected and leaves no breakpoint behind.
        {
            GdbSession s4;
            std::string e2;
            s4.start_gdb_only(e2);
            s4.load_executable(console, e2);
            Breakpoint bad;
            bad.kind = BpKind::Conditional;
            bad.condition = "nosuchvar == 2";
            Breakpoint out;
            ok = ok && !s4.add_breakpoint_opts(loc(src, L_LOOP), bad, out,
                                               e2) &&
                 !e2.empty() && s4.breakpoints().empty();
        }
        report("bp_handlers_persist_roundtrip", ok, "");
    }
}

// ---- GTK tests (run under xvfb-run) ---------------------------------------

static void gtk_tests() {
    std::string app = g_dir + "/gtkapp";
    std::string src = g_dir + "/gtkapp.cpp";
    int L_CB = mark_line(src, "MARK:CB");
    int L_CB_NEXT = mark_line(src, "MARK:CB_NEXT");
    report("gtk_marks_found", L_CB > 0 && L_CB_NEXT > 0);
    // 31. breakpoint in the button callback is hit on auto-click
    {
        GdbSession sess;
        std::string err;
        sess.stage_breakpoint(loc(src, L_CB));
        bool ok = sess.start(app, "", err);
        ok = ok && sess.wait_for_stop(15000);
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().reason == "breakpoint-hit" &&
             sess.last_stop().frame.function.find("on_clicked") !=
                 std::string::npos &&
             sess.last_stop().frame.line == L_CB;
        report("gtk_bp_in_callback", ok,
               "func=" + sess.last_stop().frame.function +
                   " line=" + std::to_string(sess.last_stop().frame.line) +
                   " reason=" + sess.last_stop().reason);
        if (ok) {
            // 32. step through the callback: counter becomes 1. (At the
            // CB line `before` is still unassigned, so step to the next
            // line first, where before == 0.)
            std::string e;
            bool ok2 = sess.step_over(e) && sess.wait_for_stop();
            ok2 = ok2 && sess.last_stop().frame.line == L_CB_NEXT;
            sess.refresh_on_stop();
            ok2 = ok2 && has(sess.locals(), "before", "0");
            ok2 = ok2 && sess.step_over(e) && sess.wait_for_stop();
            std::string v;
            ok2 = ok2 && sess.evaluate("counter", v, e) && v == "1";
            report("gtk_step_in_callback", ok2,
                   "before=" + var_value(sess.locals(), "before") +
                       " counter=" + v);
            // 33. continue: app finishes on its own timer, exit 0
            bool ok3 = sess.cont(e) && sess.wait_for_stop(15000);
            ok3 = ok3 && sess.state() == DbgState::Exited &&
                  sess.exit_code() == 0;
            ok3 = ok3 && sess.target_output().find("CLICKS=1") !=
                              std::string::npos;
            report("gtk_continue_to_exit", ok3, sess.target_output());
        } else {
            report("gtk_step_in_callback", false, "callback bp never hit");
            report("gtk_continue_to_exit", false, "callback bp never hit");
        }
    }
    // 34. GTK app runs to completion with no breakpoints
    {
        GdbSession sess;
        std::string err;
        bool ok = sess.start(app, "", err) && sess.wait_for_stop(15000);
        ok = ok && sess.state() == DbgState::Exited && sess.exit_code() == 0;
        ok = ok && sess.target_output().find("CLICKS=1") != std::string::npos;
        report("gtk_run_to_completion", ok, sess.target_output());
    }
    // 35. logpoint in the callback: prints, never stops, app exits 0
    {
        GdbSession sess;
        std::string err;
        Breakpoint opts;
        opts.kind = BpKind::Logpoint;
        opts.log_message = "CLICK counter={counter}";
        bool ok = sess.stage_breakpoint_opts(loc(src, L_CB), opts);
        ok = ok && sess.start(app, "", err);
        ok = ok && sess.wait_for_stop(15000);
        ok = ok && sess.state() == DbgState::Exited && sess.exit_code() == 0;
        ok = ok && sess.target_output().find("CLICKS=1") !=
                         std::string::npos;
        // At the callback entry counter is still 0 (the click has not
        // been counted yet); the dprintf output is on gdb's console
        // stream (-> gdb_log()).
        ok = ok && sess.gdb_log().find("CLICK counter=0") !=
                         std::string::npos;
        report("gtk_logpoint_no_stop", ok, sess.gdb_log());
    }
    // 36. conditional breakpoint in the callback: fires when counter == 0
    {
        GdbSession sess;
        std::string err;
        Breakpoint opts;
        opts.kind = BpKind::Conditional;
        opts.condition = "counter == 0";
        bool ok = sess.stage_breakpoint_opts(loc(src, L_CB), opts);
        ok = ok && sess.start(app, "", err);
        ok = ok && sess.wait_for_stop(15000);
        ok = ok && sess.state() == DbgState::Stopped &&
             sess.last_stop().reason == "breakpoint-hit" &&
             sess.last_stop().frame.line == L_CB;
        ok = ok && sess.cont(err) && sess.wait_for_stop(15000);
        ok = ok && sess.state() == DbgState::Exited && sess.exit_code() == 0;
        ok = ok && sess.target_output().find("CLICKS=1") !=
                         std::string::npos;
        report("gtk_conditional_in_callback", ok,
               "reason=" + sess.last_stop().reason);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s console|gtk <tests/debugger dir>\n",
                     argv[0]);
        return 2;
    }
    g_dir = argv[2];
    if (!std::strcmp(argv[1], "console")) console_tests();
    else if (!std::strcmp(argv[1], "gtk")) gtk_tests();
    else {
        std::fprintf(stderr, "unknown mode %s\n", argv[1]);
        return 2;
    }
    std::printf("SUMMARY pass=%d fail=%d\n", g_pass, g_fail);
    if (!g_failed.empty()) {
        std::printf("FAILED:");
        for (const auto& f : g_failed) std::printf(" %s", f.c_str());
        std::printf("\n");
    }
    return g_fail == 0 ? 0 : 1;
}
