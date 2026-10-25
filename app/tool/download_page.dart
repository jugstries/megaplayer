// Scrive la pagina per scaricare megaplayer (index.html, da mettere in
// projects/megaplayer sul sito) a partire dal manifesto firmato: versione,
// data, dimensione e novità dell'ultima release. Pagina autonoma, senza
// risorse esterne, con i colori della skin moderna.
//
// Uso (in app/): dart run tool/download_page.dart --manifest updates/windows.json --out index.html

import 'dart:convert';
import 'dart:io';

import 'package:megaplayer/skins/skin_text.dart';
import 'package:megaplayer/update/update_manifest.dart';

Future<void> main(List<String> arguments) async {
  final options = <String, String>{};
  for (var i = 0; i + 1 < arguments.length; i += 2) {
    options[arguments[i].replaceFirst('--', '')] = arguments[i + 1];
  }
  String option(String name) => options[name] ?? (throw ArgumentError('manca --$name'));

  final manifest =
      UpdateManifest.fromJson(jsonDecode(await File(option('manifest')).readAsString()) as Map<String, dynamic>);
  await File(option('out')).writeAsString(page(manifest));
}

String page(UpdateManifest windows) {
  const escape = HtmlEscape();
  final notes = windows.notes
      .split('\n')
      .where((line) => line.trim().isNotEmpty)
      .map((line) => '<li>${escape.convert(line.replaceFirst(RegExp(r'^\s*[•\-*]\s*'), ''))}</li>')
      .join('\n          ');
  return '''<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>megaplayer</title>
  <meta name="description" content="Il player per i formati audio esotici: moduli Amiga, SID, chip delle console, MIDI e AdLib.">
  <style>
    :root {
      --background: #0C0B10; --surface: #15131B; --border: #2A2633;
      --text: #F2EFF7; --soft: #CFC9DA; --faint: #8C8699;
      --gradient: linear-gradient(135deg, #FF8A3D, #EC4F8F, #8B5CF6);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0; background: var(--background); color: var(--text);
      font: 16px/1.6 system-ui, -apple-system, "Segoe UI", sans-serif;
    }
    main { max-width: 720px; margin: 0 auto; padding: 56px 16px 72px; }
    .brand { display: flex; align-items: center; gap: 14px; }
    .logo { width: 52px; height: 52px; border-radius: 16px; background: var(--gradient); }
    h1 { margin: 0; font-size: 32px; letter-spacing: -0.5px; }
    .lead { color: var(--soft); font-size: 18px; margin: 18px 0 32px; }
    .download {
      display: inline-flex; flex-direction: column; padding: 16px 26px; border-radius: 16px;
      background: var(--gradient); color: var(--background); text-decoration: none; font-weight: 700;
    }
    .download small { font-weight: 500; opacity: 0.8; }
    section {
      margin-top: 32px; padding: 22px 24px; border-radius: 20px;
      background: var(--surface); border: 1px solid var(--border);
    }
    h2 { margin: 0 0 10px; font-size: 13px; letter-spacing: 1px; text-transform: uppercase; color: var(--faint); }
    ul { margin: 0; padding-left: 20px; color: var(--soft); }
    p { margin: 0 0 10px; color: var(--soft); }
    p:last-child { margin-bottom: 0; }
    b { color: var(--text); }
  </style>
</head>
<body>
  <main>
    <div class="brand"><div class="logo"></div><h1>megaplayer</h1></div>
    <p class="lead">Il player per i formati audio esotici: moduli Amiga, SID del Commodore 64, chip delle console,
      MIDI e AdLib, anche dentro archivi e immagini disco.</p>
    <a class="download" href="updates/${escape.convert(windows.file)}">
      Scarica per Windows
      <small>versione ${escape.convert(windows.version)} · ${escape.convert(formatDate(windows.date))} · ${escape.convert(formatMegabytes(windows.size))}</small>
    </a>
    <section>
      <h2>Novità della ${escape.convert(windows.version)}</h2>
      <ul>
          $notes
      </ul>
    </section>
    <section>
      <h2>Come si installa</h2>
      <p>Serve Windows 10 o 11 a 64 bit. L'installer non chiede i diritti di amministratore e mette megaplayer nel
        menu Start.</p>
      <p>L'app non è ancora firmata da un'autorità riconosciuta, quindi Windows può avvisare che proviene da un
        autore sconosciuto: clicca <b>Ulteriori informazioni</b> e poi <b>Esegui comunque</b>.</p>
      <p>Poi megaplayer si aggiorna da solo: quando esce una versione nuova te la propone all'avvio.</p>
    </section>
  </main>
</body>
</html>
''';
}
