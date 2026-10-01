#include "check.h"
#include "sim.h"
#include <protocol.h>
#include <trace.h>

using namespace accuchek;

static Segment segmentOf(const sim::Bytes &packet) {
    Segment segment;
    std::string error;
    CHECK(parseSegment(packet.data(), packet.size(), segment, error));
    CHECK_EQ(error, std::string(""));
    return segment;
}

template<typename F>
static std::string builtHex(F build) {
    auto b = sim::sentWith(build);
    return toHex(b.data(), b.size());
}

// expected bytes typed from the field lists of the original upstream main.cpp

TEST(association_response_bytes) {
    CHECK_EQ(
        builtHex([](uint8_t *b) { return buildAssociationResponse(b); }),
        std::string("E300002C0003507900268000000280008000000000000000800000000008123456780000000000000000000000000000")
    );
}

TEST(config_received_bytes) {
    CHECK_EQ(
        builtHex([](uint8_t *b) { return buildConfigReceived(b, 0x0010); }),
        std::string("E7000016001400100201000E0000000000000D1C000440000000")
    );
}

TEST(mds_request_uses_next_invoke_id) {
    CHECK_EQ(
        builtHex([](uint8_t *b) { return buildMdsRequest(b, 0x0010); }),
        std::string("E700000E000C001101030006000000000000")
    );
}

TEST(segment_info_request_bytes) {
    CHECK_EQ(
        builtHex([](uint8_t *b) { return buildSegmentInfoRequest(b, 0x0011, 0x0100); }),
        std::string("E700001400120012" "0107000C01000C0D" "0006000100020000")
    );
}

TEST(trigger_transfer_bytes) {
    CHECK_EQ(
        builtHex([](uint8_t *b) { return buildTriggerTransfer(b, 0x0012, 0x0100); }),
        std::string("E7000010000E0013" "0107000801000C1C" "00020000")
    );
}

TEST(segment_ack_echoes_segment_fields) {
    CHECK_EQ(
        builtHex([](uint8_t *b) { return buildSegmentAck(b, 0x0020, 0x0100, 1, 2, 3); }),
        std::string("E700001E001C0020" "020100160100FFFF" "FFFF0D21000C0000" "00010000000200030080")
    );
}

TEST(release_request_bytes) {
    CHECK_EQ(
        builtHex([](uint8_t *b) { return buildReleaseRequest(b); }),
        std::string("E40000020000")
    );
}

TEST(config_info_finds_pm_store_after_other_objects) {
    auto packet = sim::configInfo(0x0010, 0x0123, 4);
    uint16_t handle = 0;
    uint16_t nbSegments = 0;
    CHECK(parseConfigInfo(packet.data(), packet.size(), handle, nbSegments));
    CHECK_EQ(handle, 0x0123);
    CHECK_EQ(nbSegments, 4);
    uint16_t invokeId = 0;
    CHECK(readInvokeId(packet.data(), packet.size(), invokeId));
    CHECK_EQ(invokeId, 0x0010);
}

TEST(config_info_without_pm_store_is_rejected) {
    auto packet = sim::configInfo(0x0010, 0x0123, 4);
    packet[25] = 1;     // only the first object (numeric metric) remains
    uint16_t handle = 0;
    uint16_t nbSegments = 0;
    CHECK(!parseConfigInfo(packet.data(), packet.size(), handle, nbSegments));
}

TEST(bcd_decoding) {
    CHECK_EQ(decodeBcd(0x00), 0);
    CHECK_EQ(decodeBcd(0x09), 9);
    CHECK_EQ(decodeBcd(0x21), 21);
    CHECK_EQ(decodeBcd(0x59), 59);
}

TEST(segment_samples_and_flags) {
    auto packet = sim::dataSegment(0x0020, 0x0100, 7, {
        {2021, 3, 29, 11, 12, 133, 0},
        {2026, 12, 31, 23, 59, 409, 0x0001},
    }, true, true);
    auto segment = segmentOf(packet);
    CHECK(segment.last);
    CHECK_EQ(segment.u0, 0x00000000u);
    CHECK_EQ(segment.u1, 0x00070000u);
    CHECK_EQ(segment.u2, 2);
    CHECK_EQ(segment.samples.size(), 2u);
    auto &a = segment.samples[0];
    CHECK_EQ(a.year, 2021);
    CHECK_EQ(a.month, 3);
    CHECK_EQ(a.day, 29);
    CHECK_EQ(a.hour, 11);
    CHECK_EQ(a.minute, 12);
    CHECK_EQ(a.value, 133);
    CHECK_EQ(a.status, 0);
    auto &b = segment.samples[1];
    CHECK_EQ(b.year, 2026);
    CHECK_EQ(b.month, 12);
    CHECK_EQ(b.minute, 59);
    CHECK_EQ(b.value, 409);
    CHECK_EQ(b.status, 1);
}

TEST(segment_not_last) {
    auto packet = sim::dataSegment(0x0020, 0x0100, 0, {{2024, 1, 1, 8, 0, 100, 0}}, true, false);
    CHECK(!segmentOf(packet).last);
}

TEST(epoch_in_winter_is_local_time) {
    Sample s = {2021, 1, 15, 8, 0, 133, 0, true};
    CHECK_EQ((long long)sampleEpoch(s), 1610694000LL);
}

// the meter shows 07:36 on 2 July 2026: that is 05:36 UTC in Paris (CEST)
TEST(epoch_in_summer_is_local_time) {
    Sample s = {2026, 7, 2, 7, 36, 120, 0, true};
    CHECK_EQ((long long)sampleEpoch(s), 1782970560LL);
}

TEST(epoch_around_dst_changes) {
    Sample beforeSpring = {2026, 3, 29, 1, 59, 100, 0, true};
    Sample afterSpring = {2026, 3, 29, 3, 0, 100, 0, true};
    Sample beforeAutumn = {2026, 10, 25, 1, 59, 100, 0, true};
    Sample afterAutumn = {2026, 10, 25, 3, 0, 100, 0, true};
    CHECK_EQ((long long)sampleEpoch(beforeSpring), 1774745940LL);
    CHECK_EQ((long long)sampleEpoch(afterSpring), 1774746000LL);
    CHECK_EQ((long long)sampleEpoch(beforeAutumn), 1792886340LL);
    CHECK_EQ((long long)sampleEpoch(afterAutumn), 1792893600LL);

    // 02:30 happens twice on 25 October: either instant is acceptable
    Sample ambiguous = {2026, 10, 25, 2, 30, 100, 0, true};
    auto e = (long long)sampleEpoch(ambiguous);
    CHECK(1792888200LL==e || 1792891800LL==e);
}

TEST(sample_json_format) {
    Sample s = {2021, 1, 15, 8, 0, 133, 0, true};
    CHECK_EQ(
        sampleJson(s, 0),
        std::string("{ \"id\":     0, \"epoch\": 1610694000, \"timestamp\":\"2021/01/15 08:00\", \"mg/dL\":133, \"mmol/L\":  7.388889, \"status\":0 }")
    );
}

TEST(sample_json_high_reading) {
    Sample s = {2021, 1, 15, 8, 0, kValueHigh, 0, true};
    auto json = sampleJson(s, 3);
    CHECK(std::string::npos!=json.find("\"mg/dL\":601"));
    CHECK(std::string::npos!=json.find("\"range\":\"high\""));
}

TEST(sample_json_low_reading) {
    Sample s = {2021, 1, 15, 8, 0, kValueLow, 0x0400, true};
    auto json = sampleJson(s, 3);
    CHECK(std::string::npos!=json.find("\"mg/dL\":  9"));
    CHECK(std::string::npos!=json.find("\"status\":1024, \"range\":\"low\" }"));
}

TEST(sample_json_keeps_flagged_status) {
    Sample s = {2021, 1, 15, 8, 0, 140, 0x0001, true};
    auto json = sampleJson(s, 3);
    CHECK(std::string::npos!=json.find("\"mg/dL\":140"));
    CHECK(std::string::npos!=json.find("\"status\":1 }"));
    CHECK(std::string::npos==json.find("range"));
}

TEST(config_file_parsing) {
    auto config = parseConfig(
        "vendor_0x173a_device_0x21d5 1 # roche accu-Chek model  929\n"
        "vendor_0x173a_device_0x21d7 0 # disabled\n"
        "\n"
        "garbage\n"
    );
    CHECK(isDeviceAllowed(config, 0x173a, 0x21d5));
    CHECK(!isDeviceAllowed(config, 0x173a, 0x21d7));
    CHECK(!isDeviceAllowed(config, 0x173a, 0x21d8));
}

TEST(config_last_line_without_newline) {
    auto config = parseConfig("vendor_0x173a_device_0x21d5 1");
    CHECK(isDeviceAllowed(config, 0x173a, 0x21d5));
}

// accuchek used to read ./config.txt and ignore every meter without it
TEST(known_devices_need_no_config_file) {
    auto config = defaultConfig();
    CHECK(isDeviceAllowed(config, 0x173a, 0x21d5));
    CHECK(isDeviceAllowed(config, 0x173a, 0x21d7));
    CHECK(isDeviceAllowed(config, 0x173a, 0x21d8));
    CHECK(!isDeviceAllowed(config, 0x173a, 0x1234));
    CHECK(!isDeviceAllowed(config, 0x046d, 0x21d5));
}

TEST(config_file_adds_and_disables_devices) {
    auto config = configWithFile(
        "vendor_0x173a_device_0x21d7 0\n"
        "vendor_0x173a_device_0x2222 1\n"
    );
    auto devices = allowedDevices(config);
    CHECK_EQ(devices.size(), 3u);
    CHECK_EQ(devices[0], std::string("173a:21d5"));
    CHECK_EQ(devices[1], std::string("173a:21d8"));
    CHECK_EQ(devices[2], std::string("173a:2222"));
}

TEST(replay_trace_parsing) {
    ReplayTransport t("# comment\nc 0000\n< E2 00\n> !-7\n");
    CHECK_EQ(t.lines.size(), 3u);
    CHECK_EQ(t.lines[1].bytes.size(), 2u);
    CHECK_EQ(t.lines[2].error, -7);
    bool threw = false;
    try {
        ReplayTransport bad("< E2Z0\n");
    } catch(const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);
}
