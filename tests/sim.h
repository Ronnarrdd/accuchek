/*

    simulated accuchek device: builds the packets a meter sends, laid out like
    the ones tidepool's driver and our parser read (ISO/IEEE 11073-20601 APDUs),
    and full session traces for ReplayTransport

 */

#ifndef __SIM_H__
    #define __SIM_H__

    #include <protocol.h>
    #include <trace.h>
    #include <string>
    #include <vector>
    #include <stdint.h>

    namespace sim {

    using Bytes = std::vector<uint8_t>;

    struct Writer {
        Bytes b;
        Writer &u8(uint8_t v) { b.push_back(v); return *this; }
        Writer &u16(uint16_t v) { u8(v >> 8); return u8(v & 0xFF); }
        Writer &u32(uint32_t v) { u16(v >> 16); return u16(v & 0xFFFF); }
        void set16(size_t at, uint16_t v) { b[at] = v >> 8; b[at+1] = v & 0xFF; }
    };

    inline uint8_t bcd(int v) {
        return uint8_t(((v / 10) << 4) | (v % 10));
    }

    // presentation APDU header with its three length fields patched by finish()
    inline Writer apdu(uint16_t invokeId, uint16_t choice) {
        Writer w;
        w.u16(accuchek::kAPDU_TYPE_PRESENTATION_APDU).u16(0).u16(0).u16(invokeId).u16(choice).u16(0);
        return w;
    }

    inline Bytes finish(Writer &w) {
        auto n = w.b.size();
        w.set16(2, n - 4);
        w.set16(4, n - 6);
        w.set16(10, n - 12);
        return w.b;
    }

    inline Bytes pairingRequest() {
        Writer w;
        w.u16(accuchek::kAPDU_TYPE_ASSOCIATION_REQUEST).u16(50).u32(0x80000000).u16(1).u16(42);
        while(w.b.size()<54) w.u8(0);
        return w.b;
    }

    inline Bytes configInfo(uint16_t invokeId, uint16_t pmStoreHandle, uint16_t nbSegments) {
        auto w = apdu(invokeId, 0x0101);
        w.u16(0).u32(0xFFFFFFFF).u16(accuchek::kEVENT_TYPE_MDC_NOTI_CONFIG).u16(0);
        w.u16(0x4000);                      // config report id
        w.u16(2).u16(0);                    // object count, length
        // a numeric metric first, so the parser has to skip it
        w.u16(accuchek::kMDC_MOC_VMO_METRIC_NU).u16(1).u16(1).u16(6);
        w.u16(accuchek::kMDC_ATTR_ID_TYPE).u16(2).u16(0x7144);
        // the pm-store holding the samples
        w.u16(accuchek::kMDC_MOC_VMO_PMSTORE).u16(pmStoreHandle).u16(2).u16(12);
        w.u16(accuchek::kMDC_ATTR_ID_HANDLE).u16(2).u16(pmStoreHandle);
        w.u16(accuchek::kMDC_ATTR_NUM_SEG).u16(2).u16(nbSegments);
        auto b = finish(w);
        w.b = b;
        w.set16(20, b.size() - 22);
        w.set16(26, b.size() - 28);
        return w.b;
    }

    inline Bytes mdsAnswer(uint16_t invokeId) {
        auto w = apdu(invokeId, accuchek::kDATA_ADPU_RESPONSE_GET);
        w.u16(0).u16(0).u16(0);
        return finish(w);
    }

    inline Bytes actionResponse(uint16_t invokeId, uint16_t pmStoreHandle) {
        auto w = apdu(invokeId, accuchek::kDATA_ADPU_RESPONSE_CONFIRMED_ACTION);
        w.u16(pmStoreHandle).u16(accuchek::kACTION_TYPE_MDC_ACT_SEG_GET_INFO).u16(0);
        return finish(w);
    }

    inline Bytes segmentHeaders(uint16_t invokeId, uint16_t pmStoreHandle, uint16_t result = 0) {
        auto w = apdu(invokeId, accuchek::kDATA_ADPU_RESPONSE_CONFIRMED_ACTION);
        w.u16(pmStoreHandle).u16(accuchek::kACTION_TYPE_MDC_ACT_SEG_TRIG_XFER).u16(4).u16(0).u16(result);
        return finish(w);
    }

    struct Record {
        int year, month, day, hour, minute;
        uint16_t value;
        uint16_t status;
    };

    inline Bytes dataSegment(
        uint16_t invokeId,
        uint16_t pmStoreHandle,
        uint32_t firstIndex,
        const std::vector<Record> &records,
        bool first,
        bool last
    ) {
        auto w = apdu(invokeId, 0x0101);
        w.u16(pmStoreHandle).u32(0xFFFFFFFF).u16(accuchek::kEVENT_TYPE_MDC_NOTI_SEGMENT_DATA).u16(0);
        w.u16(0).u32(firstIndex).u32(records.size());
        w.u16((first ? 0x8000 : 0) | (last ? 0x4000 : 0));
        w.u16(records.size() * 12);
        for(const auto &r : records) {
            w.u8(bcd(r.year / 100)).u8(bcd(r.year % 100)).u8(bcd(r.month)).u8(bcd(r.day));
            w.u8(bcd(r.hour)).u8(bcd(r.minute)).u8(0).u8(0xFF);
            w.u16(r.value).u16(r.status);
        }
        auto b = finish(w);
        w.b = b;
        w.set16(20, b.size() - 22);
        return w.b;
    }

    inline Bytes releaseResponse() {
        Writer w;
        w.u16(accuchek::kAPDU_TYPE_ASSOCIATION_RELEASE_RESPONSE).u16(2).u16(0);
        return w.b;
    }

    inline std::string line(char kind, const Bytes &b) {
        return std::string(1, kind) + " " + accuchek::toHex(b.data(), b.size()) + "\n";
    }

    // what our side must send, computed with the builders under test
    inline Bytes sent(size_t (*build)(uint8_t *)) {
        uint8_t buf[accuchek::kBufferSize];
        auto n = build(buf);
        return Bytes(buf, buf + n);
    }

    template<typename F>
    inline Bytes sentWith(F build) {
        uint8_t buf[accuchek::kBufferSize];
        auto n = build(buf);
        return Bytes(buf, buf + n);
    }

    // trace of a complete session, one inner vector per data segment
    inline std::string sessionTrace(
        const std::vector<std::vector<Record>> &segments,
        uint16_t pmStoreHandle = 0x0100
    ) {
        std::string t = "# simulated session\nc 0000\n";
        t += line('<', pairingRequest());
        t += line('>', sent(accuchek::buildAssociationResponse));
        t += line('<', configInfo(0x0010, pmStoreHandle, 1));
        t += line('>', sentWith([](uint8_t *b) { return accuchek::buildConfigReceived(b, 0x0010); }));
        t += line('>', sentWith([](uint8_t *b) { return accuchek::buildMdsRequest(b, 0x0010); }));
        t += line('<', mdsAnswer(0x0011));
        t += line('>', sentWith([&](uint8_t *b) { return accuchek::buildSegmentInfoRequest(b, 0x0011, pmStoreHandle); }));
        t += line('<', actionResponse(0x0012, pmStoreHandle));
        t += line('>', sentWith([&](uint8_t *b) { return accuchek::buildTriggerTransfer(b, 0x0012, pmStoreHandle); }));
        t += line('<', segmentHeaders(0x0013, pmStoreHandle));
        uint32_t index = 0;
        for(size_t k=0; k<segments.size(); ++k) {
            uint16_t invokeId = 0x0020 + k;
            auto seg = dataSegment(invokeId, pmStoreHandle, index, segments[k], 0==k, k+1==segments.size());
            index += segments[k].size();
            t += line('<', seg);
            size_t o = 22;
            auto u0 = accuchek::be32r(seg.data(), o);
            auto u1 = accuchek::be32r(seg.data(), o);
            auto u2 = accuchek::be16r(seg.data(), o);
            t += line('>', sentWith([&](uint8_t *b) {
                return accuchek::buildSegmentAck(b, invokeId, pmStoreHandle, u0, u1, u2);
            }));
        }
        t += line('>', sent(accuchek::buildReleaseRequest));
        t += line('<', releaseResponse());
        return t;
    }

    } // namespace sim

#endif // __SIM_H__
