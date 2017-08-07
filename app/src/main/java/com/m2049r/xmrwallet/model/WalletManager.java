/**
 * Copyright (c) 2017 m2049r
 * <p>
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.m2049r.xmrwallet.model;

import android.util.Log;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.FilenameFilter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class WalletManager {
    final static String TAG = "WalletManager";

    static {
        System.loadLibrary("monerujo");
    }

    // no need to keep a reference to the REAL WalletManager (we get it every tvTime we need it)
    private static WalletManager Instance = null;

    public static WalletManager getInstance() { // TODO not threadsafe
        if (WalletManager.Instance == null) {
            WalletManager.Instance = new WalletManager();
        }
        return WalletManager.Instance;
    }

    private WalletManager() {
        this.managedWallets = new HashMap<>();
    }

    private Map<String, Wallet> managedWallets;

    public Wallet getWallet(String walletId) {
        return managedWallets.get(walletId);
    }

    private void manageWallet(String walletId, Wallet wallet) {
        if (getWallet(walletId) != null) {
            throw new IllegalStateException("Wallet already under management!");
        }
        Log.d(TAG, "Managing " + walletId);
        managedWallets.put(walletId, wallet);
    }

    private void unmanageWallet(String walletId) {
        if (getWallet(walletId) == null) {
            throw new IllegalStateException("Wallet not under management!");
        }
        Log.d(TAG, "Unmanaging " + walletId);
        managedWallets.remove(walletId);
    }

    public Wallet createWallet(String path, String password, String language) {
        long walletHandle = createWalletJ(path, password, language, isTestNet());
        Wallet wallet = new Wallet(walletHandle);
        manageWallet(wallet.getName(), wallet);
        return wallet;
    }

    public Wallet openWallet(String path, String password) {
        long walletHandle = openWalletJ(path, password, isTestNet());
        Wallet wallet = new Wallet(walletHandle);
        manageWallet(wallet.getName(), wallet);
        return wallet;
    }

    public Wallet recoveryWallet(String path, String mnemonic) {
        Wallet wallet = recoveryWallet(path, mnemonic, 0);
        manageWallet(wallet.getName(), wallet);
        return wallet;
    }

    public Wallet recoveryWallet(String path, String mnemonic, long restoreHeight) {
        long walletHandle = recoveryWalletJ(path, mnemonic, isTestNet(), restoreHeight);
        Wallet wallet = new Wallet(walletHandle);
        manageWallet(wallet.getName(), wallet);
        return wallet;
    }

    private native long createWalletJ(String path, String password, String language, boolean isTestNet);

    private native long openWalletJ(String path, String password, boolean isTestNet);

    private native long recoveryWalletJ(String path, String mnemonic, boolean isTestNet, long restoreHeight);

    private native long createWalletFromKeysJ(String path, String language,
                                              boolean isTestNet,
                                              long restoreHeight,
                                              String addressString,
                                              String viewKeyString,
                                              String spendKeyString);

    public native boolean closeJ(Wallet wallet);

    public boolean close(Wallet wallet) {
        String walletId = new File(wallet.getFilename()).getName();
        unmanageWallet(walletId);
        boolean closed = closeJ(wallet);
        if (!closed) {
            // in case we could not close it
            // we unmanage it
            manageWallet(walletId, wallet);
        }
        return closed;
    }

    public native boolean walletExists(String path);

    public native boolean verifyWalletPassword(String keys_file_name, String password, boolean watch_only);

    //public native List<String> findWallets(String path); // this does not work - some error in boost

    public class WalletInfo {
        public File path;
        public String name;
        public String address;
    }

    public List<WalletInfo> findWallets(File path) {
        List<WalletInfo> wallets = new ArrayList<>();
        Log.d(TAG, "Scanning: " + path.getAbsolutePath());
        File[] found = path.listFiles(new FilenameFilter() {
            public boolean accept(File dir, String filename) {
                return filename.endsWith(".keys");
            }
        });
        for (int i = 0; i < found.length; i++) {
            WalletInfo info = new WalletInfo();
            info.path = path;
            String filename = found[i].getName();
            info.name = filename.substring(0, filename.length() - 5); // 5 is length of ".keys"+1
            File addressFile = new File(path, info.name + ".address.txt");
            //Log.d(TAG, addressFile.getAbsolutePath());
            info.address = "??????";
            BufferedReader addressReader = null;
            try {
                addressReader = new BufferedReader(new FileReader(addressFile));
                info.address = addressReader.readLine();
            } catch (IOException ex) {
                Log.d(TAG, ex.getLocalizedMessage());
            } finally {
                if (addressReader != null) {
                    try {
                        addressReader.close();
                    } catch (IOException ex) {
                        // that's just too bad
                    }
                }
            }
            wallets.add(info);
        }
        return wallets;
    }

    public native String getErrorString();

//TODO virtual bool checkPayment(const std::string &address, const std::string &txid, const std::string &txkey, const std::string &daemon_address, uint64_t &received, uint64_t &height, std::string &error) const = 0;

    private String daemonAddress = null;
    private boolean testnet = true;

    public boolean isTestNet() {
        if (daemonAddress == null) {
            // assume testnet not explicitly initialised
            throw new IllegalStateException("use setDaemon() to initialise daemon and net first!");
        }
        return testnet;
    }

    public void setDaemon(String address, boolean testnet) {
        this.daemonAddress = address;
        this.testnet = testnet;
        setDaemonAddressJ(address);
    }

    public String getDaemonAddress() {
        if (daemonAddress == null) {
            // assume testnet not explicitly initialised
            throw new IllegalStateException("use setDaemon() to initialise daemon and net first!");
        }
        return this.daemonAddress;
    }

    private native void setDaemonAddressJ(String address);

    public native int getConnectedDaemonVersion();

    public native long getBlockchainHeight();

    public native long getBlockchainTargetHeight();

    public native long getNetworkDifficulty();

    public native double getMiningHashRate();

    public native long getBlockTarget();

    public native boolean isMining();

    public native boolean startMining(String address, boolean background_mining, boolean ignore_battery);

    public native boolean stopMining();

    public native String resolveOpenAlias(String address, boolean dnssec_valid);

//TODO static std::tuple<bool, std::string, std::string, std::string, std::string> checkUpdates(const std::string &software, const std::string &subdir);


}