import 'dart:io';

import 'package:flutter/services.dart';

/// I font veri dell'app al posto di quello dei test, in cui ogni carattere è
/// un quadrato: senza, i testi non hanno le misure reali.
Future<void> loadAppFonts() async {
  Future<void> load(String family, Future<ByteData> data) => (FontLoader(family)..addFont(data)).load();

  await load('Sora', rootBundle.load('assets/fonts/Sora-Variable.ttf'));
  await load('JetBrainsMono', rootBundle.load('assets/fonts/JetBrainsMono-Variable.ttf'));
  await load('PixelifySans', rootBundle.load('assets/fonts/PixelifySans-Variable.ttf'));
  await load('Silkscreen', rootBundle.load('assets/fonts/Silkscreen-Regular.ttf'));
  await load('VT323', rootBundle.load('assets/fonts/VT323-Regular.ttf'));

  // Le icone Material stanno nella cache dell'SDK, accanto a flutter_tester.
  final artifacts = File(Platform.resolvedExecutable).parent.parent.parent;
  final icons = File('${artifacts.path}/material_fonts/MaterialIcons-Regular.otf');
  if (icons.existsSync()) {
    await load('MaterialIcons', Future.value(ByteData.sublistView(icons.readAsBytesSync())));
  }
}
