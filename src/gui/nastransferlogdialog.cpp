/*
 * qBittorrent (rv fork)
 * In-app viewer for the NAS transfer ledger (~/Library/Logs/qbt-nas-transfers.csv).
 */

#include "nastransferlogdialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
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
    // Minimal RFC-4180-ish CSV line parser (handles quoted fields with commas and "" escapes).
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

NasTransferLogDialog::NasTransferLogDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("NAS Transfer Log"));
    resize(760, 420);

    auto *layout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Date / Time"), tr("Name"), tr("Size"), tr("NAS destination")});
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
    auto *revealBtn = buttons->addButton(tr("Open CSV"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(refreshBtn, &QPushButton::clicked, this, &NasTransferLogDialog::reload);
    connect(revealBtn, &QPushButton::clicked, this, []
    {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ledgerPath()));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    reload();
}

NasTransferLogDialog::~NasTransferLogDialog() = default;

QString NasTransferLogDialog::ledgerPath()
{
    return QDir::homePath() + u"/Library/Logs/qbt-nas-transfers.csv"_s;
}

void NasTransferLogDialog::reload()
{
    m_table->setRowCount(0);

    QFile file {ledgerPath()};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        m_summary->setText(tr("No transfers have been logged yet."));
        return;
    }

    struct Row { QString dt, name, sizeHuman, dest; qint64 bytes; };
    QList<Row> rows;
    qint64 totalBytes = 0;
    bool header = true;
    while (!file.atEnd())
    {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty())
            continue;
        if (header) // skip the CSV header line
        {
            header = false;
            continue;
        }
        const QStringList f = parseCsvLine(line);
        if (f.size() < 5)
            continue;
        const qint64 bytes = f.at(2).toLongLong();
        rows.append({f.at(0), f.at(1), f.at(3), f.at(4), bytes});
        totalBytes += bytes;
    }

    // newest first
    std::reverse(rows.begin(), rows.end());

    m_table->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < rows.size(); ++i)
    {
        const Row &r = rows.at(i);
        m_table->setItem(i, 0, new QTableWidgetItem(r.dt));
        m_table->setItem(i, 1, new QTableWidgetItem(r.name));
        auto *sizeItem = new QTableWidgetItem(r.sizeHuman.isEmpty() ? Utils::Misc::friendlyUnit(r.bytes) : r.sizeHuman);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(i, 2, sizeItem);
        m_table->setItem(i, 3, new QTableWidgetItem(r.dest));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);

    m_summary->setText(tr("%1 transfers · %2 total")
            .arg(QString::number(rows.size()), Utils::Misc::friendlyUnit(totalBytes)));
}
