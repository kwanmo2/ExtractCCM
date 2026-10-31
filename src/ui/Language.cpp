#include "ui/Language.h"

#include <QCoreApplication>
#include <QHash>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {

struct Entry {
    const char* source;
    const char* korean;
};
const Entry kKorean[] = {
#include "ui/Translations_ko.inc"
};

// Looks up the source text in a fixed dictionary, regardless of the translation context.
class DictionaryTranslator : public QTranslator {
public:
    DictionaryTranslator() {
        for (const auto& e : kKorean) map_.insert(QString::fromUtf8(e.source), QString::fromUtf8(e.korean));
    }
    QString translate(const char*, const char* sourceText, const char* = nullptr, int = -1) const override {
        return map_.value(QString::fromUtf8(sourceText));  // empty = untranslated, Qt falls back to the source
    }
    bool isEmpty() const override { return false; }

private:
    QHash<QString, QString> map_;
};

QTranslator* g_translator = nullptr;
Language g_current = Language::English;

constexpr char kOrg[] = "ExtractCCM";
constexpr char kApp[] = "ExtractCCM";

}  // namespace

Language savedLanguage() {
    QSettings settings(kOrg, kApp);
    const QString v = settings.value("language").toString();
    if (v == "ko") return Language::Korean;
    if (v == "en") return Language::English;
    return QLocale::system().language() == QLocale::Korean ? Language::Korean : Language::English;
}

void saveLanguage(Language lang) {
    QSettings settings(kOrg, kApp);
    settings.setValue("language", lang == Language::Korean ? "ko" : "en");
}

void installLanguage(Language lang) {
    g_current = lang;
    if (g_translator) {
        QCoreApplication::removeTranslator(g_translator);
        delete g_translator;
        g_translator = nullptr;
    }
    if (lang == Language::Korean) {
        g_translator = new DictionaryTranslator;
        QCoreApplication::installTranslator(g_translator);
    }
}

Language currentLanguage() { return g_current; }

QString languageName(Language lang) { return lang == Language::Korean ? QStringLiteral("한국어") : QStringLiteral("English"); }
