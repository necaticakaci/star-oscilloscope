// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.hpp"

#include <algorithm>
#include <memory>

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace language {
namespace {

constexpr auto kKey = "appearance/language";
const QString kPrefix = QStringLiteral("star-oscilloscope_");

std::unique_ptr<QTranslator> program;
std::unique_ptr<QTranslator> qt;

// Modifier keys as keyboards and the menus' shortcuts write them. Qt's own translations spell them out in some
// languages (Turkish: "Kontrol", "Üst Karakter", "Seçenek"); the program's translations give them back, and are asked
// before Qt's (use()).
[[maybe_unused]] constexpr const char* kKeyNames[] = {
    QT_TRANSLATE_NOOP("QShortcut", "Ctrl"), QT_TRANSLATE_NOOP("QShortcut", "Shift"),
    QT_TRANSLATE_NOOP("QShortcut", "Alt"), QT_TRANSLATE_NOOP("QShortcut", "Meta")};

QString nativeName(const QString& code)
{
    QString name = QLocale(code).nativeLanguageName();
    if (name.isEmpty())
        return code;
    name[0] = name[0].toUpper();
    return name;
}

void remove(std::unique_ptr<QTranslator>& translator)
{
    if (translator)
        QCoreApplication::removeTranslator(translator.get());
    translator.reset();
}

}  // namespace

QList<Language> available()
{
    QList<Language> out{{QStringLiteral("en"), QStringLiteral("English")}};
    const QStringList files = QDir(QStringLiteral(":/i18n")).entryList({kPrefix + "*.qm"}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        const QString code = file.mid(kPrefix.size(), file.size() - kPrefix.size() - 3);
        out.push_back({code, nativeName(code)});
    }
    return out;
}

QString chosen() { return QSettings().value(kKey).toString(); }

void choose(const QString& code) { QSettings().setValue(kKey, code); }

QString startup()
{
    const QList<Language> languages = available();
    const auto has = [&](const QString& code) {
        return std::any_of(languages.begin(), languages.end(), [&](const Language& l) { return l.code == code; });
    };
    if (const QString code = chosen(); has(code))
        return code;
    for (const QString& ui : QLocale::system().uiLanguages()) {
        const QString code = QLocale(ui).name().section('_', 0, 0);
        if (has(code))
            return code;
    }
    return QStringLiteral("en");
}

bool use(const QString& code)
{
    remove(program);
    remove(qt);
    if (code == QLatin1String("en"))
        return true;
    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QStringLiteral(":/i18n/") + kPrefix + code))
        return false;

    // Qt's own texts (the buttons of message boxes, the file and colour dialogs): next to the program when it is
    // installed with them, else where Qt keeps them. Installed first: the translator installed last is asked first,
    // so the program's own keeps the few of Qt's texts it gives itself (kKeyNames).
    auto own = std::make_unique<QTranslator>();
    const QString file = QStringLiteral("qtbase_") + code;
    if (own->load(file, QCoreApplication::applicationDirPath() + QStringLiteral("/translations")) ||
        own->load(file, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QCoreApplication::installTranslator(own.get());
        qt = std::move(own);
    }
    QCoreApplication::installTranslator(translator.get());
    program = std::move(translator);
    return true;
}

}  // namespace language
