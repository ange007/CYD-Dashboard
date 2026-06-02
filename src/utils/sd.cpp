#include "sd.h"

#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

#ifdef BOARD_HAS_TF

static SPIClass _tfSpi(HSPI);
static bool _sdOk = false;
static volatile bool _sdServing = false;

bool uSD::init()
{
#ifdef BOARD_TF_SKIP_INIT
    // TF SPI pins conflict with another peripheral on this board (hardware limitation).
    // On esp32-2432S028Rv2: TF uses VSPI defaults (18/19/23) which collide with the
    // XPT2046 touch controller also on VSPI/SPI3. Calling _tfSpi.begin() reconfigures
    // the GPIO matrix and breaks touch; skip SD init to keep touch functional.
    ESP_LOGW("uSD", "SD init skipped (BOARD_TF_SKIP_INIT): TF SPI conflicts with touch controller");
    return false;
#else
    _tfSpi.begin(TF_SPI_SCLK, TF_SPI_MISO, TF_SPI_MOSI, TF_CS);
    _sdOk = SD.begin(TF_CS, _tfSpi, 4000000);
    if (!_sdOk) {
        ESP_LOGW("uSD", "SD card not found or init failed");
    } else {
        ESP_LOGI("uSD", "SD card OK, size=%" PRIu64 " MB", SD.cardSize() / (1024 * 1024));
    }
    return _sdOk;
#endif
}

bool uSD::isAvailable()
{
    return _sdOk;
}

fs::FS& uSD::getFS()
{
    if (_sdOk) return SD;
    return LittleFS;
}

uint64_t uSD::getCardSize()
{
    return _sdOk ? SD.cardSize() : 0;
}

void uSD::setSdServing(bool serving) { _sdServing = serving; }
bool uSD::isSdServing()              { return _sdServing; }

void uSD::createDir(fs::FS &fs, const char *path)
{
    fs.mkdir(path);
}

void uSD::removeDir(fs::FS &fs, const char *path)
{
    fs.rmdir(path);
}

void uSD::readFile(fs::FS &fs, const char *path)
{
    File f = fs.open(path);
    if (!f) return;
    while (f.available()) f.read();
    f.close();
}

void uSD::writeFile(fs::FS &fs, const char *path, const char *message)
{
    File f = fs.open(path, FILE_WRITE);
    if (!f) return;
    f.print(message);
    f.close();
}

void uSD::appendFile(fs::FS &fs, const char *path, const char *message)
{
    File f = fs.open(path, FILE_APPEND);
    if (!f) return;
    f.print(message);
    f.close();
}

void uSD::renameFile(fs::FS &fs, const char *path1, const char *path2)
{
    fs.rename(path1, path2);
}

void uSD::deleteFile(fs::FS &fs, const char *path)
{
    fs.remove(path);
}

#else // !BOARD_HAS_TF

// Stub implementations for boards without SD card

bool uSD::init()        { return false; }
bool uSD::isAvailable() { return false; }

// Provide a reference to LittleFS as fallback so callers compile without ifdefs
#include <LittleFS.h>
fs::FS& uSD::getFS()    { return LittleFS; }  // NOLINT

uint64_t uSD::getCardSize() { return 0; }

void uSD::createDir(fs::FS&, const char*) {}
void uSD::removeDir(fs::FS&, const char*) {}
void uSD::readFile(fs::FS&, const char*) {}
void uSD::writeFile(fs::FS&, const char*, const char*) {}
void uSD::appendFile(fs::FS&, const char*, const char*) {}
void uSD::renameFile(fs::FS&, const char*, const char*) {}
void uSD::deleteFile(fs::FS&, const char*) {}
void uSD::setSdServing(bool) {}
bool uSD::isSdServing()      { return false; }

#endif // BOARD_HAS_TF
