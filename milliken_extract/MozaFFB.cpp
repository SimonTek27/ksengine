#include <algorithm>
#include <string>
#include <cstdio>
#include "MozaFFB.h"

// ============================================================================
// Platform-specific HID headers
// ============================================================================
#ifdef _WIN32
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>

// GUID_DEVCLASS_HID - {745a17a0-74d3-11d0-b6fe-00a0c90f57da}
static const GUID GUID_DEVCLASS_HID =
    {0x745a17a0, 0x74d3, 0x11d0, {0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda}};

#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#endif

namespace ks::device {

// ============================================================================
// Known MOZA wheel PIDs and their max torque
// ============================================================================
const std::vector<MozaFFB::DeviceInfo>& MozaFFB::knownDevices() {
    static const std::vector<DeviceInfo> devices = {
        {0x0001, WheelModel::R5,      "MOZA R5",       5.0f},
        {0x0002, WheelModel::R9,      "MOZA R9",       9.0f},
        {0x0003, WheelModel::R12,     "MOZA R12",     12.0f},
        {0x0004, WheelModel::R16,     "MOZA R16",     16.0f},
        {0x0005, WheelModel::R21,     "MOZA R21",     21.0f},
        {0x0006, WheelModel::R21F,    "MOZA R21F",    21.0f},
        {0x0010, WheelModel::MBoat,   "MOZA M Boat",   9.0f},
        {0x0011, WheelModel::MBoatPro,"MOZA M Boat Pro", 9.0f},
    };
    return devices;
}

float MozaFFB::maxTorqueForModel(WheelModel model) {
    for (const auto& dev : knownDevices()) {
        if (dev.model == model) return dev.maxTorque;
    }
    return 10.0f; // conservative default
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

MozaFFB::MozaFFB() = default;

MozaFFB::~MozaFFB() {
    shutdown();
}

// ============================================================================
// Initialization — enumerate and open MOZA HID device
// ============================================================================

bool MozaFFB::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    /* log */;

    if (!enumerateDevice()) {
        std::fprintf(stderr, "FFB warning
");
        return false;
    }

    m_connected = true;
    /* log */;
    return true;
}

// ============================================================================
// Device enumeration via Windows HID API
// ============================================================================

bool MozaFFB::enumerateDevice() {
#ifdef _WIN32
    HDEVINFO devInfo = SetupDiGetClassDevs(
        &GUID_DEVCLASS_HID,
        nullptr, nullptr,
        DIGCF_PRESENT
    );
    if (devInfo == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "FFB warning
");
        return false;
    }

    SP_DEVICE_INTERFACE_DATA interfaceData;
    interfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr,
            &GUID_DEVCLASS_HID, i, &interfaceData); ++i) {

        DWORD requiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(devInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);

        auto* detailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(
            new char[requiredSize]
        );
        detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(devInfo, &interfaceData,
                detailData, requiredSize, nullptr, nullptr)) {
            delete[] reinterpret_cast<char*>(detailData);
            continue;
        }

        // Open the HID device
        HANDLE hidHandle = CreateFile(
            detailData->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr
        );
        delete[] reinterpret_cast<char*>(detailData);

        if (hidHandle == INVALID_HANDLE_VALUE) continue;

        // Get vendor ID from HID attributes
        HIDD_ATTRIBUTES attributes;
        attributes.Size = sizeof(HIDD_ATTRIBUTES);
        if (HidD_GetAttributes(hidHandle, &attributes)) {
            if (attributes.VendorID == MOZA_VID) {
                // Match against known MOZA PIDs
                for (const auto& dev : knownDevices()) {
                    if (attributes.ProductID == dev.pid) {
                        m_productId = dev.pid;
                        m_model = dev.model;
                        m_hidDevice = hidHandle;
                        /* log */;
                        SetupDiDestroyDeviceInfoList(devInfo);
                        return true;
                    }
                }
            }
        }

        CloseHandle(hidHandle);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return false;

#else
    /* log */;
    return false;
#endif
}

// ============================================================================
// FFB command — MOZA USB HID protocol
// ============================================================================
// MOZA wheels accept FFB via HID output reports.
// The protocol uses 64-byte reports with a specific header format.
// Report ID 0x01 = FFB constant force command.
// ============================================================================

bool MozaFFB::sendFFBCommand(const uint8_t* data, size_t len) {
#ifdef _WIN32
    if (!m_hidDevice) return false;

    // MOZA HID output report format:
    // Byte 0: Report ID (always 0x01 for FFB)
    // Byte 1: Command type
    // Byte 2-5: Force value (int32, little-endian, in 0.01 Nm units)
    // Byte 6-63: Padding

    DWORD bytesWritten = 0;
    BOOL result = WriteFile(
        m_hidDevice,
        data,
        static_cast<DWORD>(len),
        &bytesWritten,
        nullptr
    );

    return result != FALSE;
#else
    return false;
#endif
}

// ============================================================================
// Update FFB — map torque to MOZA protocol
// ============================================================================

void MozaFFB::updateFFB(float torqueNm) {
    if (!m_connected) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastTorque = torqueNm;

    // Clamp to wheel's max torque
    float maxNm = maxTorqueNm();
    float clampedTorque = std::clamp(-maxNm, torqueNm, maxNm);

    // Convert to protocol format: 0.01 Nm units, int32 little-endian
    int32_t forceUnits = static_cast<int32_t>(clampedTorque * 100.0f);

    uint8_t report[64] = {};
    report[0] = 0x01;  // Report ID
    report[1] = 0x01;  // Constant force command

    // Force value in little-endian int32
    report[2] = static_cast<uint8_t>(forceUnits & 0xFF);
    report[3] = static_cast<uint8_t>((forceUnits >> 8) & 0xFF);
    report[4] = static_cast<uint8_t>((forceUnits >> 16) & 0xFF);
    report[5] = static_cast<uint8_t>((forceUnits >> 24) & 0xFF);

    sendFFBCommand(report, sizeof(report));
}

void MozaFFB::setConstantForce(float magnitude) {
    if (!m_connected) return;
    float torqueNm = magnitude * maxTorqueNm();
    updateFFB(torqueNm);
}

void MozaFFB::setSpringForce(float center, float stiffness, float damping) {
    if (!m_connected) return;
    (void)center;
    (void)stiffness;
    (void)damping;

    // MOZA spring effect via HID report
    uint8_t report[64] = {};
    report[0] = 0x01;
    report[1] = 0x02;  // Spring effect command
    report[2] = static_cast<uint8_t>(std::clamp(0.0f, center, 1.0f) * 255);
    report[3] = static_cast<uint8_t>(std::clamp(0.0f, stiffness, 1.0f) * 255);
    report[4] = static_cast<uint8_t>(std::clamp(0.0f, damping, 1.0f) * 255);
    sendFFBCommand(report, sizeof(report));
}

void MozaFFB::setDamperForce(float velocity, float coefficient) {
    if (!m_connected) return;
    (void)velocity;

    uint8_t report[64] = {};
    report[0] = 0x01;
    report[1] = 0x03;  // Damper effect command
    report[2] = static_cast<uint8_t>(std::clamp(0.0f, coefficient, 1.0f) * 255);
    sendFFBCommand(report, sizeof(report));
}

void MozaFFB::setFrictionForce(float coefficient) {
    if (!m_connected) return;

    uint8_t report[64] = {};
    report[0] = 0x01;
    report[1] = 0x04;  // Friction effect command
    report[2] = static_cast<uint8_t>(std::clamp(0.0f, coefficient, 1.0f) * 255);
    sendFFBCommand(report, sizeof(report));
}

void MozaFFB::setRumble(float strongMotor, float weakMotor) {
    if (!m_connected) return;
    (void)strongMotor;
    (void)weakMotor;
    // MOZA direct drive wheels have no rumble motors — FFB only
}

// ============================================================================
// Input processing
// ============================================================================

void MozaFFB::processHIDInput() {
#ifdef _WIN32
    if (!m_hidDevice) return;

    uint8_t inputReport[64] = {};
    DWORD bytesRead = 0;

    // Non-blocking read
    BOOL result = ReadFile(
        m_hidDevice,
        inputReport,
        sizeof(inputReport),
        &bytesRead,
        nullptr
    );

    if (result && bytesRead > 0) {
        // Parse MOZA input report (button states, encoder position, etc.)
        // Byte 0: Report ID
        // Byte 1: Button state bits
        // Byte 2-3: Encoder position (14-bit)
        // Byte 4-5: Pedal values (optional)
    }
#endif
}

// ============================================================================
// Shutdown
// ============================================================================

void MozaFFB::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);

#ifdef _WIN32
    // Send zero force before disconnecting
    if (m_connected) {
        updateFFB(0.0f);
    }

    if (m_hidDevice) {
        CloseHandle(static_cast<HANDLE>(m_hidDevice));
        m_hidDevice = nullptr;
    }
#endif

    m_connected = false;
    m_model = WheelModel::Unknown;
    m_productId = 0;
}

// ============================================================================
// Model info
// ============================================================================

std::string MozaFFB::modelName() const {
    for (const auto& dev : knownDevices()) {
        if (dev.pid == m_productId) return std::string(dev.name);
    }
    return "MOZA (Unknown)";
}

float MozaFFB::maxTorqueNm() const {
    return maxTorqueForModel(m_model);
}

} // namespace ks::device
