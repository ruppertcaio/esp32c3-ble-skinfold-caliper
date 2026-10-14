import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:adipometro/screens/resultado_screen.dart';

void main() {
  testWidgets('ResultadoScreen mostra medidas e % de gordura', (tester) async {
    await tester.pumpWidget(const MaterialApp(
      home: ResultadoScreen(
        metodologia: 'Pollock 3',
        sexo: 'M',
        idade: 30,
        medidas: [10, 20, 30],
        dobras: ['Peitoral', 'Abdômen', 'Coxa'],
      ),
    ));

    expect(find.text('Metodologia: Pollock 3'), findsOneWidget);
    expect(find.text('Sexo: Masculino'), findsOneWidget);
    expect(find.text('Peitoral: 10.0 mm'), findsOneWidget);
    expect(find.text('Coxa: 30.0 mm'), findsOneWidget);
    // Pollock 3 M, soma 60, 30 anos -> 17.945% (ver composicao_corporal_test).
    expect(find.text('17.9%'), findsOneWidget);
  });
}
