// Draws the music track in every waveform style, zoomed out and zoomed in,
// and checks each style paints something. Set LIGHTTRACK_PREVIEW_DIR to also
// write waveform_styles.png for a visual check.

#include "ui/timeline/internal/TimelineEditorInternal.h"

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <cmath>
#include <iostream>
#include <utility>

int RunTimelineWaveformTests()
{
    using lighttrack::WaveformStyle;
    using lighttrack::timeline_internal::MusicSpectrumItem;

    // A song-like envelope: verses, a build-up and drops.
    QVector<qreal> levels;
    for(int index = 0; index < 1200; ++index)
    {
        const double t = index / 1200.0;
        const double section = std::fmod(t * 6.0, 1.0);
        const double beat = 0.5 + 0.5 * std::abs(std::sin(index * 0.9));
        levels.push_back(qBound(
            0.0,
            (0.25 + 0.6 * section) * beat + 0.05 * std::sin(index * 0.31),
            1.0));
    }

    // Stereo peaks (100 per second over 60 s): a kick-like pattern on the
    // left, a steadier pad on the right, so the channels look different.
    lighttrack::WaveformPeaks peaks;
    peaks.buckets_per_second = 100.0;
    for(int index = 0; index < 6000; ++index)
    {
        const double kick = std::exp(-std::fmod(index, 50.0) / 12.0);
        const double noise = 0.15 * std::sin(index * 1.7) * std::sin(index * 0.37);
        const double left = std::min(1.0, 0.15 + 0.8 * kick + noise);
        const double right = std::min(1.0, 0.35 + 0.2 * std::abs(std::sin(index * 0.02)) + noise);
        peaks.max[0].push_back(static_cast<float>(left));
        peaks.min[0].push_back(static_cast<float>(-left * 0.9));
        peaks.max[1].push_back(static_cast<float>(right));
        peaks.min[1].push_back(static_cast<float>(-right * 0.95));
    }

    const std::pair<WaveformStyle, const char*> styles[] = {
        {WaveformStyle::Studio, "Studio"},
        {WaveformStyle::Smooth, "Smooth"},
        {WaveformStyle::Bars, "Bars"},
        {WaveformStyle::Line, "Line"},
        {WaveformStyle::Progress, "Progress"},
        {WaveformStyle::Classic, "Classic"}};
    // Item widths: whole song squeezed (zoomed out) and stretched (zoomed in).
    const qreal widths[] = {600.0, 24000.0};
    constexpr int view_width = 600;
    constexpr int row_height = static_cast<int>(lighttrack::timeline_internal::MUSIC_ROW_HEIGHT);
    constexpr int style_count = 6;

    QImage sheet(view_width * 2 + 110, style_count * (row_height + 6), QImage::Format_RGB32);
    sheet.fill(QColor(30, 32, 36));
    QPainter sheet_painter(&sheet);

    for(int row = 0; row < style_count; ++row)
    {
        for(int column = 0; column < 2; ++column)
        {
            MusicSpectrumItem item;
            item.SetSpectrum(levels, widths[column], row_height);
            item.SetPeaks(peaks);
            item.SetStyle(styles[row].first);
            item.SetPlayedWidth(view_width * 0.4);

            QImage frame(view_width, row_height, QImage::Format_ARGB32);
            frame.fill(QColor(30, 32, 36));
            QPainter painter(&frame);
            QStyleOptionGraphicsItem option;
            option.exposedRect = QRectF(0.0, 0.0, view_width, row_height);
            item.paint(&painter, &option, nullptr);
            painter.end();

            bool painted = false;
            for(int y = 0; y < frame.height() && !painted; ++y)
            {
                for(int x = 0; x < frame.width(); ++x)
                {
                    if(frame.pixelColor(x, y) != QColor(30, 32, 36))
                    {
                        painted = true;
                        break;
                    }
                }
            }
            if(!painted)
            {
                std::cerr << "Waveform style " << styles[row].second
                          << " painted nothing\n";
                return 1;
            }

            sheet_painter.drawImage(
                110 + column * view_width,
                row * (row_height + 6),
                frame);
        }
        sheet_painter.setPen(Qt::white);
        sheet_painter.drawText(
            QRect(4, row * (row_height + 6), 100, row_height),
            Qt::AlignVCenter,
            styles[row].second);
    }
    sheet_painter.end();

    const QString preview_dir =
        qEnvironmentVariable("LIGHTTRACK_PREVIEW_DIR");
    if(!preview_dir.isEmpty())
    {
        sheet.save(QDir(preview_dir).filePath("waveform_styles.png"));
    }
    return 0;
}
