/*
 * qBittorrent (rv fork)
 * In-app viewer for the NAS transfer ledger (~/Library/Logs/qbt-nas-transfers.csv).
 */

#pragma once

#include <QDialog>

class QLabel;
class QTableWidget;

class NasTransferLogDialog final : public QDialog
{
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NasTransferLogDialog)

public:
    explicit NasTransferLogDialog(QWidget *parent = nullptr);
    ~NasTransferLogDialog() override;

private:
    void reload();
    static QString ledgerPath();

    QTableWidget *m_table = nullptr;
    QLabel *m_summary = nullptr;
};
