#include <log.h>
#include <session.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

extern bool gQuiet;

namespace accuchek {

// canonical hexdump of a buffer
static void hexDump(
    const uint8_t *buffer,
    uint32_t size
) {
    uint32_t i = 0;
    while(i<size) {
        auto e = (16 + i);
        for(auto j=i; j<e; ++j) {
            if(j<size) {
                fprintf(stderr, "%02X ", buffer[j]);
            } else {
                fprintf(stderr, "   ");
            }
        }
        fprintf(stderr, "   ");
        for(auto j=i; j<e; ++j) {
            if(j<size) {
                auto c = buffer[j];
                fputc(isprint(c) ? c : '.', stderr);
            }
        }
        fputc('\n', stderr);
        i = e;
    }
}

void hexDumpWithHeader(
    const char *bufferName,
    const uint8_t *buffer,
    uint32_t size
) {
    if(gQuiet) {
        return;
    }
    LOG_NFO(
        "hexdump of buffer:\n\nBUFFER START \"%s\" size=%d (0x%x) ===============================================",
        bufferName,
        (int)size,
        (int)size
    );
    hexDump(buffer, size);
    fprintf(stderr, "BUFFER END ============================================================================================\n\n");
}

/*
   much of what follows was directly reverse-engineered from the highly
   unportable (only works in effing Chrome) javascript code found here:

       https://github.com/tidepool-org/uploader/tree/master/lib/drivers/roche

   and backported to raw libusb. The original author of the tidepool code
   likely had access to a manual documenting the protocol.
*/
void downloadSamples(
    Transport &transport,
    const std::function<void(const Sample &)> &onSample
) {
    uint8_t buffer[kBufferSize];
    memset(buffer, 0, sizeof(buffer));
    uint16_t invokeId = -1;
    size_t received = 0;    // bytes of the last message received into buffer
    int phaseIndex = 1;

    auto fail = [&](
        ExitCode code,
        const std::string &msg
    ) {
        LOG_WRN("%s -- giving up", msg.c_str());
        throw SessionError(code, msg);
    };

    // send out the message at the start of buffer
    auto send = [&](
        const char *msgName,
        size_t len
    ) {
        LOG_NFO("phase %d: sending message %s", phaseIndex, msgName);
        hexDumpWithHeader(msgName, buffer, len);
        auto written = transport.bulkOut(buffer, len);
        if(written<0 || size_t(written)!=len) {
            fail(
                kExitTransfer,
                std::string("failed to send message ") + msgName + ": " +
                (written<0 ? transport.errorName(written) : "short write")
            );
        }
        LOG_NFO("successfully wrote message %s, size=%d (0x%x):", msgName, (int)len, (int)len);
        ++phaseIndex;
    };

    // receive a message into buffer
    auto receive = [&](
        const char *msgName,
        size_t maxLen = kBufferSize
    ) {
        LOG_NFO("phase %d: receiving message %s", phaseIndex, msgName);
        auto bytesRead = transport.bulkIn(buffer, maxLen);
        if(bytesRead<0) {
            fail(kExitTransfer, std::string("failed to receive message ") + msgName + ": " + transport.errorName(bytesRead));
        }
        LOG_NFO("successfully read message \"%s\" from device", msgName);
        hexDumpWithHeader(msgName, buffer, bytesRead);
        ++phaseIndex;
        received = bytesRead;

        // the device may abort the association instead of answering
        Reader r(buffer, received);
        if(kAPDU_TYPE_ASSOCIATION_ABORT==r.u16() && r.ok) {
            fail(kExitProtocol, std::string("received association abort request instead of ") + msgName);
        }
        return bytesRead;
    };

    auto updateInvokeId = [&]() {
        if(!readInvokeId(buffer, received, invokeId)) {
            fail(kExitProtocol, "message too short to hold an invoke id (" + std::to_string(received) + " bytes)");
        }
        LOG_NFO("invokeId after phase %d is: %d", phaseIndex, (int)invokeId);
    };

    // protocol step: do a control transfer in
    {
        #define PHASE_1 "initial control transfer in"
        LOG_NFO("phase 1: " PHASE_1);
        auto bytesRead = transport.controlStatus(buffer, 2);
        if(bytesRead<0) {
            fail(kExitTransfer, std::string("failed " PHASE_1 ": ") + transport.errorName(bytesRead));
        }
        if(0==bytesRead) {
            fail(kExitTransfer, "failed " PHASE_1 ": no data");
        }
        hexDumpWithHeader(PHASE_1, buffer, bytesRead);
        ++phaseIndex;
    }

    // protocol step: wait for pairing request from the device
    receive("pairing request", 64);

    // protocol step: send a pairing confirmation to the device
    send("pairing confirmation", buildAssociationResponse(buffer));

    // protocol step: wait for config info (as a response to pairing confirm)
    receive("config info");
    updateInvokeId();

    uint16_t pmStoreHandle = 0;
    uint16_t nbSegments = 0;
    if(false==parseConfigInfo(buffer, received, pmStoreHandle, nbSegments)) {
        fail(kExitProtocol, "failed to parse config info");
    }

    // protocol step: send "config well received" response
    send("config received confirmation", buildConfigReceived(buffer, invokeId));

    // protocol step: send MDS attribute request
    send("MDS attribute request", buildMdsRequest(buffer, invokeId));

    // protocol step: read MDS attr answer
    receive("MDS attribute answer");
    updateInvokeId();

    // protocol step: send action request
    send("action request", buildSegmentInfoRequest(buffer, invokeId, pmStoreHandle));

    // protocol step: read action request response
    receive("action request response");
    updateInvokeId();

    // ----> here, the original js code sets the device time ... skip for now

    // protocol step: start request for data segments
    send("request segments", buildTriggerTransfer(buffer, invokeId, pmStoreHandle));

    // step: read segment stream header answer
    auto empty = false;
    {
        auto bytesRead = receive("segment headers");
        updateInvokeId();

        uint16_t dataResponse = 0;
        if(22<=bytesRead) {
            size_t o = 20;
            dataResponse = be16r(buffer, o);
        }
        if(22==bytesRead && kDATA_RESPONSE_EMPTY==dataResponse) {
            LOG_NFO("meter holds no sample");
            empty = true;
        } else if(22==bytesRead && 0!=dataResponse) {
            fail(kExitProtocol, "error retrieving data, code = " + std::to_string(dataResponse));
        }

        uint16_t headerValue = -1;
        if(16<=bytesRead) {
            size_t o = 14;
            headerValue = be16r(buffer, o);
        }
        if(bytesRead<22 || kACTION_TYPE_MDC_ACT_SEG_TRIG_XFER!=headerValue) {
            fail(kExitProtocol, "unexpected / incorrect answer packet");
        }
    }

    // step: read segments one by one
    while(!empty) {
        receive("data segment");
        updateInvokeId();

        Segment segment;
        std::string error;
        if(!parseSegment(buffer, received, segment, error)) {
            fail(kExitProtocol, error);
        }
        for(const auto &s : segment.samples) {
            LOG_NFO(
                "sample: %04d/%02d/%02d %02d:%02d => (mg/dL=%2d, mmol/L=%7.3f, status=0x%02x)",
                s.year,
                s.month,
                s.day,
                s.hour,
                s.minute,
                (int)s.value,
                (s.value / 18.0),
                (int)s.status
            );
            onSample(s);
        }

        // send "data received" ack
        send(
            "data segment received ACK",
            buildSegmentAck(buffer, invokeId, pmStoreHandle, segment.u0, segment.u1, segment.u2)
        );

        // bail if segment was flagged as last one in the stream
        if(segment.last) {
            break;
        }
    }

    // protocol step: disconnect cleanly from device; every segment is acked by
    // now, so a failed release must not throw the samples away
    try {
        send("release request", buildReleaseRequest(buffer));
        receive("release confirmation");
    } catch(const SessionError &e) {
        LOG_WRN("release failed, samples kept: %s", e.what());
    }
}

} // namespace accuchek
