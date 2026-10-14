# Firmware ESP32-C3 - Adipômetro AS5600 + NimBLE

Firmware em C++ para ESP-IDF v5.5.x, estruturado para VS Code/Espressif IDE.

## Estrutura

```text
firmware/
  CMakeLists.txt
  sdkconfig.defaults
  main/
    CMakeLists.txt
    main.cpp
    caliper_math.h        # lógica pura (ângulo -> mm, sanitização), sem ESP-IDF
    caliper_math.cpp
    driver/
      AS5600.h
      AS5600.cpp
  components/
    ble_adipometer/
      CMakeLists.txt
      include/BleAdipometer.h
      src/BleAdipometer.cpp
  test/
    host/
      CMakeLists.txt      # testes da lógica pura no PC (CMake puro)
      test_caliper_math.cpp
```

## BLE / GATT

O ESP32-C3 atua como GATT Server. O app mobile atua como GATT Client.

| Função | UUID | Propriedades |
|---|---|---|
| Service | `7a4f0001-2b21-4f37-a602-b29e00000001` | Primary service |
| Measurement | `7a4f0002-2b21-4f37-a602-b29e00000001` | `READ / NOTIFY` |
| Command | `7a4f0003-2b21-4f37-a602-b29e00000001` | `READ / WRITE / WRITE_NO_RSP` |

## Protocolo com o app

### Medir dobra

App escreve na characteristic `Command`:

```json
{"cmd":"medir","dobra":"tricipital"}
```

Firmware responde via notify na characteristic `Measurement`:

```json
{"ok":true,"dobra":"tricipital","mm":12.34}
```

### Erro

```json
{"ok":false,"erro":"ima_nao_detectado","dobra":"tricipital"}
```

### Tare / zerar

Use quando a pinça estiver fechada ou em uma referência mecânica conhecida:

```json
{"cmd":"tare"}
```

Resposta:

```json
{"ok":true,"cmd":"tare","zero_calibrated":true,"zero_saved":true,"zero_angle_deg":83.15}
```

O valor de zero é salvo na NVS do ESP32. Depois de reiniciar o firmware, o zero calibrado é carregado automaticamente.

### Status

```json
{"cmd":"status"}
```

Resposta típica:

```json
{"ok":true,"cmd":"status","magnet":true,"weak":false,"strong":false,"zero_calibrated":true,"zero_angle_deg":83.15,"raw":1024,"angle_deg":90}
```

### Ping

```json
{"cmd":"ping"}
```

Resposta:

```json
{"ok":true,"cmd":"pong","uptime_ms":123456}
```

## Integração com o app Flutter

A ordem correta do app é:

1. Conectar no dispositivo `Adipometro-C3`.
2. Descobrir o service `7a4f0001...`.
3. Achar `_notifyChar = 7a4f0002...`.
4. Achar `_writeChar = 7a4f0003...`.
5. Executar `await _notifyChar.setNotifyValue(true)`.
6. Escrever o JSON de comando em `_writeChar`.
7. Escutar o JSON recebido em `_notifyChar.onValueReceived`.

Observação: no app, prefira `utf8.encode(jsonEncode(...))` em vez de `dados.codeUnits`, principalmente se os nomes das dobras tiverem acentos.

## Conversão ângulo -> abertura em mm

No topo de `main/main.cpp`, ajuste:

```cpp
#define ADIPO_DEFAULT_ZERO_ANGLE_DEG    0.0f
#define ADIPO_ARM_LENGTH_MM             120.0f
#define ADIPO_MM_OFFSET                 0.0f
#define ADIPO_INVERT_ANGLE_DIRECTION    1
#define ADIPO_MM_MIN                    0.0f
#define ADIPO_MM_MAX                    80.0f
```

Esses valores montam um `caliper::Geometry` passado para `caliper::angleToThicknessMm` (em `main/caliper_math.cpp`).

Modelo geométrico usado:

```text
delta_graus = angulo_atual - angulo_zero
abertura_mm = 2 * 120 * sen(delta_rad / 2) + offset
```

Essa equação considera que as duas pontas de medição estão a 120 mm do eixo de rotação e que a medida desejada é a distância entre essas pontas.

Antes de medir pela primeira vez, feche completamente a pinça e envie:

```json
{"cmd":"tare"}
```

Esse procedimento deve ser exposto no app como uma calibração guiada. Não é necessário repetir a cada medição; repita apenas se o sensor/ímã for reposicionado, se houver alteração mecânica ou se a leitura de zero parecer deslocada.

O valor atual (`1`) corresponde a uma montagem em que abrir a pinça faz o ângulo diminuir. Se na sua montagem o ângulo aumentar ao abrir, use:

```cpp
#define ADIPO_INVERT_ANGLE_DIRECTION 0
```

Para conferir a calibração: abra a pinça em uma medida conhecida, envie `{"cmd":"medir","dobra":"teste"}` e compare o campo `mm` com a abertura real. Para depuração, o comando `{"cmd":"status"}` retorna `raw` e `angle_deg`, e o log serial da medição mostra `angle`, `zero` e `delta`.

## Build

No terminal do ESP-IDF:

```bash
idf.py set-target esp32c3
idf.py menuconfig
idf.py build
idf.py flash monitor
```

No `menuconfig`, confira se NimBLE está habilitado:

```text
Component config -> Bluetooth -> NimBLE
```

O MTU preferencial vem do sdkconfig (`CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=185` em `sdkconfig.defaults`); o `BleAdipometer.cpp` usa esse valor. A maior resposta (`status`, ~143 bytes) precisa de ATT MTU ≥ 146; o app pede MTU 185 ao conectar (`ble_service.dart`, `connect(mtu: 185)`). Com o MTU padrão (23) o notify seria truncado. Se já existir um `sdkconfig` gerado antes, apague-o ou ajuste o valor no `menuconfig` para que o novo default seja aplicado.

## Testes em host

A lógica pura (`main/caliper_math.cpp`) não depende do ESP-IDF e pode ser testada no PC com CMake e um compilador C++17. A partir da raiz do repositório:

```bash
cmake -S firmware/test/host -B build-host
cmake --build build-host
ctest --test-dir build-host -C Debug --output-on-failure
```

Os testes cobrem a conversão ângulo -> mm (pinça fechada, volta completa 355° -> 5°, inversão de sentido, saturação, offset), `normalizeDeltaDeg`, `rawToDegrees` e `sanitizeJsonText`.

## Observações de implementação

- O callback GATT não lê o AS5600 diretamente. Ele apenas copia o comando BLE para uma fila FreeRTOS.
- A task `adipo_cmd` processa o JSON, lê o AS5600 e envia a resposta. Depois do boot, ela é a única task que acessa o barramento I2C; `app_main` retorna após a inicialização.
- Os flags de estado do `BleAdipometer` (conectado, notify habilitado etc.) são `std::atomic`, pois são escritos pela task do NimBLE e lidos pela task de comandos.
- A última resposta fica disponível tanto por `NOTIFY` quanto por `READ` na characteristic `Measurement`.
- O firmware bloqueia a medição se o AS5600 informar que o imã não foi detectado.


## Melhorias de experiência implementadas

- O app deve conectar uma vez na tela inicial e reutilizar a conexão nas telas seguintes.
- A tela de medição não chama `disconnect()` ao finalizar.
- O comando `tare` grava o zero mecânico na NVS.
- O comando `status` informa se existe zero calibrado salvo.
- O AS5600 usa pull-up interno habilitado no driver I2C, mas resistores externos de 4,7 kΩ em SDA/SCL ainda são recomendados para maior estabilidade.

## Atualização de protocolo - resposta curta de medição

A resposta do comando `medir` foi reduzida ao necessário para o app:

```json
{"ok":true,"dobra":"peitoral","mm":7.91}
```

Se houver alerta magnético, a resposta inclui apenas `warn` adicional:

```json
{"ok":true,"dobra":"peitoral","mm":7.91,"warn":"ima_fraco"}
```

Os campos `angle_deg`, `delta_deg`, `raw` e `zero_angle_deg` não são enviados na medição normal. Eles continuam disponíveis no comando:

```json
{"cmd":"status"}
```
