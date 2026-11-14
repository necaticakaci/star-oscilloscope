# Translating

The interface is written in English and translated with Qt's own tools (Qt Linguist). Turkish is complete; other
languages are welcome.

## 1. How it is put together

- **Source texts:** every text of the interface is in `tr()` or `QCoreApplication::translate()` in the code, in English.
- **Translation files:** `app/translations/star-oscilloscope_<lang>.ts`, e.g. `star-oscilloscope_tr.ts`. XML, kept in the
  repository, edited by hand or with Qt Linguist.
- **Build:** CMake makes a `.qm` from every `.ts` (`lrelease`) and embeds it in the program (`:/i18n`); no separate file
  is shipped. The `STAROSC_TS_FILES` list in `app/CMakeLists.txt` names the languages.
- **Qt's own texts:** message box buttons and the file and colour dialogs are translated by Qt's `qtbase_<lang>.qm`. The
  program looks for it in `translations/` beside itself first (the packages put it there), then where Qt is installed.
  Without it those dialogs stay in English and the rest of the program is still translated.
- **Key names in shortcuts:** some of Qt's translations spell out the modifier keys (Turkish: "Kontrol", "Üst Karakter");
  keyboards print Ctrl, Shift, Alt. The program's translation gives these names itself in the `QShortcut` context
  (`language.cpp`, `kKeyNames`); its translator is installed after Qt's, and the one installed last is asked first.
- **Choosing a language:** View → Language: System Default, English, and every language the program carries. The choice
  is saved (`appearance/language`) and takes effect at the next start. System Default takes the first of the system's
  languages the program knows. Language names are written in their own language, so the menu can be found whatever
  language it is in.
- **Numbers** use a decimal point in every language (`1.234 V`), as the on-screen readouts do (`main.cpp`).

## 2. Tools

Qt's Linguist tools: `lupdate` (collects the texts from the sources), `lrelease` (makes the `.qm`), `linguist` (the
translation window). Without them CMake warns and builds the program in English only.

| Environment | Packages |
|---|---|
| Windows (MSYS2 UCRT64) | `mingw-w64-ucrt-x86_64-qt6-tools`; `mingw-w64-ucrt-x86_64-qt6-declarative` (`lupdate` needs `Qt6Qml.dll` and fails to start with `0xc0000139` without it); `mingw-w64-ucrt-x86_64-qt6-translations` (Qt's own translations) |
| Debian, Ubuntu | `qt6-l10n-tools` (the tools), `qt6-tools-dev` (the CMake module), `qt6-translations-l10n` (Qt's own translations) |

## 3. Workflow

1. **Adding or changing a text:** `tr("…")` in the code, then gather the texts into the `.ts` files:
   ```bash
   cmake --build build/linux --target update_translations
   ```
   `lupdate` adds new texts as "unfinished" and drops those gone from the sources (`-no-obsolete`). It writes no line
   numbers (`-locations none`), so the `.ts` files do not change as code moves.
2. **Translating:** `linguist app/translations/star-oscilloscope_<lang>.ts`, or a text editor; a translated text loses
   its "unfinished" mark.
3. **Checking:** the GUI tests
   - `translationsAreCompleteAndKeepTheirMarks` fails on an untranslated text, a lost `%1` or a lost shortcut letter
     (`&`), and lists them;
   - `menuLettersAreUniqueInEveryLanguage` checks that the shortcut letters of every menu are unique in every language
     (Alt+letter);
   - `interfaceSpeaksTurkish` checks that the Turkish translation loads and changes the texts.
4. **A new language:** add `translations/star-oscilloscope_<code>.ts` to `STAROSC_TS_FILES` and run
   `update_translations`. The Language menu finds the language in the embedded files by itself; the packages pick up
   Qt's own translation for it.

## 4. Rules in the code

- **Context:** every class that calls `tr()` needs `Q_OBJECT` or `Q_DECLARE_TR_FUNCTIONS(Context)`. Otherwise `lupdate`
  files the text under the class's name and the program looks under the base class's name, and the translation is not
  found. The gain wizard's pages share the `GainWizard` context for this reason.
- **Whole sentences:** a sentence is never assembled from pieces; use whole sentences with `%1`, `%2`. Word order differs
  between languages (Turkish: "CH%1, %2 … %3 mV kademesinde %4 mV okuyor").
- **The core's texts:** `core/` is C++ without Qt and cannot be translated. It returns a state the interface puts into a
  sentence (`starosc::ZeroProblem`). For the core's name lists the interface has translated tables: `parameterName()`,
  `shortParameterName()`, `windowName()`.
- **Files stay English:** the settings keep the measurement selection by English names, CSV exports have English column
  names. Changing the language breaks no settings, and other programs read the files.
- **Width:** a translation can be longer than the English. Code that reserves room on the screen measures the text; it
  never reserves fixed pixels (e.g. the "RUN" and "Trig'd" boxes of the top bar, `ScopeView::stateWidth`,
  `triggeredWidth`).
- **Not translated:** units (V, Hz, Sa/s, div), channel names (CH1, R1), V/div, proper names such as FFT windows (Hann,
  Blackman). Technical error messages from the device stay English, shown inside a translated sentence
  ("Acquisition stopped: …").
- **Percent:** in Turkish `%` comes before the number, but `%20` looks like a placeholder to Qt. Where a number is
  written in the text use words; a percent sign before a `%1` placeholder is written `%%1` (giving `%0.12`).

## 5. Turkish terms

| English | Turkish | Note |
|---|---|---|
| Acquire, acquisition | Edinim | Menu and toolbar |
| Run / Stop / Single | Başlat / Durdur / Tek | ÇALIŞ / DUR, BEKLE in the top bar |
| Force Trigger | Tetiği Zorla | |
| Autoset | Otomatik Ayar | |
| Trigger, Trig'd, Auto | Tetik, Tetikli, Oto | |
| Timebase | Zaman tabanı | |
| Coupling | Kuplaj | |
| Probe | Prob | |
| Scale / Position | Ölçek / Konum | |
| Zoom | Zoom | Usual in technical Turkish, short on screen |
| Cursors | İmleçler | |
| Measurements | Ölçümler | |
| Duty cycle | Doluluk oranı | |
| Peak-to-peak | Tepeden tepeye | Short name Vpp |
| Rise / fall time | Yükselme / düşme süresi | |
| Overshoot / Undershoot | Aşma / Alt aşma | |
| Top / Base | Üst / Taban | |
| Stage | Kademe | A gain stage |
| Gain / Zero | Kazanç / Sıfır | |
| Generator | Jeneratör | |
| Sweep | Tarama | |
| Span | Açıklık | Spectrum |
| Bin | Adım | "bin" means 1000 in Turkish |
| Fundamental / Harmonic | Temel bileşen / Harmonik | |
| Window (FFT) | Pencere | Proper names stay; Rectangular → Dikdörtgen, Flat top → Düz tepe |
| Peak hold | Tepe tutma | |
| CLIP / ALIAS | KIRP / ALIAS | |
| Reference | Referans | R1, R2 |
| Math | Matematik | MAT on the front panel |
| Display | Görüntü | The display setting; the View menu is Görünüm |

## 6. Status

**Turkish:** every text translated (635). The program was checked in Turkish by eye: the main window, both control
panels, the Spectrum and Generator tabs, the menus, the calibration and measurement selection windows. Qt's own buttons
("Tamam", "İptal") come from `qtbase_tr`. The gain wizard and the error dialogs were checked by the tests only.
