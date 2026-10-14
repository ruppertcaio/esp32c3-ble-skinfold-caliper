// Testes em host (PC) da lógica pura do adipômetro. Sem framework.
#include "caliper_math.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK falhou: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) <= (tol))

static const float TOL = 0.01f;

// Mesmos valores padrão de main.cpp, mas sem inversão.
static const caliper::Geometry BASE = {120.0f, 0.0f, false, 0.0f, 80.0f};

// 2 * 120 * sen(5 graus)
static const float MM_10_DEG = 20.917f;

static void testThickness() {
    float delta = -1.0f;

    // Pinça fechada: ângulo == zero -> 0 mm
    CHECK_NEAR(caliper::angleToThicknessMm(100.0f, 100.0f, BASE, &delta), 0.0f, TOL);
    CHECK_NEAR(delta, 0.0f, TOL);

    // Delta de 10 graus com L = 120 mm
    CHECK_NEAR(caliper::angleToThicknessMm(110.0f, 100.0f, BASE), MM_10_DEG, TOL);

    // Volta completa: zero 355, ângulo 5 -> +10 graus
    CHECK_NEAR(caliper::angleToThicknessMm(5.0f, 355.0f, BASE, &delta), MM_10_DEG, TOL);
    CHECK_NEAR(delta, 10.0f, TOL);

    // invertDirection inverte o sinal do delta
    caliper::Geometry inv = BASE;
    inv.invertDirection = true;
    CHECK_NEAR(caliper::angleToThicknessMm(100.0f, 110.0f, inv, &delta), MM_10_DEG, TOL);
    CHECK_NEAR(delta, 10.0f, TOL);
    CHECK_NEAR(caliper::angleToThicknessMm(110.0f, 100.0f, inv, &delta), 0.0f, TOL);
    CHECK_NEAR(delta, -10.0f, TOL);

    // Delta negativo -> minMm (offset não é somado)
    caliper::Geometry withMin = BASE;
    withMin.minMm = 2.0f;
    withMin.offsetMm = 1.0f;
    CHECK_NEAR(caliper::angleToThicknessMm(100.0f, 110.0f, withMin), 2.0f, TOL);

    // Delta grande satura em maxMm (90 graus -> ~169.7 mm)
    CHECK_NEAR(caliper::angleToThicknessMm(190.0f, 100.0f, BASE), 80.0f, TOL);

    // Offset somado à abertura
    caliper::Geometry withOffset = BASE;
    withOffset.offsetMm = 1.5f;
    CHECK_NEAR(caliper::angleToThicknessMm(110.0f, 100.0f, withOffset), MM_10_DEG + 1.5f, TOL);
}

static void testNormalizeAndRaw() {
    CHECK_NEAR(caliper::normalizeDeltaDeg(190.0f), -170.0f, TOL);
    CHECK_NEAR(caliper::normalizeDeltaDeg(-190.0f), 170.0f, TOL);
    CHECK_NEAR(caliper::normalizeDeltaDeg(45.0f), 45.0f, TOL);

    CHECK(caliper::rawToDegrees(0) == 0.0f);
    CHECK(caliper::rawToDegrees(1024) == 90.0f);
}

static void testSanitize() {
    char dst[32];

    caliper::sanitizeJsonText("a\"b\\c\nd\te", dst, sizeof(dst));
    CHECK(std::strcmp(dst, "a_b_c_d_e") == 0);

    caliper::sanitizeJsonText("tricipital", dst, sizeof(dst));
    CHECK(std::strcmp(dst, "tricipital") == 0);

    // Trunca respeitando o tamanho de dst e termina com '\0'
    char small[4];
    std::memset(small, 'X', sizeof(small));
    caliper::sanitizeJsonText("abcdef", small, sizeof(small));
    CHECK(std::strcmp(small, "abc") == 0);

    // src nulo -> string vazia
    caliper::sanitizeJsonText(nullptr, dst, sizeof(dst));
    CHECK(dst[0] == '\0');

    // dstLen 0 -> não escreve nada
    char untouched = 'X';
    caliper::sanitizeJsonText("abc", &untouched, 0);
    CHECK(untouched == 'X');
}

int main() {
    testThickness();
    testNormalizeAndRaw();
    testSanitize();

    if (g_failures != 0) {
        std::fprintf(stderr, "%d verificação(ões) falharam\n", g_failures);
        return 1;
    }
    std::printf("Todos os testes passaram\n");
    return 0;
}
