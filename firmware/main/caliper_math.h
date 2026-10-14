#pragma once

#include <cstddef>
#include <cstdint>

/*
 * Lógica pura do adipômetro (sem ESP-IDF), para poder ser testada em host.
 * Ver test/host/.
 */

namespace caliper {

// Geometria mecânica da pinça usada na conversão ângulo -> mm.
struct Geometry {
    float armLengthMm;      // Distância eixo de rotação -> ponta de medida
    float offsetMm;         // Offset somado à abertura calculada
    bool invertDirection;   // true se abrir a pinça diminuir o ângulo
    float minMm;            // Saturação inferior da saída
    float maxMm;            // Saturação superior da saída
};

// Normaliza uma diferença de ângulos para o intervalo [-180, 180].
float normalizeDeltaDeg(float delta);

// Converte a leitura bruta de 12 bits do AS5600 (0-4095) para graus.
float rawToDegrees(uint16_t raw);

/*
 * Converte o ângulo atual em abertura da pinça (mm), pelo modelo de corda:
 *   abertura_mm = 2 * L * sen(delta_rad / 2) + offset
 * Delta negativo retorna minMm; o resultado é saturado em [minMm, maxMm].
 * Se deltaOut != nullptr, recebe o delta (graus) já normalizado/invertido.
 */
float angleToThicknessMm(float angleDeg, float zeroDeg, const Geometry &geometry, float *deltaOut = nullptr);

// Copia src para dst trocando '"', '\\' e caracteres de controle por '_'.
// Sempre termina dst com '\0', truncando se necessário.
void sanitizeJsonText(const char *src, char *dst, size_t dstLen);

} // namespace caliper
