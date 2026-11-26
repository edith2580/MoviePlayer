#include "SmiSubtitles.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <utility>

namespace {

constexpr qint64 kMaxSmiFileSize = 64 * 1024 * 1024;
constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

// In the encodings SMI files use (UTF-8, CP949, Shift-JIS), the trailing bytes of a
// multi-byte character never equal '<' or '>', so tags can be found byte by byte.
char ToLowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

size_t FindNoCase(std::string_view text, std::string_view lowerNeedle, size_t from = 0) {
    if (lowerNeedle.size() > text.size()) {
        return std::string_view::npos;
    }
    for (size_t i = from; i + lowerNeedle.size() <= text.size(); ++i) {
        size_t matched = 0;
        while (matched < lowerNeedle.size() && ToLowerAscii(text[i + matched]) == lowerNeedle[matched]) {
            ++matched;
        }
        if (matched == lowerNeedle.size()) {
            return i;
        }
    }
    return std::string_view::npos;
}

// Finds "<name" as a whole tag name, so that looking for <p doesn't match <pre.
size_t FindTag(std::string_view text, std::string_view lowerOpening, size_t from = 0) {
    while (true) {
        const size_t found = FindNoCase(text, lowerOpening, from);
        if (found == std::string_view::npos) {
            return found;
        }
        const size_t after = found + lowerOpening.size();
        if (after >= text.size() || text[after] == '>' || std::isspace(static_cast<unsigned char>(text[after]))) {
            return found;
        }
        from = after;
    }
}

bool IsNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

// Reads the value of class=... from a <P ...> tag, in upper case. Empty if there is none.
std::string ClassOf(std::string_view tag) {
    size_t at = FindNoCase(tag, "class");
    if (at == std::string_view::npos) {
        return {};
    }
    at += 5;
    while (at < tag.size() && std::isspace(static_cast<unsigned char>(tag[at]))) {
        ++at;
    }
    if (at >= tag.size() || tag[at] != '=') {
        return {};
    }
    ++at;
    while (at < tag.size() && (std::isspace(static_cast<unsigned char>(tag[at])) || tag[at] == '"' || tag[at] == '\'')) {
        ++at;
    }
    std::string name;
    while (at < tag.size() && IsNameChar(tag[at])) {
        name += static_cast<char>(std::toupper(static_cast<unsigned char>(tag[at])));
        ++at;
    }
    return name;
}

// True if anything would be visible: not only tags, spaces and &nbsp;.
bool HasVisibleText(std::string_view markup) {
    size_t i = 0;
    while (i < markup.size()) {
        const char c = markup[i];
        if (c == '<') {
            const size_t end = markup.find('>', i);
            if (end == std::string_view::npos) {
                return false;
            }
            i = end + 1;
        } else if (FindNoCase(markup.substr(i, 6), "&nbsp;") == 0) {
            i += 6;
        } else if (static_cast<unsigned char>(c) <= 0x20) {
            ++i;
        } else if (markup.substr(i, 2) == "\xC2\xA0") {  // A UTF-8 no-break space.
            i += 2;
        } else {
            return true;
        }
    }
    return false;
}

struct Paragraph {
    std::string className;  // Empty for a paragraph without a class, which every language shares.
    std::string_view markup;
};

struct SyncBlock {
    std::string_view tag;
    std::vector<Paragraph> paragraphs;
};

std::vector<Paragraph> ParseParagraphs(std::string_view body) {
    std::vector<Paragraph> paragraphs;
    size_t at = FindTag(body, "<p");
    if (at == std::string_view::npos) {
        if (HasVisibleText(body)) {
            paragraphs.push_back({std::string(), body});
        }
        return paragraphs;
    }
    if (at > 0 && HasVisibleText(body.substr(0, at))) {
        paragraphs.push_back({std::string(), body.substr(0, at)});
    }
    while (at != std::string_view::npos) {
        const size_t next = FindTag(body, "<p", at + 2);
        const std::string_view markup = body.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at);
        const size_t tagEnd = markup.find('>');
        const std::string_view tag = markup.substr(0, tagEnd == std::string_view::npos ? markup.size() : tagEnd);
        paragraphs.push_back({ClassOf(tag), markup});
        at = next;
    }
    return paragraphs;
}

// Works out a language tag from the class's style, like .KRCC { lang:ko-KR; }, or
// failing that from well-known class names.
std::string LanguageTagFor(const std::string& className, std::string_view header) {
    static const std::map<std::string, std::string> aliases = {
        {"kr", "ko"},   {"jp", "ja"},   {"cn", "zh"},     {"krcc", "ko"}, {"kocc", "ko"}, {"encc", "en"},
        {"egcc", "en"}, {"enuscc", "en"}, {"jpcc", "ja"}, {"jacc", "ja"}, {"cncc", "zh"}, {"zhcc", "zh"}, {"twcc", "zh"},
    };
    const auto normalize = [](std::string tag) {
        const auto alias = aliases.find(tag);
        return alias == aliases.end() ? tag : alias->second;
    };

    std::string lowerClass;
    std::transform(className.begin(), className.end(), std::back_inserter(lowerClass), ToLowerAscii);
    const size_t style = FindNoCase(header, "." + lowerClass);
    if (style != std::string_view::npos) {
        const size_t open = header.find('{', style);
        const size_t close = open == std::string_view::npos ? open : header.find('}', open);
        if (close != std::string_view::npos) {
            const std::string_view rules = header.substr(open, close - open);
            size_t lang = FindNoCase(rules, "lang");
            if (lang != std::string_view::npos && (lang = rules.find(':', lang)) != std::string_view::npos) {
                ++lang;
                while (lang < rules.size() && std::isspace(static_cast<unsigned char>(rules[lang]))) {
                    ++lang;
                }
                std::string code;
                while (lang < rules.size() && std::isalpha(static_cast<unsigned char>(rules[lang]))) {
                    code += ToLowerAscii(rules[lang]);
                    ++lang;
                }
                if (code.size() == 2 || code.size() == 3) {
                    return normalize(code);
                }
            }
        }
    }
    return normalize(lowerClass);
}

std::string ReadFile(const QString& path) {
    QFile file(path);
    if (!file.exists() || file.size() > kMaxSmiFileSize || !file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray bytes = file.readAll();
    return std::string(bytes.constData(), static_cast<size_t>(bytes.size()));
}

// The byte-level parser needs an ASCII-compatible encoding, which UTF-16 isn't.
std::string ConvertUtf16ToUtf8IfNeeded(std::string content) {
    const bool littleEndian = content.size() >= 2 && content[0] == '\xFF' && content[1] == '\xFE';
    const bool bigEndian = content.size() >= 2 && content[0] == '\xFE' && content[1] == '\xFF';
    if (!littleEndian && !bigEndian) {
        return content;
    }
    std::u16string utf16((content.size() - 2) / 2, u'\0');
    for (size_t i = 0; i < utf16.size(); ++i) {
        const auto first = static_cast<unsigned char>(content[2 + i * 2]);
        const auto second = static_cast<unsigned char>(content[3 + i * 2]);
        utf16[i] = static_cast<char16_t>(littleEndian ? (second << 8 | first) : (first << 8 | second));
    }
    const QByteArray utf8Bytes = QString::fromUtf16(utf16.data(), static_cast<qsizetype>(utf16.size())).toUtf8();
    std::string utf8(kUtf8Bom);
    utf8.append(utf8Bytes.constData(), static_cast<size_t>(utf8Bytes.size()));
    return utf8;
}

}  // namespace

std::vector<SmiLanguage> SplitSmiByLanguage(std::string_view content) {
    const size_t firstSync = FindTag(content, "<sync");
    if (firstSync == std::string_view::npos) {
        return {};
    }
    const std::string_view header = content.substr(0, firstSync);
    size_t bodyEnd = FindNoCase(content, "</body", firstSync);
    if (bodyEnd == std::string_view::npos) {
        bodyEnd = content.size();
    }
    const std::string_view footer = content.substr(bodyEnd);

    // Parse every timed block, and note the classes in order of first appearance.
    std::vector<SyncBlock> blocks;
    std::vector<std::string> classes;
    std::set<std::string> classesWithText;
    size_t at = firstSync;
    while (at != std::string_view::npos && at < bodyEnd) {
        size_t next = FindTag(content, "<sync", at + 5);
        if (next == std::string_view::npos || next > bodyEnd) {
            next = bodyEnd;
        }
        const size_t tagEnd = content.find('>', at);
        if (tagEnd == std::string_view::npos || tagEnd >= next) {
            at = next == bodyEnd ? std::string_view::npos : next;
            continue;
        }
        SyncBlock block;
        block.tag = content.substr(at, tagEnd + 1 - at);
        block.paragraphs = ParseParagraphs(content.substr(tagEnd + 1, next - tagEnd - 1));
        for (const Paragraph& paragraph : block.paragraphs) {
            if (paragraph.className.empty()) {
                continue;
            }
            if (std::find(classes.begin(), classes.end(), paragraph.className) == classes.end()) {
                classes.push_back(paragraph.className);
            }
            if (HasVisibleText(paragraph.markup)) {
                classesWithText.insert(paragraph.className);
            }
        }
        blocks.push_back(std::move(block));
        at = next == bodyEnd ? std::string_view::npos : next;
    }

    // Two classes need separating even if one is blank: its blank lines would
    // overwrite the other's text just the same.
    if (classes.size() < 2) {
        return {};
    }

    std::vector<SmiLanguage> languages;
    std::set<std::string> usedTags;
    for (const std::string& className : classes) {
        if (classesWithText.count(className) == 0) {
            continue;
        }
        SmiLanguage language;
        language.tag = LanguageTagFor(className, header);
        if (!usedTags.insert(language.tag).second) {
            std::string lowerClass;
            std::transform(className.begin(), className.end(), std::back_inserter(lowerClass), ToLowerAscii);
            language.tag += "-" + lowerClass;
            usedTags.insert(language.tag);
        }
        language.content.assign(header);
        for (const SyncBlock& block : blocks) {
            bool opened = false;
            for (const Paragraph& paragraph : block.paragraphs) {
                if (paragraph.className == className || paragraph.className.empty()) {
                    if (!opened) {
                        language.content.append(block.tag);
                        opened = true;
                    }
                    language.content.append(paragraph.markup);
                }
            }
        }
        language.content.append(footer);
        languages.push_back(std::move(language));
    }
    std::stable_partition(languages.begin(), languages.end(), [](const SmiLanguage& language) { return language.tag == "ko"; });
    return languages;
}

bool IsSmiFile(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("smi") || suffix == QLatin1String("sami");
}

QStringList PrepareSmiFile(const QString& smi, const QString& outputDirectory) {
    const std::string content = ConvertUtf16ToUtf8IfNeeded(ReadFile(smi));
    const std::vector<SmiLanguage> languages = SplitSmiByLanguage(content);
    if (languages.empty()) {
        return {smi};
    }

    // The caller owns a private directory for these. Never create one here: an empty path
    // would mean the current directory, and recreating a missing one under the shared temp
    // root would reopen the hole a private directory closes.
    const QDir dir(outputDirectory);
    if (outputDirectory.isEmpty() || !dir.exists()) {
        return {smi};
    }

    const QString stem = QFileInfo(smi).completeBaseName();
    QStringList files;
    for (const SmiLanguage& language : languages) {
        const QString partPath = dir.filePath(stem + QLatin1Char('.') + QString::fromStdString(language.tag) + QStringLiteral(".smi"));
        QFile file(partPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return {smi};
        }
        const qint64 written = file.write(language.content.data(), static_cast<qint64>(language.content.size()));
        file.close();
        if (written != static_cast<qint64>(language.content.size())) {
            return {smi};
        }
        files.push_back(partPath);
    }
    return files;
}

QStringList PrepareSmiFilesFor(const QString& video, const QString& outputDirectory) {
    if (video.contains(QStringLiteral("://"))) {
        return {};
    }

    const QFileInfo videoInfo(video);
    const QString stem = videoInfo.completeBaseName();
    if (stem.isEmpty()) {
        return {};
    }

    const QDir parentDir = videoInfo.dir();
    if (!parentDir.exists()) {
        return {};
    }

    QFileInfoList entries = parentDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    std::vector<QFileInfo> matches;
    for (const QFileInfo& entry : entries) {
        if (IsSmiFile(entry.filePath()) && entry.completeBaseName().contains(stem, Qt::CaseInsensitive)) {
            matches.push_back(entry);
        }
    }

    // The file with exactly the video's name comes first, then the rest in a stable order.
    std::sort(matches.begin(), matches.end(), [&](const QFileInfo& a, const QFileInfo& b) {
        const bool aExact = a.completeBaseName().compare(stem, Qt::CaseInsensitive) == 0;
        const bool bExact = b.completeBaseName().compare(stem, Qt::CaseInsensitive) == 0;
        return aExact != bExact ? aExact : a.filePath() < b.filePath();
    });

    QStringList files;
    for (const QFileInfo& smi : matches) {
        files.append(PrepareSmiFile(smi.absoluteFilePath(), outputDirectory));
    }
    return files;
}
