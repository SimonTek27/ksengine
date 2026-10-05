// Included from NetworkConfig.h inside namespace ks::sim::net
struct CarDamageMessage : public ksnet::Message {
    uint32_t carId = 0;
    float overall = 0.f;
    float engineHealth = 1.f;
    float powerMul = 1.f;
    float handlingMul = 1.f;
    float brakingMul = 1.f;
    float downforceMul = 1.f;
    float dragMul = 1.f;
    float frontWing = 0.f;
    float rearWing = 0.f;
    uint16_t flags = 0;

    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, carId);
        serialize_compressed_float(stream, overall, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, engineHealth, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, powerMul, 0.0f, 1.5f, 0.001f);
        serialize_compressed_float(stream, handlingMul, 0.0f, 1.5f, 0.001f);
        serialize_compressed_float(stream, brakingMul, 0.0f, 1.5f, 0.001f);
        serialize_compressed_float(stream, downforceMul, 0.0f, 1.5f, 0.001f);
        serialize_compressed_float(stream, dragMul, 0.5f, 2.0f, 0.001f);
        serialize_compressed_float(stream, frontWing, 0.0f, 1.0f, 0.001f);
        serialize_compressed_float(stream, rearWing, 0.0f, 1.0f, 0.001f);
        serialize_bits(stream, flags, 16);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct CarSetupMessage : public ksnet::Message {
    uint32_t carId = 0;
    float tirePressureFL = 2.2f, tirePressureFR = 2.2f, tirePressureRL = 2.0f, tirePressureRR = 2.0f;
    float brakeBias = 0.56f;
    float rideHeightFront = 30.f, rideHeightRear = 35.f;
    float springRateFront = 150.f, springRateRear = 180.f;
    float frontWingAngle = 10.f, rearWingAngle = 12.f;
    float diffPreload = 30.f;
    float fuel = 50.f;
    float ballast = 0.f;
    int tcLevel = 0;
    int absLevel = 0;

    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, carId);
        serialize_compressed_float(stream, tirePressureFL, 1.0f, 4.0f, 0.01f);
        serialize_compressed_float(stream, tirePressureFR, 1.0f, 4.0f, 0.01f);
        serialize_compressed_float(stream, tirePressureRL, 1.0f, 4.0f, 0.01f);
        serialize_compressed_float(stream, tirePressureRR, 1.0f, 4.0f, 0.01f);
        serialize_compressed_float(stream, brakeBias, 0.3f, 0.8f, 0.001f);
        serialize_compressed_float(stream, rideHeightFront, 10.0f, 80.0f, 0.1f);
        serialize_compressed_float(stream, rideHeightRear, 10.0f, 80.0f, 0.1f);
        serialize_compressed_float(stream, springRateFront, 50.0f, 400.0f, 0.5f);
        serialize_compressed_float(stream, springRateRear, 50.0f, 400.0f, 0.5f);
        serialize_compressed_float(stream, frontWingAngle, 0.0f, 40.0f, 0.1f);
        serialize_compressed_float(stream, rearWingAngle, 0.0f, 40.0f, 0.1f);
        serialize_compressed_float(stream, diffPreload, 0.0f, 200.0f, 0.5f);
        serialize_compressed_float(stream, fuel, 0.0f, 150.0f, 0.5f);
        serialize_compressed_float(stream, ballast, 0.0f, 100.0f, 0.5f);
        serialize_int(stream, tcLevel, 0, 12);
        serialize_int(stream, absLevel, 0, 12);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};

struct CarCollisionMessage : public ksnet::Message {
    uint32_t carIdA = 0, carIdB = 0;
    float impulse = 0.f;
    float posX = 0, posY = 0, posZ = 0;
    template <typename Stream> bool Serialize(Stream & stream) {
        serialize_uint32(stream, carIdA);
        serialize_uint32(stream, carIdB);
        serialize_compressed_float(stream, impulse, 0.0f, 100000.0f, 1.0f);
        serialize_compressed_float(stream, posX, -10000.0f, 10000.0f, 0.01f);
        serialize_compressed_float(stream, posY, -1000.0f, 1000.0f, 0.01f);
        serialize_compressed_float(stream, posZ, -10000.0f, 10000.0f, 0.01f);
        return true;
    }
    YOJIMBO_VIRTUAL_SERIALIZE_FUNCTIONS()
};
