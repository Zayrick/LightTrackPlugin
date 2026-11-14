#pragma once

#include <QFont>
#include <QFontDatabase>
#include <QLabel>

namespace lighttrack::ui
{
inline QString LucideFontFamily()
{
    static const QString family = []()
    {
        const int font_id = QFontDatabase::addApplicationFont(
            ":/lighttrack/fonts/lucide.ttf");
        if(font_id < 0)
        {
            return QString();
        }

        const QStringList families =
            QFontDatabase::applicationFontFamilies(font_id);
        return families.isEmpty()
            ? QString()
            : families.first();
    }();
    return family;
}

inline QLabel* HeaderLabel(
    const QString& text,
    QWidget* parent)
{
    QLabel* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    label->setContentsMargins(8, 0, 8, 0);

    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}
}
