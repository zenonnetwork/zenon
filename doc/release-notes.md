Zenon Core version *version* is now available from:

This is a new major version release, including various bug fixes and performance improvements, as well as updated translations.

Please report bugs using the issue tracker at github: <https://github.com/zenonnetwork/zenon/issues>


Mandatory Update
==============

Zenon Core 1.0.1 is a mandatory update for all users. This release contains new consensus rules and improvements that are not backwards compatible with older versions. Users will have a grace period of three weeks to update their clients before enforcement of this update is enabled.

How to Upgrade
==============

If you are running an older version, shut it down. Wait until it has completely shut down (which might take a few minutes for older versions), then run the installer (on Windows) or just copy over /Applications/Zenon-Qt (on Mac) or Zenond/Zenon-qt (on Linux).


Compatibility
==============

Zenon Core is extensively tested on multiple operating systems using the Linux kernel, macOS 10.8+, and Windows 7 and later.

Microsoft ended support for Windows XP on [April 8th, 2014](https://www.microsoft.com/en-us/WindowsForBusiness/end-of-xp-support), No attempt is made to prevent installing or running the software on Windows XP, you can still do so at your own risk but be aware that there are known instabilities and issues. Please do not report issues about Windows XP to the issue tracker.

Zenon Core should also work on most other Unix-like systems but is not frequently tested on them.

 
Notable Changes
==============


The minimum supported version of MacOS (OSX) has been moved from 10.8 Mountain Lion to 10.10 Yosemite. Users still running a MacOS version prior to Yosemite will need to upgrade their OS if they wish to continue using the latest version(s) of the Zenon Core wallet.

Attacks, Exploits, and Mitigations
------

### Fake Stake

On Janurary 22 2019, Decentralized Systems Lab out of the University of Illinois published a study entitled “[‘Fake Stake’ attacks on chain-based Proof-of-Stake cryptocurrencies](https://medium.com/@dsl_uiuc/fake-stake-attacks-on-chain-based-proof-of-stake-cryptocurrencies-b8b05723f806)”, which outlined a type of Denial of Service attack that could take place on a number of Proof of Stake based networks by exhausting a client's RAM or Disk resources.

This type of attack has no risk to users' privacy and does not affect their holdings.

### Wrapped Serials

On March 6th 2019, an attack was detected on the PIVX network zerocoin protocol, or zPIV. The vulnerability allows an attacker to fake serials accepted by the network and thus to spend zerocoins that have never been minted. As severe as it is, it does not harm users’ privacy and does not affect their holdings directly.

Major New Features
------

### BIP65 (CHECKLOCKTIMEVERIFY) Soft-Fork

Zenon Core v1.0.1 introduces new consensus rules for scripting pathways to support the [BIP65](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki) standard. This is being carried out as a soft-fork in order to provide ample time for stakers to update their wallet version.

### Wallet Autoupdate

Wallet downloads new release into /update folder in the data directory and ask user to open it.

### Regression Test Suite

The RegTest network mode has been re-worked to once again allow for the generation of on-demand PoW and PoS blocks. Additionally, many of the existing functional test scripts have been adapted for use with Zenon, and we now have a solid testing base for highly customizable tests to be written.

With this, the old `setgenerate` RPC command no longer functions in regtest mode, instead a new `generate` command has been introduced that is more suited for use in regtest mode.

GUI Changes
------

### Console Security Warning

Due to an increase in social engineering attacks/scams that rely on users relaying information from console commands, a new warning message has been added to the Console window's initial welcome message.

### Optional Hiding of Orphan Stakes

The options dialog now contains a checkbox option to hide the display of orphan stakes from both the overview and transaction history sections. Further, a right-click context menu option has been introduced in the transaction history tab to achieve the same effect.

**Note:** This option only affects the visual display of orphan stakes, and will not prevent them nor remove them from the underlying wallet database.

### Transaction Type Recoloring

The color of various transaction types has been reworked to provide better visual feedback. Staking and masternode rewards are now purple, orphan stakes are now light gray, other rejected transactions are in red, and normal receive/send transactions are black.

### Receive Tab Changes

The address to be used when creating a new payment request is now automatically displayed in the form. This field is not user-editable, and will be updated as needed by the wallet.

A new button has been added below the payment request form, "Receiving Addresses", which allows for quicker access to all the known receiving addresses. This one-click button is the same as using the `File->Receiving Addresses...` menu command, and will open up the Receiving Addresses UI dialog.

Historical payment requests now also display the address used for the request in the history table. While this information was already available when clicking the "Show" button, it was an extra step that shouldn't have been necessary.

RPC Changes
------

### Backupwallet Sanity

The `backupwallet` RPC command no longer allows for overwriting the currently in use wallet.dat file. This was done to avoid potential file corruption caused by multiple conflicting file access operations.

### Getreceivedbyaddress Update

When calling `getreceivedbyaddress` with a non-wallet address, return a proper error code/message instead of just `0`

### Validateaddress More Verbosity

`validateaddress` now has the ability to return more (non-critical or identifying) details about P2SH (multisig) addresses by removing the needless check against ISMINE_NO.

### Getblock & Getblockheader

A minor change to these two RPC commands to now display the `mediantime`, used primarialy during functional tests.

### Getwalletinfo

The `getwalletinfo` RPC command now outputs the configured transaction fee (`paytxfee` field).

Build System Changes
------

### Completely Disallow Qt4

Compiling the Zenon Core wallet against Qt4 hasn't been supported for quite some time now, but the build system still recognized Qt4 as a valid option if Qt5 couldn't be found. This has now been remedied and Qt4 will no longer be considered valid during the `configure` pre-compilation phase.

### Further OpenSSL Deprecation

Up until now, the zerocoin library relied exclusively on OpenSSL for it's bignum implementation. This has now been changed with the introduction of GMP as an arithmetic operator and the bignum implementation has now been redesigned around GMP. Users can still opt to use OpenSSL for bignum by passing `--with-zerocoin-bignum=openssl` to the `configure` script, however such configuration is now deprecated.

**Note:** This change introduces a new dependency on GMP (libgmp) by default.

### RISC-V Support

Support for the new RISC-V 64bit processors has been added, though still experimental. Pre-compiled binaries for this CPU architecture are available for linux, and users can self-compile using gitian, depends, or an appropriate host system natively.

### New Gitian Build Script

The previous `gitian-build.sh` shell script has been replaced with a more feature rich python version; `gitian-build.py`. This script now supports the use of a docker container in addition to LXC or KVM virtualization, as well as the ability to build against a pull request by number.
