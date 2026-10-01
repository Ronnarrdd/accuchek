/*

     download samples from a Roche accuchek device using libusb

     usage: accuchek [DEVICE_INDEX] [--config FILE] [--capture TRACE] [--replay TRACE]
            accuchek [--config FILE] --known-devices

     compile with: make

 */

// stuff we need
#include <log.h>
#include <trace.h>
#include <session.h>
#include <protocol.h>
#include <string>
#include <vector>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <libusb-1.0/libusb.h>

using namespace accuchek;

// globals
static Config g_config = defaultConfig();
static FILE *g_output = 0;
static auto g_lineCount = 0;
static auto g_firstLine = true;

// add a config file to the built-in device list
static void loadConfig(
    const char *path
) {
    std::string text;
    if(false==readFile(path, text)) {
        fprintf(stderr, "accuchek: cannot read config file %s\n", path);
        exit(1);
    }
    g_config = configWithFile(text);
}

// a usb device (only things about the device we actually need)
struct USBDevice {

    // data
    libusb_device *dev;
    uint16_t vendorId;
    uint16_t productId;
    std::string vendor;
    std::string product;
    uint8_t sndEndPoint;
    uint8_t rcvEndPoint;
    uint8_t configValue;
    uint8_t interfaceNumber;
    uint8_t alternateSetting;
    libusb_device_handle *devHandle;

    // constructor
    USBDevice(
        libusb_device *_dev,
        uint16_t _vendorId,
        uint16_t _productId,
        const char *_vendor,
        const char *_product,
        uint8_t _sndEndPoint,   // NB: used to write _to_ device _from_ host
        uint8_t _rcvEndPoint,   // NB: used to read _from_ device _to_ host
        const libusb_config_descriptor *cfg,
        const libusb_interface_descriptor *altSetting
    )
        :   dev(_dev),
            vendorId(_vendorId),
            productId(_productId),
            vendor(_vendor),
            product(_product),
            sndEndPoint(_sndEndPoint),
            rcvEndPoint(_rcvEndPoint),
            configValue(cfg->bConfigurationValue),
            interfaceNumber(altSetting->bInterfaceNumber),
            alternateSetting(altSetting->bAlternateSetting),
            devHandle(0)
    {
        // increase refcount on libusb device handle
        libusb_ref_device(dev);
    }

    // copy constructor
    USBDevice(
        const USBDevice &rhs
    )
        :   dev(rhs.dev),
            vendorId(rhs.vendorId),
            productId(rhs.productId),
            vendor(rhs.vendor),
            product(rhs.product),
            sndEndPoint(rhs.sndEndPoint),
            rcvEndPoint(rhs.rcvEndPoint),
            configValue(rhs.configValue),
            interfaceNumber(rhs.interfaceNumber),
            alternateSetting(rhs.alternateSetting),
            devHandle(rhs.devHandle)
    {
        // increase refcount on libusb device handle
        libusb_ref_device(dev);
    }

    // destructor
    ~USBDevice() {
        // decrease refcount on libusb device handle
        libusb_unref_device(dev);
    }

    // show device specs
    void show(
        const char *msg
    ) {
        LOG_NFO(
            "%s:\n"
            "\n"
            "    bus number:    %d\n"
            "    dev address:   %d\n"
            "    cfg value:     %d\n"
            "    alt setting:   %d\n"
            "    alt interface number: %d\n"
            "    vendor:        (0x%04x) %s\n"
            "    product:       (0x%04x) %s\n"
            "    sndEndPnt:     %d\n"
            "    rcvEndPnt:     %d\n"
            ,
            msg,
            libusb_get_bus_number(dev),
            libusb_get_device_address(dev),
            (int)configValue,
            (int)alternateSetting,
            (int)interfaceNumber,
            (int)vendorId,
            vendor.c_str(),
            (int)productId,
            product.c_str(),
            sndEndPoint,
            rcvEndPoint
        );
    }
};

// transport over an opened and claimed libusb device
struct LibusbTransport : Transport {

    explicit LibusbTransport(
        const USBDevice &_usbDevice
    )
        :   usbDevice(_usbDevice)
    {
    }

    int controlStatus(
        uint8_t *buffer,
        size_t len
    ) override {
        return libusb_control_transfer(
            usbDevice.devHandle,
            (
                LIBUSB_REQUEST_TYPE_STANDARD |
                LIBUSB_RECIPIENT_DEVICE      |
                LIBUSB_ENDPOINT_IN
            ),
            LIBUSB_REQUEST_GET_STATUS,
            0,
            0,
            buffer,
            len,
            kTimeoutMs
        );
    }

    int bulkOut(
        const uint8_t *buffer,
        size_t len
    ) override {
        int bytesWritten = -1;
        auto fail = libusb_bulk_transfer(
            usbDevice.devHandle,
            usbDevice.sndEndPoint,
            const_cast<uint8_t *>(buffer),
            len,
            &bytesWritten,
            kTimeoutMs
        );
        return (0!=fail ? fail : bytesWritten);
    }

    int bulkIn(
        uint8_t *buffer,
        size_t maxLen
    ) override {
        int bytesRead = 0;
        auto fail = libusb_bulk_transfer(
            usbDevice.devHandle,
            usbDevice.rcvEndPoint,
            buffer,
            maxLen,
            &bytesRead,
            kTimeoutMs
        );
        return (0!=fail ? fail : bytesRead);
    }

    const char *errorName(
        int code
    ) override {
        return libusb_strerror(code);
    }

    static constexpr unsigned kTimeoutMs = 5000;
    const USBDevice &usbDevice;
};

/*

    syslog excerpt from an actual accuchek connecting:

        May 26 14:35:34 machine kernel: [598051.987118] usb 3-1: new full-speed USB device number 59 using xhci_hcd
        May 26 14:35:34 machine kernel: [598052.141001] usb 3-1: New USB device found, idVendor=173a, idProduct=21d5, bcdDevice= 1.00
        May 26 14:35:34 machine kernel: [598052.141005] usb 3-1: New USB device strings: Mfr=1, Product=2, SerialNumber=0
        May 26 14:35:34 machine kernel: [598052.141007] usb 3-1: Product: ACCU-CHEK Guide
        May 26 14:35:34 machine kernel: [598052.141008] usb 3-1: Manufacturer: Roche

    lsusb -v excerpt:

        Bus 003Device 118: ID 173a:21d5 Roche
          idVendor           0x173a Roche
          idProduct          0x21d5
          bNumConfigurations      1
          Configuration Descriptor:
            bNumInterfaces          1
            bConfigurationValue     1
            Interface Descriptor:
              bInterfaceNumber        0
              bAlternateSetting       0
              bNumEndpoints           2
              bInterfaceClass        15
              iInterface              4 Personal Healthcare Device Class
              Endpoint Descriptor:
                bEndpointAddress     0x81  EP 1 IN
                bmAttributes            2
                  Transfer Type            Bulk
                wMaxPacketSize     0x0040  1x 64 bytes
              Endpoint Descriptor:
                bEndpointAddress     0x01  EP 1 OUT
                bmAttributes            2
                  Transfer Type            Bulk
                wMaxPacketSize     0x0040  1x 64 bytes

    endpoints:

        bEndpointAddress     0x81  EP 1 IN      (device to host)
        bEndpointAddress     0x01  EP 1 OUT     (host to device)
        rcvEndPnt:     129
        sndEndPnt:     1

*/

// write one sample as JSON
static void writeSample(
    const Sample &s
) {
    fprintf(
        g_output,
        "%s\n    %s",
        (g_firstLine ? "" : ","),
        sampleJson(s, g_lineCount++).c_str()
    );
    g_firstLine = false;
}

// run the protocol, exit(1) on failure
static void runSession(
    Transport &transport
) {
    try {
        downloadSamples(transport, writeSample);
    } catch(const SessionError &) {
        exit(1);
    }
}

// open an accuchek USB device and download data from it
static void operateDevice(
    USBDevice &usbDevice,
    const char *capturePath
) {
    // open device
    auto dev = usbDevice.dev;
    libusb_device_handle *devHandle = 0;
    auto fail0 = libusb_open(dev, &devHandle);
    if(fail0) {
        LOG_WRN("libusb_open failed on selected device -- giving up");
        exit(1);
    }
    usbDevice.devHandle = devHandle;

    // detach whatever kernel driver may have been attached to it
    libusb_detach_kernel_driver(
        devHandle,
        usbDevice.interfaceNumber
    );

    // load the configuration chosen during detection phase
    if(libusb_set_configuration(devHandle, usbDevice.configValue)<0) {
        LOG_WRN("failed to configure selected device -- giving up");
        exit(1);
    }

    // claim interface
    if(libusb_claim_interface(devHandle, usbDevice.interfaceNumber)<0) {
        LOG_WRN("failed to claim interface -- giving up");
        exit(1);
    }

    // set alt setting chosen during detection phase on interface
    if(libusb_set_interface_alt_setting(devHandle, usbDevice.interfaceNumber, usbDevice.alternateSetting)<0) {
        LOG_WRN("failed to set alt setting -- giving up");
        exit(1);
    }

    // make some noise
    LOG_NFO("using device snd endpoint = %d", usbDevice.sndEndPoint);
    LOG_NFO("using device rcv endpoint = %d\n", usbDevice.rcvEndPoint);

    LibusbTransport usb(usbDevice);
    if(0!=capturePath) {
        auto fp = fopen(capturePath, "w");
        if(0==fp) {
            LOG_WRN("cannot write trace %s -- giving up", capturePath);
            exit(1);
        }
        RecordingTransport recording(usb, fp);
        runSession(recording);
        fclose(fp);
    } else {
        runSession(usb);
    }

    // protocol step: close device
    LOG_NFO("closing usb device");
    libusb_close(devHandle);
}

// process one USB device and add it to the list if it matches requirements
static void addDeviceIfAccuChek(
    std::vector<USBDevice> &validDevices,
    libusb_device *dev
) {

    // get USB device description
    libusb_device_descriptor dsc;
    auto fail = libusb_get_device_descriptor(dev, &dsc);
    if(0!=fail) {
        LOG_WRN("libusb_get_device_descriptor failed");
        return;
    }

    // ugly trick to "goto done" over declarations using a break
    struct libusb_config_descriptor *cfg = 0;
    do {

        // accuchek has one config, anything with more or less is a bust
        if(1!=dsc.bNumConfigurations) {
            LOG_NFO("not a match, too many configs to be an accuchek");
            break;
        }

        // load first config
        auto confIndex = 0;
        auto fail0 = libusb_get_config_descriptor(dev, confIndex, &cfg);
        if(0!=fail0) {
            LOG_WRN("libusb_get_device_descriptor failed");
            break;
        }

        // accuchek single config has one interface, anything with more or less is a bust
        if(1!=cfg->bNumInterfaces) {
            LOG_NFO("not a match, too many interfaces to be an accuchek");
            break;
        }

        // look at all "alt settings" for first interface
        auto interfaceIndex = 0;
        auto interface = &(cfg->interface[interfaceIndex]);

        // accuchek single config has one alt setting, anything with more or less is a bust
        if(1!=interface->num_altsetting) {
            LOG_NFO("not a match, too many alt settings to be an accuchek");
            break;
        }

        // accuchek has two endpoints, anything with more or less is a bust
        int altSettingIndex = 0;
        auto altSetting = &(interface->altsetting[altSettingIndex]);
        if(2!=altSetting->bNumEndpoints) {
            LOG_NFO("not a match, too many endpoints to be an accuchek");
            break;
        }

        // look for endpoints on altsetting that match our criteria
        auto in = 0;    // 0 is an invalid endpoint
        auto out = 0;   // 0 is an invalid endpoint
        for(auto endPointIndex=0; endPointIndex<altSetting->bNumEndpoints; ++endPointIndex) {
            // accuchek endpoints should both have a max packet size of 64
            auto endPoint = &(altSetting->endpoint[endPointIndex]);
            if(64==endPoint->wMaxPacketSize) {
                // device must be non-interrupt type
                auto attributes = (endPoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK);
                auto isInterrupt = (LIBUSB_TRANSFER_TYPE_INTERRUPT==attributes);
                if(false==isInterrupt) {
                    // LIBUSB_ENDPOINT_IN means endpoint is used to transfer date from the device to the host
                    auto isInput = (LIBUSB_ENDPOINT_IN & endPoint->bEndpointAddress);
                    if(isInput) {
                        in = endPoint->bEndpointAddress;
                    } else {
                        out = endPoint->bEndpointAddress;
                    }
                }
            }
        }

        // skip devices that dont have at least one input and one output
        auto ok = (0!=in && 0!=out);
        if(false==ok) {
            LOG_NFO("not a match, need at least one input endpoint and one output endpoint");
            break;
        }

        // we found a device seems to fit the bill, open it
        LOG_NFO("found a usb device that looks good, checking further by opening it");
        libusb_device_handle *devHandle = 0;
        auto fail1 = libusb_open(dev, &devHandle);
        if(fail1) {
            LOG_WRN("libusb_open failed, giving up");
            break;
        }

        // get device vendor
        char vendor[512];
        memset(vendor, 0, sizeof(vendor));
        auto r0 = libusb_get_string_descriptor_ascii(
            devHandle,
            dsc.iManufacturer,
            (uint8_t*)vendor,
            (-1+sizeof(vendor))
        );
        if(r0<0) {
            LOG_NFO("not a match, vendorId unreadable");
            libusb_close(devHandle);
            break;
        }

        // get product id
        char product[512];
        memset(product, 0, sizeof(product));
        auto r1 = libusb_get_string_descriptor_ascii(
            devHandle,
            dsc.iProduct,
            (uint8_t*)product,
            (-1+sizeof(product))
        );
        if(r1<0) {
            LOG_NFO("not a match, productId unreadable");
            libusb_close(devHandle);
            break;
        }

        // check that device and vendor is in list of known devices
        if(isDeviceAllowed(g_config, dsc.idVendor, dsc.idProduct)) {
            // we have a new valid device, add it to the list
            LOG_NFO("========> found a matching USB device");
            validDevices.emplace_back(
                dev,
                dsc.idVendor,
                dsc.idProduct,
                vendor,
                product,
                out,
                in,
                cfg,
                altSetting
            );
        } else {
            LOG_NFO(
                "nope: looks like it, but thats not the one. this device has mfgr=%s device=%s\n",
                vendor,
                product
            );
        }
        libusb_close(devHandle);
    } while(0);

    // free config data structure
    libusb_free_config_descriptor(cfg);
}

// find all possible accuchek devices, pick one and download data from it
static void findAndOperateAccuChek(
    libusb_context *libUSBContext,
    int ix,
    const char *capturePath
) {

    // obtain a list of all USB devices in the system
    libusb_device **devices = 0;
    LOG_NFO("getting list of all USB devices in system from libusb");
    auto count = libusb_get_device_list(libUSBContext, &devices);
    LOG_NFO("found %d USB devices in system", (int)count);

    // check them one by one and add to the list if specs are a match
    std::vector<USBDevice> validDevices;
    LOG_NFO("searching for valid accuchek devices");
    for(int i=0; i<count; ++i) {
        LOG_NFO("checking if device %d is an accuchek", (int)i);
        addDeviceIfAccuChek(validDevices, devices[i]);
    }

    // clean up device list
    libusb_free_device_list(devices, 1);

    // if no devices found, bail
    if(0==validDevices.size()) {
        LOG_WRN("found no accuchek device whatsoever -- giving up");
        exit(1);
    }

    // make some noise
    LOG_NFO(
        "found altogether %d accuchek devices",
        (int)validDevices.size()
    );

    // make sure we user referes to a valid device
    if(int(validDevices.size())<=int(ix)) {
        LOG_WRN(
            "user selected device %d but only %d devices were found -- aborting",
            ix,
            (int)validDevices.size()
        );
        exit(1);
    }

    // select a specific device (first seen or as specified by user)
    auto selectedIndex = std::max(0, ix);
    auto &selectedDevice = validDevices[selectedIndex];

    // show details of selected device as gathered from libusb
    char buf[256];
    snprintf(
        buf,
        sizeof(buf),
        "selecting accuchek device #%d:",
        (int)selectedIndex
    );
    selectedDevice.show(buf);

    // talk to device to download data from it
    operateDevice(selectedDevice, capturePath);
}

// open libusb, return handle
static libusb_context *openLibUSB() {

    LOG_NFO("opening libusb");

    // init libusb
    libusb_context *libUSBContext = 0;
    auto fail = libusb_init(&libUSBContext);
    if(0!=fail || 0==libUSBContext) {
        LOG_WRN("libusb init failure");
        exit (1);
    }

    LOG_NFO("libusb opened OK");
    return libUSBContext;
}

// close libusb
static void closeLibUSB(
    libusb_context *libUSBContext
) {
    LOG_NFO("closing libusb");
    libusb_exit(libUSBContext);
}

// replay a recorded trace instead of talking to a device
static void replayTrace(
    const char *path
) {
    std::string text;
    if(false==readFile(path, text)) {
        LOG_WRN("cannot read trace %s -- giving up", path);
        exit(1);
    }
    try {
        ReplayTransport replay(text);
        runSession(replay);
    } catch(const std::runtime_error &e) {
        LOG_WRN("bad trace %s: %s -- giving up", path, e.what());
        exit(1);
    }
}

// entry point
int main(
    int argc,
    char *argv[]
) {

    // parse command line
    int deviceIndex = -1;
    const char *capturePath = 0;
    const char *replayPath = 0;
    const char *configPath = 0;
    bool listDevices = false;
    for(int i=1; i<argc; ++i) {
        if(0==strcmp(argv[i], "--config") && i+1<argc) {
            configPath = argv[++i];
        } else if(0==strcmp(argv[i], "--known-devices")) {
            listDevices = true;
        } else if(0==strcmp(argv[i], "--capture") && i+1<argc) {
            capturePath = argv[++i];
        } else if(0==strcmp(argv[i], "--replay") && i+1<argc) {
            replayPath = argv[++i];
        } else {
            deviceIndex = atoi(argv[i]);
        }
    }

    if(0!=configPath) {
        loadConfig(configPath);
    }
    if(listDevices) {
        for(const auto &device : allowedDevices(g_config)) {
            printf("%s\n", device.c_str());
        }
        return 0;
    }

    // must be root
    if(0==replayPath) {
        auto euid = geteuid();
        LOG_FTL(0!=euid, "must be root, euid is %d, bailing", euid);
    }

    // be silent unless asked to talk
    if(0!=getenv("ACCUCHEK_DBG")) {
        // unbuffer stdout/stderr
        setvbuf(stdout, 0, _IONBF, 0);
        setvbuf(stderr, 0, _IONBF, 0);
        g_output = stdout;
    } else {

        // dup stdout
        int newFD = dup(1);

        // batten down the hatches
        close(1);
        close(2);

        // fdopen dup'd stdout
        g_output = fdopen(newFD, "wb");
        fprintf(g_output, "[");
    }

    // make some noise
    LOG_NFO("starting");

    if(0!=replayPath) {
        replayTrace(replayPath);
    } else {
        // open libusb
        auto libUSBContext = openLibUSB();

        // find and talk to one accuchek device
        findAndOperateAccuChek(libUSBContext, deviceIndex, capturePath);

        closeLibUSB(libUSBContext);
    }

    // clean up and bail
    fprintf(g_output, "\n]\n");
    LOG_NFO("done");
    return 0;
}
