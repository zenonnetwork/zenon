// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019 The Phore Developers
// Copyright (c) 2019 The Syndicate Ltd developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposallist.h"
#include "ui_proposallist.h"

#include "guiutil.h"
#include "optionsmodel.h"
#include "proposaldialog.h"
#include "proposalrecord.h"
#include "proposaltablemodel.h"
#include "activemasternode.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "utilmoneystr.h"

#include "rpc/server.h"
#include "util.h"
#include "obfuscation.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QScrollBar>
#include <QSettings>
#include <QTableView>
#include <QUrl>
#include <QVBoxLayout>
#include <QSortFilterProxyModel>

ProposalList::ProposalList(QWidget *parent) : 
    QWidget(parent), 
    ui(new Ui::ProposalList),
    proposalTableModel(0),
	proxyModel(0),
    timer(0),
    contextMenu(0)
{
    proposalTableModel = new ProposalTableModel(this);

    ui->setupUi(this);
    
    QAction *voteYesAction = new QAction(tr("Vote yes"), this);
    QAction *voteAbstainAction = new QAction(tr("Vote abstain"), this);
    QAction *voteNoAction = new QAction(tr("Vote no"), this);
    QAction *openUrlAction = new QAction(tr("Visit proposal website"), this);

    contextMenu = new QMenu(this);
    contextMenu->addAction(voteYesAction);
    contextMenu->addAction(voteAbstainAction);
    contextMenu->addAction(voteNoAction);
    contextMenu->addSeparator();
    contextMenu->addAction(openUrlAction); 

    connect(ui->tableViewMyProposals, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    connect(ui->tableViewMyProposals, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(openProposalUrl()));

	// connect(ui->createButton, SIGNAL(clicked()), this, SLOT(on_createButton_clicked()));
	
    connect(voteYesAction, SIGNAL(triggered()), this, SLOT(on_voteYesButton_clicked()));
    connect(voteAbstainAction, SIGNAL(triggered()), this, SLOT(on_voteAbstainButton_clicked()));
    connect(voteNoAction, SIGNAL(triggered()), this, SLOT(on_voteNoButton_clicked()));
    connect(openUrlAction, SIGNAL(triggered()), this, SLOT(openProposalUrl()));

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(proposalTableModel);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
	
    ui->tableViewMyProposals->setModel(proxyModel);
    connect(ui->tableViewMyProposals->selectionModel(), SIGNAL(selectionChanged(QItemSelection, QItemSelection)), this, SLOT(selectionChanged()));

    if(masternodeConfig.isPillarOwner()){
        ui->tableViewMyProposals->setContextMenuPolicy(Qt::CustomContextMenu);
    }
    // else{
        ui->voteNoButton->setVisible(false);
        ui->voteYesButton->setVisible(false);
        ui->voteAbstainButton->setVisible(false);
    // }

    ui->tableViewMyProposals->horizontalHeader()->setDefaultAlignment(Qt::AlignVCenter);
    ui->tableViewMyProposals->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableViewMyProposals->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
	ui->tableViewMyProposals->sortByColumn(0, Qt::AscendingOrder);

    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::Proposal, PROPOSAL_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::ProposalUrl, PROPOSAL_URL_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::StartDate, START_DATE_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::EndDate, END_DATE_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::YesVotes, YES_VOTES_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::NoVotes, NO_VOTES_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::AbstainVotes, ABSTAIN_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::Percentage, PERCENTAGE_COLUMN_WIDTH);

	columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(ui->tableViewMyProposals, PERCENTAGE_COLUMN_WIDTH, PROOPOSALS_MINIMUM_COLUMN_WIDTH);
    columnResizingFixer->stretchColumnWidth(ProposalTableModel::Percentage);

    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::AbstainVotes, ABSTAIN_COLUMN_WIDTH);
	
    nLastUpdate = GetTime();

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(refreshProposals()));
    timer->start(1000);
}

ProposalList::~ProposalList()
{
    delete ui;
}

void ProposalList::refreshProposals(bool force) 
{
    int64_t secondsRemaining = nLastUpdate - GetTime() + PROPOSALLIST_UPDATE_SECONDS;

    QString secOrMinutes = (secondsRemaining / 60 > 1) ? tr("minute(s)") : tr("second(s)");
    ui->secondsLabel->setText(tr("List will be updated in %1 %2").arg((secondsRemaining > 60) ? QString::number(secondsRemaining / 60) : QString::number(secondsRemaining), secOrMinutes));

    if(secondsRemaining > 0 && !force) return;
    nLastUpdate = GetTime();

    proposalTableModel->refreshProposals();

    ui->secondsLabel->setText(tr("List will be updated in 0 second(s)"));
}

void ProposalList::on_createButton_clicked() 
{
    ProposalDialog dlg(ProposalDialog::PrepareProposal, this);
    if (QDialog::Accepted == dlg.exec()) {
        refreshProposals(true);
    }
}

void ProposalList::proposalType(int type) 
{
    proposalTableModel->setProposalType(type);
    refreshProposals(true);
}

void ProposalList::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableViewMyProposals->indexAt(point);
    QModelIndexList selection = ui->tableViewMyProposals->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    if(index.isValid())
        contextMenu->exec(QCursor::pos());
}

void ProposalList::on_voteYesButton_clicked()
{
    vote_click_handler("yes");
}

void ProposalList::on_voteAbstainButton_clicked()
{
    vote_click_handler("abstain");
}

void ProposalList::on_voteNoButton_clicked()
{
    vote_click_handler("no");
}

void ProposalList::vote_click_handler(const std::string voteString)
{
    if(!ui->tableViewMyProposals->selectionModel()) {
        return;
    }

    QModelIndexList selection = ui->tableViewMyProposals->selectionModel()->selectedRows();
    if(selection.empty())
        return;

    QString proposalName = selection.at(0).data(ProposalTableModel::ProposalRole).toString();

    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm vote"),
        tr("Are you sure you want to vote <strong>%1</strong> on the proposal <strong>%2</strong>?").arg(QString::fromStdString(voteString), proposalName),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if(retval != QMessageBox::Yes) return;

    uint256 hash;
    hash.SetHex(selection.at(0).data(ProposalTableModel::ProposalHashRole).toString().toStdString());

    int success = 0;
    int failed = 0;

    std::string strVote = voteString;
    int nVote = VOTE_ABSTAIN;
    if (strVote == "yes") nVote = VOTE_YES;
    if (strVote == "no") nVote = VOTE_NO;

    for (const auto& mne : masternodeConfig.getEntries()) {
            std::string errorMessage;
            std::vector<unsigned char> vchMasterNodeSignature;
            std::string strMasterNodeSignMessage;

            CPubKey pubKeyCollateralAddress;
            CKey keyCollateralAddress;
            CPubKey pubKeyMasternode;
            CKey keyMasternode;

            UniValue statusObj(UniValue::VOBJ);

            if (!obfuScationSigner.SetKey(mne.getPrivKey(), errorMessage, keyMasternode, pubKeyMasternode)) 
            {
                failed++;
                continue;
            }

            CMasternode* pmn = mnodeman.Find(pubKeyMasternode);
            if (pmn == NULL || mPillarCollaterals.count(pmn -> vin.prevout) == 0) 
            {
                failed++;
                continue;
            }

            CBudgetVote vote(pmn->vin, hash, nVote);
            if (!vote.Sign(keyMasternode, pubKeyMasternode)) 
            {
                failed++;
                continue;
            }

            std::string strError = "";
            if (budget.UpdateProposal(vote, NULL, strError)) 
            {
                budget.mapSeenMasternodeBudgetVotes.insert(std::make_pair(vote.GetHash(), vote));
                vote.Relay();
                success++;
            }
            else 
            {
                failed++;
            }
    }

    QMessageBox::information(this, tr("Voting"),
        tr("You voted %1 %2 time(s) successfully and failed %3 time(s) on %4").arg(QString::fromStdString(voteString), QString::number(success), QString::number(failed), proposalName));

    refreshProposals(true);
}

void ProposalList::openProposalUrl()
{
    if(!ui->tableViewMyProposals || !ui->tableViewMyProposals->selectionModel())
        return;

    QModelIndexList selection = ui->tableViewMyProposals->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
         QDesktopServices::openUrl(selection.at(0).data(ProposalTableModel::ProposalUrlRole).toString());
}

void ProposalList::resizeEvent(QResizeEvent* event) 
{
    QWidget::resizeEvent(event);

    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::Proposal, PROPOSAL_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::ProposalUrl, PROPOSAL_URL_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::StartDate, START_DATE_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::EndDate, END_DATE_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::YesVotes, YES_VOTES_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::NoVotes, NO_VOTES_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::AbstainVotes, ABSTAIN_COLUMN_WIDTH);
    ui->tableViewMyProposals->setColumnWidth(ProposalTableModel::Percentage, PERCENTAGE_COLUMN_WIDTH);

    columnResizingFixer->stretchColumnWidth(ProposalTableModel::Percentage);
}

void ProposalList::selectionChanged()
{
    LogPrintf("ProposalList::on_tableViewMyProposals_itemSelectionChanged triggered");

    if(!ui->tableViewMyProposals->selectionModel()) {
        return;
    }

    QModelIndexList selection = ui->tableViewMyProposals->selectionModel()->selectedRows();
    if(selection.empty())
        return;

    int StartDateRole = selection.at(0).data(ProposalTableModel::StartDateRole).toInt();
    int EndDateRole = selection.at(0).data(ProposalTableModel::EndDateRole).toInt();

    if(!masternodeConfig.isPillarOwner()){
        ui->voteNoButton->setVisible(false);
        ui->voteYesButton->setVisible(false);
        ui->voteAbstainButton->setVisible(false);
    }else{
        if(chainActive.Height() >= StartDateRole && chainActive.Height() <= EndDateRole){
            ui->tableViewMyProposals->setContextMenuPolicy(Qt::CustomContextMenu);
            ui->voteNoButton->setVisible(true);
            ui->voteYesButton->setVisible(true);
            ui->voteAbstainButton->setVisible(true);
        }else{
            ui->voteNoButton->setVisible(false);
            ui->voteYesButton->setVisible(false);
            ui->voteAbstainButton->setVisible(false); 
        }
    }
}
