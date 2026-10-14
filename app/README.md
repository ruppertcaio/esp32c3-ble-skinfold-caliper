# Adipômetro — app companheiro (Flutter/Android)

App companheiro do adipômetro BLE (ESP32-C3). Conecta à pinça por Bluetooth Low Energy, solicita a medição de cada dobra cutânea e calcula a densidade corporal e o % de gordura.

> **Protótipo educacional — não é dispositivo médico; os resultados não substituem avaliação profissional.**

## Fluxo de telas

1. **Seleção de método** (`selecao_screen.dart`): status da conexão BLE, reconectar, calibrar zero da pinça e escolher Pollock 3, Pollock 7 ou Petroski 4.
2. **Dados** (`dados_screen.dart`): sexo e idade.
3. **Medição por dobra** (`medicao_screen.dart`): para cada dobra do método, envia o comando de medição e aguarda o valor em mm.
4. **Resultado** (`resultado_screen.dart`): medidas e % de gordura. O cálculo fica em `lib/models/composicao_corporal.dart` (densidade corporal pela equação do método + conversão de Siri).

## Contrato BLE

| Item | Valor |
|---|---|
| Nome do dispositivo | `Adipometro-C3` |
| Serviço | `7a4f0001-2b21-4f37-a602-b29e00000001` |
| Medição (notify) | `7a4f0002-2b21-4f37-a602-b29e00000001` |
| Comandos (write) | `7a4f0003-2b21-4f37-a602-b29e00000001` |

Comandos JSON enviados pelo app:

- `{"cmd":"medir","dobra":"<nome_normalizado>"}` — o nome é minúsculo, sem acentos, espaços viram `_` (ex.: `Axilar Média` → `axilar_media`).
- `{"cmd":"tare"}` — calibra e grava o zero da pinça.
- `{"cmd":"status"}` — estado da calibração.
- `{"cmd":"ping"}`

As respostas chegam como JSON pela característica de notify (campo `mm` com a medida).

## Como rodar

```sh
flutter pub get
flutter run      # com um aparelho Android conectado
flutter test
```

## Permissões Android

Declaradas em `android/app/src/main/AndroidManifest.xml`:

- `BLUETOOTH_SCAN`, `BLUETOOTH_CONNECT` (Android 12+)
- `BLUETOOTH`, `BLUETOOTH_ADMIN` (até Android 11)
- `ACCESS_FINE_LOCATION` (e `ACCESS_COARSE_LOCATION` até Android 9), exigida para scan BLE em versões antigas
- Recurso `android.hardware.bluetooth_le` obrigatório

## Dependências

| Pacote | Uso | Licença |
|---|---|---|
| [flutter_blue_plus](https://pub.dev/packages/flutter_blue_plus) | BLE | ver pub.dev |
| [permission_handler](https://pub.dev/packages/permission_handler) | Permissões em tempo de execução | ver pub.dev |
