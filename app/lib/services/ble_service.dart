import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

enum AdipoBleStatus {
  idle,
  requestingPermissions,
  waitingBluetooth,
  scanning,
  connecting,
  discovering,
  connected,
  disconnected,
  permissionDenied,
  error,
}

class BleConnectionInfo {
  final AdipoBleStatus status;
  final String message;
  final bool zeroCalibrated;
  final double? zeroAngleDeg;

  const BleConnectionInfo({
    required this.status,
    required this.message,
    this.zeroCalibrated = false,
    this.zeroAngleDeg,
  });

  bool get isConnected => status == AdipoBleStatus.connected;
  bool get isBusy =>
      status == AdipoBleStatus.requestingPermissions ||
      status == AdipoBleStatus.waitingBluetooth ||
      status == AdipoBleStatus.scanning ||
      status == AdipoBleStatus.connecting ||
      status == AdipoBleStatus.discovering;

  BleConnectionInfo copyWith({
    AdipoBleStatus? status,
    String? message,
    bool? zeroCalibrated,
    double? zeroAngleDeg,
    bool clearZeroAngle = false,
  }) {
    return BleConnectionInfo(
      status: status ?? this.status,
      message: message ?? this.message,
      zeroCalibrated: zeroCalibrated ?? this.zeroCalibrated,
      zeroAngleDeg: clearZeroAngle ? null : (zeroAngleDeg ?? this.zeroAngleDeg),
    );
  }
}

class BleService {
  BleService._();

  static final BleService instance = BleService._();

  static const String nomeDispositivo = 'Adipometro-C3';

  static const String serviceUUID = '7a4f0001-2b21-4f37-a602-b29e00000001';
  static const String writeUUID = '7a4f0003-2b21-4f37-a602-b29e00000001';
  static const String notifyUUID = '7a4f0002-2b21-4f37-a602-b29e00000001';

  final ValueNotifier<BleConnectionInfo> connectionInfo = ValueNotifier(
    const BleConnectionInfo(
      status: AdipoBleStatus.idle,
      message: 'Bluetooth não conectado.',
    ),
  );

  final StreamController<Map<String, dynamic>> _respostasController =
      StreamController<Map<String, dynamic>>.broadcast();

  BluetoothDevice? _dispositivo;
  BluetoothCharacteristic? _writeChar;
  BluetoothCharacteristic? _notifyChar;
  StreamSubscription<List<int>>? _notifySub;
  Future<bool>? _connectFuture;
  String _rxBuffer = '';

  bool get conectado =>
      connectionInfo.value.isConnected && _writeChar != null && _notifyChar != null;

  static String normalizarDobra(String texto) {
    var resultado = texto.toLowerCase().trim();

    const mapa = {
      'à': 'a', 'á': 'a', 'â': 'a', 'ã': 'a', 'ä': 'a', 'å': 'a',
      'ç': 'c',
      'è': 'e', 'é': 'e', 'ê': 'e', 'ë': 'e',
      'ì': 'i', 'í': 'i', 'î': 'i', 'ï': 'i',
      'ò': 'o', 'ó': 'o', 'ô': 'o', 'õ': 'o', 'ö': 'o',
      'ù': 'u', 'ú': 'u', 'û': 'u', 'ü': 'u',
      'ý': 'y', 'ÿ': 'y',
    };

    mapa.forEach((comAcento, semAcento) {
      resultado = resultado.replaceAll(comAcento, semAcento);
    });

    return resultado.replaceAll(RegExp(r'\s+'), '_');
  }

  Future<bool> conectar({
    Duration timeout = const Duration(seconds: 12),
    bool force = false,
  }) async {
    if (!force && conectado) {
      return true;
    }

    final running = _connectFuture;
    if (!force && running != null) {
      return running;
    }

    _connectFuture = _conectarImpl(timeout: timeout, force: force);
    try {
      return await _connectFuture!;
    } finally {
      _connectFuture = null;
    }
  }

  Future<bool> garantirConectado() async {
    if (conectado) {
      return true;
    }
    return conectar();
  }

  Future<bool> _conectarImpl({required Duration timeout, required bool force}) async {
    try {
      if (force) {
        await desconectar();
      }

      _setConnectionState(
        AdipoBleStatus.requestingPermissions,
        'Solicitando permissões de Bluetooth...',
      );
      await _garantirPermissoes();

      _setConnectionState(
        AdipoBleStatus.waitingBluetooth,
        'Aguardando Bluetooth ligado...',
      );
      await _aguardarBluetoothLigado(timeout);

      await FlutterBluePlus.stopScan();

      _setConnectionState(
        AdipoBleStatus.scanning,
        'Procurando $nomeDispositivo...',
      );
      final device = await _procurarDispositivo(timeout);
      if (device == null) {
        _limparHandles();
        _setConnectionState(
          AdipoBleStatus.error,
          'Adipômetro não encontrado. Verifique se o firmware está ligado.',
        );
        return false;
      }

      _dispositivo = device;

      _setConnectionState(
        AdipoBleStatus.connecting,
        'Conectando ao adipômetro...',
      );
      try {
        await _dispositivo!.connect(
          license: License.nonprofit,
          autoConnect: false,
          timeout: const Duration(seconds: 15),
          mtu: 185,
        );
      } catch (e) {
        final msg = e.toString().toLowerCase();
        if (!msg.contains('already') && !msg.contains('connected')) {
          rethrow;
        }
      }

      _setConnectionState(
        AdipoBleStatus.discovering,
        'Configurando serviço BLE...',
      );
      await _configurarCharacteristics();

      _setConnectionState(
        AdipoBleStatus.connected,
        'Adipômetro conectado.',
      );

      // Tenta carregar do firmware o estado da calibração persistida.
      unawaited(solicitarStatus().catchError((_) => <String, dynamic>{}));

      return true;
    } on TimeoutException {
      _limparHandles();
      _setConnectionState(
        AdipoBleStatus.error,
        'Tempo esgotado ao procurar/conectar ao adipômetro.',
      );
      return false;
    } catch (e) {
      await desconectar(silencioso: true);
      _setConnectionState(
        AdipoBleStatus.error,
        'Falha BLE: $e',
      );
      return false;
    }
  }

  void _setConnectionState(AdipoBleStatus status, String message) {
    connectionInfo.value = connectionInfo.value.copyWith(
      status: status,
      message: message,
    );
  }

  Future<void> _garantirPermissoes() async {
    if (!Platform.isAndroid) {
      return;
    }

    final statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
    ].request();

    final negada = statuses.values.any(
      (status) =>
          status.isDenied || status.isPermanentlyDenied || status.isRestricted,
    );

    if (negada) {
      connectionInfo.value = connectionInfo.value.copyWith(
        status: AdipoBleStatus.permissionDenied,
        message: 'Permissões de Bluetooth/localização negadas.',
      );
      throw Exception('Permissões de Bluetooth/localização negadas.');
    }
  }

  Future<void> _aguardarBluetoothLigado(Duration timeout) async {
    await FlutterBluePlus.adapterState
        .where((state) => state == BluetoothAdapterState.on)
        .first
        .timeout(timeout);
  }

  Future<BluetoothDevice?> _procurarDispositivo(Duration timeout) async {
    final completer = Completer<BluetoothDevice?>();

    late final StreamSubscription<List<ScanResult>> sub;
    sub = FlutterBluePlus.scanResults.listen((results) {
      for (final result in results) {
        final platformName = result.device.platformName;
        final advName = result.advertisementData.advName;
        final serviceMatch = result.advertisementData.serviceUuids.any(
          (uuid) => uuid.toString().toLowerCase() == serviceUUID,
        );

        if (platformName == nomeDispositivo ||
            advName == nomeDispositivo ||
            serviceMatch) {
          if (!completer.isCompleted) {
            completer.complete(result.device);
          }
          break;
        }
      }
    });

    try {
      await FlutterBluePlus.startScan(
        timeout: timeout,
        androidUsesFineLocation: true,
      );

      final device = await completer.future.timeout(
        timeout,
        onTimeout: () => null,
      );

      return device;
    } finally {
      await FlutterBluePlus.stopScan();
      await sub.cancel();
    }
  }

  Future<void> _configurarCharacteristics() async {
    final services = await _dispositivo!.discoverServices();

    _writeChar = null;
    _notifyChar = null;

    for (final service in services) {
      if (service.uuid.toString().toLowerCase() != serviceUUID) {
        continue;
      }

      for (final char in service.characteristics) {
        final uuid = char.uuid.toString().toLowerCase();

        if (uuid == writeUUID) {
          _writeChar = char;
        } else if (uuid == notifyUUID) {
          _notifyChar = char;
        }
      }
    }

    if (_writeChar == null || _notifyChar == null) {
      throw Exception('Characteristics BLE não encontradas.');
    }

    await _notifySub?.cancel();
    await _notifyChar!.setNotifyValue(true);
    _notifySub = _notifyChar!.onValueReceived.listen(
      (bytes) {
        final resposta = _parseResposta(bytes);
        if (resposta == null) {
          return;
        }
        _atualizarEstadoComResposta(resposta);
        _respostasController.add(resposta);
      },
      onError: (Object error) {
        _respostasController.add({
          'ok': false,
          'erro': 'notify_erro',
          'raw': error.toString(),
        });
      },
    );
  }

  Future<void> enviarDobra(String dobra) async {
    await _enviarComando({
      'cmd': 'medir',
      'dobra': normalizarDobra(dobra),
    });
  }

  Future<Map<String, dynamic>> calibrarZero() async {
    final ok = await garantirConectado();
    if (!ok) {
      throw Exception('Adipômetro não conectado.');
    }

    final respostaFuture = escutarResposta().firstWhere(
      (resp) => resp['cmd'] == 'tare' || resp['cmd'] == 'zero' || resp['erro'] != null,
    ).timeout(const Duration(seconds: 5));

    await _enviarComando({'cmd': 'tare'});
    final resposta = await respostaFuture;

    if (resposta['ok'] != true) {
      throw Exception(resposta['erro']?.toString() ?? 'Falha ao calibrar zero.');
    }

    return resposta;
  }

  Future<Map<String, dynamic>> solicitarStatus() async {
    final ok = await garantirConectado();
    if (!ok) {
      throw Exception('Adipômetro não conectado.');
    }

    final respostaFuture = escutarResposta().firstWhere(
      (resp) => resp['cmd'] == 'status' || resp['erro'] != null,
    ).timeout(const Duration(seconds: 5));

    await _enviarComando({'cmd': 'status'});
    return respostaFuture;
  }

  Future<void> enviarPing() async {
    await _enviarComando({'cmd': 'ping'});
  }

  Future<void> _enviarComando(Map<String, dynamic> comando) async {
    final ok = await garantirConectado();
    if (!ok || _writeChar == null) {
      throw Exception('Characteristic WRITE não configurada.');
    }

    try {
      await _writeChar!.write(
        utf8.encode(jsonEncode(comando)),
        withoutResponse: false,
      );
    } catch (e) {
      _setConnectionState(
        AdipoBleStatus.disconnected,
        'Conexão perdida. Toque em Reconectar.',
      );
      throw Exception('Falha ao enviar comando BLE: $e');
    }
  }

  Stream<Map<String, dynamic>> escutarResposta() {
    return _respostasController.stream;
  }

  Map<String, dynamic>? _parseResposta(List<int> bytes) {
    final texto = utf8
        .decode(bytes, allowMalformed: true)
        .replaceAll('\u0000', '')
        .trim();

    if (texto.isEmpty) {
      return null;
    }

    final candidato = (_rxBuffer + texto).trim();

    try {
      final obj = jsonDecode(candidato);
      _rxBuffer = '';
      return _normalizarResposta(obj, candidato);
    } catch (_) {
      // Em BLE, se o MTU estiver baixo, uma resposta JSON pode chegar truncada.
      // Nesse caso, guarda o fragmento e espera a próxima notificação em vez de
      // exibir json_invalido ao usuário.
      if (candidato.startsWith('{') && !candidato.endsWith('}')) {
        _rxBuffer = candidato;
        return null;
      }

      final inicio = candidato.indexOf('{');
      final fim = candidato.lastIndexOf('}');
      if (inicio >= 0 && fim > inicio) {
        final extraido = candidato.substring(inicio, fim + 1);
        try {
          final obj = jsonDecode(extraido);
          _rxBuffer = '';
          return _normalizarResposta(obj, extraido);
        } catch (_) {}
      }

      _rxBuffer = '';
      return {
        'ok': false,
        'erro': 'json_invalido',
        'raw': candidato,
      };
    }
  }

  Map<String, dynamic> _normalizarResposta(dynamic obj, String raw) {
    if (obj is! Map<String, dynamic>) {
      return {
        'ok': false,
        'erro': 'json_invalido',
        'raw': raw,
      };
    }

    final cmd = obj['cmd']?.toString();

    final resposta = <String, dynamic>{
      'ok': obj['ok'] is bool ? obj['ok'] as bool : true,
      'cmd': cmd,
      'erro': obj['erro']?.toString(),
      'dobra': obj['dobra']?.toString() ?? '',
      'mm': _toDouble(obj['mm']),
      'zero_angle_deg': _toNullableDouble(obj['zero_angle_deg']),
      'zero_calibrated': obj['zero_calibrated'] == true,
      'magnet': obj['magnet'] == true,
      'weak': obj['weak'] == true,
      'strong': obj['strong'] == true,
      'warn': obj['warn']?.toString(),
      'raw': raw,
    };

    // angle_deg e delta_deg são diagnósticos. O app de medição usa apenas mm;
    // esses campos só são preservados quando o firmware responde ao comando status.
    if (cmd == 'status') {
      resposta['angle_deg'] = _toNullableDouble(obj['angle_deg']);
      resposta['delta_deg'] = _toNullableDouble(obj['delta_deg']);
    }

    return resposta;
  }

  void _atualizarEstadoComResposta(Map<String, dynamic> resposta) {
    final cmd = resposta['cmd'];
    if (cmd != 'tare' && cmd != 'status') {
      return;
    }

    final zeroAngle = resposta['zero_angle_deg'] as double?;
    final zeroCalibrated = resposta['zero_calibrated'] == true || cmd == 'tare';

    connectionInfo.value = connectionInfo.value.copyWith(
      zeroCalibrated: zeroCalibrated,
      zeroAngleDeg: zeroAngle,
    );
  }

  double _toDouble(dynamic valor) {
    return _toNullableDouble(valor) ?? 0.0;
  }

  double? _toNullableDouble(dynamic valor) {
    if (valor is num) {
      return valor.toDouble();
    }
    if (valor is String) {
      return double.tryParse(valor.replaceAll(',', '.'));
    }
    return null;
  }

  Future<void> desconectar({bool silencioso = false}) async {
    try {
      await _notifySub?.cancel();
    } catch (_) {}
    _notifySub = null;

    try {
      await _notifyChar?.setNotifyValue(false);
    } catch (_) {}

    try {
      await _dispositivo?.disconnect();
    } catch (_) {}

    _limparHandles();

    if (!silencioso) {
      _setConnectionState(
        AdipoBleStatus.disconnected,
        'Bluetooth desconectado.',
      );
    }
  }

  void _limparHandles() {
    _dispositivo = null;
    _writeChar = null;
    _notifyChar = null;
  }

  void dispose() {
    _notifySub?.cancel();
    _respostasController.close();
    connectionInfo.dispose();
  }
}
