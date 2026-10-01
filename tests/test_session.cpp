#include "check.h"
#include "sim.h"
#include <session.h>
#include <trace.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

using namespace accuchek;

static const std::vector<std::vector<sim::Record>> kTwoSegments = {
    {
        {2021, 1, 15, 8, 0, 133, 0},
        {2021, 1, 15, 12, 30, 192, 0},
    },
    {
        {2021, 1, 16, 7, 45, 98, 0},
    },
};

// trace line indexes in sim::sessionTrace (line 0 is the comment)
enum {
    kLinePairingConfirm = 3,
    kLineMdsAnswer = 7,
    kLineSegmentHeaders = 11,
    kLineFirstSegment = 12,
};

static std::string replaceLine(
    const std::string &trace,
    int index,
    const std::string &replacement
) {
    std::string out;
    size_t start = 0;
    for(int i=0; start<trace.size(); ++i) {
        auto nl = trace.find('\n', start);
        auto line = trace.substr(start, nl - start + 1);
        out += (i==index ? replacement + "\n" : line);
        start = nl + 1;
    }
    return out;
}

static std::vector<Sample> download(
    const std::string &trace,
    ReplayTransport **leftover = 0
) {
    static ReplayTransport *last = 0;
    delete last;
    last = new ReplayTransport(trace);
    if(leftover) {
        *leftover = last;
    }
    std::vector<Sample> samples;
    downloadSamples(*last, [&](const Sample &s) { samples.push_back(s); });
    return samples;
}

struct Failure {
    int code;
    std::string msg;
};

static Failure sessionFailure(
    const std::string &trace
) {
    try {
        download(trace);
    } catch(const SessionError &e) {
        return {e.code, e.what()};
    }
    return {kExitOk, ""};
}

static std::string sessionError(
    const std::string &trace
) {
    return sessionFailure(trace).msg;
}

TEST(session_downloads_all_segments) {
    ReplayTransport *t = 0;
    auto samples = download(sim::sessionTrace(kTwoSegments), &t);
    CHECK_EQ(samples.size(), 3u);
    CHECK_EQ(samples[0].value, 133);
    CHECK_EQ(samples[1].value, 192);
    CHECK_EQ(samples[2].value, 98);
    CHECK_EQ(samples[2].day, 16);
    CHECK(t->finished());
}

TEST(session_with_many_segments) {
    std::vector<std::vector<sim::Record>> segments;
    for(int k=0; k<10; ++k) {
        segments.push_back({{2024, 1, 1 + k, 8, 0, uint16_t(100 + k), 0}});
    }
    ReplayTransport *t = 0;
    auto samples = download(sim::sessionTrace(segments), &t);
    CHECK_EQ(samples.size(), 10u);
    CHECK_EQ(samples[9].value, 109);
    CHECK(t->finished());
}

TEST(session_association_abort) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineMdsAnswer, "< E60000020000");
    auto f = sessionFailure(trace);
    CHECK_EQ(f.code, kExitProtocol);
    CHECK(std::string::npos!=f.msg.find("abort"));
}

// an empty meter used to end in an error, tidepool treats it as no data
TEST(session_empty_meter_is_not_an_error) {
    ReplayTransport *t = 0;
    auto samples = download(sim::sessionTrace({}), &t);
    CHECK_EQ(samples.size(), 0u);
    CHECK(t->finished());
}

TEST(session_data_response_error) {
    auto headers = sim::segmentHeaders(0x0013, 0x0100, 2);
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineSegmentHeaders, sim::line('<', headers));
    auto f = sessionFailure(trace);
    CHECK_EQ(f.code, kExitProtocol);
    CHECK(std::string::npos!=f.msg.find("code = 2"));
}

TEST(session_timeout) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineFirstSegment, "< !-7");
    auto f = sessionFailure(trace);
    CHECK_EQ(f.code, kExitTransfer);
    CHECK(std::string::npos!=f.msg.find("timed out"));
}

TEST(session_meter_unplugged) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineMdsAnswer, "< !-4");
    auto f = sessionFailure(trace);
    CHECK_EQ(f.code, kExitTransfer);
    CHECK(std::string::npos!=f.msg.find("MDS attribute answer"));
}

TEST(session_garbage_answer) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineSegmentHeaders, "< E700000200");
    CHECK_EQ(sessionFailure(trace).code, kExitProtocol);
}

TEST(session_release_failure_keeps_samples) {
    auto full = sim::sessionTrace(kTwoSegments);
    auto lastLine = full.rfind("\n<") + 1;
    auto trace = full.substr(0, lastLine) + "< !-4\n";
    CHECK_EQ(sessionError(trace), std::string(""));
    CHECK_EQ(download(trace).size(), 3u);
    CHECK_EQ(download(full.substr(0, lastLine)).size(), 3u);
}

TEST(session_detects_unexpected_outgoing_message) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLinePairingConfirm, "> 00");
    CHECK(std::string::npos!=sessionError(trace).find("pairing confirmation"));
}

TEST(session_truncated_trace) {
    auto full = sim::sessionTrace(kTwoSegments);
    auto cut = full.substr(0, full.find("\n<", full.find("\n<", full.find("\n<") + 1) + 1));
    CHECK(!sessionError(cut).empty());
}

// checked-in copy of the simulated session, replayed by evals/accuchek_replay.py
// regenerate with: ACCUCHEK_UPDATE_FIXTURES=1 make test
// off scale readings and a reading with a non zero status
static const std::vector<std::vector<sim::Record>> kFlags = {
    {
        {2026, 9, 1, 8, 0, 120, 0},
        {2026, 9, 1, 12, 0, kValueHigh, 0},
        {2026, 9, 2, 3, 15, kValueLow, 0},
        {2026, 9, 2, 8, 0, 140, 0x0001},
    },
};

TEST(session_reports_every_sample_whatever_its_status) {
    auto samples = download(sim::sessionTrace(kFlags));
    CHECK_EQ(samples.size(), 4u);
    CHECK_EQ(samples[1].value, kValueHigh);
    CHECK_EQ(samples[2].value, kValueLow);
    CHECK_EQ(samples[3].status, 1);
}

static const std::vector<std::vector<sim::Record>> kSummerAndDst = {
    {
        {2026, 3, 29, 1, 59, 101, 0},
        {2026, 3, 29, 3, 0, 102, 0},
        {2026, 7, 2, 7, 36, 120, 0},
    },
    {
        {2026, 10, 25, 1, 59, 103, 0},
        {2026, 10, 25, 3, 0, 104, 0},
        {2026, 12, 24, 19, 0, 105, 0},
    },
};

static void checkFixture(
    const char *path,
    const std::string &expected
) {
    if(getenv("ACCUCHEK_UPDATE_FIXTURES")) {
        auto fp = fopen(path, "w");
        CHECK(0!=fp);
        fputs(expected.c_str(), fp);
        fclose(fp);
    }
    std::string text;
    CHECK(readFile(path, text));
    CHECK_EQ(text, expected);
}

TEST(fixture_traces_match_simulator) {
    checkFixture("tests/fixtures/two_segments.trace", sim::sessionTrace(kTwoSegments));
    checkFixture("tests/fixtures/summer_and_dst.trace", sim::sessionTrace(kSummerAndDst));
    checkFixture("tests/fixtures/flags.trace", sim::sessionTrace(kFlags));
    checkFixture("tests/fixtures/empty_meter.trace", sim::sessionTrace({}));
}

// command line: run the real binary on a trace

struct CliResult {
    int code;
    std::string out;
    std::string err;
};

// run the binary with args from directory cwd (default: a fresh empty one)
static CliResult runBinary(
    const std::string &args,
    const std::string &cwd = "",
    const std::string &env = "env -u ACCUCHEK_DBG"
) {
    std::string dir = cwd;
    char tmpdir[] = "/tmp/accuchek-cwd-XXXXXX";
    if(dir.empty()) {
        CHECK(0!=mkdtemp(tmpdir));
        dir = tmpdir;
    }
    char errPath[] = "/tmp/accuchek-err-XXXXXX";
    close(mkstemp(errPath));
    auto bin = getenv("ACCUCHEK_BIN");
    auto cmd = "cd '" + dir + "' && " + env + " " + (bin ? bin : "./accuchek") + " " + args + " 2>" + errPath;
    auto pipe = popen(cmd.c_str(), "r");
    CliResult result = {-1, "", ""};
    char chunk[4096];
    size_t n;
    while(0<(n = fread(chunk, 1, sizeof(chunk), pipe))) {
        result.out.append(chunk, n);
    }
    auto status = pclose(pipe);
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    readFile(errPath, result.err);
    unlink(errPath);
    if(cwd.empty()) {
        rmdir(tmpdir);
    }
    return result;
}

static std::string writeTemp(
    const std::string &text
) {
    char path[] = "/tmp/accuchek-test-XXXXXX";
    auto fd = mkstemp(path);
    CHECK(0<=fd);
    CHECK_EQ(write(fd, text.data(), text.size()), (ssize_t)text.size());
    close(fd);
    return path;
}

static CliResult runCli(
    const std::string &trace,
    const std::string &args = "",
    const std::string &env = "env -u ACCUCHEK_DBG"
) {
    auto path = writeTemp(trace);
    auto result = runBinary("--replay " + path + " " + args, "", env);
    unlink(path.c_str());
    return result;
}

TEST(cli_known_devices_without_config_file) {
    auto r = runBinary("--known-devices");
    CHECK_EQ(r.code, 0);
    CHECK_EQ(r.out, std::string("173a:21d5\n173a:21d7\n173a:21d8\n"));
}

TEST(cli_ignores_config_txt_in_current_directory) {
    char dir[] = "/tmp/accuchek-cwd-XXXXXX";
    CHECK(0!=mkdtemp(dir));
    auto cfg = std::string(dir) + "/config.txt";
    auto fp = fopen(cfg.c_str(), "w");
    fputs("vendor_0x173a_device_0x21d5 0\n", fp);
    fclose(fp);
    auto r = runBinary("--known-devices", dir);
    CHECK_EQ(r.out, std::string("173a:21d5\n173a:21d7\n173a:21d8\n"));
    auto withFile = runBinary("--config config.txt --known-devices", dir);
    CHECK_EQ(withFile.out, std::string("173a:21d7\n173a:21d8\n"));
    unlink(cfg.c_str());
    rmdir(dir);
}

TEST(cli_missing_config_file_is_an_error) {
    auto r = runBinary("--config /nonexistent/config.txt --known-devices");
    CHECK_EQ(r.code, kExitUsage);
    CHECK_EQ(r.out, std::string(""));
    CHECK_EQ(r.err, std::string("accuchek: cannot read config file /nonexistent/config.txt\n"));
}

TEST(cli_outputs_json_array) {
    auto r = runCli(sim::sessionTrace(kTwoSegments));
    CHECK_EQ(r.code, 0);
    CHECK(9<=r.out.size());
    if(r.out.size()<9) {
        return;
    }
    CHECK_EQ(r.out.substr(0, 6), std::string("[\n    "));
    CHECK_EQ(r.out.substr(r.out.size() - 3), std::string("\n]\n"));
    CHECK(std::string::npos!=r.out.find("\"timestamp\":\"2021/01/16 07:45\", \"mg/dL\": 98"));
    CHECK(std::string::npos!=r.out.find("\"id\":     2"));
}

TEST(cli_outputs_flagged_samples) {
    auto r = runCli(sim::sessionTrace(kFlags));
    CHECK_EQ(r.code, 0);
    CHECK(std::string::npos!=r.out.find("\"mg/dL\":601, \"mmol/L\": 33.388889, \"status\":0, \"range\":\"high\""));
    CHECK(std::string::npos!=r.out.find("\"range\":\"low\""));
    CHECK(std::string::npos!=r.out.find("\"mg/dL\":140, \"mmol/L\":  7.777778, \"status\":1 }"));
}

// failures used to leave "[" plus some samples on stdout and exit 1, whatever the cause
TEST(cli_timeout_exit_code_and_message) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineFirstSegment, "< !-7");
    auto r = runCli(trace);
    CHECK_EQ(r.code, kExitTransfer);
    CHECK_EQ(r.out, std::string(""));
    CHECK_EQ(r.err, std::string("accuchek: failed to receive message data segment: Operation timed out\n"));
}

TEST(cli_failure_after_some_samples_writes_none) {
    auto lastSegment = kLineFirstSegment + 2;
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), lastSegment, "< !-4");
    auto r = runCli(trace);
    CHECK_EQ(r.code, kExitTransfer);
    CHECK_EQ(r.out, std::string(""));
    CHECK(std::string::npos!=r.err.find("2 samples received before the error, none written"));
}

TEST(cli_abort_exit_code) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineMdsAnswer, "< E60000020000");
    auto r = runCli(trace);
    CHECK_EQ(r.code, kExitProtocol);
    CHECK_EQ(r.out, std::string(""));
    CHECK(0==r.err.find("accuchek: received association abort"));
}

TEST(cli_empty_meter_outputs_empty_array) {
    auto r = runCli(sim::sessionTrace({}));
    CHECK_EQ(r.code, kExitOk);
    CHECK_EQ(r.out, std::string("[\n]\n"));
    CHECK_EQ(r.err, std::string(""));
}

TEST(cli_unreadable_trace_is_a_usage_error) {
    auto r = runBinary("--replay /nonexistent.trace");
    CHECK_EQ(r.code, kExitUsage);
    CHECK_EQ(r.out, std::string(""));
    auto bad = runCli("garbage line\n");
    CHECK_EQ(bad.code, kExitUsage);
    CHECK(0==bad.err.find("accuchek: bad trace"));
}

TEST(cli_unknown_option_is_a_usage_error) {
    auto r = runBinary("--bogus");
    CHECK_EQ(r.code, kExitUsage);
}

// ACCUCHEK_DBG used to send logs and hexdumps to stdout, inside the JSON
TEST(cli_debug_logs_stay_off_stdout) {
    auto quiet = runCli(sim::sessionTrace(kTwoSegments));
    auto debug = runCli(sim::sessionTrace(kTwoSegments), "", "env ACCUCHEK_DBG=1");
    CHECK_EQ(debug.code, kExitOk);
    CHECK_EQ(debug.out, quiet.out);
    CHECK(std::string::npos!=debug.err.find("BUFFER START"));
    CHECK_EQ(quiet.err, std::string(""));
}

TEST(cli_not_root_exit_code) {
    if(0==geteuid()) {
        return;
    }
    auto r = runBinary("");
    CHECK_EQ(r.code, kExitAccessDenied);
    CHECK(0==r.err.find("accuchek: must be root"));
}
