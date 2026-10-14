import 'package:flutter_test/flutter_test.dart';

import 'package:adipometro/models/composicao_corporal.dart';
import 'package:adipometro/services/ble_service.dart';

// Valores esperados calculados à mão a partir das próprias equações do código:
// DC = a - b*soma + c*soma² - d*idade ; %G = ((4.95 / DC) - 4.50) * 100 (Siri).
void main() {
  void confere(String metodologia, String sexo, int idade, List<double> medidas,
      double dcEsperada, double gorduraEsperada) {
    final soma = medidas.reduce((a, b) => a + b);
    expect(
      densidadeCorporal(
          metodologia: metodologia, sexo: sexo, idade: idade, soma: soma),
      closeTo(dcEsperada, 1e-9),
    );
    expect(
      percentualGordura(
          metodologia: metodologia, sexo: sexo, idade: idade, medidas: medidas),
      closeTo(gorduraEsperada, 1e-3),
    );
  }

  group('percentualGordura', () {
    test('Pollock 3 masculino (soma 60, 30 anos)', () {
      confere('Pollock 3', 'M', 30, [10, 20, 30], 1.057816, 17.9453);
    });

    test('Pollock 3 feminino (soma 60, 25 anos)', () {
      confere('Pollock 3', 'F', 25, [15, 20, 25], 1.0447181, 23.8120);
    });

    test('Pollock 7 masculino (soma 100, 40 anos)', () {
      confere('Pollock 7', 'M', 40, [10, 12, 14, 16, 18, 15, 15], 1.0624706,
          15.8952);
    });

    test('Pollock 7 feminino (soma 100, 35 anos)', () {
      confere('Pollock 7', 'F', 35, [10, 12, 14, 16, 18, 15, 15], 1.0511392,
          20.9176);
    });

    test('Petroski 4 masculino (soma 50, 30 anos)', () {
      confere('Petroski 4', 'M', 30, [10, 12, 15, 13], 1.05943983, 17.2280);
    });

    test('Petroski 4 feminino (soma 80, 30 anos) - equação log10', () {
      // DC = 1,19547130 - 0,07513507*log10(80) - 0,00041072*30
      confere('Petroski 4', 'F', 30, [15, 20, 25, 20], 1.0401609006, 25.8879);
    });

    test('sem medidas retorna 0', () {
      expect(
        percentualGordura(
            metodologia: 'Pollock 3', sexo: 'M', idade: 30, medidas: []),
        0.0,
      );
    });

    test('metodologia desconhecida usa DC = 1.0 (45%)', () {
      expect(
        percentualGordura(
            metodologia: 'Outra', sexo: 'M', idade: 30, medidas: [10, 10]),
        closeTo(45.0, 1e-9),
      );
    });

    test('sexo diferente de M usa a equação feminina', () {
      expect(
        densidadeCorporal(
            metodologia: 'Pollock 3', sexo: 'x', idade: 25, soma: 60),
        densidadeCorporal(
            metodologia: 'Pollock 3', sexo: 'F', idade: 25, soma: 60),
      );
    });
  });

  group('BleService.normalizarDobra', () {
    test('minúsculas, sem acento, espaços viram _', () {
      expect(BleService.normalizarDobra('Supra Ilíaca'), 'supra_iliaca');
      expect(BleService.normalizarDobra('Axilar Média'), 'axilar_media');
      expect(BleService.normalizarDobra('Abdômen'), 'abdomen');
      expect(BleService.normalizarDobra('Tríceps'), 'triceps');
      expect(BleService.normalizarDobra('Suprailíaca'), 'suprailiaca');
    });

    test('remove espaços nas pontas e agrupa espaços internos', () {
      expect(BleService.normalizarDobra('  Coxa  '), 'coxa');
      expect(BleService.normalizarDobra('Axilar   Média'), 'axilar_media');
    });
  });
}
