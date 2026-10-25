import 'package:flutter_test/flutter_test.dart';
import 'package:megaplayer/player/library_browser.dart';

import 'fake_library.dart';

void main() {
  late FakeLibraryStore store;
  late LibraryBrowser browser;

  final library = [
    libraryItem('', 'giro.sid', platform: 'c64', format: 'PSID'),
    libraryItem('Amiga', 'intro.mod'),
    libraryItem('Amiga/Jesper Kyd', 'nebula.mod', title: 'Nebula'),
    libraryItem('Amiga/Jesper Kyd', 'heroes.med', format: 'MED'),
    libraryItem('Amiga/Moduli.lha/dentro', 'zippato.mod'),
    libraryItem('Console', 'sonic.vgz', platform: 'sega', format: 'VGM'),
  ];

  setUp(() {
    store = FakeLibraryStore()..rootPath = '/Musica';
    browser = LibraryBrowser(store);
  });

  test("all'avvio aggiorna la cartella e mostra cartelle e brani della radice", () {
    browser.restore();
    expect(store.scans, 1);
    expect(browser.scanning, isTrue);

    store.finishScan(library);
    browser.poll();
    expect(browser.scanning, isFalse);
    expect(browser.folders.map((f) => (f.name, f.count)), [('Amiga', 4), ('Console', 1)]);
    expect(browser.items.map((i) => i.name), ['giro.sid']);
    expect(browser.total, 1);
    expect(browser.facets.platforms.map((f) => f.id), containsAll(['amiga', 'c64', 'sega']));
  });

  test('si naviga per cartelle, archivi compresi', () {
    browser.restore();
    store.finishScan(library);
    browser.poll();

    browser.openSubfolder('Amiga');
    expect(browser.path, ['Amiga']);
    expect(browser.folders.map((f) => f.name), ['Jesper Kyd', 'Moduli.lha']);
    expect(browser.items.map((i) => i.name), ['intro.mod']);

    browser.openSubfolder('Moduli.lha');
    browser.openSubfolder('dentro');
    expect(browser.items.map((i) => i.name), ['zippato.mod']);
    expect(browser.allTracks(folder: 'Amiga').map((t) => t.fileName), hasLength(4));

    browser.openFolder('');
    expect(browser.path, isEmpty);
  });

  test('una ricerca o un filtro mostra tutti i brani da lì in giù, senza cartelle', () async {
    browser.restore();
    store.finishScan(library);
    browser.poll();
    browser.openSubfolder('Amiga');

    browser.toggleFormat('MED');
    expect(browser.browsing, isFalse);
    expect(browser.folders, isEmpty);
    expect(browser.items.map((i) => i.name), ['heroes.med']);
    browser.toggleFormat('MED');

    browser.setText('nebula');
    expect(browser.items.map((i) => i.name), isNot(['nebula.mod']), reason: 'la ricerca aspetta che si smetta di scrivere');
    await Future<void>.delayed(const Duration(milliseconds: 250));
    expect(browser.items.map((i) => i.name), ['nebula.mod']);

    browser.clearFilters();
    expect(browser.browsing, isTrue);
    expect(browser.path, ['Amiga'], reason: 'la cartella resta');
  });

  test('i risultati arrivano a pagine', () {
    browser.restore();
    store.finishScan([for (var i = 0; i < 450; i++) libraryItem('Tanti', 'brano$i.mod')]);
    browser.poll();
    browser.toggleFormat('ProTracker');
    expect(browser.items, hasLength(LibraryBrowser.pageSize));
    expect(browser.total, 450);
    browser.loadMore();
    browser.loadMore();
    browser.loadMore();
    expect(browser.items, hasLength(450));
  });

  test('cambiare cartella durante una scansione aspetta che si fermi', () {
    browser.restore();
    browser.setRoot('/Altra');
    expect(store.cancelled, isTrue);
    expect(store.rootPath, '/Musica');
    expect(browser.scanning, isTrue);

    store.finishScan();
    browser.poll();
    expect(store.rootPath, '/Altra');
    expect(browser.root, '/Altra');
    expect(store.scans, 2);
  });

  test('senza cartella scelta non parte nessuna scansione', () {
    store.rootPath = '';
    browser.restore();
    expect(store.scans, 0);
    browser.rescan();
    expect(store.scans, 0);
    expect(browser.error, isNull);
  });

  test('senza database non succede niente', () {
    final alone = LibraryBrowser(null)..restore();
    alone.setRoot('/Musica');
    alone.poll();
    expect(alone.available, isFalse);
    expect(alone.items, isEmpty);
    expect(alone.allTracks(), isEmpty);
  });
}
