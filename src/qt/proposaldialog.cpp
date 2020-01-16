// Copyright (c) 2017-2019 The Bulwark Developers
// Copyright (c) 2019 The Syndicate Ltd developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/Zenon-config.h>
#endif

#include <fstream>
#include <iostream>
#include <string>

#include <base58.h>
#include <bitcoingui.h>
#include <boost/tokenizer.hpp>
#include <guiutil.h>
#include <main.h>
#include <masternode-budget.h>
#include <masternode-sync.h>
#include <proposaldialog.h>
#include <ui_proposaldialog.h>
#include <univalue.h>
#include <utilstrencodings.h>
#include <wallet/wallet.h>

#include <QMessageBox>
#include <QString>
#include <QTimer>

ProposalDialog::ProposalDialog(Mode mode, QWidget* parent) : QDialog(parent), ui(new Ui::ProposalDialog), mapper(0), mode(mode), counter(0) {
    ui->setupUi(this);

    switch (mode) {
    case PrepareProposal:
        setWindowTitle(tr("Prepare Proposal"));
        ui->confirmLabel->setVisible(false);
        ui->hashEdit->setVisible(false);
        ui->hashLabel->setVisible(false);
        break;
    case SubmitProposal:
        setWindowTitle(tr("Submit Proposal"));
        ui->confirmLabel->setVisible(true);
        ui->hashEdit->setVisible(true);
        ui->hashLabel->setVisible(true);
        break;
    }

    ui->nameEdit->setFont(GUIUtil::bitcoinAddressFont());
    ui->nameEdit->setPlaceholderText(tr("Provide a title, keep it short"));
    ui->nameEdit->setToolTip(tr("Provide a title, keep it short"));

    ui->urlEdit->setFont(GUIUtil::bitcoinAddressFont());
    ui->urlEdit->setPlaceholderText(tr("Valid http or https URL"));
    ui->urlEdit->setToolTip(tr("Valid http or https URL"));

    ui->blockStart->setFont(GUIUtil::bitcoinAddressFont());
    ui->blockStart->setPlaceholderText(tr("Starting block"));
    ui->blockStart->setToolTip(tr("Starting block"));
    ui->blockStart->setValidator(new QIntValidator(1, INT_MAX));

    ui->blockEnd->setFont(GUIUtil::bitcoinAddressFont());
    ui->blockEnd->setPlaceholderText(tr("Ending block"));
    ui->blockEnd->setToolTip(tr("Ending block"));
    ui->blockEnd->setValidator(new QIntValidator(1, INT_MAX));

    ui->hashEdit->setFont(GUIUtil::bitcoinAddressFont());
    ui->hashEdit->setPlaceholderText(tr("The TXID of the proposal hash, must be confirmed before use"));
    ui->hashEdit->setToolTip(tr("The TXID of the proposal hash, must be confirmed before use"));

    ui->confirmLabel->setWordWrap(true);
    ui->infoLabel->setWordWrap(true);

    // Load next superblock number.
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) return;

    // Start periodic updates to handle submit block depth validation.
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(checkProposalTX()));
}

ProposalDialog::~ProposalDialog() {
    timer->stop();
    delete ui;
}

void ProposalDialog::prepareProposal() {
    std::string strError = "";

    if (pwalletMain->IsLocked()) {
        strError = "Error: Please enter the wallet passphrase with walletpassphrase first.";
        QMessageBox::critical(this, "Prepare Proposal Error", QString::fromStdString(strError));
        return;
    }

    std::string strProposalName = SanitizeString(ui->nameEdit->text().toStdString());
    std::string strURL = SanitizeString(ui->urlEdit->text().toStdString());
    int nBlockStart = ui->blockStart->text().toInt();
    int nBlockEnd = ui->blockEnd->text().toInt();

    //*************************************************************************

    // create the proposal incase we're the first to make it
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nBlockStart, nBlockEnd, 0);
    std::string err;
    if (!budgetProposalBroadcast.Sign() && !budgetProposalBroadcast.IsValid(err, false)) strError = "Proposal is not valid - " + budgetProposalBroadcast.GetHash().ToString() + " - " + err;

    bool useIX = false;
    if (!budgetProposalBroadcast.SignatureValid() && strError.empty() && !pwalletMain->GetBudgetSystemCollateralTX(wtx, budgetProposalBroadcast.GetHash(), useIX)) {
        strError = "Error making collateral transaction for proposal. Please check your wallet balance.";
    }

    // make our change address
    CReserveKey reservekey(pwalletMain);
    // send the tx to the network
    if (!budgetProposalBroadcast.SignatureValid() && strError.empty() && !pwalletMain->CommitTransaction(wtx, reservekey, useIX ? "ix" : "tx")) {
        strError = "Unable to commit proposal transaction.";
    }

    if (!strError.empty()) {
        QMessageBox::critical(this, "Prepare Proposal Error", QString::fromStdString(strError));
        return;
    }

    // update the local view with submit view
    ui->cancelButton->setDisabled(true);
    ui->nameEdit->setDisabled(true);
    ui->urlEdit->setDisabled(true);
    ui->blockStart->setDisabled(true);
    ui->blockEnd->setDisabled(true);
    ui->hashEdit->setDisabled(true);

    ui->acceptButton->setDisabled(true);
    ui->acceptButton->setText(tr("Waiting..."));

    ui->confirmLabel->setVisible(true);
    ui->confirmLabel->setText(QString::fromStdString("Waiting for confirmations..."));

    ui->hashEdit->setText(QString::fromStdString(wtx.GetHash().ToString()));
    ui->hashEdit->setVisible(true);
    ui->hashLabel->setVisible(true);

    mode = SubmitProposal;
    setWindowTitle(tr("Submit Proposal"));

    timer->start(1000);
    counter = chainActive.Tip()->nHeight + 1;
}

void ProposalDialog::submitProposal() {
    std::string strError = "";

    std::string strProposalName = SanitizeString(ui->nameEdit->text().toStdString());
    std::string strURL = SanitizeString(ui->urlEdit->text().toStdString());
    int nBlockStart = ui->blockStart->text().toInt();
    int nBlockEnd = ui->blockEnd->text().toInt();
    uint256 hash = ParseHashV(ui->hashEdit->text().toStdString(), "parameter 1");

    //*************************************************************************

    // create the proposal incase we're the first to make it
    int nConf = 0;
    std::string err = "";
    CBudgetProposalBroadcast budgetProposalBroadcast(strProposalName, strURL, nBlockStart, nBlockEnd, hash);
    if (!budgetProposalBroadcast.Sign() && !IsBudgetCollateralValid(hash, budgetProposalBroadcast.GetHash(), err, budgetProposalBroadcast.nTime, nConf)) {
        strError = "Proposal FeeTX is not valid - " + hash.ToString() + " - " + err;
    }

    if (strError.empty() && !budget.AddProposal(budgetProposalBroadcast)) strError = "Invalid proposal, see debug.log for details.";

    if (!budgetProposalBroadcast.SignatureValid() && !strError.empty()) {
        QMessageBox::critical(this, tr("Submit Proposal Error"), QString::fromStdString(strError));
        return;
    }

    budget.mapSeenMasternodeBudgetProposals.insert(std::make_pair(budgetProposalBroadcast.GetHash(), budgetProposalBroadcast));
    budgetProposalBroadcast.Relay();

    this->accept();
}

bool ProposalDialog::validateProposal() {
    std::string strError = "";

    if (!masternodeSync.IsBlockchainSynced()) strError = "Must wait for client to sync with network. Try again in a minute or so.";

    std::string strProposalName = SanitizeString(ui->nameEdit->text().toStdString());
    if (strProposalName.size() < 5 || strProposalName.size() > 20) strError = "Invalid proposal name, it must be between 5 and 20 characters.";

    std::string strURL = SanitizeString(ui->urlEdit->text().toStdString());
    if (strURL.size() < 11 || strURL.size() > 64) strError = "Invalid url, must be between 11 and 64 characters.";
    if(!(strURL.find("http://") == std::string::npos || strURL.find("https://") == std::string::npos)) strError = "Invalid url, must contain http:// or https:// .";

    int nBlockStart = ui->blockStart->text().toInt();
    if(ui->blockStart->text().isEmpty())
        strError = "Block start cannot be empty.";
    if (nBlockStart < chainActive.Tip()->nHeight) strError = "Invalid block start, must be higher than current height.";

    int nBlockEnd = ui->blockEnd->text().toInt();
    if(ui->blockEnd->text().isEmpty())
        strError = "Block end cannot be empty.";
    if (nBlockEnd < nBlockStart) strError = "Ending block must be higher than starting block.";

    if (!strError.empty()) {
        QMessageBox::critical(this, tr("Submit Proposal Error"), QString::fromStdString(strError));
        return false;
    }

    return true;
}

void ProposalDialog::checkProposalTX() {
    if (mode != SubmitProposal) return;

    int nConf = Params().Budget_Fee_Confirmations();
    int nDepth = (chainActive.Tip()->nHeight + 1) - counter;
    if (mapArgs.count("-sporkkey") || nDepth > nConf) {
        ui->acceptButton->setDisabled(false);
        ui->acceptButton->setText("Finish");
        ui->confirmLabel->setText(tr("Click on Finish to complete the submission and start voting."));

        timer->stop();
    } else if (nDepth == nConf) {
        ui->confirmLabel->setText(QString::fromStdString("Waiting for final confirmation..."));
    } else if (nDepth > 0) {
        ui->confirmLabel->setText(QString::fromStdString(strprintf("Currently %d of %d confirmations...", nDepth, (nConf + 1)).c_str()));
    }
}

void ProposalDialog::on_acceptButton_clicked() {
    if (!validateProposal()) return;

    if (mode == PrepareProposal) {
        prepareProposal();
    } else if (mode == SubmitProposal) {
        submitProposal();
    }
}

void ProposalDialog::on_cancelButton_clicked() {
    this->reject();
}

