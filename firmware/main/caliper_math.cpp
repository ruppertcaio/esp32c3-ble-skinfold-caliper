#include "caliper_math.h"

#include <cmath>

namespace caliper {

static float clampFloat(float value, float minValue, float maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static float degToRad(float deg) {
    constexpr float PI = 3.14159265358979323846f;
    return deg * (PI / 180.0f);
}

float normalizeDeltaDeg(float delta) {
    while (delta > 180.0f) delta -= 360.0f;
    while (delta < -180.0f) delta += 360.0f;
    return delta;
}

float rawToDegrees(uint16_t raw) {
    // AS5600: 12 bits de resolução (4096 passos por volta)
    return (static_cast<float>(raw) / 4096.0f) * 360.0f;
}

float angleToThicknessMm(float angleDeg, float zeroDeg, const Geometry &geometry, float *deltaOut) {
    float delta = normalizeDeltaDeg(angleDeg - zeroDeg);

    if (geometry.invertDirection) {
        delta = -delta;
    }

    if (deltaOut != nullptr) {
        *deltaOut = delta;
    }

    // Abertura física não deve ser negativa. Se ficar negativa, ajuste
    // invertDirection conforme o sentido real da sua montagem.
    if (delta < 0.0f) {
        return geometry.minMm;
    }

    const float deltaRad = degToRad(delta);
    const float mm = (2.0f * geometry.armLengthMm * std::sin(deltaRad / 2.0f)) + geometry.offsetMm;
    return clampFloat(mm, geometry.minMm, geometry.maxMm);
}

void sanitizeJsonText(const char *src, char *dst, size_t dstLen) {
    if (dst == nullptr || dstLen == 0) return;
    dst[0] = '\0';
    if (src == nullptr) return;

    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 1 < dstLen; ++i) {
        const unsigned char c = static_cast<unsigned char>(src[i]);
        if (c < 0x20 || c == '"' || c == '\\') {
            dst[j++] = '_';
        } else {
            dst[j++] = static_cast<char>(c);
        }
    }
    dst[j] = '\0';
}

} // namespace caliper
