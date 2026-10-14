#include "AS5600.h"
#include "BleAdipometer.h"
#include "caliper_math.h"

#include <cmath>
#include <cstring>
#include <cstdio>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* ========================================================================== */
/*                         CONFIGURAÇÕES DO PROJETO                           */
/* ========================================================================== */

// Pinos I2C do AS5600. Ajuste conforme o seu hardware.
#define ADIPO_I2C_SDA_GPIO              GPIO_NUM_8
#define ADIPO_I2C_SCL_GPIO              GPIO_NUM_9

// Nome anunciado no BLE.
#define ADIPO_BLE_DEVICE_NAME           "Adipometro-C3"

// Tamanho máximo aceito para o nome da dobra recebido do app.
#define ADIPO_DOBRA_MAX_LEN             40

// Fila de comandos recebidos via BLE.
#define ADIPO_CMD_QUEUE_LEN             5
#define ADIPO_CMD_TASK_STACK_BYTES      4096
#define ADIPO_CMD_TASK_PRIORITY         5

// Namespace/chave usados para persistir o zero mecânico da pinça na NVS.
#define ADIPO_NVS_NAMESPACE             "adipo"
#define ADIPO_NVS_KEY_ZERO_ANGLE        "zero_deg"

/*
 * Conversão geométrica ângulo -> abertura da pinça em mm.
 *
 * Medida mecânica informada:
 *   eixo de rotação / AS5600 -> média da ponta de medida = 120 mm
 *
 * Quando a pinça está fechada, use o comando {"cmd":"tare"} para salvar o
 * ângulo atual como zero mecânico. Depois disso:
 *
 *   delta_graus = angulo_atual - angulo_zero
 *   abertura_mm = 2 * ADIPO_ARM_LENGTH_MM * sen(delta_rad / 2)
 *
 * Esse modelo calcula a distância entre duas pontas que estão a 120 mm do eixo
 * e se separam por um ângulo relativo delta. Para ângulos pequenos, ele fica
 * próximo de abertura_mm ~= ADIPO_ARM_LENGTH_MM * delta_rad.
 */
#define ADIPO_DEFAULT_ZERO_ANGLE_DEG    0.0f
#define ADIPO_ARM_LENGTH_MM             120.0f
#define ADIPO_MM_OFFSET                 0.0f

// Use 1 se a abertura da pinça fizer o ângulo diminuir em vez de aumentar.
#define ADIPO_INVERT_ANGLE_DIRECTION    1

// Limites físicos usados apenas para saturar a saída calculada.
#define ADIPO_MM_MIN                    0.0f
#define ADIPO_MM_MAX                    80.0f

/* ========================================================================== */
/*                                  OBJETOS                                   */
/* ========================================================================== */

static const char *TAG = "Adipometro";

static AS5600 g_encoder(ADIPO_I2C_SDA_GPIO, ADIPO_I2C_SCL_GPIO);
static BleAdipometer g_ble;
static QueueHandle_t g_cmdQueue = nullptr;

static const caliper::Geometry g_geometry = {
    ADIPO_ARM_LENGTH_MM,                    // armLengthMm
    ADIPO_MM_OFFSET,                        // offsetMm
    ADIPO_INVERT_ANGLE_DIRECTION != 0,      // invertDirection
    ADIPO_MM_MIN,                           // minMm
    ADIPO_MM_MAX,                           // maxMm
};

static float g_zeroAngleDeg = ADIPO_DEFAULT_ZERO_ANGLE_DEG;
static bool g_zeroCalibrated = false;

struct BleCommandMsg {
    char json[BLE_ADIPOMETER_MAX_PAYLOAD];
    size_t len;
};

/* ========================================================================== */
/*                             FUNÇÕES AUXILIARES                             */
/* ========================================================================== */


static esp_err_t initStorage() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t loadZeroAngleFromNvs() {
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(ADIPO_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        g_zeroAngleDeg = ADIPO_DEFAULT_ZERO_ANGLE_DEG;
        g_zeroCalibrated = false;
        ESP_LOGW(TAG, "Zero mecânico ainda não salvo. Use o app para calibrar com a pinça fechada.");
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    float storedZero = 0.0f;
    size_t requiredSize = sizeof(storedZero);
    ret = nvs_get_blob(handle, ADIPO_NVS_KEY_ZERO_ANGLE, &storedZero, &requiredSize);
    nvs_close(handle);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        g_zeroAngleDeg = ADIPO_DEFAULT_ZERO_ANGLE_DEG;
        g_zeroCalibrated = false;
        ESP_LOGW(TAG, "Zero mecânico ainda não salvo. Use o app para calibrar com a pinça fechada.");
        return ESP_OK;
    }

    if (ret == ESP_OK && requiredSize == sizeof(storedZero)) {
        g_zeroAngleDeg = storedZero;
        g_zeroCalibrated = true;
        ESP_LOGI(TAG, "Zero mecânico carregado da NVS: %.2f graus", g_zeroAngleDeg);
        return ESP_OK;
    }

    g_zeroAngleDeg = ADIPO_DEFAULT_ZERO_ANGLE_DEG;
    g_zeroCalibrated = false;
    return ret == ESP_OK ? ESP_ERR_INVALID_SIZE : ret;
}

static esp_err_t saveZeroAngleToNvs(float zeroAngleDeg) {
    nvs_handle_t handle = 0;
    esp_err_t ret = nvs_open(ADIPO_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(handle, ADIPO_NVS_KEY_ZERO_ANGLE, &zeroAngleDeg, sizeof(zeroAngleDeg));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);
    return ret;
}

static const char *magnetWarningString(const AS5600Status &status) {
    if (!status.magnetDetected) return "ima_nao_detectado";
    if (status.magnetWeak) return "ima_fraco";
    if (status.magnetStrong) return "ima_forte";
    return nullptr;
}

static void addStringIfNotEmpty(cJSON *obj, const char *key, const char *value) {
    if (value != nullptr && value[0] != '\0') {
        cJSON_AddStringToObject(obj, key, value);
    }
}

static esp_err_t sendTextPayload(const char *payload) {
    if (payload == nullptr) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "BLE notify/read payload: %s", payload);

    esp_err_t ret = g_ble.notifyText(payload);

    // notifyText retorna ESP_ERR_INVALID_STATE se o app ainda não habilitou notify.
    // Mesmo assim, o payload fica disponível para READ na characteristic Measurement.
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Payload salvo, mas notify não enviado. Cliente conectado? Notify habilitado?");
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao enviar notify: %s", esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t sendJson(cJSON *root) {
    if (root == nullptr) return ESP_ERR_INVALID_ARG;

    char *payload = cJSON_PrintUnformatted(root);
    if (payload == nullptr) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = sendTextPayload(payload);

    cJSON_free(payload);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t sendError(const char *erro, const char *dobra = nullptr) {
    cJSON *root = cJSON_CreateObject();
    if (root == nullptr) return ESP_ERR_NO_MEM;

    cJSON_AddBoolToObject(root, "ok", false);
    cJSON_AddStringToObject(root, "erro", erro != nullptr ? erro : "erro_desconhecido");
    addStringIfNotEmpty(root, "dobra", dobra);

    return sendJson(root);
}

static bool copyJsonStringField(cJSON *root, const char *key, char *dst, size_t dstLen) {
    if (root == nullptr || key == nullptr || dst == nullptr || dstLen == 0) return false;

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;

    std::strncpy(dst, item->valuestring, dstLen - 1);
    dst[dstLen - 1] = '\0';
    return true;
}

static esp_err_t readCurrentAngle(float *angleDegOut, uint16_t *rawOut = nullptr) {
    if (angleDegOut == nullptr) return ESP_ERR_INVALID_ARG;

    uint16_t raw = 0;
    esp_err_t ret = g_encoder.readRaw(&raw);
    if (ret != ESP_OK) return ret;

    *angleDegOut = caliper::rawToDegrees(raw);
    if (rawOut != nullptr) *rawOut = raw;

    return ESP_OK;
}

/* ========================================================================== */
/*                         TRATAMENTO DOS COMANDOS BLE                        */
/* ========================================================================== */

static esp_err_t handleMeasureCommand(cJSON *root) {
    char dobra[ADIPO_DOBRA_MAX_LEN + 1] = {0};
    if (!copyJsonStringField(root, "dobra", dobra, sizeof(dobra))) {
        return sendError("campo_dobra_invalido");
    }

    AS5600Status status{};
    esp_err_t ret = g_encoder.readStatus(&status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler status do AS5600: %s", esp_err_to_name(ret));
        return sendError("as5600_status_erro", dobra);
    }

    if (!status.magnetDetected) {
        ESP_LOGW(TAG, "Medição bloqueada: imã não detectado");
        return sendError("ima_nao_detectado", dobra);
    }

    float angleDeg = 0.0f;
    uint16_t raw = 0;
    ret = readCurrentAngle(&angleDeg, &raw);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro ao ler ângulo do AS5600: %s", esp_err_to_name(ret));
        return sendError("as5600_leitura_erro", dobra);
    }

    float deltaDeg = 0.0f;
    float mm = caliper::angleToThicknessMm(angleDeg, g_zeroAngleDeg, g_geometry, &deltaDeg);
    mm = std::round(mm * 100.0f) / 100.0f;

    ESP_LOGI(TAG,
             "Medida: dobra=%s raw=%u angle=%.2f zero=%.2f delta=%.2f mm=%.2f",
             dobra,
             raw,
             angleDeg,
             g_zeroAngleDeg,
             deltaDeg,
             mm);

    /*
     * Resposta principal de medição mantida intencionalmente curta.
     *
     * Não envia angle_deg nem delta_deg aqui porque o app usa apenas mm.
     * Esses campos continuam disponíveis no comando {"cmd":"status"} para diagnóstico.
     * Usamos snprintf em vez de cJSON_Print para garantir mm com duas casas e evitar
     * floats longos em notifications BLE.
     */
    char dobraSafe[ADIPO_DOBRA_MAX_LEN + 1] = {0};
    caliper::sanitizeJsonText(dobra, dobraSafe, sizeof(dobraSafe));

    char payload[128] = {0};
    const char *warn = magnetWarningString(status);

    int written = 0;
    if (warn != nullptr) {
        written = std::snprintf(
            payload,
            sizeof(payload),
            "{\"ok\":true,\"dobra\":\"%s\",\"mm\":%.2f,\"warn\":\"%s\"}",
            dobraSafe,
            mm,
            warn
        );
    } else {
        written = std::snprintf(
            payload,
            sizeof(payload),
            "{\"ok\":true,\"dobra\":\"%s\",\"mm\":%.2f}",
            dobraSafe,
            mm
        );
    }

    if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
        return sendError("payload_medida_grande", dobra);
    }

    return sendTextPayload(payload);
}

static esp_err_t handleTareCommand() {
    AS5600Status status{};
    esp_err_t ret = g_encoder.readStatus(&status);
    if (ret != ESP_OK) return sendError("as5600_status_erro");
    if (!status.magnetDetected) return sendError("ima_nao_detectado");

    float angleDeg = 0.0f;
    ret = readCurrentAngle(&angleDeg);
    if (ret != ESP_OK) return sendError("as5600_leitura_erro");

    ret = saveZeroAngleToNvs(angleDeg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao salvar zero na NVS: %s", esp_err_to_name(ret));
        return sendError("nvs_salvar_zero_erro");
    }

    g_zeroAngleDeg = angleDeg;
    g_zeroCalibrated = true;

    cJSON *resp = cJSON_CreateObject();
    if (resp == nullptr) return ESP_ERR_NO_MEM;

    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "tare");
    cJSON_AddBoolToObject(resp, "zero_calibrated", true);
    cJSON_AddBoolToObject(resp, "zero_saved", true);
    cJSON_AddNumberToObject(resp, "zero_angle_deg", std::round(g_zeroAngleDeg * 100.0f) / 100.0f);

    ESP_LOGI(TAG, "Tare/zero atualizado: %.2f graus", g_zeroAngleDeg);
    return sendJson(resp);
}

static esp_err_t handleStatusCommand() {
    AS5600Status status{};
    esp_err_t ret = g_encoder.readStatus(&status);
    if (ret != ESP_OK) return sendError("as5600_status_erro");

    float angleDeg = 0.0f;
    uint16_t raw = 0;
    ret = readCurrentAngle(&angleDeg, &raw);

    cJSON *resp = cJSON_CreateObject();
    if (resp == nullptr) return ESP_ERR_NO_MEM;

    cJSON_AddBoolToObject(resp, "ok", ret == ESP_OK);
    cJSON_AddStringToObject(resp, "cmd", "status");
    cJSON_AddBoolToObject(resp, "magnet", status.magnetDetected);
    cJSON_AddBoolToObject(resp, "weak", status.magnetWeak);
    cJSON_AddBoolToObject(resp, "strong", status.magnetStrong);
    cJSON_AddBoolToObject(resp, "zero_calibrated", g_zeroCalibrated);
    cJSON_AddNumberToObject(resp, "zero_angle_deg", std::round(g_zeroAngleDeg * 100.0f) / 100.0f);

    if (ret == ESP_OK) {
        cJSON_AddNumberToObject(resp, "raw", raw);
        cJSON_AddNumberToObject(resp, "angle_deg", std::round(angleDeg * 100.0f) / 100.0f);
    } else {
        cJSON_AddStringToObject(resp, "erro", "as5600_leitura_erro");
    }

    return sendJson(resp);
}

static esp_err_t handlePingCommand() {
    cJSON *resp = cJSON_CreateObject();
    if (resp == nullptr) return ESP_ERR_NO_MEM;

    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "cmd", "pong");
    cJSON_AddNumberToObject(resp, "uptime_ms", static_cast<double>(esp_timer_get_time() / 1000ULL));

    return sendJson(resp);
}

static void processCommandJson(const char *json, size_t len) {
    if (json == nullptr || len == 0) {
        sendError("comando_vazio");
        return;
    }

    ESP_LOGI(TAG, "Comando BLE recebido: %.*s", static_cast<int>(len), json);

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == nullptr) {
        sendError("json_invalido");
        return;
    }

    char cmd[24] = {0};
    if (!copyJsonStringField(root, "cmd", cmd, sizeof(cmd))) {
        cJSON_Delete(root);
        sendError("campo_cmd_invalido");
        return;
    }

    if (std::strcmp(cmd, "medir") == 0) {
        handleMeasureCommand(root);
    } else if (std::strcmp(cmd, "tare") == 0 || std::strcmp(cmd, "zerar") == 0 || std::strcmp(cmd, "calibrar_zero") == 0) {
        handleTareCommand();
    } else if (std::strcmp(cmd, "status") == 0) {
        handleStatusCommand();
    } else if (std::strcmp(cmd, "ping") == 0) {
        handlePingCommand();
    } else {
        sendError("cmd_desconhecido");
    }

    cJSON_Delete(root);
}

static void commandTask(void *param) {
    (void)param;

    BleCommandMsg msg{};
    while (true) {
        if (xQueueReceive(g_cmdQueue, &msg, portMAX_DELAY) == pdTRUE) {
            processCommandJson(msg.json, msg.len);
        }
    }
}

static void onBleCommandReceived(const uint8_t *data, size_t len, void *ctx) {
    (void)ctx;

    if (g_cmdQueue == nullptr || data == nullptr || len == 0) return;

    BleCommandMsg msg{};
    const size_t copyLen = (len < sizeof(msg.json) - 1) ? len : sizeof(msg.json) - 1;
    std::memcpy(msg.json, data, copyLen);
    msg.json[copyLen] = '\0';
    msg.len = copyLen;

    if (xQueueSend(g_cmdQueue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Fila de comandos BLE cheia. Comando descartado.");
    }
}

/* ========================================================================== */
/*                                  app_main                                  */
/* ========================================================================== */

extern "C" void app_main() {
    ESP_LOGI(TAG, "Inicializando adipômetro AS5600 + NimBLE");

    esp_err_t ret = initStorage();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao inicializar NVS: %s", esp_err_to_name(ret));
        return;
    }

    ret = loadZeroAngleFromNvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao carregar zero da NVS: %s", esp_err_to_name(ret));
    }

    ret = g_encoder.begin();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AS5600 não inicializado: %s", esp_err_to_name(ret));
        return;
    }

    AS5600Status status{};
    ret = g_encoder.readStatus(&status);
    if (ret == ESP_OK) {
        if (!status.magnetDetected) ESP_LOGW(TAG, "Imã não detectado no boot");
        if (status.magnetWeak) ESP_LOGW(TAG, "Imã fraco no boot");
        if (status.magnetStrong) ESP_LOGW(TAG, "Imã forte no boot");
    } else {
        ESP_LOGW(TAG, "Não foi possível ler status inicial do AS5600: %s", esp_err_to_name(ret));
    }

    g_cmdQueue = xQueueCreate(ADIPO_CMD_QUEUE_LEN, sizeof(BleCommandMsg));
    if (g_cmdQueue == nullptr) {
        ESP_LOGE(TAG, "Falha ao criar fila de comandos BLE");
        return;
    }

    BaseType_t taskOk = xTaskCreate(
        commandTask,
        "adipo_cmd",
        ADIPO_CMD_TASK_STACK_BYTES,
        nullptr,
        ADIPO_CMD_TASK_PRIORITY,
        nullptr
    );

    if (taskOk != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar task de comandos");
        return;
    }

    g_ble.setCommandCallback(onBleCommandReceived, nullptr);

    BleAdipometerConfig bleCfg{};
    bleCfg.deviceName = ADIPO_BLE_DEVICE_NAME;
    bleCfg.initNvs = false;
    bleCfg.autoAdvertise = true;

    ret = g_ble.begin(bleCfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Falha ao iniciar BLE: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Firmware pronto. Serviço BLE aguardando conexão do app.");

    // app_main não precisa medir em loop. A medição é disparada por comando BLE.
    // A partir daqui só a task adipo_cmd acessa o AS5600/I2C; app_main pode retornar.
}
