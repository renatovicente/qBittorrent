/*
 * qBittorrent (rv fork)
 * Book search & download (LibGen) with NAS destination options.
 */

#include "booksearchdialog.h"

#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "base/global.h"
#include "base/settingsstorage.h"
#include "base/utils/misc.h"

namespace
{
    const QString KEY_NASFOLDERHISTORY = u"BookSearchDialog/NasFolderHistory"_s;
    const QString DEFAULT_NAS_FOLDER = u"/volume1/homes/rvicente/inbox"_s;

    enum Column { COL_TITLE, COL_AUTHOR, COL_YEAR, COL_FORMAT, COL_SIZE, COL_COUNT };

    QString csvField(const QString &value)
    {
        QString v = value;
        v.replace(u'"', u"\"\""_s);
        return u'"' + v + u'"';
    }
}

BookSearchDialog::BookSearchDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Search Books (LibGen)"));
    resize(860, 520);

    auto *layout = new QVBoxLayout(this);

    // Search row
    auto *searchRow = new QHBoxLayout;
    m_query = new QLineEdit(this);
    m_query->setPlaceholderText(tr("Title, author, ISBN..."));
    m_query->setClearButtonEnabled(true);
    m_searchBtn = new QPushButton(tr("Search"), this);
    m_searchBtn->setDefault(true);
    searchRow->addWidget(m_query);
    searchRow->addWidget(m_searchBtn);
    layout->addLayout(searchRow);

    // Results table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(COL_COUNT);
    m_table->setHorizontalHeaderLabels({tr("Title"), tr("Author"), tr("Year"), tr("Format"), tr("Size")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(COL_TITLE, QHeaderView::Stretch);
    layout->addWidget(m_table);

    // NAS destination group
    auto *nasRow = new QHBoxLayout;
    nasRow->addWidget(new QLabel(tr("NAS folder:"), this));
    m_nasFolder = new QComboBox(this);
    m_nasFolder->setEditable(true);
    m_nasFolder->setInsertPolicy(QComboBox::NoInsert);
    m_nasFolder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    {
        QStringList hist = SettingsStorage::instance()->loadValue<QStringList>(KEY_NASFOLDERHISTORY);
        if (!hist.contains(DEFAULT_NAS_FOLDER))
            hist.append(DEFAULT_NAS_FOLDER);
        m_nasFolder->addItems(hist);
    }
    m_newFolderBtn = new QPushButton(tr("New folder…"), this);
    nasRow->addWidget(m_nasFolder);
    nasRow->addWidget(m_newFolderBtn);
    layout->addLayout(nasRow);

    auto *renameRow = new QHBoxLayout;
    renameRow->addWidget(new QLabel(tr("Save as (optional):"), this));
    m_rename = new QLineEdit(this);
    m_rename->setPlaceholderText(tr("Leave empty to keep the original file name"));
    m_rename->setClearButtonEnabled(true);
    renameRow->addWidget(m_rename);
    layout->addLayout(renameRow);

    // Progress bar (shown during download) + status
    m_progress = new QProgressBar(this);
    m_progress->setTextVisible(true);
    m_progress->setVisible(false);
    layout->addWidget(m_progress);

    m_status = new QLabel(this);
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(this);
    m_downloadBtn = buttons->addButton(tr("Download to NAS"), QDialogButtonBox::AcceptRole);
    m_downloadBtn->setEnabled(false);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(m_searchBtn, &QPushButton::clicked, this, &BookSearchDialog::doSearch);
    connect(m_query, &QLineEdit::returnPressed, this, &BookSearchDialog::doSearch);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]
    {
        m_downloadBtn->setEnabled(!m_table->selectedItems().isEmpty());
    });
    connect(m_downloadBtn, &QPushButton::clicked, this, &BookSearchDialog::downloadSelected);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_newFolderBtn, &QPushButton::clicked, this, [this]
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("New NAS subfolder")
                , tr("Subfolder name (created on the NAS when the transfer runs):")
                , QLineEdit::Normal, {}, &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        QString parent = m_nasFolder->currentText().trimmed();
        while (parent.endsWith(u'/'))
            parent.chop(1);
        m_nasFolder->insertItem(0, parent.isEmpty() ? name : (parent + u'/' + name));
        m_nasFolder->setCurrentIndex(0);
    });
}

BookSearchDialog::~BookSearchDialog()
{
    // If a search/download is still running when the dialog is destroyed, the
    // QProcess destructor would re-emit finished() and our slots would touch
    // already-freed widgets. Cut the process loose first.
    if (m_proc)
    {
        m_proc->disconnect(this);
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

QString BookSearchDialog::nodeExecutable()
{
    for (const QString &p : {u"/opt/homebrew/bin/node"_s, u"/usr/local/bin/node"_s, u"/usr/bin/node"_s})
    {
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

QString BookSearchDialog::backendScript()
{
    return QDir::homePath() + u"/.local/share/qbt-libgen/libgen.mjs"_s;
}

QString BookSearchDialog::stagingDir()
{
    return QDir::homePath() + u"/Downloads/books"_s;
}

void BookSearchDialog::setBusy(const bool busy, const QString &message)
{
    m_searchBtn->setEnabled(!busy);
    m_query->setEnabled(!busy);
    m_downloadBtn->setEnabled(!busy && !m_table->selectedItems().isEmpty());
    if (!message.isEmpty())
        m_status->setText(message);
}

void BookSearchDialog::doSearch()
{
    const QString query = m_query->text().trimmed();
    if (query.size() < 3)
    {
        m_status->setText(tr("Enter at least 3 characters."));
        return;
    }
    const QString node = nodeExecutable();
    if (node.isEmpty() || !QFileInfo::exists(backendScript()))
    {
        QMessageBox::warning(this, tr("Book search"),
                tr("The LibGen backend is not available.\nExpected Node at a standard path and the script at:\n%1")
                        .arg(backendScript()));
        return;
    }

    setBusy(true, tr("Searching..."));
    m_procOut.clear();
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] { m_procOut += m_proc->readAllStandardOutput(); });
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this
            , [this](const int code, QProcess::ExitStatus) { onSearchFinished(code); });
    m_proc->start(node, {backendScript(), u"search"_s, query, u"1"_s});
}

void BookSearchDialog::onSearchFinished(const int /*exitCode*/)
{
    if (!m_proc)
        return;
    m_proc->deleteLater();
    m_proc = nullptr;
    setBusy(false);

    const QJsonObject obj = QJsonDocument::fromJson(m_procOut).object();
    if (!obj.value(u"ok"_s).toBool())
    {
        m_status->setText(tr("Search failed: %1").arg(obj.value(u"error"_s).toString(tr("unknown error"))));
        return;
    }

    const QJsonArray entries = obj.value(u"entries"_s).toArray();
    m_table->setRowCount(static_cast<int>(entries.size()));
    for (int i = 0; i < entries.size(); ++i)
    {
        const QJsonObject e = entries.at(i).toObject();
        auto *titleItem = new QTableWidgetItem(e.value(u"title"_s).toString());
        titleItem->setData(Qt::UserRole, e.value(u"md5"_s).toString());
        m_table->setItem(i, COL_TITLE, titleItem);
        m_table->setItem(i, COL_AUTHOR, new QTableWidgetItem(e.value(u"authors"_s).toString()));
        m_table->setItem(i, COL_YEAR, new QTableWidgetItem(e.value(u"year"_s).toString()));
        m_table->setItem(i, COL_FORMAT, new QTableWidgetItem(e.value(u"extension"_s).toString()));
        m_table->setItem(i, COL_SIZE, new QTableWidgetItem(e.value(u"size"_s).toString()));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(COL_TITLE, QHeaderView::Stretch);
    m_status->setText(tr("%1 results.").arg(entries.size()));
}

void BookSearchDialog::downloadSelected()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    const QString md5 = m_table->item(row, COL_TITLE)->data(Qt::UserRole).toString();
    m_pendingDownloadName = m_table->item(row, COL_TITLE)->text();
    m_pendingAuthor = m_table->item(row, COL_AUTHOR) ? m_table->item(row, COL_AUTHOR)->text() : QString();
    m_pendingFormat = m_table->item(row, COL_FORMAT) ? m_table->item(row, COL_FORMAT)->text() : QString();
    m_pendingMd5 = md5;
    if (md5.isEmpty())
        return;

    QDir().mkpath(stagingDir());
    setBusy(true, tr("Downloading \"%1\"...").arg(m_pendingDownloadName));
    m_procOut.clear();
    m_procErr.clear();
    m_progress->setRange(0, 0); // indeterminate until first PROGRESS line with a total
    m_progress->setValue(0);
    m_progress->setVisible(true);
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, [this] { m_procOut += m_proc->readAllStandardOutput(); });
    connect(m_proc, &QProcess::readyReadStandardError, this, &BookSearchDialog::onDownloadProgress);
    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this
            , [this](const int code, QProcess::ExitStatus) { onDownloadFinished(code); });
    m_proc->start(nodeExecutable(), {backendScript(), u"download"_s, md5, stagingDir()});
}

void BookSearchDialog::onDownloadProgress()
{
    if (!m_proc)
        return;
    m_procErr += m_proc->readAllStandardError();
    // parse the last complete "PROGRESS <bytes> <total>" line
    const int nl = m_procErr.lastIndexOf('\n');
    if (nl < 0)
        return;
    const QList<QByteArray> lines = m_procErr.left(nl).split('\n');
    int lastProgressIdx = -1;
    int lastRetryIdx = -1;
    for (int i = lines.size() - 1; i >= 0; --i)
    {
        const QByteArray t = lines.at(i).trimmed();
        if ((lastProgressIdx < 0) && t.startsWith("PROGRESS "))
            lastProgressIdx = i;
        if ((lastRetryIdx < 0) && t.startsWith("RETRY "))
            lastRetryIdx = i;
        if ((lastProgressIdx >= 0) && (lastRetryIdx >= 0))
            break;
    }
    if (lastProgressIdx >= 0)
    {
        const QList<QByteArray> parts = lines.at(lastProgressIdx).trimmed().split(' ');
        const qint64 done = parts.at(1).toLongLong();
        const qint64 total = parts.at(2).toLongLong();
        if (total > 0)
        {
            m_progress->setRange(0, 100);
            m_progress->setValue(static_cast<int>((done * 100) / total));
            m_progress->setFormat(u"%1 / %2  (%p%)"_s.arg(
                    Utils::Misc::friendlyUnit(done), Utils::Misc::friendlyUnit(total)));
        }
        else
        {
            m_progress->setFormat(Utils::Misc::friendlyUnit(done));
        }
    }
    // A retry that happened after the last progress line means a mirror stalled
    // and we're reconnecting to another — reflect that instead of a frozen bar.
    if (lastRetryIdx > lastProgressIdx)
    {
        m_progress->setRange(0, 0); // indeterminate while reconnecting
        m_status->setText(tr("A mirror stalled — trying another..."));
    }
    m_procErr = m_procErr.mid(nl + 1);
}

void BookSearchDialog::onDownloadFinished(const int /*exitCode*/)
{
    if (!m_proc)
        return;
    m_proc->deleteLater();
    m_proc = nullptr;
    m_progress->setVisible(false);
    setBusy(false);

    const QJsonObject obj = QJsonDocument::fromJson(m_procOut).object();
    if (!obj.value(u"ok"_s).toBool())
    {
        m_status->setText(tr("Download failed: %1").arg(obj.value(u"error"_s).toString(tr("unknown error"))));
        return;
    }

    const QString localPath = obj.value(u"path"_s).toString();
    const QString filename = obj.value(u"filename"_s).toString();
    const qint64 sizeBytes = static_cast<qint64>(obj.value(u"size"_s).toDouble());

    // Persist NAS folder history
    QString nasFolder = m_nasFolder->currentText().trimmed();
    while (nasFolder.endsWith(u'/'))
        nasFolder.chop(1);
    if (!nasFolder.isEmpty())
    {
        QStringList hist = SettingsStorage::instance()->loadValue<QStringList>(KEY_NASFOLDERHISTORY);
        hist.removeAll(nasFolder);
        hist.prepend(nasFolder);
        SettingsStorage::instance()->storeValue(KEY_NASFOLDERHISTORY, hist.mid(0, 20));
    }

    const QString rename = m_rename->text().trimmed();
    const QString nasDestination = nasFolder + u'/' + (rename.isEmpty() ? filename : rename);

    // Dedicated book-download ledger (parallel to the NAS transfer log)
    const QString bookLog = QDir::homePath() + u"/Library/Logs/qbt-books.csv"_s;
    QDir().mkpath(QFileInfo(bookLog).absolutePath());
    const bool newLog = !QFileInfo::exists(bookLog);
    if (QFile f {bookLog}; f.open(QIODevice::Append | QIODevice::Text))
    {
        if (newLog)
            f.write("datetime,title,author,format,size_bytes,size_human,md5,nas_destination\n");
        const QString row = QStringList {
            csvField(QDateTime::currentDateTime().toString(u"yyyy-MM-dd HH:mm:ss"_s)),
            csvField(m_pendingDownloadName), csvField(m_pendingAuthor), csvField(m_pendingFormat),
            QString::number(sizeBytes), csvField(Utils::Misc::friendlyUnit(sizeBytes)),
            csvField(m_pendingMd5), csvField(nasDestination)
        }.join(u',') + u'\n';
        f.write(row.toUtf8());
    }

    // Hand off to the detached NAS copy script (serializes with the torrent pipeline)
    const QString copyScript = QDir::homePath() + u"/.local/bin/qbt-nas-copy.sh"_s;
    QProcess::startDetached(u"/bin/bash"_s,
            {copyScript, localPath, nasFolder, rename, m_pendingDownloadName});

    m_status->setText(tr("\"%1\" downloaded — copying to NAS folder %2.")
            .arg(m_pendingDownloadName, nasFolder));
}
