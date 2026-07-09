/*
 * qBittorrent (rv fork)
 * In-app viewer for the book download log (~/Library/Logs/qbt-books.csv).
 */

#include "booklogdialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

#include "base/global.h"
#include "base/utils/misc.h"

namespace
{
    // Minimal CSV line parser (quoted fields, "" escapes).
    QStringList parseCsvLine(const QString &line)
    {
        QStringList fields;
        QString cur;
        bool inQuotes = false;
        for (int i = 0; i < line.size(); ++i)
        {
            const QChar c = line.at(i);
            if (inQuotes)
            {
                if (c == u'"')
                {
                    if (((i + 1) < line.size()) && (line.at(i + 1) == u'"'))
                    {
                        cur += u'"';
                        ++i;
                    }
                    else
                    {
                        inQuotes = false;
                    }
                }
                else
                {
                    cur += c;
                }
            }
            else
            {
                if (c == u'"')
                    inQuotes = true;
                else if (c == u',')
                {
                    fields.append(cur);
                    cur.clear();
                }
                else
                {
                    cur += c;
                }
            }
        }
        fields.append(cur);
        return fields;
    }
}

BookLogDialog::BookLogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Book Download Log"));
    resize(820, 420);

    auto *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({tr("Date / Time"), tr("Title"), tr("Author"), tr("Format"), tr("Size"), tr("NAS destination")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(m_table);

    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(this);
    auto *refreshBtn = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    auto *openBtn = buttons->addButton(tr("Open CSV"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(refreshBtn, &QPushButton::clicked, this, &BookLogDialog::reload);
    connect(openBtn, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile(logPath())); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    reload();
}

BookLogDialog::~BookLogDialog() = default;

QString BookLogDialog::logPath()
{
    return QDir::homePath() + u"/Library/Logs/qbt-books.csv"_s;
}

void BookLogDialog::reload()
{
    m_table->setRowCount(0);

    QFile file {logPath()};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_summary->setText(tr("No books have been downloaded yet."));
        return;
    }

    // columns: datetime,title,author,format,size_bytes,size_human,md5,nas_destination
    struct Row { QString dt, title, author, format, sizeHuman, dest; qint64 bytes; };
    QList<Row> rows;
    qint64 totalBytes = 0;
    bool header = true;
    while (!file.atEnd())
    {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;
        if (header)
        {
            header = false;
            continue;
        }
        const QStringList f = parseCsvLine(line);
        if (f.size() < 8)
            continue;
        const qint64 bytes = f.at(4).toLongLong();
        rows.append({f.at(0), f.at(1), f.at(2), f.at(3), f.at(5), f.at(7), bytes});
        totalBytes += bytes;
    }

    std::reverse(rows.begin(), rows.end()); // newest first

    m_table->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < rows.size(); ++i)
    {
        const Row &r = rows.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(r.dt));
        m_table->setItem(i, 1, new QTableWidgetItem(r.title));
        m_table->setItem(i, 2, new QTableWidgetItem(r.author));
        m_table->setItem(i, 3, new QTableWidgetItem(r.format));
        auto *sizeItem = new QTableWidgetItem(r.sizeHuman.isEmpty() ? Utils::Misc::friendlyUnit(r.bytes) : r.sizeHuman);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(i, 4, sizeItem);
        m_table->setItem(i, 5, new QTableWidgetItem(r.dest));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    m_summary->setText(tr("%1 books · %2 total")
            .arg(QString::number(rows.size()), Utils::Misc::friendlyUnit(totalBytes)));
}
