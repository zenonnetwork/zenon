// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018-2019 The Zenon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZZNNCONTROLDIALOG_H
#define ZZNNCONTROLDIALOG_H

#include <QDialog>
#include <QTreeWidgetItem>
#include "zznn/zerocoin.h"
#include "privacydialog.h"

class CZerocoinMint;
class WalletModel;

namespace Ui {
class ZZnnControlDialog;
}

class CZZnnControlWidgetItem : public QTreeWidgetItem
{
public:
    explicit CZZnnControlWidgetItem(QTreeWidget *parent, int type = Type) : QTreeWidgetItem(parent, type) {}
    explicit CZZnnControlWidgetItem(int type = Type) : QTreeWidgetItem(type) {}
    explicit CZZnnControlWidgetItem(QTreeWidgetItem *parent, int type = Type) : QTreeWidgetItem(parent, type) {}

    bool operator<(const QTreeWidgetItem &other) const;
};

class ZZnnControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ZZnnControlDialog(QWidget *parent);
    ~ZZnnControlDialog();

    void setModel(WalletModel* model);

    static std::set<std::string> setSelectedMints;
    static std::set<CMintMeta> setMints;
    static std::vector<CMintMeta> GetSelectedMints();

private:
    Ui::ZZnnControlDialog *ui;
    WalletModel* model;
    PrivacyDialog* privacyDialog;

    void updateList();
    void updateLabels();

    enum {
        COLUMN_CHECKBOX,
        COLUMN_DENOMINATION,
        COLUMN_PUBCOIN,
        COLUMN_VERSION,
        COLUMN_PRECOMPUTE,
        COLUMN_CONFIRMATIONS,
        COLUMN_ISSPENDABLE
    };
    friend class CZZnnControlWidgetItem;

private slots:
    void updateSelection(QTreeWidgetItem* item, int column);
    void ButtonAllClicked();
};

#endif // ZZNNCONTROLDIALOG_H
