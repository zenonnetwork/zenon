// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019 The Phore Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PROPOSALLIST_H
#define BITCOIN_QT_PROPOSALLIST_H

#include "guiutil.h"
#include "proposaltablemodel.h"
#include "columnalignedlayout.h"

#include <QWidget>
#include <QKeyEvent>
#include <QTimer>

namespace Ui
{
class ProposalList;
}

class ClientModel;
class WalletModel;

QT_BEGIN_NAMESPACE
class QModelIndex;
class QSortFilterProxyModel;
QT_END_NAMESPACE

#define PROPOSALLIST_UPDATE_SECONDS 30

class ProposalList : public QWidget
{
    Q_OBJECT

public:
    explicit ProposalList(QWidget *parent = 0);
    ~ProposalList();

    enum ColumnWidths {
        PROPOSAL_COLUMN_WIDTH = 180,
        PROPOSAL_URL_WIDTH = 260,
        START_DATE_COLUMN_WIDTH = 80,
        END_DATE_COLUMN_WIDTH = 80,
        YES_VOTES_COLUMN_WIDTH = 25,
        NO_VOTES_COLUMN_WIDTH = 25,
        ABSTAIN_COLUMN_WIDTH = 60,
        PERCENTAGE_COLUMN_WIDTH = 80,
        PROOPOSALS_MINIMUM_COLUMN_WIDTH = 25
    };

private:
    Ui::ProposalList* ui;
    ProposalTableModel *proposalTableModel;
	QSortFilterProxyModel* proxyModel;
    QTimer *timer;
    QMenu *contextMenu;
    int64_t nLastUpdate = 0;
	GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer;

    void vote_click_handler(const std::string voteString);
	virtual void resizeEvent(QResizeEvent* event);

private Q_SLOTS:
    void contextualMenu(const QPoint &);
    void on_voteYesButton_clicked();
    void on_voteAbstainButton_clicked();
    void on_voteNoButton_clicked();
    void on_createButton_clicked();
    void openProposalUrl();
    void proposalType(int type);
    void selectionChanged();

Q_SIGNALS:
    void doubleClicked(const QModelIndex&);

public Q_SLOTS:
    void refreshProposals(bool force = false);

};

#endif // BITCOIN_QT_PROPOSALLIST_H
