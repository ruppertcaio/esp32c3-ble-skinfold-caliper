import 'dart:async';

import 'package:flutter/material.dart';

import '../services/ble_service.dart';
import 'resultado_screen.dart';

class MedicaoScreen extends StatefulWidget {
  final String metodologia;
  final String sexo;
  final int idade;

  const MedicaoScreen({
    super.key,
    required this.metodologia,
    required this.sexo,
    required this.idade,
  });

  @override
  State<MedicaoScreen> createState() => _MedicaoScreenState();
}

class _MedicaoScreenState extends State<MedicaoScreen> {
  final BleService _ble = BleService.instance;

  StreamSubscription<Map<String, dynamic>>? _respostaSub;

  int indiceAtual = 0;
  final List<double> medidas = [];

  bool conectando = true;
  bool erroConexao = false;
  bool aguardandoMedida = false;
  String? mensagem;

  List<String> get dobras {
    switch (widget.metodologia) {
      case 'Pollock 3':
        return widget.sexo == 'M'
            ? ['Peitoral', 'Abdômen', 'Coxa']
            : ['Tríceps', 'Suprailíaca', 'Coxa'];
      case 'Pollock 7':
        return [
          'Peitoral',
          'Axilar Média',
          'Tríceps',
          'Subescapular',
          'Abdômen',
          'Suprailíaca',
          'Coxa',
        ];
      case 'Petroski 4':
        return widget.sexo == 'M'
            ? ['Subescapular', 'Tríceps', 'Suprailíaca', 'Panturrilha']
            : ['Axilar Média', 'Suprailíaca', 'Coxa', 'Panturrilha'];
      default:
        return [];
    }
  }

  @override
  void initState() {
    super.initState();
    _prepararBle();
  }

  Future<void> _prepararBle() async {
    setState(() {
      conectando = true;
      erroConexao = false;
      mensagem = null;
    });

    final sucesso = await _ble.garantirConectado();

    if (!mounted) {
      return;
    }

    setState(() {
      conectando = false;
      erroConexao = !sucesso;
      mensagem = sucesso
          ? 'Adipômetro pronto. Posicione a pinça e pressione Medir.'
          : 'Não foi possível conectar ao adipômetro.';
    });

    if (sucesso) {
      _escutarRespostas();
    }
  }

  void _escutarRespostas() {
    _respostaSub?.cancel();

    _respostaSub = _ble.escutarResposta().listen((resposta) async {
      if (!mounted || dobras.isEmpty) {
        return;
      }

      final cmd = resposta['cmd']?.toString();
      final temDobra = (resposta['dobra']?.toString() ?? '').isNotEmpty;

      if (!aguardandoMedida && !temDobra) {
        return;
      }

      if (cmd != null && cmd != 'medir' && !temDobra) {
        return;
      }

      final ok = resposta['ok'] as bool? ?? true;
      if (!ok) {
        setState(() {
          aguardandoMedida = false;
          mensagem = 'Erro do dispositivo: ${resposta['erro'] ?? 'desconhecido'}';
        });
        return;
      }

      final dobraEsperada = BleService.normalizarDobra(dobras[indiceAtual]);
      final dobraRecebida = BleService.normalizarDobra(
        resposta['dobra']?.toString() ?? '',
      );

      if (dobraEsperada != dobraRecebida) {
        setState(() {
          aguardandoMedida = false;
          mensagem = 'Resposta ignorada: esperava $dobraEsperada, recebeu $dobraRecebida.';
        });
        return;
      }

      final mm = resposta['mm'] as double? ?? 0.0;
      if (mm <= 0.0) {
        setState(() {
          aguardandoMedida = false;
          mensagem = 'Medida inválida recebida: ${mm.toStringAsFixed(2)} mm.';
        });
        return;
      }

      final terminou = indiceAtual + 1 >= dobras.length;

      setState(() {
        medidas.add(mm);
        aguardandoMedida = false;
        mensagem = '${dobras[indiceAtual]}: ${mm.toStringAsFixed(1)} mm';

        if (!terminou) {
          indiceAtual++;
        }
      });

      if (terminou) {
        await _finalizarMedicao();
      }
    });
  }

  Future<void> _enviarMedicaoAtual() async {
    if (aguardandoMedida || dobras.isEmpty) {
      return;
    }

    setState(() {
      aguardandoMedida = true;
      mensagem = 'Aguardando resposta do adipômetro...';
    });

    try {
      await _ble.enviarDobra(dobras[indiceAtual]);
    } catch (e) {
      if (!mounted) {
        return;
      }
      setState(() {
        aguardandoMedida = false;
        mensagem = 'Falha ao enviar comando BLE.';
      });
    }
  }

  Future<void> _finalizarMedicao() async {
    await _respostaSub?.cancel();

    if (!mounted) {
      return;
    }

    Navigator.pushReplacement(
      context,
      MaterialPageRoute(
        builder: (_) => ResultadoScreen(
          metodologia: widget.metodologia,
          sexo: widget.sexo,
          idade: widget.idade,
          medidas: medidas,
          dobras: dobras,
        ),
      ),
    );
  }

  @override
  void dispose() {
    _respostaSub?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (conectando) {
      return Scaffold(
        appBar: AppBar(title: Text(widget.metodologia)),
        body: const Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              CircularProgressIndicator(),
              SizedBox(height: 16),
              Text('Conectando ao dispositivo...'),
            ],
          ),
        ),
      );
    }

    if (erroConexao) {
      return Scaffold(
        appBar: AppBar(title: Text(widget.metodologia)),
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              const Icon(Icons.bluetooth_disabled, size: 64, color: Colors.red),
              const SizedBox(height: 16),
              const Text(
                'Dispositivo não encontrado',
                style: TextStyle(fontSize: 18),
              ),
              const SizedBox(height: 8),
              const Text(
                'Verifique se o Adipometro-C3 está ligado e anunciando via BLE.',
                textAlign: TextAlign.center,
                style: TextStyle(color: Colors.grey),
              ),
              const SizedBox(height: 24),
              ElevatedButton(
                onPressed: _prepararBle,
                child: const Text('Tentar novamente'),
              ),
            ],
          ),
        ),
      );
    }

    if (dobras.isEmpty) {
      return Scaffold(
        appBar: AppBar(title: Text(widget.metodologia)),
        body: const Center(child: Text('Metodologia inválida.')),
      );
    }

    return Scaffold(
      appBar: AppBar(title: Text(widget.metodologia)),
      body: Padding(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text(
              'Dobra ${indiceAtual + 1} de ${dobras.length}',
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 16, color: Colors.grey),
            ),
            const SizedBox(height: 16),
            Text(
              dobras[indiceAtual],
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 32, fontWeight: FontWeight.bold),
            ),
            const SizedBox(height: 8),
            const Text(
              'Posicione a pinça na dobra selecionada e pressione Medir.',
              textAlign: TextAlign.center,
              style: TextStyle(fontSize: 16),
            ),
            const SizedBox(height: 8),
            ValueListenableBuilder<BleConnectionInfo>(
              valueListenable: _ble.connectionInfo,
              builder: (context, info, _) {
                if (info.zeroCalibrated) {
                  return const SizedBox.shrink();
                }
                return const Text(
                  'Atenção: calibre o zero na tela inicial para obter medidas corretas.',
                  textAlign: TextAlign.center,
                  style: TextStyle(fontSize: 13, color: Colors.orange),
                );
              },
            ),
            const SizedBox(height: 32),
            ElevatedButton.icon(
              onPressed: aguardandoMedida ? null : _enviarMedicaoAtual,
              icon: const Icon(Icons.straighten),
              label: Text(aguardandoMedida ? 'Aguardando...' : 'Medir'),
            ),
            const SizedBox(height: 24),
            if (aguardandoMedida)
              const Center(child: CircularProgressIndicator()),
            if (mensagem != null) ...[
              const SizedBox(height: 24),
              Text(
                mensagem!,
                textAlign: TextAlign.center,
                style: const TextStyle(fontSize: 14, color: Colors.grey),
              ),
            ],
            if (medidas.isNotEmpty) ...[
              const SizedBox(height: 32),
              const Text(
                'Medidas já registradas:',
                style: TextStyle(fontWeight: FontWeight.bold),
              ),
              const SizedBox(height: 8),
              ...List.generate(medidas.length, (i) {
                return Text('${dobras[i]}: ${medidas[i].toStringAsFixed(1)} mm');
              }),
            ],
          ],
        ),
      ),
    );
  }
}
