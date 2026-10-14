import 'package:flutter/material.dart';

import '../services/ble_service.dart';
import 'dados_screen.dart';

class SelecaoScreen extends StatefulWidget {
  const SelecaoScreen({super.key});

  @override
  State<SelecaoScreen> createState() => _SelecaoScreenState();
}

class _SelecaoScreenState extends State<SelecaoScreen> {
  final BleService _ble = BleService.instance;
  bool _calibrando = false;

  @override
  void initState() {
    super.initState();
    _ble.conectar();
  }

  Future<void> _reconectar() async {
    await _ble.conectar(force: true);
  }

  Future<void> _calibrarZero() async {
    final confirmar = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Calibrar zero da pinça'),
        content: const Text(
          'Feche completamente a pinça, mantenha o conjunto parado e toque em Salvar zero. '
          'Esse valor será gravado no firmware e continuará salvo após reiniciar o ESP32.',
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancelar'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('Salvar zero'),
          ),
        ],
      ),
    );

    if (confirmar != true) {
      return;
    }

    setState(() => _calibrando = true);
    try {
      final resposta = await _ble.calibrarZero();
      if (!mounted) return;

      final zero = resposta['zero_angle_deg'] as double?;
      final textoZero = zero == null ? '' : ' (${zero.toStringAsFixed(2)}°)';
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Zero calibrado e salvo$textoZero.')),
      );
    } catch (e) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text('Falha ao calibrar zero: $e')),
      );
    } finally {
      if (mounted) {
        setState(() => _calibrando = false);
      }
    }
  }

  Future<void> _abrirMetodo(String metodologia) async {
    if (!_ble.conectado) {
      final conectado = await _ble.conectar();
      if (!conectado && mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Conecte o adipômetro antes de iniciar a medição.'),
          ),
        );
        return;
      }
    }

    if (!mounted) return;
    Navigator.push(
      context,
      MaterialPageRoute(
        builder: (_) => DadosScreen(metodologia: metodologia),
      ),
    );
  }

  IconData _iconeStatus(AdipoBleStatus status) {
    switch (status) {
      case AdipoBleStatus.connected:
        return Icons.bluetooth_connected;
      case AdipoBleStatus.error:
      case AdipoBleStatus.permissionDenied:
      case AdipoBleStatus.disconnected:
        return Icons.bluetooth_disabled;
      default:
        return Icons.bluetooth_searching;
    }
  }

  Color _corStatus(BuildContext context, AdipoBleStatus status) {
    switch (status) {
      case AdipoBleStatus.connected:
        return Colors.green;
      case AdipoBleStatus.error:
      case AdipoBleStatus.permissionDenied:
      case AdipoBleStatus.disconnected:
        return Colors.red;
      default:
        return Theme.of(context).colorScheme.primary;
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Adipômetro')),
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: ValueListenableBuilder<BleConnectionInfo>(
            valueListenable: _ble.connectionInfo,
            builder: (context, info, _) {
              final conectado = info.isConnected;
              final ocupado = info.isBusy || _calibrando;
              final corStatus = _corStatus(context, info.status);

              return Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Card(
                    child: Padding(
                      padding: const EdgeInsets.all(16.0),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Row(
                            children: [
                              Icon(_iconeStatus(info.status), color: corStatus),
                              const SizedBox(width: 12),
                              Expanded(
                                child: Text(
                                  info.message,
                                  style: const TextStyle(fontSize: 16),
                                ),
                              ),
                              if (info.isBusy || _calibrando)
                                const SizedBox(
                                  width: 20,
                                  height: 20,
                                  child: CircularProgressIndicator(strokeWidth: 2),
                                ),
                            ],
                          ),
                          const SizedBox(height: 12),
                          Text(
                            info.zeroCalibrated
                                ? 'Zero calibrado${info.zeroAngleDeg == null ? '' : ': ${info.zeroAngleDeg!.toStringAsFixed(2)}°'}'
                                : 'Zero ainda não calibrado.',
                            style: TextStyle(
                              color: info.zeroCalibrated ? Colors.green : Colors.orange[800],
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                          const SizedBox(height: 16),
                          Row(
                            children: [
                              Expanded(
                                child: OutlinedButton.icon(
                                  onPressed: ocupado ? null : _reconectar,
                                  icon: const Icon(Icons.refresh),
                                  label: const Text('Reconectar'),
                                ),
                              ),
                              const SizedBox(width: 12),
                              Expanded(
                                child: FilledButton.icon(
                                  onPressed: conectado && !ocupado ? _calibrarZero : null,
                                  icon: const Icon(Icons.tune),
                                  label: const Text('Calibrar zero'),
                                ),
                              ),
                            ],
                          ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 32),
                  const Text(
                    'Escolha a metodologia:',
                    textAlign: TextAlign.center,
                    style: TextStyle(fontSize: 20),
                  ),
                  const SizedBox(height: 24),
                  ElevatedButton(
                    onPressed: ocupado ? null : () => _abrirMetodo('Pollock 3'),
                    child: const Text('Pollock 3 Dobras'),
                  ),
                  const SizedBox(height: 12),
                  ElevatedButton(
                    onPressed: ocupado ? null : () => _abrirMetodo('Pollock 7'),
                    child: const Text('Pollock 7 Dobras'),
                  ),
                  const SizedBox(height: 12),
                  ElevatedButton(
                    onPressed: ocupado ? null : () => _abrirMetodo('Petroski 4'),
                    child: const Text('Petroski 4 Dobras'),
                  ),
                  const Spacer(),
                  const Text(
                    'Fluxo recomendado: ligue o adipômetro, aguarde conectar, feche a pinça e calibre o zero antes das medições.',
                    textAlign: TextAlign.center,
                    style: TextStyle(fontSize: 13, color: Colors.grey),
                  ),
                ],
              );
            },
          ),
        ),
      ),
    );
  }
}
