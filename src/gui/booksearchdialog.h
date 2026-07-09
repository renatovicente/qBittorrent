/*
 * qBittorrent (rv fork)
 * Book search & download (LibGen) with NAS destination options.
 * Uses a headless Node backend (~/.local/share/qbt-libgen/libgen.mjs).
 */

#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QProcess;
class QPushButton;
class QTableWidget;

class BookSearchDialog final : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(BookSearchDialog)

public:
    explicit BookSearchDialog(QWidget *parent = nullptr);
    ~BookSearchDialog() override;

private:
    void doSearch();
    void onSearchFinished(int exitCode);
    void downloadSelected();
    void onDownloadFinished(int exitCode);
    void setBusy(bool busy, const QString &message = {});
    static QString nodeExecutable();
    static QString backendScript();
    static QString stagingDir();

    QLineEdit *m_query = nullptr;
    QPushButton *m_searchBtn = nullptr;
    QTableWidget *m_table = nullptr;
    QComboBox *m_nasFolder = nullptr;
    QPushButton *m_newFolderBtn = nullptr;
    QLineEdit *m_rename = nullptr;
    QPushButton *m_downloadBtn = nullptr;
    QLabel *m_status = nullptr;

    QProcess *m_proc = nullptr;
    QByteArray m_procOut;
    QString m_pendingDownloadName; // label for the download in progress
    QString m_pendingAuthor;
    QString m_pendingFormat;
    QString m_pendingMd5;
};
