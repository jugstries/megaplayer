/// Un effetto della catena MegaSound e il tratto di manopola in cui cresce,
/// con gli stessi valori di src/dsp/MegaSound.cpp.
class MegaSoundStage {
  const MegaSoundStage(this.name, this.short, this.from, this.fullAt);

  final String name;
  final String short;
  final double from;
  final double fullAt;

  /// Quanto pesa l'effetto alla posizione [amount] della manopola, 0..1.
  double level(double amount) {
    if (amount <= from) return 0;
    return ((amount - from) / (fullAt - from)).clamp(0.0, 1.0);
  }
}

const megaSoundStages = [
  MegaSoundStage('Compressore', 'COMP', 0, 1),
  // Lo Spazio è pieno appena la catena è tutta miscelata (kFullMixAt).
  MegaSoundStage('Spazio', 'SPAZIO', 0, 0.12),
  // Fino a metà corsa la saturazione, poi il corpo sui bassi.
  MegaSoundStage('Saturazione', 'SAT', 0, 1),
  MegaSoundStage('Brillantezza', 'EXC', 0, 0.7),
  MegaSoundStage('Riverbero', 'REV', 0.1, 1),
  MegaSoundStage('Clipper', 'CLIP', 0.7, 1),
];

/// Il carattere del suono a una posizione della manopola.
String megaSoundCharacter(double amount) {
  if (amount <= 0) return 'Spenta';
  if (amount < 0.25) return 'Leggera';
  if (amount < 0.5) return 'Calda';
  if (amount < 0.75) return 'Grintosa';
  return 'Mega';
}
