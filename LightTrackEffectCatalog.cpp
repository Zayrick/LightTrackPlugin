#include "LightTrackEffectCatalog.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTextStream>

#include <algorithm>

namespace
{
QString EffectsRootPath()
{
#ifdef LIGHTTRACK_EFFECTS_SOURCE_DIR
    return QString::fromUtf8(LIGHTTRACK_EFFECTS_SOURCE_DIR);
#else
    return QCoreApplication::applicationDirPath() + "/OpenRGBEffectsPlugin/Effects";
#endif
}

QString CaptureStaticString(const QString& text, const QString& function_name)
{
    const QRegularExpression expression(
        QStringLiteral("static\\s+std::string\\s+const\\s+%1\\s*\\(\\s*\\)\\s*\\{\\s*return\\s+(?:QT_TR_NOOP\\s*\\(\\s*)?\"([^\"]+)\"").arg(function_name));
    const QRegularExpressionMatch match = expression.match(text);
    return match.hasMatch() ? match.captured(1) : QString();
}

QString CaptureRegisterArgument(const QString& text, int argument_index)
{
    const QRegularExpression expression(QStringLiteral("EFFECT_REGISTERER\\s*\\(([^\\n;]+)\\)"));
    QRegularExpressionMatchIterator matches = expression.globalMatch(text);

    while(matches.hasNext())
    {
        const QRegularExpressionMatch match = matches.next();
        const int line_start = text.lastIndexOf('\n', match.capturedStart()) + 1;
        const QString line = text.mid(line_start, match.capturedStart() - line_start).trimmed();
        if(line.startsWith(QStringLiteral("#define")))
        {
            continue;
        }

        const QStringList arguments = match.captured(1).split(',', Qt::KeepEmptyParts);
        if(argument_index < 0 || argument_index >= arguments.size())
        {
            return QString();
        }

        return arguments[argument_index].trimmed();
    }

    return QString();
}

QString ResolveRegisterString(const QString& text, int argument_index)
{
    QString argument = CaptureRegisterArgument(text, argument_index);
    if(argument.endsWith(QStringLiteral("()")))
    {
        argument.chop(2);
        const QString resolved = CaptureStaticString(text, argument);
        if(!resolved.isEmpty())
        {
            return resolved;
        }
    }

    if(argument.startsWith(QStringLiteral("QT_TR_NOOP")))
    {
        const QRegularExpression expression(QStringLiteral("QT_TR_NOOP\\s*\\(\\s*\"([^\"]+)\"\\s*\\)"));
        const QRegularExpressionMatch match = expression.match(argument);
        return match.hasMatch() ? match.captured(1) : QString();
    }

    if(argument.startsWith('"') && argument.endsWith('"'))
    {
        return argument.mid(1, argument.size() - 2);
    }

    return argument;
}

QString ResolveCategory(const QString& token)
{
    static const QHash<QString, QString> categories = {
        {QStringLiteral("CAT_ADVANCED"), QStringLiteral("Advanced")},
        {QStringLiteral("CAT_AUDIO"), QStringLiteral("Audio")},
        {QStringLiteral("CAT_BEAMS"), QStringLiteral("Beams")},
        {QStringLiteral("CAT_RAINBOW"), QStringLiteral("Rainbow")},
        {QStringLiteral("CAT_RANDOM"), QStringLiteral("Random")},
        {QStringLiteral("CAT_SIMPLE"), QStringLiteral("Simple")},
        {QStringLiteral("CAT_SPECIAL"), QStringLiteral("Special")},
    };

    return categories.value(token, token);
}

QColor ColorForEffect(const QString& id)
{
    const uint hash = qHash(id);
    return QColor::fromHsv(static_cast<int>(hash % 360), 148 + static_cast<int>((hash >> 8) % 58), 168 + static_cast<int>((hash >> 16) % 56));
}

QVector<LightTrackEffectInfo> LoadEffects()
{
    QVector<LightTrackEffectInfo> effects;
    QDirIterator iterator(EffectsRootPath(), QStringList() << QStringLiteral("*.h"), QDir::Files, QDirIterator::Subdirectories);

    while(iterator.hasNext())
    {
        QFile file(iterator.next());
        if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            continue;
        }

        QTextStream stream(&file);
        const QString text = stream.readAll();
        if(!text.contains(QStringLiteral("EFFECT_REGISTERER")))
        {
            continue;
        }

        const QString id = ResolveRegisterString(text, 0);
        const QString name = ResolveRegisterString(text, 1);
        const QString category = ResolveCategory(CaptureRegisterArgument(text, 2));
        if(id.isEmpty() || name.isEmpty() || category.isEmpty())
        {
            continue;
        }

        effects.push_back({id, name, category, ColorForEffect(id)});
    }

    std::sort(effects.begin(), effects.end(), [](const LightTrackEffectInfo& a, const LightTrackEffectInfo& b)
    {
        if(a.category != b.category)
        {
            return a.category < b.category;
        }
        return a.name < b.name;
    });

    return effects;
}
}

QVector<LightTrackEffectGroup> LoadOpenRGBEffectsCatalog()
{
    QVector<LightTrackEffectGroup> groups;

    for(const LightTrackEffectInfo& effect : LoadEffects())
    {
        auto group = std::find_if(groups.begin(), groups.end(), [&effect](const LightTrackEffectGroup& candidate)
        {
            return candidate.name == effect.category;
        });

        if(group == groups.end())
        {
            groups.push_back({effect.category, {}});
            group = groups.end() - 1;
        }

        group->effects.push_back(effect);
    }

    return groups;
}

bool FindOpenRGBEffect(const QString& id, LightTrackEffectInfo* effect)
{
    for(const LightTrackEffectInfo& candidate : LoadEffects())
    {
        if(candidate.id == id)
        {
            if(effect != nullptr)
            {
                *effect = candidate;
            }
            return true;
        }
    }

    return false;
}
