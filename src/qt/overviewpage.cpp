// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017 The Zenon developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "context.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "obfuscation.h"
#include "obfuscationconfig.h"
#include "optionsmodel.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "autoupdatemodel.h"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QMessageBox>
#include <QPushButton>

#define DECORATION_SIZE 48
#define ICON_OFFSET 16
#define NUM_ITEMS 5

extern CWallet* pwalletMain;

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate() : QAbstractItemDelegate(), unit(BitcoinUnits::ZNN)
    {
    }

    inline void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        mainRect.moveLeft(ICON_OFFSET);
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2 * ypad) / 2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top() + ypad, mainRect.width() - xspace - ICON_OFFSET, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top() + ypad + halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();

        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = COLOR_BLACK;
        if (value.canConvert<QBrush>()) {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft | Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool()) {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top() + ypad + halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if (amount < 0) {
            foreground = COLOR_NEGATIVE;
        } else if (!confirmed) {
            foreground = COLOR_UNCONFIRMED;
        } else {
            foreground = COLOR_BLACK;
        }

        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if (!confirmed) {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight | Qt::AlignVCenter, amountText);

        painter->setPen(COLOR_BLACK);
        painter->drawText(amountRect, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget* parent) : QWidget(parent),
                                              ui(new Ui::OverviewPage),
                                              clientModel(0),
                                              walletModel(0),
                                              currentBalance(-1),
                                              currentUnconfirmedBalance(-1),
                                              currentImmatureBalance(-1),
                                              currentZerocoinBalance(-1),
                                              currentUnconfirmedZerocoinBalance(-1),
                                              currentimmatureZerocoinBalance(-1),
                                              currentWatchOnlyBalance(-1),
                                              currentWatchUnconfBalance(-1),
                                              currentWatchImmatureBalance(-1),
                                              txdelegate(new TxViewDelegate()),
                                              filter(0)
{
    nDisplayUnit = 0; // just make sure it's not unitialized
    ui->setupUi(this);

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2) + 2);
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
    connect(ui->labelAlerts, SIGNAL(linkActivated(QString)), this, SLOT(alertLinkActivated(QString)));

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::handleTransactionClicked(const QModelIndex& index)
{
    if (filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::getPercentage(CAmount nUnlockedBalance, CAmount nZerocoinBalance, QString& sZNNPercentage, QString& szZNNPercentage)
{
    int nPrecision = 2;
    double dzPercentage = 0.0;

    if (nZerocoinBalance <= 0){
        dzPercentage = 0.0;
    }
    else{
        if (nUnlockedBalance <= 0){
            dzPercentage = 100.0;
        }
        else{
            dzPercentage = 100.0 * (double)(nZerocoinBalance / (double)(nZerocoinBalance + nUnlockedBalance));
        }
    }

    double dPercentage = 100.0 - dzPercentage;
    
    szZNNPercentage = "(" + QLocale(QLocale::system()).toString(dzPercentage, 'f', nPrecision) + " %)";
    sZNNPercentage = "(" + QLocale(QLocale::system()).toString(dPercentage, 'f', nPrecision) + " %)";
    
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                              const CAmount& zerocoinBalance, const CAmount& unconfirmedZerocoinBalance, const CAmount& immatureZerocoinBalance,
                              const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentZerocoinBalance = zerocoinBalance;
    currentUnconfirmedZerocoinBalance = unconfirmedZerocoinBalance;
    currentimmatureZerocoinBalance = immatureZerocoinBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;

    CAmount nLockedBalance = 0;
    CAmount nWatchOnlyLockedBalance = 0;
    if (pwalletMain) {
        nLockedBalance = pwalletMain->GetLockedCoins();
        nWatchOnlyLockedBalance = pwalletMain->GetLockedWatchOnlyBalance();
    }

    // ZNN Balance
    CAmount nTotalBalance = balance + unconfirmedBalance;
    CAmount znnAvailableBalance = balance - immatureBalance - nLockedBalance;
    CAmount nUnlockedBalance = nTotalBalance - nLockedBalance;

    // ZNN Watch-Only Balance
    CAmount nTotalWatchBalance = watchOnlyBalance + watchUnconfBalance;
    CAmount nAvailableWatchBalance = watchOnlyBalance - watchImmatureBalance - nWatchOnlyLockedBalance;

    // zZNN Balance
    CAmount matureZerocoinBalance = zerocoinBalance - unconfirmedZerocoinBalance - immatureZerocoinBalance;

    // Percentages
    QString szPercentage = "";
    QString sPercentage = "";
    getPercentage(nUnlockedBalance, zerocoinBalance, sPercentage, szPercentage);
    // Combined balances
    CAmount availableTotalBalance = znnAvailableBalance + matureZerocoinBalance;
    CAmount sumTotalBalance = nTotalBalance + zerocoinBalance;

    // ZNN labels
    ui->labelBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, znnAvailableBalance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelLockedBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nTotalBalance, false, BitcoinUnits::separatorAlways));

    // Watchonly labels
    ui->labelWatchAvailable->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nAvailableWatchBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchPending->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchLocked->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nWatchOnlyLockedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelWatchTotal->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, nTotalWatchBalance, false, BitcoinUnits::separatorAlways));

    // zZNN labels
//    ui->labelzBalance->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, zerocoinBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelzBalanceUnconfirmed->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, unconfirmedZerocoinBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelzBalanceMature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, matureZerocoinBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelzBalanceImmature->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, immatureZerocoinBalance, false, BitcoinUnits::separatorAlways));

    // Combined labels
//    ui->labelBalancez->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, availableTotalBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelTotalz->setText(BitcoinUnits::floorHtmlWithUnit(nDisplayUnit, sumTotalBalance, false, BitcoinUnits::separatorAlways));

    // Percentage labels
//    ui->labelZNNPercent->setText(sPercentage);
//    ui->labelzZNNPercent->setText(szPercentage);

    // Adjust bubble-help according to AutoMint settings
    QString automintHelp = tr("Current percentage of zZNN.\nIf AutoMint is enabled this percentage will settle around the configured AutoMint percentage (default = 10%).\n");
    bool fEnableZeromint = GetBoolArg("-enablezeromint", false);
    int nZeromintPercentage = GetArg("-zeromintpercentage", 10);
    if (fEnableZeromint) {
        automintHelp += tr("AutoMint is currently enabled and set to ") + QString::number(nZeromintPercentage) + "%.\n";
        automintHelp += tr("To disable AutoMint add 'enablezeromint=0' in Zenon.conf.");
    }
    else {
        automintHelp += tr("AutoMint is currently disabled.\nTo enable AutoMint change 'enablezeromint=0' to 'enablezeromint=1' in Zenon.conf");
    }

    // Only show most balances if they are non-zero for the sake of simplicity
    QSettings settings;
    bool settingShowAllBalances = !settings.value("fHideZeroBalances").toBool();

//    bool showSumAvailable = settingShowAllBalances || sumTotalBalance != availableTotalBalance;
//    ui->labelBalanceTextz->setVisible(showSumAvailable);
//    ui->labelBalancez->setVisible(showSumAvailable);

    bool showWatchOnly = nTotalWatchBalance != 0;

    // ZNN Available
    bool showZNNAvailable = settingShowAllBalances || znnAvailableBalance != nTotalBalance;
    bool showWatchOnlyZNNAvailable = showZNNAvailable || nAvailableWatchBalance != nTotalWatchBalance;
    ui->labelBalanceText->setVisible(showZNNAvailable || showWatchOnlyZNNAvailable);
    ui->labelBalance->setVisible(showZNNAvailable || showWatchOnlyZNNAvailable);
    ui->labelWatchAvailable->setVisible(showWatchOnlyZNNAvailable && showWatchOnly);

    // ZNN Pending
    bool showZNNPending = settingShowAllBalances || unconfirmedBalance != 0;
    bool showWatchOnlyZNNPending = showZNNPending || watchUnconfBalance != 0;
    ui->labelPendingText->setVisible(showZNNPending || showWatchOnlyZNNPending);
    ui->labelUnconfirmed->setVisible(showZNNPending || showWatchOnlyZNNPending);
    ui->labelWatchPending->setVisible(showWatchOnlyZNNPending && showWatchOnly);

    // ZNN Immature
    bool showZNNImmature = settingShowAllBalances || immatureBalance != 0;
    bool showWatchOnlyImmature = showZNNImmature || watchImmatureBalance != 0;
    ui->labelImmatureText->setVisible(showZNNImmature || showWatchOnlyImmature);
    ui->labelImmature->setVisible(showZNNImmature || showWatchOnlyImmature); // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature && showWatchOnly); // show watch-only immature balance

    // ZNN Locked
    bool showZNNLocked = settingShowAllBalances || nLockedBalance != 0;
    bool showWatchOnlyZNNLocked = showZNNLocked || nWatchOnlyLockedBalance != 0;
    ui->labelLockedBalanceText->setVisible(showZNNLocked || showWatchOnlyZNNLocked);
    ui->labelLockedBalance->setVisible(showZNNLocked || showWatchOnlyZNNLocked);
    ui->labelWatchLocked->setVisible(showWatchOnlyZNNLocked && showWatchOnly);

    // zZNN
    bool showzZNNAvailable = settingShowAllBalances || zerocoinBalance != matureZerocoinBalance;
    bool showzZNNUnconfirmed = settingShowAllBalances || unconfirmedZerocoinBalance != 0;
    bool showzZNNImmature = settingShowAllBalances || immatureZerocoinBalance != 0;
    ui->labelzBalanceMature->setVisible(showzZNNAvailable);
    ui->labelzBalanceMatureText->setVisible(showzZNNAvailable);
    ui->labelzBalanceUnconfirmed->setVisible(showzZNNUnconfirmed);
    ui->labelzBalanceUnconfirmedText->setVisible(showzZNNUnconfirmed);
    ui->labelzBalanceImmature->setVisible(showzZNNImmature);
    ui->labelzBalanceImmatureText->setVisible(showzZNNImmature);

    // Percent split
    //bool showPercentages = ! (zerocoinBalance == 0 && nTotalBalance == 0);
    //ui->labelZNNPercent->setVisible(showPercentages);
    //ui->labelzZNNPercent->setVisible(showPercentages);

    static int cachedTxLocks = 0;

    if (cachedTxLocks != nCompleteTXLocks) {
        cachedTxLocks = nCompleteTXLocks;
        ui->listTransactions->update();
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
    ui->labelWatchLocked->setVisible(showWatchOnly);     // show watch-only total balance
    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance

    ui->frame_ZerocoinBalances->setVisible(false);

    if (!showWatchOnly) {
        ui->labelWatchImmature->hide();
    } else {
        ui->labelBalance->setIndent(20);
        ui->labelUnconfirmed->setIndent(20);
        ui->labelLockedBalance->setIndent(20);
        ui->labelImmature->setIndent(20);
        ui->labelTotal->setIndent(20);
    }
}

void OverviewPage::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model) {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());

        connect(model, SIGNAL(newVersionAvailable()), this, SLOT(updateNewVersionAvailability()));
        connect(model, SIGNAL(refreshDownloadProgress(QString, int)), this, SLOT(updateNewVersionDownloadProgress(QString, int)));
        updateNewVersionAvailability();
    }
}

void OverviewPage::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
    if (model && model->getOptionsModel()) {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getZerocoinBalance(), model->getUnconfirmedZerocoinBalance(), model->getImmatureZerocoinBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)), this,
                         SLOT(setBalance(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(hideZeroBalancesChanged(bool)), this, SLOT(updateDisplayUnit()));
        connect(model->getOptionsModel(), SIGNAL(hideOrphansChanged(bool)), this, SLOT(hideOrphans(bool)));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("ZNN")
    updateDisplayUnit();

    // Hide orphans
    QSettings settings;
    hideOrphans(settings.value("fHideOrphans", false).toBool());
}

void OverviewPage::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if (currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentZerocoinBalance, currentUnconfirmedZerocoinBalance, currentimmatureZerocoinBalance,
                currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = nDisplayUnit;

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString& warnings)
{
    QStringList alertList;
    if (!warnings.isEmpty())
        alertList << warnings;
    if (!this->newVersionNotification.isEmpty())
        alertList << this->newVersionNotification;

    this->ui->labelAlerts->setVisible(!alertList.isEmpty());
    this->ui->labelAlerts->setText(alertList.join("<br>"));
}

void OverviewPage::updateNewVersionAvailability()
{
    const QString msg1 = tr("A new wallet version is available on Github <a href=\"Go\">Go to download page</a> or <a href=\"Download\">Download now</a>");
    const QString msg2 = tr("The new wallet version is ready for installation <a href=\"Open\">Close wallet and start update</a>");
    const QString msg3 = tr("New wallet version is downloading");

    AutoUpdateModelPtr m = GetContext().GetAutoUpdateModel();
    if (m->IsUpdateAvailable() && !m->FindLocalFile().empty())
        this->newVersionNotification = msg2;
    else if (m->IsDownloadRunning())
        this->newVersionNotification = msg3;
    else if (m->IsUpdateAvailable())
        this->newVersionNotification = msg1;
    else
        this->newVersionNotification.clear();

    updateAlerts(this->clientModel->getStatusBarWarnings());

    if (msg1 == this->newVersionNotification) {
        QMessageBox mbox(this);
        mbox.setWindowTitle(QString::fromStdString(CLIENT_NAME));
        mbox.setText(tr("New version is available, please update your wallet"));
        mbox.addButton(tr("Postpone"), QMessageBox::NoRole);
        QAbstractButton* pButtonDownload = mbox.addButton(tr("Download now"), QMessageBox::YesRole);
        mbox.exec();
        if (mbox.clickedButton() == pButtonDownload)
            alertLinkActivated("Download");
    } else if (msg2 == this->newVersionNotification) {
        QMessageBox mbox(this);
        mbox.setWindowTitle(QString::fromStdString(CLIENT_NAME));
        mbox.setText(tr("New wallet version is ready for installation"));
        mbox.addButton(tr("Later"), QMessageBox::NoRole);
        QAbstractButton* pButtonStart = mbox.addButton(tr("Close wallet and start update"), QMessageBox::YesRole);
        mbox.exec();
        if (mbox.clickedButton() == pButtonStart)
            alertLinkActivated("Open");
    }
}

void OverviewPage::updateNewVersionDownloadProgress(const QString& msg, int nProgress)
{
    if (0 == nProgress || nProgress > 100) // download has started/completed
        updateNewVersionAvailability();
}

void OverviewPage::alertLinkActivated(const QString& link)
{
    assert(GetContext().GetAutoUpdateModel()->IsUpdateAvailable());

    if (link == "Go")
        GUIUtil::openURL(QString::fromStdString(GetContext().GetAutoUpdateModel()->GetUpdateUrlTag()));
    else if (link == "Download") {
        string err;
        if (!GetContext().GetAutoUpdateModel()->DownloadUpdateUrlFile(err))
            QMessageBox::warning(this, QString::fromStdString(CLIENT_NAME), tr(err.c_str()), QMessageBox::Ok, QMessageBox::Ok);
    } else if (link == "Open") {
        string localPath = GetContext().GetAutoUpdateModel()->FindLocalFile();
        if (!localPath.empty()) {
            StartShutdown();
            GUIUtil::openFileInDefaultApp(QString::fromStdString(localPath));
        } else
            QMessageBox::warning(this, QString::fromStdString(CLIENT_NAME), tr("Local file was not found."), QMessageBox::Ok, QMessageBox::Ok);
    }
    else
        assert(false); // unexpected link
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::hideOrphans(bool fHide)
{
    if (filter)
        filter->setHideOrphans(fHide);
}
