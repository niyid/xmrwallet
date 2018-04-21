/*
 * Copyright 2018 m2049r
 * Copyright 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.m2049r.xmrwallet.util;

import android.annotation.TargetApi;
import android.content.Context;
import android.os.Build;
import android.security.KeyPairGeneratorSpec;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.security.InvalidAlgorithmParameterException;
import java.security.InvalidKeyException;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.KeyStoreException;
import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.PrivateKey;
import java.security.Signature;
import java.security.SignatureException;
import java.util.Calendar;
import java.util.GregorianCalendar;

import javax.security.auth.x500.X500Principal;

import timber.log.Timber;

public class KeyStoreHelper {

    static {
        System.loadLibrary("monerujo");
    }

    public static native byte[] cnSlowHash(byte[] data);

    static final private String RSA_ALIAS = "MonerujoRSA";

    public static String getCrazyPass(Context context, String password) {
        // TODO we should check Locale.getDefault().getLanguage() here but for now we default to English
        return getCrazyPass(context, password, "English");
    }

    public static String getCrazyPass(Context context, String password, String language) {
        byte[] data = password.getBytes(StandardCharsets.UTF_8);
        byte[] sig = null;
        try {
            KeyStoreHelper.createKeys(context, RSA_ALIAS);
            sig = KeyStoreHelper.signData(RSA_ALIAS, data);
        } catch (Exception ex) {
            throw new IllegalStateException(ex);
        }
        return CrazyPassEncoder.encode(cnSlowHash(sig));
    }

    /**
     * Creates a public and private key and stores it using the Android Key
     * Store, so that only this application will be able to access the keys.
     */
    public static void createKeys(Context context, String alias) throws NoSuchProviderException,
            NoSuchAlgorithmException, InvalidAlgorithmParameterException, KeyStoreException {
        KeyStore keyStore = KeyStore.getInstance(SecurityConstants.KEYSTORE_PROVIDER_ANDROID_KEYSTORE);
        try {
            keyStore.load(null);
        } catch (Exception ex) { // don't care why it failed
            throw new IllegalStateException("Could not load KeySotre", ex);
        }
        if (!keyStore.containsAlias(alias)) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
                createKeysJBMR2(context, alias);
            } else {
                createKeysM(alias);
            }
        }
    }

    public static void deleteKeys(String alias) throws KeyStoreException {
        KeyStore keyStore = KeyStore.getInstance(SecurityConstants.KEYSTORE_PROVIDER_ANDROID_KEYSTORE);
        try {
            keyStore.load(null);
            keyStore.deleteEntry(alias);
        } catch (Exception ex) { // don't care why it failed
            throw new IllegalStateException("Could not load KeySotre", ex);
        }
    }

    @TargetApi(Build.VERSION_CODES.JELLY_BEAN_MR2)
    private static void createKeysJBMR2(Context context, String alias) throws NoSuchProviderException,
            NoSuchAlgorithmException, InvalidAlgorithmParameterException {

        Calendar start = new GregorianCalendar();
        Calendar end = new GregorianCalendar();
        end.add(Calendar.YEAR, 300);

        KeyPairGeneratorSpec spec = new KeyPairGeneratorSpec.Builder(context)
                .setAlias(alias)
                .setSubject(new X500Principal("CN=" + alias))
                .setSerialNumber(BigInteger.valueOf(Math.abs(alias.hashCode())))
                .setStartDate(start.getTime()).setEndDate(end.getTime())
                .build();
        // defaults to 2048 bit modulus
        KeyPairGenerator kpGenerator = KeyPairGenerator.getInstance(
                SecurityConstants.TYPE_RSA,
                SecurityConstants.KEYSTORE_PROVIDER_ANDROID_KEYSTORE);
        kpGenerator.initialize(spec);
        KeyPair kp = kpGenerator.generateKeyPair();
        Timber.d("preM Keys created");
    }

    @TargetApi(Build.VERSION_CODES.M)
    private static void createKeysM(String alias) {
        try {
            KeyPairGenerator keyPairGenerator = KeyPairGenerator.getInstance(
                    KeyProperties.KEY_ALGORITHM_RSA, SecurityConstants.KEYSTORE_PROVIDER_ANDROID_KEYSTORE);
            keyPairGenerator.initialize(
                    new KeyGenParameterSpec.Builder(
                            alias, KeyProperties.PURPOSE_SIGN)
                            .setDigests(KeyProperties.DIGEST_SHA256)
                            .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1)
                            .build());
            KeyPair keyPair = keyPairGenerator.generateKeyPair();
            Timber.d("M Keys created");
        } catch (NoSuchProviderException | NoSuchAlgorithmException | InvalidAlgorithmParameterException e) {
            throw new RuntimeException(e);
        }
    }

    private static KeyStore.PrivateKeyEntry getPrivateKeyEntry(String alias) {
        try {
            KeyStore ks = KeyStore
                    .getInstance(SecurityConstants.KEYSTORE_PROVIDER_ANDROID_KEYSTORE);
            ks.load(null);
            KeyStore.Entry entry = ks.getEntry(alias, null);

            if (entry == null) {
                Timber.w("No key found under alias: %s", alias);
                return null;
            }

            if (!(entry instanceof KeyStore.PrivateKeyEntry)) {
                Timber.w("Not an instance of a PrivateKeyEntry");
                return null;
            }
            return (KeyStore.PrivateKeyEntry) entry;
        } catch (Exception ex) {
            Timber.e(ex);
            return null;
        }
    }

    /**
     * Signs the data using the key pair stored in the Android Key Store. This
     * signature can be used with the data later to verify it was signed by this
     * application.
     *
     * @return The data signature generated
     */
    public static byte[] signData(String alias, byte[] data) throws NoSuchAlgorithmException,
            InvalidKeyException, SignatureException {

        PrivateKey privateKey = getPrivateKeyEntry(alias).getPrivateKey();
        Signature s = Signature.getInstance(SecurityConstants.SIGNATURE_SHA256withRSA);
        s.initSign(privateKey);
        s.update(data);
        return s.sign();
    }

    /**
     * Given some data and a signature, uses the key pair stored in the Android
     * Key Store to verify that the data was signed by this application, using
     * that key pair.
     *
     * @param data      The data to be verified.
     * @param signature The signature provided for the data.
     * @return A boolean value telling you whether the signature is valid or
     * not.
     */
    public static boolean verifyData(String alias, byte[] data, byte[] signature)
            throws NoSuchAlgorithmException, InvalidKeyException, SignatureException {

        // Make sure the signature string exists
        if (signature == null) {
            Timber.w("Invalid signature.");
            return false;
        }

        KeyStore.PrivateKeyEntry keyEntry = getPrivateKeyEntry(alias);
        Signature s = Signature.getInstance(SecurityConstants.SIGNATURE_SHA256withRSA);
        s.initVerify(keyEntry.getCertificate());
        s.update(data);
        return s.verify(signature);
    }

    public interface SecurityConstants {
        String KEYSTORE_PROVIDER_ANDROID_KEYSTORE = "AndroidKeyStore";
        String TYPE_RSA = "RSA";
        String SIGNATURE_SHA256withRSA = "SHA256withRSA";
    }
}