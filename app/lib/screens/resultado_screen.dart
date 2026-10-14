import 'package:flutter/material.dart';

import '../models/composicao_corporal.dart';

class ResultadoScreen extends StatelessWidget {
  final String metodologia;
  final String sexo;
  final int idade;
  final List<double> medidas;
  final List<String> dobras;

  const ResultadoScreen({
    super.key,
    required this.metodologia,
    required this.sexo,
    required this.idade,
    required this.medidas,
    required this.dobras,
  });

  double calcularPercentual() => percentualGordura(
        metodologia: metodologia,
        sexo: sexo,
        idade: idade,
        medidas: medidas,
      );

  @override
  Widget build(BuildContext context) {
    double percentual = calcularPercentual();

    return Scaffold(
      appBar: AppBar(title: const Text('Resultado')),
      body: Padding(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('Metodologia: $metodologia',
                style: const TextStyle(fontSize: 16)),
            Text('Sexo: ${sexo == "M" ? "Masculino" : "Feminino"}',
                style: const TextStyle(fontSize: 16)),
            Text('Idade: $idade anos',
                style: const TextStyle(fontSize: 16)),
            const Divider(height: 32),
            const Text('Medidas realizadas:',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold)),
            const SizedBox(height: 8),
            ...List.generate(dobras.length, (i) =>
              Text('${dobras[i]}: ${medidas[i].toStringAsFixed(1)} mm',
                  style: const TextStyle(fontSize: 16)),
            ),
            const Divider(height: 32),
            Center(
              child: Column(
                children: [
                  const Text('% de Gordura',
                      style: TextStyle(fontSize: 20)),
                  const SizedBox(height: 8),
                  Text(
                    '${percentual.toStringAsFixed(1)}%',
                    style: const TextStyle(
                        fontSize: 56, fontWeight: FontWeight.bold,
                        color: Colors.blue),
                  ),
                ],
              ),
            ),
            const SizedBox(height: 32),
            SizedBox(
              width: double.infinity,
              child: ElevatedButton(
                onPressed: () => Navigator.popUntil(
                    context, (route) => route.isFirst),
                child: const Text('Nova Medição'),
              ),
            ),
          ],
        ),
      ),
    );
  }
}