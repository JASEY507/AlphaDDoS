#!/bin/bash
# Zeta Oblivion Daemon - Derleme Betiği
# OpenSSL bağımlılığı gerektirir

echo "[Oblivion] Derleme başlatılıyor..."

# OpenSSL kontrolü
if ! command -v openssl &> /dev/null; then
    echo "[!] OpenSSL bulunamadı. Kuruluyor..."
    apt-get update
    apt-get install -y openssl libssl-dev
fi

# Derleme
g++ -std=c++17 -O2 -pthread -o zeta_oblivion_daemon zeta_oblivion_daemon.cpp -lcrypto -lssl

if [ $? -eq 0 ]; then
    echo "[+] Derleme başarılı. Çalıştırmak için: ./zeta_oblivion_daemon"
    chmod +x zeta_oblivion_daemon
else
    echo "[!] Derleme hatası!"
    exit 1
fi
