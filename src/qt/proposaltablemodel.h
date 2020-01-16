// Copyright (c) 2011-2015 The Bitcoin Core developers
// Copyright (c) 2019 The Phore Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_PROPOSALTABLEMODEL_H
#define BITCOIN_QT_PROPOSALTABLEMODEL_H

#include "bitcoinunits.h"

#include <QAbstractTableModel>
#include <QStringList>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include "masternode-budget.h"
#include "masternode-payments.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "rpc/server.h"

#include "obfuscation.h"

class ProposalRecord;

class ProposalTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ProposalTableModel(QObject *parent = 0);
    ~ProposalTableModel();

    enum ColumnIndex {
        Proposal = 0,
        ProposalUrl = 1,
        StartDate = 2,
        EndDate = 3,
        YesVotes = 4,
        NoVotes = 5,
        AbstainVotes = 6,
        Percentage = 7
    };

    enum RoleIndex {
        ProposalRole = Qt::UserRole,
        ProposalUrlRole,
        StartDateRole,
        EndDateRole,
        YesVotesRole,
        NoVotesRole,
        AbstainVotesRole,
        PercentageRole,
        ProposalHashRole
    };

    void refreshProposals();

    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QVariant data(const QModelIndex &index, int role) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
	void setProposalType(const int &type);

private:
    QList<ProposalRecord*> proposalRecords;
    QStringList columns;
	int proposalType = 0;

public Q_SLOTS:
};

#endif // BITCOIN_QT_PROPOSALTABLEMODEL_H
