// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019 The Phore Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "proposaltablemodel.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "proposalrecord.h"
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "rpc/server.h"

#include "obfuscation.h"

#include "core_io.h"
#include "sync.h"
#include "uint256.h"
#include "util.h"
 
#include <cmath>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QIcon>
#include <QList>
#include <univalue.h>

static int column_alignments[] = {
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter
};

ProposalTableModel::ProposalTableModel(QObject *parent):
    QAbstractTableModel(parent)
{
    columns << tr("Proposal") << tr("Proposal Url") << tr("Start Block") << tr("End Block") << tr("Yes") << tr("No") << tr("Abstain") << tr("Percentage");
    refreshProposals();
}

ProposalTableModel::~ProposalTableModel()
{
}

void ProposalTableModel::refreshProposals() 
{
    beginResetModel();
    proposalRecords.clear();

    int mnCount = mnodeman.CountEnabled();
    std::vector<CBudgetProposal*> budgetProposals = budget.GetAllProposals();

    for (CBudgetProposal* pbudgetProposal : budgetProposals)
    {
		int percentage = 0;
        percentage = pbudgetProposal->GetRatio();

        proposalRecords.append(new ProposalRecord(
                                   QString::fromStdString(pbudgetProposal->GetHash().ToString()),
                                   pbudgetProposal->GetBlockStart(),
                                   pbudgetProposal->GetBlockEnd(),
                                   QString::fromStdString(pbudgetProposal->GetURL()),
                                   QString::fromStdString(pbudgetProposal->GetName()),
                                   pbudgetProposal->GetYeas(),
                                   pbudgetProposal->GetNays(),
                                   pbudgetProposal->GetAbstains(),
                                   percentage));
    }
    endResetModel();
}

void ProposalTableModel::setProposalType(const int &type) 
{
    proposalType = type;
    refreshProposals();
}

int ProposalTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return proposalRecords.size();
}

int ProposalTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant ProposalTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    
	ProposalRecord *rec = static_cast<ProposalRecord*>(index.internalPointer());
	
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Proposal:
            return rec->name;
        case ProposalUrl:
            return rec->url;
        case YesVotes:
            return rec->yesVotes;
        case NoVotes:
            return rec->noVotes;
        case AbstainVotes:
            return rec->abstainVotes;
        case StartDate:
            return rec->start_epoch;
        case EndDate:
            return rec->end_epoch;
        case Percentage:
            return QString("%1\%").arg(rec->percentage);
        }
        break;
    case Qt::EditRole:
        switch (index.column()) {
        case Proposal:
            return rec->name;
        case ProposalUrl:
            return rec->url;
        case StartDate:
            return rec->start_epoch;
        case EndDate:
            return rec->end_epoch;
        case YesVotes:
            return rec->yesVotes;
        case NoVotes:
            return rec->noVotes;
        case AbstainVotes:
            return rec->abstainVotes;
        case Percentage:
            return rec->percentage;
        }
        break;
    case Qt::TextAlignmentRole:
        return column_alignments[index.column()];
    case Qt::ForegroundRole:
        if(index.column() == Percentage) {
            if (rec->percentage < 10) {
                return COLOR_NEGATIVE;
            } else {
                return QColor(23, 168, 26);
            }
        }
        return COLOR_BAREADDRESS;
    case ProposalRole:
        return rec->name;
    case ProposalUrlRole:
        return rec->url;
    case StartDateRole:
        return rec->start_epoch;
    case EndDateRole:
        return rec->end_epoch;
    case YesVotesRole:
        return rec->yesVotes;
    case NoVotesRole:
        return rec->noVotes;
    case AbstainVotesRole:
        return rec->abstainVotes;
    case PercentageRole:
        return rec->percentage;
    case ProposalHashRole:
        return rec->hash;
    }
    return QVariant();
}

QVariant ProposalTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return Qt::AlignVCenter;
        } 
        else if (role == Qt::ToolTipRole)
        {
			switch (section) {
            case Proposal:
                return tr("Proposal Name");
            case ProposalUrl:
                return tr("Proposal URL");
            case StartDate:
                return tr("Date and time that the proposal starts.");
            case EndDate:
                return tr("Date and time that the proposal ends.");
            case YesVotes:
                return tr("Obtained yes votes.");
            case NoVotes:
                return tr("Obtained no votes.");
            case AbstainVotes:
                return tr("Obtained abstain votes.");
            case Percentage:
                return tr("Current vote percentage.");
            }	
        }
    }
    return QVariant();
}

QModelIndex ProposalTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);

    if(row >= 0 && row < proposalRecords.size()) {
        ProposalRecord *rec = proposalRecords[row];
        return createIndex(row, column, rec);
    }
    return QModelIndex();
}
