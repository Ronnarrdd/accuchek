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

static std::string sessionError(
    const std::string &trace
) {
    try {
        download(trace);
    } catch(const SessionError &e) {
        return e.what();
    }
    return "";
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
    CHECK(std::string::npos!=sessionError(trace).find("abort"));
}

TEST(session_empty_meter) {
    auto headers = sim::segmentHeaders(0x0013, 0x0100, 3);
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineSegmentHeaders, sim::line('<', headers));
    CHECK(std::string::npos!=sessionError(trace).find("empty"));
}

TEST(session_timeout) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineFirstSegment, "< !-7");
    CHECK(std::string::npos!=sessionError(trace).find("timed out"));
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
}

// command line: run the real binary on a trace

struct CliResult {
    int code;
    std::string out;
};

static CliResult runCli(
    const std::string &trace,
    const std::string &args = ""
) {
    char path[] = "/tmp/accuchek-test-XXXXXX";
    auto fd = mkstemp(path);
    CHECK(0<=fd);
    CHECK_EQ(write(fd, trace.data(), trace.size()), (ssize_t)trace.size());
    close(fd);

    auto bin = getenv("ACCUCHEK_BIN");
    auto cmd = std::string("env -u ACCUCHEK_DBG ") + (bin ? bin : "./accuchek") +
        " --replay " + path + " " + args + " 2>/dev/null";
    auto pipe = popen(cmd.c_str(), "r");
    CliResult result = {-1, ""};
    char chunk[4096];
    size_t n;
    while(0<(n = fread(chunk, 1, sizeof(chunk), pipe))) {
        result.out.append(chunk, n);
    }
    auto status = pclose(pipe);
    result.code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    unlink(path);
    return result;
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

TEST(cli_fails_on_broken_session) {
    auto trace = replaceLine(sim::sessionTrace(kTwoSegments), kLineFirstSegment, "< !-7");
    auto r = runCli(trace);
    CHECK(0!=r.code);
}
