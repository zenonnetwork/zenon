// Copyright (c) 2018 The PIVX developers
// Copyright (c) 2018-2019 The Zenon developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZNN_INVALID_OUTPOINTS_JSON_H
#define ZNN_INVALID_OUTPOINTS_JSON_H
#include <string>

std::string LoadInvalidOutPoints()
{
    std::string str = "[\n"
    "  {\n"
    "    \"txid\": \"a156d2fe204815c6df7554720e12e59705c24c9f7a58e25e5833c165e139a529\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"f149c5f8cad65da144cbf3471dd4813270380dd1d477ed9a7da7e5871265a5d8\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"c671cfa44d6091564d9eabfaef40dfd4a1a1c82e8335b3c55005929055995676\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"b4ff4a5de319e5e2aa1a90da35abf4f97c407a7ef8d7a7fd42f5abfd11375464\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"06a17b48e5aa49bbfd00277d05aa1cc4751c2f922ab8b0ad07171f2fe09377e3\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"cffba7d64f5c50e3108c1e3ae4165eb061c38ae9f3ae542f2be1f65d0c242984\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"61307f744a9cb027c4d93a8ae2df64740902b9d7977f833a4940db21e49b3a98\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"0124a8f8fa0563418cdd49ac47752d31bb30bc839aef6454900c15f39c658563\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"62770c5b4aa2904b4430b7f1b7cf89b4e508c1f5a4651daf0507945790abe2a6\",\n"
    "    \"n\": 1\n"
    "  },\n"
    "  {\n"
    "    \"txid\": \"1cb80e66ea221584f6e07e223df6c435a5cf9e3e1870eca216798a7b7c7c21e3\",\n"
    "    \"n\": 1\n"
    "  }\n"
    "]";
    return str;
}

#endif //ZNN_INVALID_OUTPOINTS_JSON_H
