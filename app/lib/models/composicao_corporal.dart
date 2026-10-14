/// Cálculo de composição corporal a partir das dobras cutâneas (Dart puro).
///
/// Densidade corporal (DC) pela equação de cada metodologia e conversão
/// para % de gordura pela equação de Siri: ((4.95 / DC) - 4.50) * 100.
/// `sexo` diferente de 'M' usa a equação feminina.
library;

import 'dart:math' as math;

double densidadeCorporal({
  required String metodologia,
  required String sexo,
  required int idade,
  required double soma,
}) {
  switch (metodologia) {
    case 'Pollock 3':
      if (sexo == 'M') {
        return 1.10938 - (0.0008267 * soma) + (0.0000016 * soma * soma) - (0.0002574 * idade);
      }
      return 1.0994921 - (0.0009929 * soma) + (0.0000023 * soma * soma) - (0.0001392 * idade);
    case 'Pollock 7':
      if (sexo == 'M') {
        return 1.112 - (0.00043499 * soma) + (0.00000055 * soma * soma) - (0.00028826 * idade);
      }
      return 1.097 - (0.00046971 * soma) + (0.00000056 * soma * soma) - (0.00012828 * idade);
    case 'Petroski 4':
      if (sexo == 'M') {
        return 1.10726863 - (0.00081201 * soma) + (0.00000212 * soma * soma) - (0.00041761 * idade);
      }
      // Petroski (1995), mulheres: axilar média + supra-ilíaca + coxa + panturrilha medial.
      return 1.19547130 - (0.07513507 * math.log(soma) / math.ln10) - (0.00041072 * idade);
    default:
      return 1.0;
  }
}

/// % de gordura (Siri). Retorna 0.0 se não houver medidas.
double percentualGordura({
  required String metodologia,
  required String sexo,
  required int idade,
  required List<double> medidas,
}) {
  if (medidas.isEmpty) return 0.0;
  final soma = medidas.reduce((a, b) => a + b);
  final dc = densidadeCorporal(
    metodologia: metodologia,
    sexo: sexo,
    idade: idade,
    soma: soma,
  );
  return ((4.95 / dc) - 4.50) * 100;
}
