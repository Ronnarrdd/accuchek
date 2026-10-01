/*

    one download session with an accuchek device, over an abstract transport
    (libusb in production, recorded traces in tests)

 */

#ifndef __SESSION_H__
    #define __SESSION_H__

    #include <protocol.h>
    #include <string>
    #include <stdexcept>
    #include <functional>

    namespace accuchek {

    // byte pipe to the device; negative return values are transport error codes
    struct Transport {
        virtual ~Transport() = default;

        // standard GET_STATUS control transfer in, returns bytes read
        virtual int controlStatus(uint8_t *buffer, size_t len) = 0;

        // bulk transfer to the device, returns bytes written
        virtual int bulkOut(const uint8_t *buffer, size_t len) = 0;

        // bulk transfer from the device, returns bytes read
        virtual int bulkIn(uint8_t *buffer, size_t maxLen) = 0;

        virtual const char *errorName(int code) = 0;
    };

    // process exit codes, mirrored by contracts.AccuchekExit on the Glucofi side
    enum ExitCode {
        kExitOk = 0,            // all samples written (none if the meter is empty)
        kExitUsage = 1,         // bad arguments, unreadable config, trace or capture file
        kExitNoDevice = 2,      // no known meter on the USB bus
        kExitAccessDenied = 3,  // meter found but not allowed to open it
        kExitTransfer = 4,      // USB transfer failed: timeout, meter unplugged
        kExitProtocol = 5,      // meter aborted or answered something unexpected
    };

    struct SessionError : std::runtime_error {
        SessionError(ExitCode _code, const std::string &msg) : std::runtime_error(msg), code(_code) {}
        ExitCode code;
    };

    // run the whole protocol, call onSample for every sample, whatever its status;
    // an empty meter is not an error (no sample), neither is a failed release
    void downloadSamples(
        Transport &transport,
        const std::function<void(const Sample &)> &onSample
    );

    // canonical hexdump of a buffer with header (debug output)
    void hexDumpWithHeader(const char *bufferName, const uint8_t *buffer, uint32_t size);

    } // namespace accuchek

#endif // __SESSION_H__
