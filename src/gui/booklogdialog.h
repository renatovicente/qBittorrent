/*
 * qBittorrent (rv fork)
 * In-app viewer for the book download log (~/Library/Logs/qbt-books.csv).
 */

#pragma once

#include <QDialog>

class QLabel;
class QTableWidget;

class BookLogDialog final : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(BookLogDialog)

public:
    explicit BookLogDialog(QWidget *parent = nullptr);
    ~BookLogDialog() override;

private:
    void reload();
    static QString logPath();

    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
};
