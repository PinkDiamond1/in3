/*******************************************************************************
 * This file is part of the Incubed project.
 * Sources: https://github.com/blockchainsllc/in3
 * 
 * Copyright (C) 2018-2020 slock.it GmbH, Blockchains LLC
 * 
 * 
 * COMMERCIAL LICENSE USAGE
 * 
 * Licensees holding a valid commercial license may use this file in accordance 
 * with the commercial license agreement provided with the Software or, alternatively, 
 * in accordance with the terms contained in a written agreement between you and 
 * slock.it GmbH/Blockchains LLC. For licensing terms and conditions or further 
 * information please contact slock.it at in3@slock.it.
 * 	
 * Alternatively, this file may be used under the AGPL license as follows:
 *    
 * AGPL LICENSE USAGE
 * 
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free Software 
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY 
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A 
 * PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.
 * [Permissions of this strong copyleft license are conditioned on making available 
 * complete source code of licensed works and modifications, which include larger 
 * works using a licensed work, under the same license. Copyright and license notices 
 * must be preserved. Contributors provide an express grant of patent rights.]
 * You should have received a copy of the GNU Affero General Public License along 
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *******************************************************************************/

/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class in3_IN3 */

#ifndef _Included_in3_IN3
#define _Included_in3_IN3
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Class:     in3_IN3
 * Method:    getVersion
 * Signature: (I)V
 */
JNIEXPORT jstring JNICALL Java_in3_IN3_getVersion(JNIEnv*, jclass);

/*
 * Class:     in3_IN3
 * Method:    setConfig
 * Signature: (Ljava/lang/String)V
 */
JNIEXPORT void JNICALL Java_in3_IN3_setConfig(JNIEnv* env, jobject ob, jstring val);

/*
 * Class:     in3_IN3
 * Method:    getChainId
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_in3_IN3_getChainId(JNIEnv*, jobject);

/*
 * Class:     in3_IN3
 * Method:    setChainId
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_in3_IN3_setChainId(JNIEnv*, jobject, jlong);

/*
 * Class:     in3_IN3
 * Method:    sendinternal
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_in3_IN3_sendinternal(JNIEnv*, jobject, jstring);

/*
 * Class:     in3_IN3
 * Method:    sendobjectinternal
 * Signature: (Ljava/lang/String;)Ljava/lang/Object;
 */
JNIEXPORT jobject JNICALL Java_in3_IN3_sendobjectinternal(JNIEnv*, jobject, jstring);

/*
 * Class:     in3_IN3
 * Method:    free
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_in3_IN3_free(JNIEnv*, jobject);

/*
 * Class:     in3_IN3
 * Method:    getDefaultConfig
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_in3_IN3_getDefaultConfig(JNIEnv*, jobject);

#ifdef IPFS
/*
 * Class:     in3_ipfs_API
 * Method:    base64Decode
 * Signature: (Ljava/lang/String;)[B
 */
JNIEXPORT jbyteArray JNICALL Java_in3_ipfs_API_base64Decode(JNIEnv* env, jobject ob, jstring jinput);

/*
 * Class:     in3_ipfs_API
 * Method:    base64Encode
 * Signature: ([B)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_in3_ipfs_API_base64Encode(JNIEnv* env, jobject ob, jbyteArray jinput);
#endif

/*
 * Class:     in3_IN3
 * Method:    init
 * Signature: ()J
 */
JNIEXPORT jlong JNICALL Java_in3_IN3_init(JNIEnv*, jobject, jlong);

/*
 * Class:     in3_IN3
 * Method:    initcache
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_in3_IN3_initcache(JNIEnv*, jobject);

#ifdef __cplusplus
}
#endif
#endif
/* Header for class in3_JSON */

#ifndef _Included_in3_JSON
#define _Included_in3_JSON
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     in3_JSON
 * Method:    key
 * Signature: (Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_in3_utils_JSON_key(JNIEnv*, jclass, jstring);

#ifdef __cplusplus
}
#endif
#endif
/* Header for class in3_eth1_TransactionRequest */

#ifndef _Included_in3_eth1_TransactionRequest
#define _Included_in3_eth1_TransactionRequest
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     in3_eth1_TransactionRequest
 * Method:    abiEncode
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_in3_eth1_TransactionRequest_abiEncode(JNIEnv*, jclass, jstring, jstring);

/*
 * Class:     in3_eth1_TransactionRequest
 * Method:    abiDecode
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Lin3/JSON;
 */
JNIEXPORT jobject JNICALL Java_in3_eth1_TransactionRequest_abiDecode(JNIEnv*, jclass, jstring, jstring);

/*
 * Class:     in3_utils_JSON
 * Method:    parse
 * Signature: (Ljava/lang/String;)Lin3/JSON;
 */
JNIEXPORT jobject JNICALL Java_in3_utils_JSON_parse(JNIEnv*, jclass, jstring);


/*
 * Class:     in3_eth1_SimpleWallet
 * Method:    getAddressFromKey
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_in3_eth1_SimpleWallet_getAddressFromKey(JNIEnv*, jclass, jstring);

/*
 * Class:     in3_eth1_SimpleWallet
 * Method:    signData
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_in3_eth1_SimpleWallet_signData(JNIEnv*, jclass, jstring, jstring);

/*
 * Class:     in3_eth1_SimpleWallet
 * Method:    decodeKeystore
 * Signature: (Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_in3_eth1_SimpleWallet_decodeKeystore(JNIEnv*, jclass, jstring, jstring);

/*
 * Class:     in3_Loader
 * Method:    libInit
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_in3_Loader_libInit(JNIEnv*, jclass);

#ifdef __cplusplus
}
#endif
#endif
