#include <log.h>
#include <protocol.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

namespace accuchek {

const char *findKeyByValue(
    uint16_t value
) {
    #define x(a, b) if((b)==value) return #a;
        MDC_LIST
    #undef x
    return (const char *)0;
}

// one "key value" pair per line, anything after the value is ignored
Config parseConfig(
    const std::string &text
) {
    Config config;
    size_t start = 0;
    while(start<text.size()) {

        auto nl = text.find('\n', start);
        auto stop = (std::string::npos==nl ? text.size() : (1 + nl));
        auto line = text.data() + start;
        auto end = text.data() + stop;
        start = stop;

        const char *firstSep = 0;
        const char *secondSep = 0;
        const char *secondFirst = 0;
        for(auto p = line; p<end; ++p) {
            auto c = p[0];
            auto validChar = (
                ('0'<=c && c<='9')  ||
                ('A'<=c && c<='Z')  ||
                ('a'<=c && c<='z')  ||
                ('_'==c)
            );
            if(false==validChar) {
                if(0==firstSep) {
                    firstSep = p;
                } else {
                    if(0==secondSep && 0!=secondFirst) {
                        secondSep = p;
                    }
                }
            } else {
                if(0!=firstSep) {
                    if(0==secondFirst) {
                        secondFirst = p;
                    }
                }
            }
        }
        if(0==firstSep || 0==secondFirst || 0==secondSep) {
            continue;
        }
        config[std::string(line, firstSep)] = std::string(secondFirst, secondSep);
    }
    return config;
}

bool isDeviceAllowed(
    const Config &config,
    uint16_t vendorId,
    uint16_t productId
) {
    char key[64];
    snprintf(
        key,
        sizeof(key),
        "vendor_0x%04x_device_0x%04x",
        vendorId,
        productId
    );
    auto it = config.find(key);
    return (config.end()!=it && "1"==it->second);
}

void be16(
    uint8_t *&p,
    uint16_t v
) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = (v >> 0) & 0xFF;
    p += 2;
}

void be32(
    uint8_t *&p,
    uint32_t v
) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] = (v >>  0) & 0xFF;
    p += 4;
}

uint16_t be16r(
    const uint8_t *p,
    size_t &offset
) {
    auto hi = p[0 + offset];
    auto lo = p[1 + offset];
    offset += 2;
    return (((uint16_t)hi)<<8) | lo;
}

uint32_t be32r(
    const uint8_t *p,
    size_t &offset
) {
    uint32_t p0 = p[0 + offset];
    uint32_t p1 = p[1 + offset];
    uint32_t p2 = p[2 + offset];
    uint32_t p3 = p[3 + offset];
    offset += 4;
    return (
        (p0 << 24)  |
        (p1 << 16)  |
        (p2 <<  8)  |
        (p3 <<  0)
    );
}

size_t buildAssociationResponse(
    uint8_t *buffer
) {
    auto p = buffer;
    memset(buffer, 0, kBufferSize);
    be16(p, kAPDU_TYPE_ASSOCIATION_RESPONSE); // msg type
    be16(p,         44);                      // length (p, excludes initial 4 bytes)
    be16(p,     0x0003);                      // accepted-unknown-config
    be16(p,      20601);                      // data-proto-id
    be16(p,         38);                      // data-proto-info length
    be32(p, 0x80000002);                      // protocolVersion
    be16(p,     0x8000);                      // encoding-rules = MDER
    be32(p, 0x80000000);                      // nomenclatureVersion
    be32(p,          0);                      // functionalUnits = normal association
    be32(p, 0x80000000);                      // systemType = sys-type-manager
    be16(p,          8);                      // system-id length
    be32(p, 0x12345678);                      // system-id high
    be32(p, 0x00000000);                      // zero
    be32(p, 0x00000000);                      // zero
    be32(p, 0x00000000);                      // zero
    be16(p,     0x0000);                      // zero
    return (p-buffer);
}

size_t buildConfigReceived(
    uint8_t *buffer,
    uint16_t invokeId
) {
    auto p = buffer;
    memset(buffer, 0, kBufferSize);
    be16(p, kAPDU_TYPE_PRESENTATION_APDU);               // msg type
    be16(p,     22);                                     // length
    be16(p,     20);                                     // octet stringlength
    be16(p, invokeId);                                   // invoke-id read from config
    be16(p, kDATA_ADPU_RESPONSE_CONFIRMED_EVENT_REPORT); //
    be16(p,     14);                                     // length
    be16(p,      0);                                     // obj-handle = 0
    be32(p,      0);                                     // currentTime = 0
    be16(p, kEVENT_TYPE_MDC_NOTI_CONFIG);                // event-type
    be16(p,      4);                                     // length
    be16(p, 0x4000);                                     // config-report-id = extended-config-start
    be16(p,      0);                                     // config-result = accepted-config
    return (p-buffer);
}

size_t buildMdsRequest(
    uint8_t *buffer,
    uint16_t invokeId
) {
    auto p = buffer;
    memset(buffer, 0, kBufferSize);
    be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
    be16(p,     14);                       // length
    be16(p,     12);                       // octet stringlength
    be16(p, (1+invokeId));                 // invoke-id from config
    be16(p, kDATA_ADPU_INVOKE_GET);        //
    be16(p,      6);                       // length
    be16(p,      0);                       // obj-handle = 0
    be32(p,      0);                       // currentTime = 0
    return (p-buffer);
}

size_t buildSegmentInfoRequest(
    uint8_t *buffer,
    uint16_t invokeId,
    uint16_t pmStoreHandle
) {
    auto p = buffer;
    memset(buffer, 0, kBufferSize);
    be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
    be16(p,     20);                       // length
    be16(p,     18);                       // octet stringlength
    be16(p, (1+invokeId));                 // invoke-id from prev answer
    be16(p, kDATA_ADPU_INVOKE_CONFIRMED_ACTION);
    be16(p,     12);                       // length of what follows (could also be zero)
    be16(p, pmStoreHandle);                // store handle
    be16(p, kACTION_TYPE_MDC_ACT_SEG_GET_INFO);
    be16(p,      6);                       // length
    be16(p,      1);                       // all segments
    be16(p,      2);                       // length
    be16(p,      0);                       // something
    return (p-buffer);
}

size_t buildTriggerTransfer(
    uint8_t *buffer,
    uint16_t invokeId,
    uint16_t pmStoreHandle
) {
    auto p = buffer;
    memset(buffer, 0, kBufferSize);
    be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
    be16(p,     16);                       // length
    be16(p,     14);                       // octet stringlength
    be16(p, (1+invokeId));                 // invoke-id from prev answer
    be16(p, kDATA_ADPU_INVOKE_CONFIRMED_ACTION);
    be16(p,      8);                       // length of what follows (could also be zero)
    be16(p, pmStoreHandle);                // store handle
    be16(p, kACTION_TYPE_MDC_ACT_SEG_TRIG_XFER);
    be16(p,      2);                       // length
    be16(p,      0);                       // segment
    return (p-buffer);
}

size_t buildSegmentAck(
    uint8_t *buffer,
    uint16_t invokeId,
    uint16_t pmStoreHandle,
    uint32_t u0,
    uint32_t u1,
    uint16_t u2
) {
    auto p = buffer;
    memset(buffer, 0, kBufferSize);
    be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
    be16(p,     30);                       // length
    be16(p,     28);                       // octet stringlength
    be16(p, invokeId);                     // invoke-id from prev answer
    be16(p, kDATA_ADPU_RESPONSE_CONFIRMED_EVENT_REPORT);
    be16(p,     22);                       // length of what follows (could also be zero)
    be16(p, pmStoreHandle);                // store handle
    be32(p, 0xFFFFFFFF);                   // relative time
    be16(p, kEVENT_TYPE_MDC_NOTI_SEGMENT_DATA);
    be16(p,     12);
    be32(p,     u0);
    be32(p,     u1);
    be16(p,     u2);
    be16(p, 0x0080);
    return (p-buffer);
}

size_t buildReleaseRequest(
    uint8_t *buffer
) {
    auto p = buffer;
    memset(buffer, 0, kBufferSize);
    be16(p, kAPDU_TYPE_ASSOCIATION_RELEASE_REQUEST); // msg type
    be16(p,      2);                       // length = 2
    be16(p, 0x0000);                       // normal release
    return (p-buffer);
}

uint16_t readInvokeId(
    const uint8_t *buffer
) {
    size_t offset = 6;
    return be16r(buffer, offset);
}

// find object of a given "class" in a config info message
static const uint8_t *findObject(
    const uint8_t *buffer,
    uint16_t objRequestedClass,
    uint16_t &objHandle,
    uint16_t &objAttrCount
) {
    auto offset = size_t(24);
    auto count = be16r(buffer, offset);
    be16r(buffer, offset);
    LOG_NFO("got %d object in config info response", (int)count);
    for(int i=0; i<count; ++i) {
        auto objClass = be16r(buffer, offset);
        auto handle = be16r(buffer, offset);
        auto attrCount = be16r(buffer, offset);
        auto objSize = be16r(buffer, offset);
        if(objRequestedClass==objClass) {
            objHandle = handle;
            objAttrCount = attrCount;
            return (offset + buffer);
        }
        offset += objSize;
    }
    return 0;
}

// find attribute of a given "class" in an object
static const uint8_t *findAttribute(
    const uint8_t *object,
    uint16_t attributeCount,
    uint16_t attrRequestedClass
) {
    LOG_NFO(
        "looking for attribute of class %d among %d attributes",
        (int)attrRequestedClass,
        (int)attributeCount
    );
    auto offset = size_t(0);
    for(int i=0; i<attributeCount; ++i) {
        auto attrClass = be16r(object, offset);
        auto attrSize = be16r(object, offset);
        if(attrRequestedClass==attrClass) {
            return (offset + object);
        }
        offset += attrSize;
    }
    return 0;
}

bool parseConfigInfo(
    const uint8_t *buffer,
    uint16_t &pmStoreHandle,
    uint16_t &nbSegments
) {
    LOG_NFO("parsing config info response");
    uint16_t attrCount = 0;
    auto pmStore = findObject(buffer, kMDC_MOC_VMO_PMSTORE, pmStoreHandle, attrCount);
    if(0==pmStore) {
        LOG_WRN("failed to parse config buffer for pmStore");
        return false;
    }
    LOG_NFO(
        "found pmStore with %d attributes, handle = %d",
        (int)attrCount,
        (int)pmStoreHandle
    );

    auto numSeg = findAttribute(pmStore, attrCount, kMDC_ATTR_NUM_SEG);
    if(0==numSeg) {
        LOG_WRN("failed to parse pmStore for nbSegments");
        return false;
    }
    size_t o = 0;
    nbSegments = be16r(numSeg, o);
    LOG_NFO("data is split into %d segments", (int)nbSegments);
    return true;
}

int decodeBcd(
    uint8_t x
) {
    int v = -1;
    char buf[8];
    sprintf(buf, "%02X", x);
    sscanf(buf, "%d", &v);
    return v;
}

Segment parseSegment(
    const uint8_t *buffer
) {
    Segment segment;
    size_t o = 22;
    segment.u0 = be32r(buffer, o);
    segment.u1 = be32r(buffer, o);
    segment.u2 = be16r(buffer, o);
    segment.last = (0 != (0x40 & buffer[32]));

    o = 30;
    auto nbEntries = be16r(buffer, o);
    LOG_NFO("segment has %d entries", (int)nbEntries);
    o -= 2;

    for(int i=0; i<nbEntries; ++i) {
        Sample s;
        auto cc = decodeBcd(buffer[ 6 + o]);
        auto yy = decodeBcd(buffer[ 7 + o]);
        s.year = (cc*100 + yy);
        s.month = decodeBcd(buffer[ 8 + o]);
        s.day = decodeBcd(buffer[ 9 + o]);
        s.hour = decodeBcd(buffer[10 + o]);
        s.minute = decodeBcd(buffer[11 + o]);

        auto ro = (14 + o);
        s.value = be16r(buffer, ro);
        s.status = be16r(buffer, ro);
        o += 12;

        segment.samples.push_back(s);
    }
    return segment;
}

time_t sampleEpoch(
    const Sample &s
) {
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_min = s.minute;
    t.tm_hour = s.hour;
    t.tm_mday = s.day;
    t.tm_mon = (s.month-1);
    t.tm_year = (s.year - 1900);
    t.tm_isdst = -1;    // let the timezone rules decide, 0 would mean "winter time" all year
    return mktime(&t);
}

std::string sampleJson(
    const Sample &s,
    int id
) {
    char buf[256];
    snprintf(
        buf,
        sizeof(buf),
        "{ \"id\":%6d, \"epoch\":%11" PRIu64 ", \"timestamp\":\"%04d/%02d/%02d %02d:%02d\", \"mg/dL\":%3d, \"mmol/L\":%10.6f }",
        id,
        (uint64_t)sampleEpoch(s),
        s.year,
        s.month,
        s.day,
        s.hour,
        s.minute,
        (int)s.value,
        (s.value / 18.0)
    );
    return buf;
}

} // namespace accuchek
