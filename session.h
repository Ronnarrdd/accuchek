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

    struct SessionError : std::runtime_error {
        explicit SessionError(const std::string &msg) : std::runtime_error(msg) {}
    };

    // run the whole protocol, call onSample for every sample worth reporting
    void downloadSamples(
        Transport &transport,
        const std::function<void(const Sample &)> &onSample
    );

    // canonical hexdump of a buffer with header (debug output)
    void hexDumpWithHeader(const char *bufferName, const uint8_t *buffer, uint32_t size);

    } // namespace accuchek

#endif // __SESSION_H__
