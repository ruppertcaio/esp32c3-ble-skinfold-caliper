import 'package:flutter/material.dart';
import 'medicao_screen.dart';

class DadosScreen extends StatefulWidget {
  final String metodologia;
  const DadosScreen({super.key, required this.metodologia});

  @override
  State<DadosScreen> createState() => _DadosScreenState();
}

class _DadosScreenState extends State<DadosScreen> {
  String? sexo;
  final idadeController = TextEditingController();

  @override
  void dispose() {
    idadeController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text(widget.metodologia)),
      body: Padding(
        padding: const EdgeInsets.all(24.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('Sexo:', style: TextStyle(fontSize: 18)),
            const SizedBox(height: 8),
            Row(
              children: [
                ElevatedButton(
                  onPressed: () => setState(() => sexo = 'M'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: sexo == 'M' ? Colors.blue : null,
                  ),
                  child: const Text('Masculino'),
                ),
                const SizedBox(width: 12),
                ElevatedButton(
                  onPressed: () => setState(() => sexo = 'F'),
                  style: ElevatedButton.styleFrom(
                    backgroundColor: sexo == 'F' ? Colors.blue : null,
                  ),
                  child: const Text('Feminino'),
                ),
              ],
            ),
            const SizedBox(height: 24),
            const Text('Idade:', style: TextStyle(fontSize: 18)),
            const SizedBox(height: 8),
            TextField(
              controller: idadeController,
              keyboardType: TextInputType.number,
              decoration: const InputDecoration(
                border: OutlineInputBorder(),
                hintText: 'Digite a idade',
              ),
            ),
            const SizedBox(height: 32),
            SizedBox(
              width: double.infinity,
              child: ElevatedButton(
                onPressed: () {
                  final idade = int.tryParse(idadeController.text);
                  if (sexo == null || idade == null || idade <= 0) {
                    ScaffoldMessenger.of(context).showSnackBar(
                      const SnackBar(content: Text('Informe sexo e idade válidos.')),
                    );
                    return;
                  }

                  Navigator.push(context, MaterialPageRoute(
                    builder: (_) => MedicaoScreen(
                      metodologia: widget.metodologia,
                      sexo: sexo!,
                      idade: idade,
                    ),
                  ));
                },
                child: const Text('Iniciar Medição'),
              ),
            ),
          ],
        ),
      ),
    );
  }
}