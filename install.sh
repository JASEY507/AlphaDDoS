#!/bin/bash
# AlphaDDoS - Installation Script

echo "AlphaDDoS - Kurulum Başlatılıyor..."
apt-get update
apt-get install -y python3 python3-pip ssh iptables net-tools curl dnsutils
pip3 install -r requirements.txt
chmod +x alpha_ddos.py
mkdir -p /root/.cache/
cp alpha_ddos.py /root/.cache/
echo "Kurulum tamamlandı. Çalıştırmak için: python3 alpha_ddos.py"
