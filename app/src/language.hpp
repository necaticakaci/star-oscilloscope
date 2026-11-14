// SPDX-License-Identifier: GPL-3.0-or-later
// The language of the interface: the translations built into the program (app/translations, docs/translating.md) and the
// user's choice among them, which takes effect when the program starts. Numbers keep their decimal point in every
// language, as the readouts on the screen write them.
#pragma once

#include <QList>
#include <QString>

namespace language {

struct Language {
    QString code;  // "en", "tr"
    QString name;  // in the language itself: "English", "Türkçe"
};

// English, the language of the sources, then the ones the program is translated to.
QList<Language> available();

// The choice kept in the settings: a code, or empty to follow the system.
QString chosen();
void choose(const QString& code);

// The language the program starts in: the chosen one, else the first of the system's languages it has, else English.
QString startup();

// Installs the translations of `code` for the program and, where Qt has them, for its own dialogs, in place of any
// installed before; "en" removes them. False when the program has no translation to that language.
bool use(const QString& code);

}  // namespace language
