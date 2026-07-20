#!/bin/bash
# AlphaDDoS - Tek Komut Çalıştırıcı
# Tüm kurulum ve başlatma işlemlerini otomatik yapar

set -e

echo "=================================================="
echo "ALPHADDoS - ZETA NETWORK ATTACK SYSTEM v2.0"
echo "=================================================="
echo ""

# Renkli çıktı için
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 1. Bağımlılıkları kontrol et ve kur
echo -e "${YELLOW}[*] Bağımlılıklar kontrol ediliyor...${NC}"

# Python kontrol
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}[!] Python3 bulunamadı. Kuruluyor...${NC}"
    apt-get update && apt-get install -y python3 python3-pip
fi

# pip kontrol
if ! command -v pip3 &> /dev/null; then
    echo -e "${RED}[!] pip3 bulunamadı. Kuruluyor...${NC}"
    apt-get update && apt-get install -y python3-pip
fi

# g++ kontrol
if ! command -v g++ &> /dev/null; then
    echo -e "${RED}[!] g++ bulunamadı. Kuruluyor...${NC}"
    apt-get update && apt-get install -y g++ build-essential
fi

# OpenSSL kontrol
if ! command -v openssl &> /dev/null; then
    echo -e "${RED}[!] OpenSSL bulunamadı. Kuruluyor...${NC}"
    apt-get update && apt-get install -y openssl libssl-dev
fi

# 2. Python bağımlılıklarını yükle
echo -e "${YELLOW}[*] Python bağımlılıkları yükleniyor...${NC}"
pip3 install -r requirements.txt 2>/dev/null || pip3 install dns requests paramiko pyOpenSSL

# 3. C++ modülünü derle
echo -e "${YELLOW}[*] Zeta Oblivion Daemon derleniyor...${NC}"
g++ -std=c++17 -O2 -pthread -o zeta_oblivion_daemon zeta_oblivion_daemon.cpp -lcrypto -lssl 2>/dev/null

if [ -f "zeta_oblivion_daemon" ]; then
    chmod +x zeta_oblivion_daemon
    echo -e "${GREEN}[+] C++ modülü derlendi.${NC}"
else
    echo -e "${YELLOW}[!] C++ derleme hatası, Python modülü ile devam ediliyor...${NC}"
fi

# 4. Gizlilik modülünü başlat (arka planda)
echo -e "${YELLOW}[*] Gizlilik modülü başlatılıyor...${NC}"
if [ -f "zeta_oblivion_daemon" ]; then
    ./zeta_oblivion_daemon &
    OBLIVION_PID=$!
    echo -e "${GREEN}[+] Oblivion Daemon başlatıldı (PID: $OBLIVION_PID)${NC}"
else
    echo -e "${YELLOW}[!] Oblivion Daemon çalışmıyor, Python gizlilik modülü kullanılacak.${NC}"
fi

# 5. Ana saldırı aracını çalıştır
echo -e "${YELLOW}[*] AlphaDDoS ana sistemi başlatılıyor...${NC}"
echo ""
echo "=================================================="
echo "  HEDEF DOMAIN ADRESİNİ GİRİNİZ"
echo "=================================================="
echo ""

python3 alpha_ddos.py

# 6. Çıkış sonrası temizlik
echo ""
echo -e "${YELLOW}[*] Temizlik yapılıyor...${NC}"

# Oblivion Daemon'u durdur
if [ ! -z "$OBLIVION_PID" ]; then
    kill $OBLIVION_PID 2>/dev/null
    echo -e "${GREEN}[+] Oblivion Daemon durduruldu.${NC}"
fi

# Logları temizle
./zeta_oblivion_daemon --purge 2>/dev/null || true

echo -e "${GREEN}[+] AlphaDDoS tamamlandı. Hiçbir iz bırakılmadı.${NC}"
echo ""
