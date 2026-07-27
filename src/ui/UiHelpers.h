#pragma once

#include <QFont>
#include <QLabel>

namespace lighttrack::ui
{
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
